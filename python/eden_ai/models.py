from __future__ import annotations

import hashlib
import threading
from dataclasses import dataclass, field, asdict
from typing import Any, Callable, Dict, List, Optional


@dataclass(frozen=True)
class PackContext:
    pack_id: str
    text: str
    doc_title: str = ""
    author: str = ""
    chapter: str = ""
    page: int = 0
    before: str = ""
    after: str = ""
    language: str = "en"

    def fingerprint(self) -> str:
        digest = hashlib.sha256()
        for part in (self.text, self.doc_title, self.chapter, self.language):
            digest.update(part.encode("utf-8"))
            digest.update(b"\x00")
        return digest.hexdigest()

    @staticmethod
    def from_dict(data: Dict[str, Any]) -> "PackContext":
        text = data.get("text", "")
        if not isinstance(text, str) or not text.strip():
            from .errors import InvalidRequestError
            raise InvalidRequestError("pack text is required")
        return PackContext(
            pack_id=str(data.get("pack_id", "")),
            text=text,
            doc_title=str(data.get("doc_title", "")),
            author=str(data.get("author", "")),
            chapter=str(data.get("chapter", "")),
            page=int(data.get("page", 0) or 0),
            before=str(data.get("before", "")),
            after=str(data.get("after", "")),
            language=str(data.get("language", "en") or "en"),
        )


@dataclass
class Message:
    role: str
    content: str

    def to_dict(self) -> Dict[str, str]:
        return {"role": self.role, "content": self.content}


@dataclass
class ProviderRequest:
    messages: List[Message]
    system: str = ""
    model: str = ""
    max_tokens: int = 1024
    temperature: float = 0.3
    stop: List[str] = field(default_factory=list)


@dataclass
class Usage:
    input_tokens: int = 0
    output_tokens: int = 0

    def add(self, other: "Usage") -> "Usage":
        return Usage(self.input_tokens + other.input_tokens, self.output_tokens + other.output_tokens)

    def to_dict(self) -> Dict[str, int]:
        return {"input_tokens": self.input_tokens, "output_tokens": self.output_tokens}


@dataclass
class ProviderResponse:
    text: str
    provider: str
    model: str
    usage: Usage = field(default_factory=Usage)
    latency_ms: int = 0
    finish_reason: str = ""


@dataclass
class FunctionResult:
    function_id: str
    provider: str
    model: str
    text: str
    usage: Usage = field(default_factory=Usage)
    latency_ms: int = 0
    cached: bool = False
    data: Optional[Any] = None
    warnings: List[str] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        out = {
            "function_id": self.function_id,
            "provider": self.provider,
            "model": self.model,
            "text": self.text,
            "usage": self.usage.to_dict(),
            "latency_ms": self.latency_ms,
            "cached": self.cached,
        }
        if self.data is not None:
            out["data"] = self.data
        if self.warnings:
            out["warnings"] = list(self.warnings)
        return out


class CancelToken:
    def __init__(self) -> None:
        self._event = threading.Event()

    def cancel(self) -> None:
        self._event.set()

    @property
    def cancelled(self) -> bool:
        return self._event.is_set()

    def check(self) -> None:
        if self._event.is_set():
            from .errors import Cancelled
            raise Cancelled("operation cancelled")


ChunkCallback = Callable[[str], None]


def to_plain(value: Any) -> Any:
    if hasattr(value, "__dataclass_fields__"):
        return asdict(value)
    return value
