from __future__ import annotations

import base64
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Callable, Dict, List, Tuple
from urllib.parse import parse_qs, urlparse

PNG_1X1 = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==")

DDG_HTML = """
<html><body>
<div class="result"><a class="result__a" href="//duckduckgo.com/l/?uddg=https%3A%2F%2Fexample.org%2Feden%3Futm_source%3Dx&rut=1">Eden &amp; Gardens</a>
<a class="result__snippet" href="#">A <b>river</b> flowed out of Eden.</a></div>
<div class="result"><a class="result__a" href="https://blocked.test/page">Blocked Page</a>
<a class="result__snippet" href="#">Should be filtered.</a></div>
<div class="result"><a class="result__a" href="https://example.org/eden">Duplicate</a>
<a class="result__snippet" href="#">Same url as first.</a></div>
</body></html>
"""


class FakeApi:
    def __init__(self, reply: Callable[[str, Dict[str, Any]], str] = lambda kind, body: "reply text") -> None:
        self.reply = reply
        self.requests: List[Tuple[str, Dict[str, str], Any]] = []
        self.failures: Dict[str, List[Tuple[int, Dict[str, str]]]] = {}
        self._lock = threading.Lock()
        api = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.0"

            def log_message(self, *args):
                pass

            def _body(self):
                length = int(self.headers.get("Content-Length") or 0)
                raw = self.rfile.read(length) if length else b""
                try:
                    return json.loads(raw.decode("utf-8")) if raw else None
                except ValueError:
                    return None

            def _send(self, status: int, payload: bytes, content_type: str = "application/json", headers=None):
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(payload)))
                for key, value in (headers or {}).items():
                    self.send_header(key, value)
                self.end_headers()
                self.wfile.write(payload)

            def _route(self):
                parsed = urlparse(self.path)
                body = self._body() if self.command == "POST" else None
                with api._lock:
                    api.requests.append((parsed.path, {k.lower(): v for k, v in self.headers.items()}, body))
                    queue = api.failures.get(parsed.path)
                    failure = queue.pop(0) if queue else None
                if failure is not None:
                    self._send(failure[0], b'{"error": {"message": "scripted failure"}}', headers=failure[1])
                    return
                path = parsed.path
                if path == "/v1/messages":
                    text = api.reply("anthropic", body)
                    if body.get("stream"):
                        self._anthropic_stream(text)
                    else:
                        payload = {"id": "m1", "type": "message", "model": body["model"], "stop_reason": "end_turn",
                                   "content": [{"type": "text", "text": text}],
                                   "usage": {"input_tokens": 11, "output_tokens": 7}}
                        self._send(200, json.dumps(payload).encode())
                elif path == "/chat/completions":
                    text = api.reply("openai", body)
                    if body.get("stream"):
                        self._openai_stream(text, body)
                    else:
                        payload = {"model": body["model"], "choices": [{"message": {"content": text},
                                                                         "finish_reason": "stop"}],
                                   "usage": {"prompt_tokens": 5, "completion_tokens": 3}}
                        self._send(200, json.dumps(payload).encode())
                elif path == "/ddg":
                    self._send(200, DDG_HTML.encode(), "text/html")
                elif path == "/wiki":
                    payload = {"query": {"search": [
                        {"title": "Garden of Eden", "snippet": "The <span>garden</span> described in Genesis"},
                        {"title": "Pishon", "snippet": "A river"}]}}
                    self._send(200, json.dumps(payload).encode())
                elif path == "/scholar":
                    payload = {"data": [{"title": "Eden Studies", "url": "https://s2.test/p1", "year": 2019,
                                         "abstract": "An abstract.", "authors": [{"name": "A. Author"}],
                                         "externalIds": {"DOI": "10.1/abc"}}]}
                    self._send(200, json.dumps(payload).encode())
                elif path == "/crossref":
                    payload = {"message": {"items": [{"title": ["Eden Studies"], "DOI": "10.1/abc",
                                                      "issued": {"date-parts": [[2019]]},
                                                      "author": [{"given": "A.", "family": "Author"}],
                                                      "container-title": ["Journal of Eden"]},
                                                     {"title": ["Rivers"], "DOI": "10.2/xyz",
                                                      "issued": {"date-parts": [[2020]]}}]}}
                    self._send(200, json.dumps(payload).encode())
                elif path == "/commons":
                    payload = {"query": {"pages": {"1": {"title": "File:Eden.jpg", "imageinfo": [
                        {"url": "https://upload.test/eden.jpg", "descriptionurl": "https://commons.test/Eden"}]}}}}
                    self._send(200, json.dumps(payload).encode())
                elif path == "/images/generations":
                    payload = {"data": [{"b64_json": base64.b64encode(PNG_1X1).decode()}]}
                    self._send(200, json.dumps(payload).encode())
                else:
                    self._send(404, b'{"error": "not found"}')

            def _anthropic_stream(self, text: str):
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.end_headers()
                events = [("message_start", {"type": "message_start", "message": {"model": "fake-model",
                                                                                   "usage": {"input_tokens": 9}}})]
                for piece in [text[i:i + 6] for i in range(0, len(text), 6)]:
                    events.append(("content_block_delta", {"type": "content_block_delta",
                                                           "delta": {"type": "text_delta", "text": piece}}))
                events.append(("message_delta", {"type": "message_delta", "delta": {"stop_reason": "end_turn"},
                                                 "usage": {"output_tokens": 4}}))
                events.append(("message_stop", {"type": "message_stop"}))
                for name, data in events:
                    self.wfile.write(f"event: {name}\ndata: {json.dumps(data)}\n\n".encode())
                    self.wfile.flush()

            def _openai_stream(self, text: str, body):
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.end_headers()
                for piece in [text[i:i + 6] for i in range(0, len(text), 6)]:
                    chunk = {"model": body["model"], "choices": [{"delta": {"content": piece}}]}
                    self.wfile.write(f"data: {json.dumps(chunk)}\n\n".encode())
                final = {"model": body["model"], "choices": [{"delta": {}, "finish_reason": "stop"}],
                         "usage": {"prompt_tokens": 6, "completion_tokens": 2}}
                self.wfile.write(f"data: {json.dumps(final)}\n\ndata: [DONE]\n\n".encode())
                self.wfile.flush()

            do_GET = _route
            do_POST = _route

        self.httpd = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    @property
    def url(self) -> str:
        return f"http://127.0.0.1:{self.httpd.server_address[1]}"

    def fail_next(self, path: str, status: int, count: int = 1, headers=None) -> None:
        with self._lock:
            self.failures.setdefault(path, []).extend([(status, headers or {})] * count)

    def count(self, path: str) -> int:
        with self._lock:
            return sum(1 for p, _, _ in self.requests if p == path)

    def close(self) -> None:
        self.httpd.shutdown()
        self.httpd.server_close()
