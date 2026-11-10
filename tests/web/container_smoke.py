"""Smoke test of a running container: python3 container_smoke.py <base-url> <password>

Checks the health probe, login, the website page, that the VNC desktop answers through the WebSocket proxy
(i.e. the virtual screen and VNC server are up), and a file upload/download round trip.
"""
import base64
import http.client
import os
import socket
import sys
import time
import urllib.parse

base, password = sys.argv[1], sys.argv[2]
url = urllib.parse.urlsplit(base)
host, port = url.hostname, url.port or 80


def call(method, path, body=None, headers=None):
    conn = http.client.HTTPConnection(host, port, timeout=15)
    conn.request(method, path, body=body, headers=headers or {})
    r = conn.getresponse()
    data = r.read()
    out = (r.status, dict(r.getheaders()), data)
    conn.close()
    return out


def check(ok, what):
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        sys.exit(1)


check(call("GET", "/healthz")[0] == 200, "health probe")
check(call("GET", "/")[0] == 303, "site is behind a login")
status, headers, _ = call("POST", "/login", "password=" + urllib.parse.quote_plus(password),
                          {"Content-Type": "application/x-www-form-urlencoded"})
check(status == 303 and "Set-Cookie" in headers, "login with the password works")
cookie = headers["Set-Cookie"].split(";")[0]
status, _, body = call("GET", "/", headers={"Cookie": cookie})
check(status == 200 and b"vnc.html" in body, "desktop page served")
status, _, body = call("GET", "/desktop/vnc.html", headers={"Cookie": cookie})
check(status == 200 and b"noVNC" in body, "noVNC files are installed")

# The VNC server must greet with "RFB" through the WebSocket proxy (retry while the desktop is starting)
greeting = b""
for _ in range(40):
    sock = socket.create_connection((host, port), timeout=10)
    key = base64.b64encode(os.urandom(16)).decode()
    sock.sendall(("GET /websockify HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: binary\r\n"
                  "Cookie: %s\r\n\r\n" % (host, key, cookie)).encode())
    reader = sock.makefile("rb")
    while reader.readline().strip():
        pass
    head = reader.read(2)
    if len(head) == 2 and head[0] & 0x0F == 2:
        greeting = reader.read(head[1] & 0x7F)
    sock.close()
    if greeting.startswith(b"RFB"):
        break
    time.sleep(1.5)
check(greeting.startswith(b"RFB"), "VNC desktop answers through the WebSocket proxy (%r)" % greeting[:12])

hdr = {"Cookie": cookie, "X-Eden": "1"}
check(call("PUT", "/api/upload?name=smoke.txt", b"hello eden", hdr)[0] == 200, "upload")
check(call("GET", "/api/download?name=smoke.txt", headers=hdr)[2] == b"hello eden", "download")
check(call("DELETE", "/api/files?name=smoke.txt", headers=hdr)[0] == 200, "delete")
print("ALL PASSED")
