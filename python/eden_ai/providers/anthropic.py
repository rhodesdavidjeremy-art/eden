from __future__ import annotations

import json
import time
from typing import Dict, List, Optional

from ..errors import ProviderError
from ..http import iter_sse
from ..models import CancelToken, ChunkCallback, ProviderRequest, ProviderResponse, Usage
from .base import Provider, normalize_messages

API_VERSION = "2023-06-01"


class AnthropicProvider(Provider):
    kind = "anthropic"

    def _headers(self) -> Dict[str, str]:
        headers = {"x-api-key": self.require_key(), "anthropic-version": API_VERSION}
        headers.update(self.config.extra_headers)
        return headers

    def _payload(self, request: ProviderRequest, stream: bool) -> dict:
        payload: dict = {
            "model": self.model_for(request),
            "max_tokens": request.max_tokens,
            "temperature": request.temperature,
            "messages": [m.to_dict() for m in normalize_messages(request.messages)],
        }
        if request.system:
            payload["system"] = request.system
        if request.stop:
            payload["stop_sequences"] = list(request.stop)
        if stream:
            payload["stream"] = True
        return payload

    def _url(self) -> str:
        return self.config.base_url + "/v1/messages"

    def complete(self, request: ProviderRequest, cancel: Optional[CancelToken] = None) -> ProviderResponse:
        started = time.monotonic()
        data = self.http.request_json("POST", self._url(), headers=self._headers(),
                                      payload=self._payload(request, False), provider=self.name, cancel=cancel)
        if data.get("type") == "error":
            raise ProviderError(str(data.get("error", {}).get("message", "unknown error")), provider=self.name)
        parts: List[str] = []
        for block in data.get("content", []):
            if isinstance(block, dict) and block.get("type") == "text":
                parts.append(str(block.get("text", "")))
        usage = data.get("usage", {}) or {}
        return ProviderResponse(
            text="".join(parts),
            provider=self.name,
            model=str(data.get("model", self.model_for(request))),
            usage=Usage(int(usage.get("input_tokens", 0)), int(usage.get("output_tokens", 0))),
            latency_ms=int((time.monotonic() - started) * 1000),
            finish_reason=str(data.get("stop_reason") or ""),
        )

    def stream(self, request: ProviderRequest, on_chunk: ChunkCallback,
               cancel: Optional[CancelToken] = None) -> ProviderResponse:
        started = time.monotonic()
        lines = self.http.stream_lines("POST", self._url(), headers=self._headers(),
                                       payload=self._payload(request, True), provider=self.name, cancel=cancel)
        parts: List[str] = []
        input_tokens = 0
        output_tokens = 0
        finish = ""
        model = self.model_for(request)
        for event, raw in iter_sse(lines):
            try:
                data = json.loads(raw)
            except ValueError:
                continue
            kind = data.get("type", event)
            if kind == "message_start":
                message = data.get("message", {})
                model = str(message.get("model", model))
                input_tokens = int((message.get("usage") or {}).get("input_tokens", 0))
            elif kind == "content_block_delta":
                delta = data.get("delta", {})
                if delta.get("type") == "text_delta":
                    text = str(delta.get("text", ""))
                    if text:
                        parts.append(text)
                        on_chunk(text)
            elif kind == "message_delta":
                finish = str((data.get("delta") or {}).get("stop_reason") or finish)
                output_tokens = int((data.get("usage") or {}).get("output_tokens", output_tokens))
            elif kind == "error":
                raise ProviderError(str(data.get("error", {}).get("message", "stream error")),
                                    provider=self.name, retryable=False)
            elif kind == "message_stop":
                break
        return ProviderResponse("".join(parts), self.name, model, Usage(input_tokens, output_tokens),
                                int((time.monotonic() - started) * 1000), finish)
