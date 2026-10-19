import socket

s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", 5000))
s.listen()
print("Server started, Port is 5000")

while True:
    conn, addr = s.accept()
    data = conn.recv(1024).decode().strip()
    print(addr[0], "->", data)
    conn.close()
