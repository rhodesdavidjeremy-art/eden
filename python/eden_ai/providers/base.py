from __future__ import annotations

from typing import Dict, List, Optional

from ..config import ProviderConfig
from ..errors import AuthError
from ..http import HttpClient
from ..models import CancelToken, ChunkCallback, Message, ProviderRequest, ProviderResponse
from ..secrets import SecretStore


def normalize_messages(messages: List[Message]) -> List[Message]:
    merged: List[Message] = []
    for message in messages:
        if not message.content.strip():
            continue
        if merged and merged[-1].role == message.role:
            merged[-1] = Message(message.role, merged[-1].content + "\n\n" + message.content)
        else:
            merged.append(Message(message.role, message.content))
    if merged and merged[0].role != "user":
        merged.insert(0, Message("user", "Continue."))
    return merged


class Provider:
    kind = "base"

    def __init__(self, config: ProviderConfig, secrets: SecretStore, http: Optional[HttpClient] = None) -> None:
        self.config = config
        self.secrets = secrets
        self.http = http or HttpClient(timeout=config.timeout, max_retries=config.max_retries)

    @property
    def name(self) -> str:
        return self.config.name

    def api_key(self) -> Optional[str]:
        return self.secrets.get(self.config.name)

    def require_key(self) -> str:
        key = self.api_key()
        if not key and self.config.requires_key:
            raise AuthError(f"no api key configured for provider '{self.name}'", provider=self.name)
        return key or ""

    def available(self) -> bool:
        if not self.config.enabled:
            return False
        return bool(self.api_key()) or not self.config.requires_key

    def model_for(self, request: ProviderRequest) -> str:
        return request.model or self.config.model

    def complete(self, request: ProviderRequest, cancel: Optional[CancelToken] = None) -> ProviderResponse:
        raise NotImplementedError

    def stream(self, request: ProviderRequest, on_chunk: ChunkCallback,
               cancel: Optional[CancelToken] = None) -> ProviderResponse:
        response = self.complete(request, cancel)
        if response.text:
            on_chunk(response.text)
        return response

    def describe(self) -> Dict[str, object]:
        return {
            "name": self.name,
            "kind": self.config.kind,
            "model": self.config.model,
            "base_url": self.config.base_url,
            "enabled": self.config.enabled,
            "available": self.available(),
            "requires_key": self.config.requires_key,
        }
