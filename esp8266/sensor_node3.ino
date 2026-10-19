#include <ESP8266WiFi.h>

const char *NODE_NAME = "node3";
const char *WIFI_SSID = "Sameer's S23 Ultra";
const char *WIFI_PASS = "11225799";
const char *GATEWAY_IP = "192.168.1.101";
const uint16_t GATEWAY_PORT = 5000;
const unsigned long SEND_INTERVAL_MS = 2000;

void connectWiFi() {
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());
}

void sendReading() {
  WiFiClient client;
  if (!client.connect(GATEWAY_IP, GATEWAY_PORT)) {
    Serial.println("Could not reach gateway, will retry next cycle");
    return;
  }

  // Placeholder "sensor" value until a real sensor is wired up. I have a plan to add a heart rate sensor
  // https://electronics.com.bd/heart-rate-max30102-sensor-module
  int fakeReading = random(60, 100);

  client.printf("%s,%d\n", NODE_NAME, fakeReading);
  client.stop();

  Serial.printf("Sent: %s,%d\n", NODE_NAME, fakeReading);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  connectWiFi();
  randomSeed(analogRead(A0));
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  sendReading();
  delay(SEND_INTERVAL_MS);
}
