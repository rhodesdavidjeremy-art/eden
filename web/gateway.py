#!/usr/bin/env python3
"""Eden web gateway.

One password-protected port that puts the real Eden Research Reader (running unchanged inside the container)
in a browser tab:

  /            the desktop (noVNC) with links to Files and Record
  /websockify  WebSocket -> local VNC server (what noVNC talks to)
  /files       upload documents / download exports (the app's file dialogs start in the same folder)
  /record      record audio or video in the browser and send it to the same folder
  /healthz     liveness probe (no login)

Standard library only. Configuration through environment variables (see CONFIG below).
"""
import base64
import hashlib
import hmac
import html
import http.cookies
import http.server
import json
import mimetypes
import os
import secrets
import socket
import socketserver
import struct
import sys
import threading
import time
import urllib.parse

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
COOKIE = "eden_session"
SESSION_SECONDS = 7 * 24 * 3600


class Config:
    def __init__(self, env=None):
        env = os.environ if env is None else env
        self.password = env.get("EDEN_WEB_PASSWORD", "")
        self.allow_no_password = env.get("EDEN_WEB_ALLOW_NO_PASSWORD", "") == "1"
        self.host = env.get("EDEN_WEB_HOST", "0.0.0.0")
        self.port = int(env.get("EDEN_WEB_PORT", "8080"))
        self.vnc_port = int(env.get("EDEN_VNC_PORT", "5900"))
        self.files_dir = env.get("EDEN_FILES_DIR", "/data/files")
        self.novnc_dir = env.get("EDEN_NOVNC_DIR", "/usr/share/novnc")
        self.max_upload = int(env.get("EDEN_WEB_MAX_UPLOAD", str(2 * 1024 ** 3)))
        self.cookie_secure = env.get("EDEN_WEB_COOKIE_SECURE", "") == "1"
        self.secret = secrets.token_bytes(32)  # sessions end when the container restarts


# --------------------------------------------------------------------------- pages

STYLE = """
:root{color-scheme:dark light;--bg:#14161a;--fg:#e8e8ea;--mut:#9aa0a6;--acc:#6aa5ff;--card:#1e2127;--bd:#2f343c}
@media (prefers-color-scheme:light){:root{--bg:#f6f7f9;--fg:#1b1d21;--mut:#5f6670;--acc:#1f6feb;--card:#fff;--bd:#d8dce2}}
*{box-sizing:border-box}body{margin:0;font:16px/1.45 system-ui,sans-serif;background:var(--bg);color:var(--fg)}
a{color:var(--acc)}main{max-width:760px;margin:0 auto;padding:20px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:16px;margin:14px 0}
button,.btn{font:inherit;background:var(--acc);color:#fff;border:0;border-radius:8px;padding:9px 16px;cursor:pointer;text-decoration:none;display:inline-block}
button.alt{background:transparent;color:var(--fg);border:1px solid var(--bd)}
input[type=password]{font:inherit;width:100%;padding:10px;border-radius:8px;border:1px solid var(--bd);background:var(--bg);color:var(--fg)}
table{width:100%;border-collapse:collapse}td,th{padding:8px 6px;border-bottom:1px solid var(--bd);text-align:left;word-break:break-all}
.mut{color:var(--mut);font-size:14px}.err{color:#ff6b6b}
"""


def page(title, body, extra_head=""):
    return (
        "<!doctype html><html lang=en><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        f"<title>{html.escape(title)}</title><style>{STYLE}</style>{extra_head}</head><body>{body}</body></html>"
    ).encode("utf-8")


def login_page(message=""):
    note = f"<p class=err>{html.escape(message)}</p>" if message else ""
    return page(
        "Eden Research Reader",
        "<main><div class=card><h2>Eden Research Reader</h2>"
        f"{note}<form method=post action=/login><p><input type=password name=password placeholder=Password "
        "autofocus autocomplete=current-password></p><p><button type=submit>Open</button></p></form></div></main>",
    )


def home_page():
    bar = (
        "<div style='display:flex;gap:10px;align-items:center;padding:6px 10px;background:var(--card);"
        "border-bottom:1px solid var(--bd)'><b>Eden</b><span style='flex:1'></span>"
        "<a href=/files target=_blank>Files</a><a href=/record target=_blank>Record</a><a href=/logout>Log out</a></div>"
    )
    frame = (
        "<iframe title=Eden src='/desktop/vnc.html?autoconnect=1&resize=scale&reconnect=1&path=websockify' "
        "style='border:0;width:100%;height:calc(100vh - 38px)' allow='clipboard-read; clipboard-write'></iframe>"
    )
    return page("Eden Research Reader", bar + frame)


FILES_PAGE = page(
    "Eden files",
    """<main>
<p><a href=/>&larr; Back to Eden</a></p>
<div class=card><h2>Send files to Eden</h2>
<p class=mut>Documents (PDF, DOCX, EPUB, TXT, MD, EDEN) and media you upload here show up in Eden's file dialogs,
which open in this same folder. Exports you save from Eden appear in the list below.</p>
<input type=file id=pick multiple> <button id=send>Upload</button>
<progress id=prog value=0 max=1 style='width:100%;display:none'></progress><p id=msg class=mut></p></div>
<div class=card><h2>Your files</h2><table id=list></table><p id=empty class=mut></p></div>
<script>
const $=id=>document.getElementById(id);
function size(n){return n>1048576?(n/1048576).toFixed(1)+' MB':n>1024?(n/1024).toFixed(0)+' KB':n+' B'}
async function refresh(){
  const r=await fetch('/api/files');const items=await r.json();const t=$('list');t.textContent='';
  $('empty').textContent=items.length?'':'No files yet.';
  for(const f of items){
    const tr=t.insertRow();
    const a=document.createElement('a');a.href='/api/download?name='+encodeURIComponent(f.name);a.textContent=f.name;
    tr.insertCell().appendChild(a);tr.insertCell().textContent=size(f.size);
    const b=document.createElement('button');b.className='alt';b.textContent='Delete';
    b.onclick=async()=>{if(!confirm('Delete '+f.name+'?'))return;
      await fetch('/api/files?name='+encodeURIComponent(f.name),{method:'DELETE',headers:{'X-Eden':'1'}});refresh()};
    tr.insertCell().appendChild(b);}
}
function put(file){return new Promise((ok,fail)=>{
  const x=new XMLHttpRequest();x.open('PUT','/api/upload?name='+encodeURIComponent(file.name));
  x.setRequestHeader('X-Eden','1');
  x.upload.onprogress=e=>{if(e.lengthComputable){$('prog').value=e.loaded/e.total}};
  x.onload=()=>x.status<300?ok():fail(new Error(x.responseText||x.status));x.onerror=()=>fail(new Error('network'));x.send(file)})}
$('send').onclick=async()=>{
  const files=[...$('pick').files];if(!files.length){$('msg').textContent='Choose a file first.';return}
  $('prog').style.display='block';
  for(const f of files){$('msg').textContent='Uploading '+f.name+' ...';$('prog').value=0;
    try{await put(f)}catch(e){$('msg').textContent='Failed: '+e.message;return}}
  $('msg').textContent='Done.';$('prog').style.display='none';$('pick').value='';refresh()};
refresh();
</script></main>""",
)

RECORD_PAGE = page(
    "Eden recorder",
    """<main>
<p><a href=/>&larr; Back to Eden</a></p>
<div class=card><h2>Record a comment</h2>
<p class=mut>Your browser records; the file is sent to Eden's file folder. In Eden, use the attach button and pick it.
(Recording needs https or localhost.)</p>
<p><button id=a>Record audio</button> <button id=v>Record video</button> <button id=s class=alt disabled>Stop &amp; upload</button></p>
<video id=prev muted playsinline style='width:100%;display:none;border-radius:8px'></video><p id=msg class=mut></p></div>
<script>
const $=id=>document.getElementById(id);let rec,chunks=[],stream,kind;
async function begin(video){
  try{stream=await navigator.mediaDevices.getUserMedia({audio:true,video:video});}
  catch(e){$('msg').textContent='Cannot use the microphone/camera: '+e.message;return}
  kind=video?'video':'audio';chunks=[];rec=new MediaRecorder(stream);
  rec.ondataavailable=e=>{if(e.data.size)chunks.push(e.data)};rec.start(1000);
  if(video){$('prev').srcObject=stream;$('prev').style.display='block';$('prev').play()}
  $('s').disabled=false;$('a').disabled=$('v').disabled=true;$('msg').textContent='Recording...'}
$('a').onclick=()=>begin(false);$('v').onclick=()=>begin(true);
$('s').onclick=()=>{rec.onstop=async()=>{
  stream.getTracks().forEach(t=>t.stop());$('prev').style.display='none';
  const blob=new Blob(chunks,{type:rec.mimeType||'video/webm'});
  const name='recording-'+new Date().toISOString().replace(/[:.]/g,'-')+'.webm';
  $('msg').textContent='Uploading '+name+' ...';
  const r=await fetch('/api/upload?name='+encodeURIComponent(name),{method:'PUT',headers:{'X-Eden':'1'},body:blob});
  $('msg').textContent=r.ok?'Saved as '+name+'. Attach it from Eden.':'Upload failed ('+r.status+')';
  $('a').disabled=$('v').disabled=false;$('s').disabled=true};rec.stop()};
</script></main>""",
)


# --------------------------------------------------------------------------- helpers

def safe_name(name):
    """A plain file name, never a path."""
    name = os.path.basename(name.replace("\\", "/")).strip().strip(".")
    name = "".join(ch for ch in name if ch >= " " and ch not in '<>:"|?*')
    return name[:200]


def sign(secret, expiry):
    return hmac.new(secret, str(expiry).encode(), hashlib.sha256).hexdigest()


def make_token(secret, now=None):
    expiry = int((now if now is not None else time.time()) + SESSION_SECONDS)
    return f"{expiry}.{sign(secret, expiry)}"


def valid_token(secret, token, now=None):
    try:
        expiry_text, signature = token.split(".", 1)
        expiry = int(expiry_text)
    except (ValueError, AttributeError):
        return False
    if expiry < (now if now is not None else time.time()):
        return False
    return hmac.compare_digest(signature, sign(secret, expiry))


def unmask(data, key):
    if not data:
        return b""
    n = len(data)
    repeated = (key * (n // 4 + 1))[:n]
    return (int.from_bytes(data, "big") ^ int.from_bytes(repeated, "big")).to_bytes(n, "big")


def ws_frame(opcode, payload=b""):
    head = bytes([0x80 | opcode])
    n = len(payload)
    if n < 126:
        head += bytes([n])
    elif n < 65536:
        head += bytes([126]) + struct.pack(">H", n)
    else:
        head += bytes([127]) + struct.pack(">Q", n)
    return head + payload


class FailureLimiter:
    """Slows down password guessing: 8 failures per minute, then 429."""

    def __init__(self, limit=8, window=60.0):
        self.limit, self.window, self.stamps, self.lock = limit, window, [], threading.Lock()

    def blocked(self, now=None):
        now = time.time() if now is None else now
        with self.lock:
            self.stamps = [t for t in self.stamps if now - t < self.window]
            return len(self.stamps) >= self.limit

    def record(self, now=None):
        with self.lock:
            self.stamps.append(time.time() if now is None else now)


# --------------------------------------------------------------------------- server

class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, config):
        self.config = config
        self.limiter = FailureLimiter()
        os.makedirs(config.files_dir, exist_ok=True)
        super().__init__((config.host, config.port), Handler)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "EdenGateway"

    def log_message(self, fmt, *args):
        if os.environ.get("EDEN_WEB_ACCESS_LOG") == "1":
            sys.stderr.write("gateway: " + (fmt % args) + "\n")

    # -- plumbing
    @property
    def cfg(self):
        return self.server.config

    def send_bytes(self, status, body, content_type="text/html; charset=utf-8", headers=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def redirect(self, location, headers=None):
        self.send_bytes(303, b"", headers={"Location": location, **(headers or {})})

    def send_json(self, status, payload):
        self.send_bytes(status, json.dumps(payload).encode(), "application/json")

    def authed(self):
        if not self.cfg.password:
            return True  # only reachable with EDEN_WEB_ALLOW_NO_PASSWORD=1
        jar = http.cookies.SimpleCookie()
        try:
            jar.load(self.headers.get("Cookie", ""))
        except http.cookies.CookieError:
            return False
        morsel = jar.get(COOKIE)
        return bool(morsel) and valid_token(self.cfg.secret, morsel.value)

    def secure_cookie(self):
        return self.cfg.cookie_secure or self.headers.get("X-Forwarded-Proto", "").lower() == "https"

    def session_cookie(self, token, max_age):
        flags = f"{COOKIE}={token}; Path=/; HttpOnly; SameSite=Strict; Max-Age={max_age}"
        return flags + ("; Secure" if self.secure_cookie() else "")

    def require_custom_header(self):
        if self.headers.get("X-Eden") != "1":
            self.send_json(403, {"error": "missing X-Eden header"})
            return False
        return True

    # -- routing
    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        path = url.path
        if path == "/healthz":
            return self.send_bytes(200, b"ok", "text/plain")
        if path == "/login":
            return self.send_bytes(200, login_page())
        if path == "/logout":
            return self.redirect("/login", {"Set-Cookie": self.session_cookie("", 0)})
        if not self.authed():
            if path == "/websockify" or path.startswith("/api/"):
                return self.send_json(401, {"error": "login required"})
            return self.redirect("/login")
        if path == "/":
            return self.send_bytes(200, home_page())
        if path == "/files":
            return self.send_bytes(200, FILES_PAGE)
        if path == "/record":
            return self.send_bytes(200, RECORD_PAGE)
        if path == "/websockify":
            return self.websocket_proxy()
        if path == "/api/files":
            return self.api_list()
        if path == "/api/download":
            return self.api_download(urllib.parse.parse_qs(url.query))
        if path.startswith("/desktop/"):
            return self.static_file(path[len("/desktop/"):])
        self.send_bytes(404, page("Not found", "<main><p>Not found.</p></main>"))

    def do_POST(self):
        if urllib.parse.urlsplit(self.path).path != "/login":
            return self.send_bytes(404, b"not found", "text/plain")
        length = min(int(self.headers.get("Content-Length") or 0), 4096)
        form = urllib.parse.parse_qs(self.rfile.read(length).decode("utf-8", "replace"))
        if self.server.limiter.blocked():
            return self.send_bytes(429, login_page("Too many attempts. Wait a minute."))
        supplied = (form.get("password") or [""])[0]
        if self.cfg.password and hmac.compare_digest(supplied.encode(), self.cfg.password.encode()):
            return self.redirect("/", {"Set-Cookie": self.session_cookie(make_token(self.cfg.secret), SESSION_SECONDS)})
        self.server.limiter.record()
        self.send_bytes(401, login_page("Wrong password."))

    def do_PUT(self):
        url = urllib.parse.urlsplit(self.path)
        if not self.authed():
            return self.send_json(401, {"error": "login required"})
        if url.path != "/api/upload":
            return self.send_json(404, {"error": "not found"})
        if not self.require_custom_header():
            return
        name = safe_name((urllib.parse.parse_qs(url.query).get("name") or [""])[0])
        if not name:
            return self.send_json(400, {"error": "missing file name"})
        try:
            length = int(self.headers.get("Content-Length", ""))
        except ValueError:
            return self.send_json(411, {"error": "Content-Length required"})
        if length < 0 or length > self.cfg.max_upload:
            return self.send_json(413, {"error": "file too large"})
        target = os.path.join(self.cfg.files_dir, name)
        temporary = target + ".part"
        remaining = length
        with open(temporary, "wb") as out:
            while remaining > 0:
                chunk = self.rfile.read(min(remaining, 1 << 20))
                if not chunk:
                    break
                out.write(chunk)
                remaining -= len(chunk)
        if remaining:
            os.remove(temporary)
            self.close_connection = True
            return self.send_json(400, {"error": "upload interrupted"})
        os.replace(temporary, target)
        self.send_json(200, {"name": name, "size": length})

    def do_DELETE(self):
        url = urllib.parse.urlsplit(self.path)
        if not self.authed():
            return self.send_json(401, {"error": "login required"})
        if url.path != "/api/files":
            return self.send_json(404, {"error": "not found"})
        if not self.require_custom_header():
            return
        name = safe_name((urllib.parse.parse_qs(url.query).get("name") or [""])[0])
        target = os.path.join(self.cfg.files_dir, name)
        if not name or not os.path.isfile(target):
            return self.send_json(404, {"error": "no such file"})
        os.remove(target)
        self.send_json(200, {"deleted": name})

    # -- files
    def api_list(self):
        items = []
        for entry in sorted(os.scandir(self.cfg.files_dir), key=lambda e: e.name.lower()):
            if entry.is_file() and not entry.name.endswith(".part"):
                info = entry.stat()
                items.append({"name": entry.name, "size": info.st_size, "mtime": int(info.st_mtime)})
        self.send_json(200, items)

    def api_download(self, query):
        name = safe_name((query.get("name") or [""])[0])
        target = os.path.join(self.cfg.files_dir, name)
        if not name or not os.path.isfile(target):
            return self.send_json(404, {"error": "no such file"})
        size = os.path.getsize(target)
        quoted = urllib.parse.quote(name)
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(size))
        self.send_header("Content-Disposition", f"attachment; filename*=UTF-8''{quoted}")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        with open(target, "rb") as source:
            while True:
                chunk = source.read(1 << 20)
                if not chunk:
                    break
                self.wfile.write(chunk)

    # -- noVNC static files
    def static_file(self, relative):
        root = os.path.realpath(self.cfg.novnc_dir)
        target = os.path.realpath(os.path.join(root, urllib.parse.unquote(relative) or "vnc.html"))
        if os.path.commonpath([root, target]) != root or not os.path.isfile(target):
            return self.send_bytes(404, b"not found", "text/plain")
        kind = mimetypes.guess_type(target)[0] or "application/octet-stream"
        with open(target, "rb") as source:
            self.send_bytes(200, source.read(), kind, {"Cache-Control": "public, max-age=3600"})

    # -- WebSocket <-> VNC
    def websocket_proxy(self):
        key = self.headers.get("Sec-WebSocket-Key")
        if self.headers.get("Upgrade", "").lower() != "websocket" or not key:
            return self.send_bytes(400, b"WebSocket upgrade required", "text/plain")
        accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
        offered = [p.strip() for p in self.headers.get("Sec-WebSocket-Protocol", "").split(",") if p.strip()]
        lines = ["HTTP/1.1 101 Switching Protocols", "Upgrade: websocket", "Connection: Upgrade",
                 f"Sec-WebSocket-Accept: {accept}"]
        if "binary" in offered:
            lines.append("Sec-WebSocket-Protocol: binary")
        self.connection.sendall(("\r\n".join(lines) + "\r\n\r\n").encode())
        self.close_connection = True
        send_lock = threading.Lock()

        def send(opcode, payload=b""):
            with send_lock:
                self.connection.sendall(ws_frame(opcode, payload))

        try:
            upstream = socket.create_connection(("127.0.0.1", self.cfg.vnc_port), timeout=5)
        except OSError:
            send(8, struct.pack(">H", 1011) + b"desktop is not running")
            return
        upstream.settimeout(None)
        done = threading.Event()

        def pump_to_browser():
            try:
                while not done.is_set():
                    data = upstream.recv(65536)
                    if not data:
                        break
                    send(2, data)
            except OSError:
                pass
            finally:
                done.set()
                try:
                    send(8, struct.pack(">H", 1000))
                except OSError:
                    pass
                try:
                    self.connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass

        threading.Thread(target=pump_to_browser, daemon=True).start()
        message = b""
        try:
            while not done.is_set():
                header = self.rfile.read(2)
                if len(header) < 2:
                    break
                opcode, masked, length = header[0] & 0x0F, header[1] & 0x80, header[1] & 0x7F
                if length == 126:
                    length = struct.unpack(">H", self.rfile.read(2))[0]
                elif length == 127:
                    length = struct.unpack(">Q", self.rfile.read(8))[0]
                mask_key = self.rfile.read(4) if masked else b""
                payload = self.rfile.read(length) if length else b""
                if len(payload) < length:
                    break
                if masked:
                    payload = unmask(payload, mask_key)
                if opcode in (0, 1, 2):
                    message += payload
                    if header[0] & 0x80:
                        upstream.sendall(message)
                        message = b""
                elif opcode == 8:
                    send(8, payload[:2])
                    break
                elif opcode == 9:
                    send(10, payload)
        except OSError:
            pass
        finally:
            done.set()
            try:
                upstream.close()
            except OSError:
                pass


def main():
    config = Config()
    if not config.password and not config.allow_no_password:
        sys.exit("Set EDEN_WEB_PASSWORD (or EDEN_WEB_ALLOW_NO_PASSWORD=1 for a private network only).")
    server = Server(config)
    sys.stderr.write(f"gateway: listening on {config.host}:{config.port}\n")
    server.serve_forever()


if __name__ == "__main__":
    main()
