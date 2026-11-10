from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

from .errors import ConfigError


@dataclass
class ProviderConfig:
    name: str
    kind: str
    base_url: str
    model: str
    enabled: bool = True
    timeout: float = 60.0
    max_retries: int = 2
    requires_key: bool = True
    extra_headers: Dict[str, str] = field(default_factory=dict)

    @staticmethod
    def from_dict(data: Dict[str, Any]) -> "ProviderConfig":
        for key in ("name", "kind", "base_url", "model"):
            if not data.get(key):
                raise ConfigError(f"provider config is missing '{key}'")
        if data["kind"] not in ("anthropic", "openai_compat", "xai"):
            raise ConfigError(f"unknown provider kind: {data['kind']}")
        return ProviderConfig(
            name=str(data["name"]),
            kind=str(data["kind"]),
            base_url=str(data["base_url"]).rstrip("/"),
            model=str(data["model"]),
            enabled=bool(data.get("enabled", True)),
            timeout=float(data.get("timeout", 60.0)),
            max_retries=int(data.get("max_retries", 2)),
            requires_key=bool(data.get("requires_key", True)),
            extra_headers={str(k): str(v) for k, v in dict(data.get("extra_headers", {})).items()},
        )


@dataclass
class SearchConfig:
    duckduckgo_url: str = "https://html.duckduckgo.com/html/"
    wikipedia_url: str = "https://en.wikipedia.org/w/api.php"
    semantic_scholar_url: str = "https://api.semanticscholar.org/graph/v1/paper/search"
    crossref_url: str = "https://api.crossref.org/works"
    commons_url: str = "https://commons.wikimedia.org/w/api.php"
    timeout: float = 15.0
    user_agent: str = "EdenResearchReader/0.1 (research reader)"


@dataclass
class ImageConfig:
    base_url: str = "https://api.openai.com/v1"
    model: str = "gpt-image-1"
    size: str = "1024x1024"
    cache_dir: str = ""
    timeout: float = 120.0


@dataclass
class EngineConfig:
    default_provider: str = "claude"
    providers: List[ProviderConfig] = field(default_factory=list)
    cache_path: str = ":memory:"
    cache_ttl_seconds: int = 60 * 60 * 24 * 30
    max_workers: int = 8
    search: SearchConfig = field(default_factory=SearchConfig)
    images: ImageConfig = field(default_factory=ImageConfig)
    allow_domains: List[str] = field(default_factory=list)
    block_domains: List[str] = field(default_factory=list)

    @staticmethod
    def defaults() -> "EngineConfig":
        return EngineConfig(
            providers=[
                ProviderConfig("claude", "anthropic", "https://api.anthropic.com", "claude-sonnet-5-5"),
                ProviderConfig("grok", "xai", "https://api.x.ai/v1", "grok-4"),
                ProviderConfig("local", "openai_compat", "http://127.0.0.1:11434/v1", "llama3.1",
                               requires_key=False, timeout=180.0),
            ]
        )

    @staticmethod
    def from_dict(data: Dict[str, Any]) -> "EngineConfig":
        base = EngineConfig.defaults()
        if "providers" in data:
            base.providers = [ProviderConfig.from_dict(item) for item in data["providers"]]
        base.default_provider = str(data.get("default_provider", base.default_provider))
        base.cache_path = str(data.get("cache_path", base.cache_path))
        base.cache_ttl_seconds = int(data.get("cache_ttl_seconds", base.cache_ttl_seconds))
        base.max_workers = max(1, int(data.get("max_workers", base.max_workers)))
        base.allow_domains = [str(x) for x in data.get("allow_domains", [])]
        base.block_domains = [str(x) for x in data.get("block_domains", [])]
        for key, target in (("search", base.search), ("images", base.images)):
            for name, value in dict(data.get(key, {})).items():
                if hasattr(target, name):
                    setattr(target, name, type(getattr(target, name))(value))
        names = [p.name for p in base.providers]
        if len(set(names)) != len(names):
            raise ConfigError("provider names must be unique")
        return base


def load_config(path: Optional[str]) -> EngineConfig:
    if not path:
        return EngineConfig.defaults()
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return EngineConfig.from_dict(json.load(handle))
    except OSError as error:
        raise ConfigError(f"cannot read config {path}: {error}")
    except ValueError as error:
        raise ConfigError(f"invalid config json: {error}")
