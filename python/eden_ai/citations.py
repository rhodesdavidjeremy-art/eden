from __future__ import annotations

import difflib
import re
import string
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

from .errors import InvalidRequestError

SUPPORTED_STYLES = ("apa", "mla", "chicago", "harvard")


@dataclass
class CitationMeta:
    title: str
    authors: List[str] = field(default_factory=list)
    year: str = ""
    publisher: str = ""
    container: str = ""
    url: str = ""
    doi: str = ""
    volume: str = ""
    issue: str = ""
    pages: str = ""
    accessed: str = ""
    kind: str = "book"

    @staticmethod
    def from_dict(data: dict) -> "CitationMeta":
        if not str(data.get("title", "")).strip():
            raise InvalidRequestError("citation metadata needs a title")
        authors = data.get("authors", [])
        if isinstance(authors, str):
            authors = [authors]
        return CitationMeta(
            title=str(data["title"]).strip(),
            authors=[str(a).strip() for a in authors if str(a).strip()],
            year=str(data.get("year", "")).strip(),
            publisher=str(data.get("publisher", "")).strip(),
            container=str(data.get("container", "")).strip(),
            url=str(data.get("url", "")).strip(),
            doi=str(data.get("doi", "")).strip(),
            volume=str(data.get("volume", "")).strip(),
            issue=str(data.get("issue", "")).strip(),
            pages=str(data.get("pages", "")).strip(),
            accessed=str(data.get("accessed", "")).strip(),
            kind=str(data.get("kind", "book")).strip() or "book",
        )


def split_name(name: str) -> Tuple[str, str]:
    name = name.strip()
    if "," in name:
        last, _, first = name.partition(",")
        return last.strip(), first.strip()
    parts = name.split()
    if len(parts) == 1:
        return parts[0], ""
    return parts[-1], " ".join(parts[:-1])


def initials(first: str) -> str:
    out = []
    for piece in re.split(r"[\s]+", first.strip()):
        if not piece:
            continue
        for part in piece.split("-"):
            if part:
                out.append(part[0].upper() + ".")
    return " ".join(out)


def _sentence(text: str) -> str:
    text = text.strip()
    if not text:
        return ""
    return text if text[-1] in ".?!" else text + "."


def _doi_url(meta: CitationMeta) -> str:
    if meta.doi:
        return meta.doi if meta.doi.startswith("http") else "https://doi.org/" + meta.doi
    return meta.url


def _apa_authors(authors: List[str]) -> str:
    formatted = []
    for name in authors:
        last, first = split_name(name)
        formatted.append(f"{last}, {initials(first)}".strip().rstrip(","))
    if not formatted:
        return ""
    if len(formatted) == 1:
        return formatted[0]
    if len(formatted) <= 20:
        return ", ".join(formatted[:-1]) + ", & " + formatted[-1]
    return ", ".join(formatted[:19]) + ", ... " + formatted[-1]


def _mla_authors(authors: List[str]) -> str:
    if not authors:
        return ""
    first_last, first_first = split_name(authors[0])
    lead = f"{first_last}, {first_first}".rstrip(", ")
    if len(authors) == 1:
        return lead
    if len(authors) == 2:
        last, first = split_name(authors[1])
        return f"{lead}, and {first} {last}".replace("  ", " ")
    return lead + ", et al"


def _chicago_authors(authors: List[str]) -> str:
    if not authors:
        return ""
    last, first = split_name(authors[0])
    lead = f"{last}, {first}".rstrip(", ")
    rest = []
    for name in authors[1:]:
        l, f = split_name(name)
        rest.append(f"{f} {l}".strip())
    if not rest:
        return lead
    if len(rest) == 1:
        return f"{lead}, and {rest[0]}"
    return f"{lead}, " + ", ".join(rest[:-1]) + f", and {rest[-1]}"


def _harvard_authors(authors: List[str]) -> str:
    formatted = []
    for name in authors:
        last, first = split_name(name)
        formatted.append(f"{last}, {initials(first)}".strip().rstrip(","))
    if not formatted:
        return ""
    if len(formatted) == 1:
        return formatted[0]
    if len(formatted) > 3:
        return formatted[0] + " et al."
    return ", ".join(formatted[:-1]) + " and " + formatted[-1]


def format_apa(meta: CitationMeta) -> str:
    authors = _apa_authors(meta.authors)
    year = f"({meta.year})" if meta.year else "(n.d.)"
    parts = []
    lead = f"{authors} {year}." if authors else f"{meta.title}. {year}."
    parts.append(lead)
    if authors:
        parts.append(_sentence(meta.title))
    if meta.container:
        detail = meta.container
        if meta.volume:
            detail += f", {meta.volume}"
            if meta.issue:
                detail += f"({meta.issue})"
        if meta.pages:
            detail += f", {meta.pages}"
        parts.append(_sentence(detail))
    elif meta.publisher:
        parts.append(_sentence(meta.publisher))
    link = _doi_url(meta)
    if link:
        parts.append(link)
    return " ".join(p for p in parts if p)


def format_mla(meta: CitationMeta) -> str:
    authors = _mla_authors(meta.authors)
    parts = []
    if authors:
        parts.append(_sentence(authors))
    parts.append(_sentence(meta.title))
    details = []
    if meta.container:
        details.append(meta.container)
    if meta.volume:
        details.append(f"vol. {meta.volume}")
    if meta.issue:
        details.append(f"no. {meta.issue}")
    if meta.publisher and not meta.container:
        details.append(meta.publisher)
    if meta.year:
        details.append(meta.year)
    if meta.pages:
        details.append(f"pp. {meta.pages}")
    link = _doi_url(meta)
    if link:
        details.append(link)
    if details:
        parts.append(_sentence(", ".join(details)))
    return " ".join(parts)


def format_chicago(meta: CitationMeta) -> str:
    authors = _chicago_authors(meta.authors)
    parts = []
    if authors:
        parts.append(_sentence(authors))
    parts.append(_sentence(meta.title))
    if meta.container:
        detail = meta.container
        if meta.volume:
            detail += f" {meta.volume}"
            if meta.issue:
                detail += f", no. {meta.issue}"
        if meta.year:
            detail += f" ({meta.year})"
        if meta.pages:
            detail += f": {meta.pages}"
        parts.append(_sentence(detail))
    else:
        publication = ", ".join(x for x in (meta.publisher, meta.year) if x)
        if publication:
            parts.append(_sentence(publication))
    link = _doi_url(meta)
    if link:
        parts.append(link + ".")
    return " ".join(parts)


def format_harvard(meta: CitationMeta) -> str:
    authors = _harvard_authors(meta.authors)
    year = meta.year or "n.d."
    parts = []
    parts.append(f"{authors} ({year})" if authors else f"{meta.title} ({year})")
    if authors:
        parts.append(_sentence(meta.title))
    if meta.container:
        detail = meta.container
        if meta.volume:
            detail += f", {meta.volume}"
            if meta.issue:
                detail += f"({meta.issue})"
        if meta.pages:
            detail += f", pp. {meta.pages}"
        parts.append(_sentence(detail))
    elif meta.publisher:
        parts.append(_sentence(meta.publisher))
    link = _doi_url(meta)
    if link:
        available = f"Available at: {link}"
        if meta.accessed:
            available += f" (Accessed: {meta.accessed})"
        parts.append(available + ".")
    return " ".join(parts)


def format_custom(meta: CitationMeta, template: str) -> str:
    values = {
        "authors": ", ".join(meta.authors),
        "title": meta.title,
        "year": meta.year,
        "publisher": meta.publisher,
        "container": meta.container,
        "url": _doi_url(meta),
        "doi": meta.doi,
        "volume": meta.volume,
        "issue": meta.issue,
        "pages": meta.pages,
        "accessed": meta.accessed,
    }
    rendered = string.Template(template).safe_substitute(values)
    return re.sub(r"\s+", " ", rendered).strip()


def format_citation(meta: CitationMeta, style: str, template: str = "") -> str:
    key = style.lower()
    if key == "custom":
        if not template:
            raise InvalidRequestError("custom citation style needs a template")
        return format_custom(meta, template)
    formatter = {"apa": format_apa, "mla": format_mla, "chicago": format_chicago, "harvard": format_harvard}.get(key)
    if formatter is None:
        raise InvalidRequestError(f"unsupported citation style: {style}")
    return formatter(meta)


def _fold(text: str) -> Tuple[str, List[int]]:
    folded: List[str] = []
    mapping: List[int] = []
    replacements = {"\u2018": "'", "\u2019": "'", "\u201c": '"', "\u201d": '"', "\u2013": "-", "\u2014": "-",
                    "\u00a0": " ", "\u2026": "..."}
    previous_space = True
    for index, char in enumerate(text):
        char = replacements.get(char, char)
        for piece in char:
            if piece.isspace():
                if previous_space:
                    continue
                folded.append(" ")
                mapping.append(index)
                previous_space = True
            else:
                folded.append(piece.lower())
                mapping.append(index)
                previous_space = False
    while folded and folded[-1] == " ":
        folded.pop()
        mapping.pop()
    return "".join(folded), mapping


@dataclass
class QuoteLocation:
    start: int
    end: int
    exact: bool
    score: float


def locate_quote(text: str, quote: str, min_ratio: float = 0.88) -> Optional[QuoteLocation]:
    needle, _ = _fold(quote)
    if not needle:
        return None
    haystack, mapping = _fold(text)
    position = haystack.find(needle)
    if position != -1:
        start = mapping[position]
        end = mapping[position + len(needle) - 1] + 1
        return QuoteLocation(start, end, text[start:end] == quote, 1.0)
    if len(needle) < 12:
        return None
    matcher = difflib.SequenceMatcher(None, haystack, needle, autojunk=False)
    best: Optional[Tuple[int, int, float]] = None
    step = max(1, len(needle) // 4)
    window = len(needle)
    for offset in range(0, max(1, len(haystack) - window // 2), step):
        chunk = haystack[offset:offset + window + window // 4]
        if not chunk:
            break
        local = difflib.SequenceMatcher(None, chunk, needle, autojunk=False)
        ratio = local.ratio()
        if best is None or ratio > best[2]:
            blocks = local.get_matching_blocks()
            first = next((b for b in blocks if b.size > 0), None)
            last = next((b for b in reversed(blocks) if b.size > 0), None)
            if first is None or last is None:
                continue
            best = (offset + first.a, offset + last.a + last.size, ratio)
    del matcher
    if best is None or best[2] < min_ratio * 0.85:
        return None
    start = mapping[min(best[0], len(mapping) - 1)]
    end = mapping[min(best[1] - 1, len(mapping) - 1)] + 1
    candidate = text[start:end]
    ratio = difflib.SequenceMatcher(None, _fold(candidate)[0], needle, autojunk=False).ratio()
    if ratio < min_ratio:
        return None
    return QuoteLocation(start, end, False, ratio)


def reference_label(title: str, page: int, offset: int) -> str:
    base = title.strip() or "Untitled"
    if page > 0:
        return f"{base}, p. {page}"
    return f"{base}, offset {offset}"
