from __future__ import annotations

import json
import re
from typing import Any, List, Optional

from .errors import ParseError

_FENCE = re.compile(r"^```[a-zA-Z0-9_-]*\s*\n(.*?)\n?```\s*$", re.DOTALL)
_BULLET = re.compile(r"^\s*(?:[-*\u2022]|\d+[.)])\s+(.*\S)\s*$")


def strip_fences(text: str) -> str:
    stripped = text.strip()
    match = _FENCE.match(stripped)
    return match.group(1).strip() if match else stripped


def extract_json(text: str) -> Any:
    candidate = strip_fences(text)
    try:
        return json.loads(candidate)
    except ValueError:
        pass
    for opener, closer in (("{", "}"), ("[", "]")):
        start = candidate.find(opener)
        end = candidate.rfind(closer)
        if start != -1 and end > start:
            try:
                return json.loads(candidate[start:end + 1])
            except ValueError:
                continue
    raise ParseError("response did not contain valid json")


def split_list(text: str) -> List[str]:
    items: List[str] = []
    for line in strip_fences(text).splitlines():
        match = _BULLET.match(line)
        if match:
            items.append(match.group(1).strip())
        elif line.strip() and items and line.startswith((" ", "\t")):
            items[-1] += " " + line.strip()
    if not items:
        items = [line.strip() for line in text.splitlines() if line.strip()]
    return items


def extract_mermaid(text: str) -> str:
    stripped = text.strip()
    match = re.search(r"```mermaid\s*\n(.*?)```", stripped, re.DOTALL)
    if match:
        return match.group(1).strip()
    return strip_fences(stripped)


def clip(text: str, limit: int) -> str:
    if len(text) <= limit:
        return text
    cut = text.rfind(" ", 0, limit - 3)
    cut = cut if cut > limit // 2 else limit - 3
    return text[:cut].rstrip() + "..."


def first_sentences(text: str, count: int) -> str:
    parts = re.split(r"(?<=[.!?])\s+", text.strip())
    return " ".join(parts[:count])


def normalize_whitespace(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def estimate_tokens(text: str) -> int:
    return max(1, (len(text) + 3) // 4)


def coerce_optional_str(value: Any) -> Optional[str]:
    return value if isinstance(value, str) and value else None
