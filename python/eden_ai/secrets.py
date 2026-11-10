from __future__ import annotations

import json
import os
import threading
from typing import Dict, Optional


class SecretStore:
    def get(self, name: str) -> Optional[str]:
        raise NotImplementedError

    def set(self, name: str, value: str) -> None:
        raise NotImplementedError

    def delete(self, name: str) -> None:
        raise NotImplementedError


class MemorySecretStore(SecretStore):
    def __init__(self) -> None:
        self._values: Dict[str, str] = {}
        self._lock = threading.Lock()

    def get(self, name: str) -> Optional[str]:
        with self._lock:
            return self._values.get(name)

    def set(self, name: str, value: str) -> None:
        with self._lock:
            self._values[name] = value

    def delete(self, name: str) -> None:
        with self._lock:
            self._values.pop(name, None)

    def names(self):
        with self._lock:
            return sorted(self._values)


class EnvSecretStore(SecretStore):
    def __init__(self, prefix: str = "EDEN_KEY_") -> None:
        self._prefix = prefix

    def _variable(self, name: str) -> str:
        return self._prefix + name.upper().replace("-", "_")

    def get(self, name: str) -> Optional[str]:
        value = os.environ.get(self._variable(name)) or os.environ.get(name)
        return value or None

    def set(self, name: str, value: str) -> None:
        os.environ[self._variable(name)] = value

    def delete(self, name: str) -> None:
        os.environ.pop(self._variable(name), None)


class FileSecretStore(SecretStore):
    def __init__(self, path: str) -> None:
        self._path = path
        self._lock = threading.Lock()

    def _load(self) -> Dict[str, str]:
        try:
            with open(self._path, "r", encoding="utf-8") as handle:
                data = json.load(handle)
            return {str(k): str(v) for k, v in data.items()}
        except (OSError, ValueError):
            return {}

    def _save(self, data: Dict[str, str]) -> None:
        directory = os.path.dirname(self._path)
        if directory:
            os.makedirs(directory, exist_ok=True)
        descriptor = os.open(self._path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            json.dump(data, handle)
        os.chmod(self._path, 0o600)

    def get(self, name: str) -> Optional[str]:
        with self._lock:
            return self._load().get(name)

    def set(self, name: str, value: str) -> None:
        with self._lock:
            data = self._load()
            data[name] = value
            self._save(data)

    def delete(self, name: str) -> None:
        with self._lock:
            data = self._load()
            if name in data:
                del data[name]
                self._save(data)


class ChainedSecretStore(SecretStore):
    def __init__(self, *stores: SecretStore) -> None:
        if not stores:
            raise ValueError("at least one store is required")
        self._stores = stores

    def get(self, name: str) -> Optional[str]:
        for store in self._stores:
            value = store.get(name)
            if value:
                return value
        return None

    def set(self, name: str, value: str) -> None:
        self._stores[0].set(name, value)

    def delete(self, name: str) -> None:
        for store in self._stores:
            store.delete(name)
