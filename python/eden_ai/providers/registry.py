from __future__ import annotations

import threading
from typing import Callable, Dict, Iterable, List, Optional

from ..config import ProviderConfig
from ..errors import ConfigError, NotFoundError
from ..http import HttpClient
from ..secrets import SecretStore
from .anthropic import AnthropicProvider
from .base import Provider
from .openai_compat import OpenAICompatProvider, XaiProvider

_KINDS = {
    "anthropic": AnthropicProvider,
    "openai_compat": OpenAICompatProvider,
    "xai": XaiProvider,
}


def build_provider(config: ProviderConfig, secrets: SecretStore, http: Optional[HttpClient] = None) -> Provider:
    factory = _KINDS.get(config.kind)
    if factory is None:
        raise ConfigError(f"unknown provider kind: {config.kind}")
    return factory(config, secrets, http)


class ProviderRegistry:
    def __init__(self, secrets: SecretStore, http_factory: Optional[Callable[[ProviderConfig], HttpClient]] = None) -> None:
        self._secrets = secrets
        self._http_factory = http_factory
        self._providers: Dict[str, Provider] = {}
        self._lock = threading.RLock()

    def configure(self, configs: Iterable[ProviderConfig]) -> None:
        with self._lock:
            self._providers.clear()
            for config in configs:
                self.add(config)

    def add(self, config: ProviderConfig) -> Provider:
        http = self._http_factory(config) if self._http_factory else None
        provider = build_provider(config, self._secrets, http)
        with self._lock:
            self._providers[config.name] = provider
        return provider

    def get(self, name: str) -> Provider:
        with self._lock:
            provider = self._providers.get(name)
        if provider is None:
            raise NotFoundError(f"unknown provider: {name}")
        return provider

    def has(self, name: str) -> bool:
        with self._lock:
            return name in self._providers

    def names(self) -> List[str]:
        with self._lock:
            return list(self._providers)

    def available(self) -> List[str]:
        with self._lock:
            return [name for name, provider in self._providers.items() if provider.available()]

    def set_key(self, name: str, key: str) -> None:
        self.get(name)
        if key:
            self._secrets.set(name, key)
        else:
            self._secrets.delete(name)

    def describe(self) -> List[dict]:
        with self._lock:
            return [provider.describe() for provider in self._providers.values()]

    def remove(self, name: str) -> None:
        with self._lock:
            self._providers.pop(name, None)
