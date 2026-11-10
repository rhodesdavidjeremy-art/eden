from __future__ import annotations

import html
import re
import threading
import urllib.parse
from dataclasses import dataclass, field
from html.parser import HTMLParser
from typing import Dict, Iterable, List, Optional, Sequence, Set

from .config import SearchConfig
from .errors import InvalidRequestError, ProviderError
from .http import HttpClient
from .models import CancelToken

STOPWORDS = frozenset(
    "a an and are as at be by for from has have he her his i in is it its of on or she that the their them they this "
    "to was were which who will with you your we our not but if then than so such these those there been being into "
    "out up down over under again further once here when where why how all any both each few more most other some "
    "no nor only own same too very can just should now".split())


@dataclass
class SearchResult:
    title: str
    url: str
    snippet: str = ""
    source: str = ""
    year: str = ""
    authors: List[str] = field(default_factory=list)
    doi: str = ""
    image_url: str = ""
    kind: str = "web"

    def to_dict(self) -> dict:
        return {
            "title": self.title, "url": self.url, "snippet": self.snippet, "source": self.source,
            "year": self.year, "authors": list(self.authors), "doi": self.doi, "image_url": self.image_url,
            "kind": self.kind, "domain": domain_of(self.url),
        }


def domain_of(url: str) -> str:
    host = urllib.parse.urlparse(url if "://" in url else "//" + url).hostname or ""
    host = host.lower()
    return host[4:] if host.startswith("www.") else host


def normalize_url(url: str) -> str:
    parsed = urllib.parse.urlparse(url)
    query = urllib.parse.parse_qsl(parsed.query)
    query = [(k, v) for k, v in query if not k.lower().startswith("utm_")]
    path = parsed.path.rstrip("/")
    return urllib.parse.urlunparse((parsed.scheme.lower(), (parsed.hostname or "").lower().removeprefix("www."), path,
                                    "", urllib.parse.urlencode(query), ""))


class SourcePolicy:
    def __init__(self, allow: Iterable[str] = (), block: Iterable[str] = ()) -> None:
        self._allow: Set[str] = {domain_of(d) or d.lower() for d in allow if d}
        self._block: Set[str] = {domain_of(d) or d.lower() for d in block if d}
        self._lock = threading.Lock()

    @staticmethod
    def _matches(host: str, rule: str) -> bool:
        return host == rule or host.endswith("." + rule)

    def allowed(self, url: str) -> bool:
        host = domain_of(url)
        with self._lock:
            if any(self._matches(host, rule) for rule in self._block):
                return False
            if self._allow:
                return any(self._matches(host, rule) for rule in self._allow)
            return True

    def add(self, domain: str, kind: str) -> None:
        rule = domain_of(domain) or domain.lower()
        with self._lock:
            if kind == "allow":
                self._allow.add(rule)
                self._block.discard(rule)
            elif kind == "block":
                self._block.add(rule)
                self._allow.discard(rule)
            else:
                raise InvalidRequestError("rule kind must be 'allow' or 'block'")

    def remove(self, domain: str) -> None:
        rule = domain_of(domain) or domain.lower()
        with self._lock:
            self._allow.discard(rule)
            self._block.discard(rule)

    def snapshot(self) -> Dict[str, List[str]]:
        with self._lock:
            return {"allow": sorted(self._allow), "block": sorted(self._block)}


class SearchBackend:
    name = "base"
    kinds: Sequence[str] = ("web",)

    def __init__(self, config: SearchConfig, http: Optional[HttpClient] = None) -> None:
        self.config = config
        self.http = http or HttpClient(timeout=config.timeout, max_retries=1, user_agent=config.user_agent)

    def search(self, query: str, limit: int, cancel: Optional[CancelToken] = None) -> List[SearchResult]:
        raise NotImplementedError


class _DuckParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.results: List[SearchResult] = []
        self._current: Optional[SearchResult] = None
        self._capture: Optional[str] = None
        self._buffer: List[str] = []

    def handle_starttag(self, tag: str, attrs) -> None:
        attributes = dict(attrs)
        classes = (attributes.get("class") or "").split()
        if tag == "a" and "result__a" in classes:
            self._flush()
            self._current = SearchResult("", unwrap_duck_url(attributes.get("href") or ""), source="duckduckgo")
            self._capture = "title"
            self._buffer = []
        elif tag in ("a", "div") and "result__snippet" in classes and self._current is not None:
            self._capture = "snippet"
            self._buffer = []

    def handle_endtag(self, tag: str) -> None:
        if self._capture == "title" and tag == "a" and self._current is not None:
            self._current.title = "".join(self._buffer).strip()
            self._capture = None
        elif self._capture == "snippet" and tag in ("a", "div") and self._current is not None:
            self._current.snippet = re.sub(r"\s+", " ", "".join(self._buffer)).strip()
            self._capture = None

    def handle_data(self, data: str) -> None:
        if self._capture:
            self._buffer.append(data)

    def _flush(self) -> None:
        if self._current is not None and self._current.title and self._current.url:
            self.results.append(self._current)
        self._current = None

    def close(self) -> None:
        super().close()
        self._flush()


def unwrap_duck_url(href: str) -> str:
    if href.startswith("//"):
        href = "https:" + href
    parsed = urllib.parse.urlparse(href)
    if parsed.path.startswith("/l/"):
        target = urllib.parse.parse_qs(parsed.query).get("uddg")
        if target:
            return target[0]
    return href


class DuckDuckGoBackend(SearchBackend):
    name = "duckduckgo"
    kinds = ("web", "news", "video")

    def search(self, query: str, limit: int, cancel: Optional[CancelToken] = None) -> List[SearchResult]:
        url = self.config.duckduckgo_url + "?" + urllib.parse.urlencode({"q": query})
        _, body = self.http.request("GET", url, headers={"Accept": "text/html"}, provider=self.name, cancel=cancel)
        parser = _DuckParser()
        parser.feed(body.decode("utf-8", "replace"))
        parser.close()
        return parser.results[:limit]


class WikipediaBackend(SearchBackend):
    name = "wikipedia"
    kinds = ("web", "discover")

    def search(self, query: str, limit: int, cancel: Optional[CancelToken] = None) -> List[SearchResult]:
        params = {"action": "query", "list": "search", "srsearch": query, "srlimit": str(limit), "format": "json"}
        data = self.http.request_json("GET", self.config.wikipedia_url + "?" + urllib.parse.urlencode(params),
                                      provider=self.name, cancel=cancel)
        base = self.config.wikipedia_url.rsplit("/w/", 1)[0] if "/w/" in self.config.wikipedia_url else self.config.wikipedia_url
        results = []
        for item in data.get("query", {}).get("search", []):
            title = str(item.get("title", ""))
            snippet = html.unescape(re.sub(r"<[^>]+>", "", str(item.get("snippet", ""))))
            results.append(SearchResult(title, f"{base}/wiki/{urllib.parse.quote(title.replace(' ', '_'))}", snippet,
                                        "wikipedia"))
        return results[:limit]


class SemanticScholarBackend(SearchBackend):
    name = "semantic_scholar"
    kinds = ("academic",)

    def search(self, query: str, limit: int, cancel: Optional[CancelToken] = None) -> List[SearchResult]:
        params = {"query": query, "limit": str(limit), "fields": "title,authors,year,abstract,externalIds,url"}
        data = self.http.request_json("GET", self.config.semantic_scholar_url + "?" + urllib.parse.urlencode(params),
                                      provider=self.name, cancel=cancel)
        results = []
        for item in data.get("data", []) or []:
            external = item.get("externalIds") or {}
            results.append(SearchResult(
                title=str(item.get("title", "")),
                url=str(item.get("url", "")),
                snippet=str(item.get("abstract") or "")[:500],
                source="semantic_scholar",
                year=str(item.get("year") or ""),
                authors=[str(a.get("name", "")) for a in item.get("authors", []) if a.get("name")],
                doi=str(external.get("DOI", "")),
                kind="academic"))
        return [r for r in results if r.title and r.url][:limit]


class CrossrefBackend(SearchBackend):
    name = "crossref"
    kinds = ("academic",)

    def search(self, query: str, limit: int, cancel: Optional[CancelToken] = None) -> List[SearchResult]:
        params = {"query": query, "rows": str(limit)}
        data = self.http.request_json("GET", self.config.crossref_url + "?" + urllib.parse.urlencode(params),
                                      provider=self.name, cancel=cancel)
        results = []
        for item in (data.get("message", {}).get("items", []) or []):
            titles = item.get("title") or []
            doi = str(item.get("DOI", ""))
            issued = (item.get("issued", {}).get("date-parts") or [[None]])[0]
            authors = []
            for author in item.get("author", []) or []:
                name = " ".join(x for x in (author.get("given"), author.get("family")) if x)
                if name:
                    authors.append(name)
            if titles and doi:
                results.append(SearchResult(str(titles[0]), "https://doi.org/" + doi,
                                            str((item.get("container-title") or [""])[0]), "crossref",
                                            str(issued[0] or ""), authors, doi, kind="academic"))
        return results[:limit]


class CommonsImageBackend(SearchBackend):
    name = "commons"
    kinds = ("image",)

    def search(self, query: str, limit: int, cancel: Optional[CancelToken] = None) -> List[SearchResult]:
        params = {"action": "query", "generator": "search", "gsrnamespace": "6", "gsrsearch": query,
                  "gsrlimit": str(limit), "prop": "imageinfo", "iiprop": "url|extmetadata", "format": "json"}
        data = self.http.request_json("GET", self.config.commons_url + "?" + urllib.parse.urlencode(params),
                                      provider=self.name, cancel=cancel)
        results = []
        for page in (data.get("query", {}).get("pages", {}) or {}).values():
            info = (page.get("imageinfo") or [{}])[0]
            if not info.get("url"):
                continue
            results.append(SearchResult(str(page.get("title", "")).replace("File:", ""),
                                        str(info.get("descriptionurl", info["url"])), "", "commons",
                                        image_url=str(info["url"]), kind="image"))
        return results[:limit]


def keywords(text: str, count: int = 6) -> List[str]:
    frequencies: Dict[str, int] = {}
    for word in re.findall(r"[A-Za-z][A-Za-z'-]{3,}", text):
        lowered = word.lower()
        if lowered in STOPWORDS:
            continue
        frequencies[lowered] = frequencies.get(lowered, 0) + 1
    ranked = sorted(frequencies.items(), key=lambda item: (-item[1], -len(item[0]), item[0]))
    return [word for word, _ in ranked[:count]]


class SearchService:
    MODES = ("web", "academic", "news", "image", "video", "discover")

    def __init__(self, config: SearchConfig, policy: Optional[SourcePolicy] = None,
                 http: Optional[HttpClient] = None) -> None:
        self.config = config
        self.policy = policy or SourcePolicy()
        shared = http
        self._web = DuckDuckGoBackend(config, shared)
        self._wiki = WikipediaBackend(config, shared)
        self._scholar = SemanticScholarBackend(config, shared)
        self._crossref = CrossrefBackend(config, shared)
        self._images = CommonsImageBackend(config, shared)

    def _backends(self, mode: str) -> List[SearchBackend]:
        table = {
            "web": [self._web, self._wiki],
            "news": [self._web],
            "video": [self._web],
            "academic": [self._scholar, self._crossref],
            "image": [self._images],
            "discover": [self._wiki],
        }
        if mode not in table:
            raise InvalidRequestError(f"unknown search mode: {mode}")
        return table[mode]

    @staticmethod
    def _shape_query(query: str, mode: str) -> str:
        if mode == "news":
            return query + " news"
        if mode == "video":
            return query + " site:youtube.com OR site:vimeo.com"
        return query

    def search(self, query: str, mode: str = "web", limit: int = 8,
               cancel: Optional[CancelToken] = None) -> Dict[str, object]:
        query = query.strip()
        if not query:
            raise InvalidRequestError("search query is empty")
        limit = max(1, min(limit, 25))
        shaped = self._shape_query(query, mode)
        per_backend: List[List[SearchResult]] = []
        errors: Dict[str, str] = {}
        for backend in self._backends(mode):
            try:
                per_backend.append(backend.search(shaped, limit, cancel))
            except ProviderError as error:
                errors[backend.name] = str(error)
        merged: List[SearchResult] = []
        seen: Set[str] = set()
        blocked = 0
        depth = max((len(group) for group in per_backend), default=0)
        for index in range(depth):
            for group in per_backend:
                if index >= len(group):
                    continue
                result = group[index]
                key = normalize_url(result.url)
                if key in seen:
                    continue
                if not self.policy.allowed(result.url):
                    blocked += 1
                    continue
                seen.add(key)
                merged.append(result)
        return {"query": query, "mode": mode, "results": [r.to_dict() for r in merged[:limit]],
                "blocked": blocked, "errors": errors}

    def discover(self, text: str, limit: int = 5, seed: int = 0,
                 cancel: Optional[CancelToken] = None) -> Dict[str, object]:
        words = keywords(text, 8)
        if not words:
            raise InvalidRequestError("not enough text to find related topics")
        start = seed % len(words)
        chosen = [words[(start + i) % len(words)] for i in range(min(2, len(words)))]
        return self.search(" ".join(chosen), "discover", limit, cancel)
