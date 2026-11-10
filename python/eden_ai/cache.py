from __future__ import annotations

import hashlib
import json
import sqlite3
import threading
import time
from typing import Any, Callable, Dict, Optional


class ResultCache:
    def __init__(self, path: str = ":memory:", ttl_seconds: int = 60 * 60 * 24 * 30,
                 clock: Callable[[], float] = time.time) -> None:
        self._connection = sqlite3.connect(path, check_same_thread=False, isolation_level=None)
        self._connection.execute("PRAGMA journal_mode = WAL") if path != ":memory:" else None
        self._lock = threading.Lock()
        self._ttl = ttl_seconds
        self._clock = clock
        self._hits = 0
        self._misses = 0
        self._connection.executescript(
            """
            CREATE TABLE IF NOT EXISTS cache (
                key TEXT PRIMARY KEY,
                pack_id TEXT NOT NULL,
                function_id TEXT NOT NULL,
                provider TEXT NOT NULL,
                model TEXT NOT NULL,
                payload TEXT NOT NULL,
                created_at REAL NOT NULL,
                expires_at REAL NOT NULL,
                hits INTEGER NOT NULL DEFAULT 0
            );
            CREATE INDEX IF NOT EXISTS idx_cache_pack ON cache(pack_id);
            CREATE INDEX IF NOT EXISTS idx_cache_expiry ON cache(expires_at);
            """
        )

    @staticmethod
    def make_key(provider: str, model: str, function_id: str, fingerprint: str, options_part: str = "") -> str:
        digest = hashlib.sha256()
        for part in (provider, model, function_id, fingerprint, options_part):
            digest.update(part.encode("utf-8"))
            digest.update(b"\x00")
        return digest.hexdigest()

    def get(self, key: str) -> Optional[Dict[str, Any]]:
        now = self._clock()
        with self._lock:
            row = self._connection.execute(
                "SELECT payload, expires_at FROM cache WHERE key = ?", (key,)).fetchone()
            if row is None or row[1] <= now:
                if row is not None:
                    self._connection.execute("DELETE FROM cache WHERE key = ?", (key,))
                self._misses += 1
                return None
            self._connection.execute("UPDATE cache SET hits = hits + 1 WHERE key = ?", (key,))
            self._hits += 1
            return json.loads(row[0])

    def put(self, key: str, payload: Dict[str, Any], *, pack_id: str, function_id: str, provider: str,
            model: str, ttl_seconds: Optional[int] = None) -> None:
        now = self._clock()
        ttl = self._ttl if ttl_seconds is None else ttl_seconds
        with self._lock:
            self._connection.execute(
                "INSERT OR REPLACE INTO cache(key, pack_id, function_id, provider, model, payload, created_at, "
                "expires_at, hits) VALUES(?, ?, ?, ?, ?, ?, ?, ?, 0)",
                (key, pack_id, function_id, provider, model, json.dumps(payload), now, now + ttl))

    def invalidate_pack(self, pack_id: str) -> int:
        with self._lock:
            cursor = self._connection.execute("DELETE FROM cache WHERE pack_id = ?", (pack_id,))
            return cursor.rowcount

    def invalidate_function(self, function_id: str) -> int:
        with self._lock:
            cursor = self._connection.execute("DELETE FROM cache WHERE function_id = ?", (function_id,))
            return cursor.rowcount

    def purge_expired(self) -> int:
        with self._lock:
            cursor = self._connection.execute("DELETE FROM cache WHERE expires_at <= ?", (self._clock(),))
            return cursor.rowcount

    def clear(self) -> None:
        with self._lock:
            self._connection.execute("DELETE FROM cache")
            self._hits = 0
            self._misses = 0

    def stats(self) -> Dict[str, Any]:
        with self._lock:
            count = self._connection.execute("SELECT COUNT(*) FROM cache").fetchone()[0]
            by_provider = dict(self._connection.execute(
                "SELECT provider, COUNT(*) FROM cache GROUP BY provider").fetchall())
            return {"entries": count, "hits": self._hits, "misses": self._misses, "by_provider": by_provider}

    def close(self) -> None:
        with self._lock:
            self._connection.close()
