from __future__ import annotations

import json
import time
from typing import Dict, List, Optional

from ..errors import ProviderError
from ..http import iter_sse
from ..models import CancelToken, ChunkCallback, ProviderRequest, ProviderResponse, Usage
from .base import Provider, normalize_messages


def _content_text(content) -> str:
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        return "".join(str(part.get("text", "")) for part in content if isinstance(part, dict))
    return ""


class OpenAICompatProvider(Provider):
    kind = "openai_compat"

    def _headers(self) -> Dict[str, str]:
        headers: Dict[str, str] = {}
        key = self.api_key()
        if key:
            headers["Authorization"] = "Bearer " + key
        elif self.config.requires_key:
            self.require_key()
        headers.update(self.config.extra_headers)
        return headers

    def _payload(self, request: ProviderRequest, stream: bool) -> dict:
        messages: List[dict] = []
        if request.system:
            messages.append({"role": "system", "content": request.system})
        messages.extend(m.to_dict() for m in normalize_messages(request.messages))
        payload: dict = {
            "model": self.model_for(request),
            "messages": messages,
            "max_tokens": request.max_tokens,
            "temperature": request.temperature,
        }
        if request.stop:
            payload["stop"] = list(request.stop)
        if stream:
            payload["stream"] = True
            payload["stream_options"] = {"include_usage": True}
        return payload

    def _url(self) -> str:
        return self.config.base_url + "/chat/completions"

    def complete(self, request: ProviderRequest, cancel: Optional[CancelToken] = None) -> ProviderResponse:
        started = time.monotonic()
        data = self.http.request_json("POST", self._url(), headers=self._headers(),
                                      payload=self._payload(request, False), provider=self.name, cancel=cancel)
        if "error" in data and "choices" not in data:
            error = data["error"]
            message = error.get("message") if isinstance(error, dict) else str(error)
            raise ProviderError(str(message), provider=self.name)
        choices = data.get("choices") or []
        if not choices:
            raise ProviderError("provider returned no choices", provider=self.name)
        choice = choices[0]
        usage = data.get("usage", {}) or {}
        return ProviderResponse(
            text=_content_text((choice.get("message") or {}).get("content")),
            provider=self.name,
            model=str(data.get("model", self.model_for(request))),
            usage=Usage(int(usage.get("prompt_tokens", 0)), int(usage.get("completion_tokens", 0))),
            latency_ms=int((time.monotonic() - started) * 1000),
            finish_reason=str(choice.get("finish_reason") or ""),
        )

    def stream(self, request: ProviderRequest, on_chunk: ChunkCallback,
               cancel: Optional[CancelToken] = None) -> ProviderResponse:
        started = time.monotonic()
        lines = self.http.stream_lines("POST", self._url(), headers=self._headers(),
                                       payload=self._payload(request, True), provider=self.name, cancel=cancel)
        parts: List[str] = []
        usage = Usage()
        finish = ""
        model = self.model_for(request)
        for _, raw in iter_sse(lines):
            if raw.strip() == "[DONE]":
                break
            try:
                data = json.loads(raw)
            except ValueError:
                continue
            if "error" in data:
                error = data["error"]
                message = error.get("message") if isinstance(error, dict) else str(error)
                raise ProviderError(str(message), provider=self.name)
            model = str(data.get("model", model))
            if data.get("usage"):
                usage = Usage(int(data["usage"].get("prompt_tokens", 0)), int(data["usage"].get("completion_tokens", 0)))
            for choice in data.get("choices") or []:
                text = _content_text((choice.get("delta") or {}).get("content"))
                if text:
                    parts.append(text)
                    on_chunk(text)
                if choice.get("finish_reason"):
                    finish = str(choice["finish_reason"])
        return ProviderResponse("".join(parts), self.name, model, usage,
                                int((time.monotonic() - started) * 1000), finish)


class XaiProvider(OpenAICompatProvider):
    kind = "xai"
