import base64
import hashlib
import http.client
import os
import socket
import struct
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "web"))
import gateway  # noqa: E402


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class EchoVnc(threading.Thread):
    """Stands in for x11vnc: sends a greeting, then echoes everything back."""

    def __init__(self):
        super().__init__(daemon=True)
        self.sock = socket.socket()
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(5)
        self.port = self.sock.getsockname()[1]

    def run(self):
        while True:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            threading.Thread(target=self.serve, args=(conn,), daemon=True).start()

    def serve(self, conn):
        try:
            conn.sendall(b"RFB 003.008\n")
            while True:
                data = conn.recv(65536)
                if not data:
                    return
                conn.sendall(data)
        except OSError:
            pass
        finally:
            conn.close()


class GatewayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.novnc = os.path.join(cls.tmp.name, "novnc")
        os.makedirs(cls.novnc)
        with open(os.path.join(cls.novnc, "vnc.html"), "w") as f:
            f.write("<html>vnc</html>")
        with open(os.path.join(cls.tmp.name, "secret.txt"), "w") as f:
            f.write("outside")
        cls.vnc = EchoVnc()
        cls.vnc.start()
        cls.config = gateway.Config({
            "EDEN_WEB_PASSWORD": "correct horse", "EDEN_WEB_PORT": str(free_port()), "EDEN_WEB_HOST": "127.0.0.1",
            "EDEN_VNC_PORT": str(cls.vnc.port), "EDEN_FILES_DIR": os.path.join(cls.tmp.name, "files"),
            "EDEN_NOVNC_DIR": cls.novnc, "EDEN_WEB_MAX_UPLOAD": "200000"})
        cls.server = gateway.Server(cls.config)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.port = cls.config.port

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.tmp.cleanup()

    def setUp(self):
        self.server.limiter.stamps.clear()

    def request(self, method, path, body=None, headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        conn.request(method, path, body=body, headers=headers or {})
        response = conn.getresponse()
        data = response.read()
        result = (response.status, dict(response.getheaders()), data)
        conn.close()
        return result

    def login(self, password="correct horse"):
        status, headers, _ = self.request("POST", "/login", "password=" + password.replace(" ", "+"),
                                          {"Content-Type": "application/x-www-form-urlencoded"})
        return status, headers

    def cookie(self):
        status, headers = self.login()
        self.assertEqual(status, 303)
        return headers["Set-Cookie"].split(";")[0]

    # ---- auth
    def test_health_needs_no_login(self):
        self.assertEqual(self.request("GET", "/healthz")[0], 200)

    def test_pages_redirect_to_login(self):
        for path in ("/", "/files", "/record", "/desktop/vnc.html"):
            status, headers, _ = self.request("GET", path)
            self.assertEqual((status, headers["Location"]), (303, "/login"), path)
        self.assertEqual(self.request("GET", "/api/files")[0], 401)

    def test_wrong_password_rejected_and_rate_limited(self):
        for _ in range(8):
            self.assertEqual(self.login("nope")[0], 401)
        self.assertEqual(self.login("nope")[0], 429)
        self.assertEqual(self.login()[0], 429)  # even the right one waits out the lockout

    def test_login_then_pages_work_and_cookie_flags(self):
        status, headers = self.login()
        self.assertEqual(status, 303)
        flags = headers["Set-Cookie"]
        self.assertIn("HttpOnly", flags)
        self.assertIn("SameSite=Strict", flags)
        cookie = flags.split(";")[0]
        for path in ("/", "/files", "/record"):
            self.assertEqual(self.request("GET", path, headers={"Cookie": cookie})[0], 200, path)

    def test_forged_and_expired_tokens(self):
        secret = self.config.secret
        self.assertTrue(gateway.valid_token(secret, gateway.make_token(secret)))
        self.assertFalse(gateway.valid_token(secret, gateway.make_token(secret, now=0)))
        self.assertFalse(gateway.valid_token(secret, "9999999999.deadbeef"))
        self.assertFalse(gateway.valid_token(secret, "garbage"))
        status, _, _ = self.request("GET", "/files", headers={"Cookie": "eden_session=9999999999.deadbeef"})
        self.assertEqual(status, 303)

    # ---- files
    def test_upload_list_download_delete(self):
        cookie = {"Cookie": self.cookie(), "X-Eden": "1"}
        payload = os.urandom(50000)
        status, _, body = self.request("PUT", "/api/upload?name=paper.pdf", payload, cookie)
        self.assertEqual(status, 200, body)
        status, _, body = self.request("GET", "/api/files", headers=cookie)
        self.assertIn(b"paper.pdf", body)
        status, headers, body = self.request("GET", "/api/download?name=paper.pdf", headers=cookie)
        self.assertEqual((status, body), (200, payload))
        self.assertIn("attachment", headers["Content-Disposition"])
        status, _, _ = self.request("DELETE", "/api/files?name=paper.pdf", headers=cookie)
        self.assertEqual(status, 200)
        self.assertEqual(self.request("GET", "/api/download?name=paper.pdf", headers=cookie)[0], 404)

    def test_upload_requires_login_and_custom_header(self):
        self.assertEqual(self.request("PUT", "/api/upload?name=a.txt", b"x")[0], 401)
        self.assertEqual(self.request("PUT", "/api/upload?name=a.txt", b"x", {"Cookie": self.cookie()})[0], 403)

    def test_upload_path_traversal_stays_inside_folder(self):
        cookie = {"Cookie": self.cookie(), "X-Eden": "1"}
        status, _, _ = self.request("PUT", "/api/upload?name=../../../escape.txt", b"data", cookie)
        self.assertEqual(status, 200)
        self.assertTrue(os.path.isfile(os.path.join(self.config.files_dir, "escape.txt")))
        self.assertFalse(os.path.exists(os.path.join(self.tmp.name, "escape.txt")))
        self.assertEqual(self.request("GET", "/api/download?name=../secret.txt", headers=cookie)[0], 404)

    def test_upload_size_limit(self):
        cookie = {"Cookie": self.cookie(), "X-Eden": "1"}
        status, _, _ = self.request("PUT", "/api/upload?name=big.bin", b"x" * 300000, cookie)
        self.assertEqual(status, 413)

    def test_safe_name(self):
        self.assertEqual(gateway.safe_name("../../etc/passwd"), "passwd")
        self.assertEqual(gateway.safe_name("C:\\dir\\file.docx"), "file.docx")
        self.assertEqual(gateway.safe_name(".."), "")
        self.assertEqual(gateway.safe_name("na<me>.pdf"), "name.pdf")

    # ---- static noVNC
    def test_static_files_and_traversal(self):
        cookie = {"Cookie": self.cookie()}
        status, _, body = self.request("GET", "/desktop/vnc.html", headers=cookie)
        self.assertEqual((status, body), (200, b"<html>vnc</html>"))
        self.assertEqual(self.request("GET", "/desktop/../secret.txt", headers=cookie)[0], 404)
        self.assertEqual(self.request("GET", "/desktop/%2e%2e/secret.txt", headers=cookie)[0], 404)

    # ---- websocket
    def ws_connect(self, cookie=None, protocols="binary"):
        sock = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        lines = ["GET /websockify HTTP/1.1", f"Host: 127.0.0.1:{self.port}", "Upgrade: websocket",
                 "Connection: Upgrade", f"Sec-WebSocket-Key: {key}", "Sec-WebSocket-Version: 13",
                 f"Sec-WebSocket-Protocol: {protocols}"]
        if cookie:
            lines.append(f"Cookie: {cookie}")
        sock.sendall(("\r\n".join(lines) + "\r\n\r\n").encode())
        reader = sock.makefile("rb")
        status_line = reader.readline().decode()
        headers = {}
        while True:
            line = reader.readline().decode().strip()
            if not line:
                break
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()
        return sock, reader, status_line, headers, key

    @staticmethod
    def client_frame(opcode, payload):
        mask = os.urandom(4)
        n = len(payload)
        head = bytes([0x80 | opcode])
        if n < 126:
            head += bytes([0x80 | n])
        elif n < 65536:
            head += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            head += bytes([0x80 | 127]) + struct.pack(">Q", n)
        return head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload))

    @staticmethod
    def read_frame(reader):
        h = reader.read(2)
        opcode, n = h[0] & 0x0F, h[1] & 0x7F
        if n == 126:
            n = struct.unpack(">H", reader.read(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", reader.read(8))[0]
        return opcode, reader.read(n)

    def test_websocket_requires_login(self):
        sock, _, status_line, _, _ = self.ws_connect()
        self.assertIn("401", status_line)
        sock.close()

    def test_websocket_proxy_round_trip(self):
        sock, reader, status_line, headers, key = self.ws_connect(self.cookie())
        self.assertIn("101", status_line)
        expected = base64.b64encode(hashlib.sha1((key + gateway.WS_GUID).encode()).digest()).decode()
        self.assertEqual(headers["sec-websocket-accept"], expected)
        self.assertEqual(headers["sec-websocket-protocol"], "binary")
        opcode, data = self.read_frame(reader)
        self.assertEqual((opcode, data), (2, b"RFB 003.008\n"))
        for size in (5, 300, 70000):  # 1-byte, 2-byte and 8-byte length encodings
            payload = os.urandom(size)
            sock.sendall(self.client_frame(2, payload))
            got = b""
            while len(got) < size:
                opcode, chunk = self.read_frame(reader)
                self.assertEqual(opcode, 2)
                got += chunk
            self.assertEqual(got, payload)
        sock.sendall(self.client_frame(9, b"hi"))  # ping -> pong
        opcode, data = self.read_frame(reader)
        self.assertEqual((opcode, data), (10, b"hi"))
        sock.sendall(self.client_frame(8, struct.pack(">H", 1000)))
        opcode, _ = self.read_frame(reader)
        self.assertEqual(opcode, 8)
        sock.close()

    def test_websocket_when_desktop_is_down(self):
        original = self.config.vnc_port
        self.config.vnc_port = free_port()
        try:
            sock, reader, status_line, _, _ = self.ws_connect(self.cookie())
            self.assertIn("101", status_line)
            opcode, data = self.read_frame(reader)
            self.assertEqual(opcode, 8)
            self.assertIn(b"not running", data)
            sock.close()
        finally:
            self.config.vnc_port = original

    def test_refuses_to_start_without_password(self):
        config = gateway.Config({})
        self.assertFalse(config.password or config.allow_no_password)


if __name__ == "__main__":
    unittest.main()
