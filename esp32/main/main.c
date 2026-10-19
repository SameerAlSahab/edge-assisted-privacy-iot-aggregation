#define WIFI_SSID   "Sameer's S23 Ultra"
#define WIFI_PASS   "11225799"
#define PC_HOST_IP  "192.168.1.100"   // <-- PC's IP  )`ip a`)
#define GATEWAY_PORT 5000             // port this ESP32
#define PC_PORT      5000             // port the PC server.py

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "u8g2.h"
#include "u8g2_esp32_hal.h"

#define I2C_SDA_GPIO     21
#define I2C_SCL_GPIO     22
#define SSD1306_I2C_ADDR 0x3C
#define OLED_WIDTH  128
#define OLED_HEIGHT 64

static const char *TAG = "gateway";

static u8g2_t u8g2;
static bool wifi_connected = false;
static int msg_count = 0;
static char last_msg[64] = "waiting...";



static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}



static void display_init(void)
{
    u8g2_esp32_hal_t hal_config = U8G2_ESP32_HAL_DEFAULT;
    hal_config.bus.i2c.sda = I2C_SDA_GPIO;
    hal_config.bus.i2c.scl = I2C_SCL_GPIO;
    u8g2_esp32_hal_init(hal_config);

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &u8g2, U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb
    );
    u8g2_SetI2CAddress(&u8g2, SSD1306_I2C_ADDR << 1);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_SetContrast(&u8g2, 0xFF);
}

static void display_update(void)
{
    char line[32];
    u8g2_ClearBuffer(&u8g2);

    u8g2_SetFont(&u8g2, u8g2_font_helvB08_tr);
    u8g2_DrawStr(&u8g2, 2, 10, wifi_connected ? "GATEWAY: ONLINE" : "GATEWAY: NO WIFI");

    u8g2_SetFont(&u8g2, u8g2_font_6x12_tr);
    snprintf(line, sizeof(line), "Msgs: %d", msg_count);
    u8g2_DrawStr(&u8g2, 2, 28, line);

    u8g2_DrawStr(&u8g2, 2, 44, "Last:");
    u8g2_DrawStr(&u8g2, 2, 58, last_msg);

    u8g2_SendBuffer(&u8g2);
}



static void forward_to_pc(const char *line)
{
    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(PC_PORT);
    dest.sin_addr.s_addr = inet_addr(PC_HOST_IP);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGW(TAG, "forward_to_pc: socket() failed");
        return;
    }

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        ESP_LOGW(TAG, "forward_to_pc: could not reach %s:%d", PC_HOST_IP, PC_PORT);
        close(sock);
        return;
    }

    send(sock, line, strlen(line), 0);
    close(sock);
}



static void tcp_server_task(void *pvParameters)
{
    struct sockaddr_in listen_addr = {0};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_addr.sin_port = htons(GATEWAY_PORT);

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create listening socket");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(listen_sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) != 0) {
        ESP_LOGE(TAG, "bind() failed");
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }
    listen(listen_sock, 4);
    ESP_LOGI(TAG, "TCP server listening on port %d", GATEWAY_PORT);

    while (1) {
        struct sockaddr_in src_addr;
        socklen_t addr_len = sizeof(src_addr);
        int client_sock = accept(listen_sock, (struct sockaddr *)&src_addr, &addr_len);
        if (client_sock < 0) {
            ESP_LOGW(TAG, "accept() failed");
            continue;
        }

        char rx_buf[64] = {0};
        int len = recv(client_sock, rx_buf, sizeof(rx_buf) - 1, 0);
        if (len > 0) {
            rx_buf[len] = '\0';
            /* strip trailing newline if present */
            char *nl = strchr(rx_buf, '\n');
            if (nl) *nl = '\0';

            char client_ip[16];
            inet_ntoa_r(src_addr.sin_addr, client_ip, sizeof(client_ip));
            ESP_LOGI(TAG, "from %s: %s", client_ip, rx_buf);

            msg_count++;
            strncpy(last_msg, rx_buf, sizeof(last_msg) - 1);
            display_update();

            forward_to_pc(rx_buf);
        }
        close(client_sock);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    display_init();
    display_update();

    wifi_init_sta();


    while (!wifi_connected) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    display_update();

    xTaskCreate(tcp_server_task, "tcp_server_task", 4096, NULL, 5, NULL);
}
