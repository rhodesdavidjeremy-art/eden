from __future__ import annotations

import json
import random
import socket
import time
import urllib.error
import urllib.request
from typing import Callable, Dict, Iterator, Optional, Tuple

from .errors import AuthError, NetworkError, ProviderError, RateLimitError
from .models import CancelToken


def classify_status(status: int, body: str, provider: str, retry_after: float = 0.0) -> ProviderError:
    snippet = body.strip()[:500]
    message = f"{provider or 'http'} returned status {status}: {snippet}"
    if status in (401, 403):
        return AuthError(message, status=status, provider=provider)
    if status == 429:
        return RateLimitError(message, status=status, retryable=True, retry_after=retry_after, provider=provider)
    if status >= 500 or status == 408:
        return ProviderError(message, status=status, retryable=True, provider=provider)
    return ProviderError(message, status=status, retryable=False, provider=provider)


def _retry_after(headers) -> float:
    value = headers.get("Retry-After") if headers is not None else None
    try:
        return max(0.0, float(value)) if value else 0.0
    except ValueError:
        return 0.0


class HttpClient:
    def __init__(self, *, timeout: float = 60.0, max_retries: int = 2, backoff: float = 0.5,
                 max_backoff: float = 8.0, sleep: Callable[[float], None] = time.sleep,
                 user_agent: str = "EdenResearchReader/0.1") -> None:
        self.timeout = timeout
        self.max_retries = max_retries
        self.backoff = backoff
        self.max_backoff = max_backoff
        self._sleep = sleep
        self.user_agent = user_agent

    def _build(self, method: str, url: str, headers: Dict[str, str], body: Optional[bytes]):
        merged = {"User-Agent": self.user_agent}
        merged.update(headers)
        return urllib.request.Request(url, data=body, headers=merged, method=method)

    def _delay(self, attempt: int, retry_after: float) -> float:
        base = min(self.max_backoff, self.backoff * (2 ** attempt))
        jitter = random.uniform(0, base * 0.25)
        return max(retry_after, base + jitter)

    def request(self, method: str, url: str, *, headers: Optional[Dict[str, str]] = None,
                body: Optional[bytes] = None, provider: str = "",
                cancel: Optional[CancelToken] = None) -> Tuple[int, bytes]:
        attempt = 0
        while True:
            if cancel is not None:
                cancel.check()
            try:
                request = self._build(method, url, headers or {}, body)
                with urllib.request.urlopen(request, timeout=self.timeout) as response:
                    return response.status, response.read()
            except urllib.error.HTTPError as error:
                payload = error.read().decode("utf-8", "replace")
                failure = classify_status(error.code, payload, provider, _retry_after(error.headers))
            except (urllib.error.URLError, socket.timeout, ConnectionError, TimeoutError) as error:
                failure = NetworkError(f"{provider or 'http'} connection failed: {error}", retryable=True,
                                       provider=provider)
            if not failure.retryable or attempt >= self.max_retries:
                raise failure
            self._sleep(self._delay(attempt, failure.retry_after))
            attempt += 1

    def request_json(self, method: str, url: str, *, headers: Optional[Dict[str, str]] = None,
                     payload: Optional[dict] = None, provider: str = "",
                     cancel: Optional[CancelToken] = None) -> dict:
        merged = {"Accept": "application/json"}
        body = None
        if payload is not None:
            merged["Content-Type"] = "application/json"
            body = json.dumps(payload).encode("utf-8")
        merged.update(headers or {})
        _, raw = self.request(method, url, headers=merged, body=body, provider=provider, cancel=cancel)
        try:
            return json.loads(raw.decode("utf-8"))
        except ValueError as error:
            raise ProviderError(f"{provider or 'http'} returned invalid json: {error}", provider=provider)

    def stream_lines(self, method: str, url: str, *, headers: Optional[Dict[str, str]] = None,
                     payload: Optional[dict] = None, provider: str = "",
                     cancel: Optional[CancelToken] = None) -> Iterator[str]:
        merged = {"Accept": "text/event-stream", "Content-Type": "application/json"}
        merged.update(headers or {})
        body = json.dumps(payload).encode("utf-8") if payload is not None else None
        attempt = 0
        while True:
            if cancel is not None:
                cancel.check()
            try:
                request = self._build(method, url, merged, body)
                response = urllib.request.urlopen(request, timeout=self.timeout)
                break
            except urllib.error.HTTPError as error:
                text = error.read().decode("utf-8", "replace")
                failure = classify_status(error.code, text, provider, _retry_after(error.headers))
            except (urllib.error.URLError, socket.timeout, ConnectionError, TimeoutError) as error:
                failure = NetworkError(f"{provider or 'http'} connection failed: {error}", retryable=True,
                                       provider=provider)
            if not failure.retryable or attempt >= self.max_retries:
                raise failure
            self._sleep(self._delay(attempt, failure.retry_after))
            attempt += 1
        try:
            while True:
                if cancel is not None and cancel.cancelled:
                    cancel.check()
                line = response.readline()
                if not line:
                    return
                yield line.decode("utf-8", "replace").rstrip("\r\n")
        finally:
            response.close()


def iter_sse(lines: Iterator[str]) -> Iterator[Tuple[str, str]]:
    event = "message"
    data_lines = []
    for line in lines:
        if line == "":
            if data_lines:
                yield event, "\n".join(data_lines)
            event = "message"
            data_lines = []
            continue
        if line.startswith(":"):
            continue
        field, _, value = line.partition(":")
        value = value[1:] if value.startswith(" ") else value
        if field == "event":
            event = value
        elif field == "data":
            data_lines.append(value)
    if data_lines:
        yield event, "\n".join(data_lines)
