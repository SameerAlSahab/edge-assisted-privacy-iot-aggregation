import re
import socket
import subprocess
import threading
import time

PORT = 5000
DISCOVERY_PORT = 4999


def broadcast_addrs():
    """Broadcast address of every IPv4 interface (so it works whichever one is on the hotspot)."""
    out = subprocess.run(["ip", "-4", "-o", "addr", "show"],
                         capture_output=True, text=True).stdout
    return re.findall(r"brd (\d+\.\d+\.\d+\.\d+)", out)


def beacon():
    """Tell the ESP32 where we are: broadcast "PC" every 2s."""
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    u.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    while True:
        for b in broadcast_addrs():
            try:
                u.sendto(b"PC", (b, DISCOVERY_PORT))
            except OSError:
                pass
        time.sleep(2)


threading.Thread(target=beacon, daemon=True).start()

s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", PORT))
s.listen()
print(f"Server started, Port is {PORT} (broadcasting discovery on UDP {DISCOVERY_PORT})")

while True:
    conn, addr = s.accept()
    data = conn.recv(1024).decode().strip()
    print(addr[0], "->", data)
    conn.close()
