from __future__ import annotations

from typing import Optional


class EdenAiError(Exception):
    code = "internal"

    def __init__(self, message: str, *, details: Optional[dict] = None):
        super().__init__(message)
        self.message = message
        self.details = details or {}

    def to_dict(self) -> dict:
        return {"code": self.code, "message": self.message, "details": self.details}


class ConfigError(EdenAiError):
    code = "config"


class NotFoundError(EdenAiError):
    code = "not_found"


class InvalidRequestError(EdenAiError):
    code = "invalid_argument"


class Cancelled(EdenAiError):
    code = "cancelled"


class ProviderError(EdenAiError):
    code = "provider"

    def __init__(self, message: str, *, status: int = 0, retryable: bool = False,
                 retry_after: float = 0.0, provider: str = "", details: Optional[dict] = None):
        super().__init__(message, details=details)
        self.status = status
        self.retryable = retryable
        self.retry_after = retry_after
        self.provider = provider

    def to_dict(self) -> dict:
        data = super().to_dict()
        data.update({"status": self.status, "retryable": self.retryable, "provider": self.provider})
        return data


class AuthError(ProviderError):
    code = "auth"


class RateLimitError(ProviderError):
    code = "rate_limit"


class NetworkError(ProviderError):
    code = "network"


class ParseError(EdenAiError):
    code = "parse"
