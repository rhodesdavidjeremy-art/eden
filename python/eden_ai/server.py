from __future__ import annotations

import concurrent.futures
import json
import os
import sys
import threading
import traceback
from typing import Any, Callable, Dict, IO, Optional

from . import __version__
from .cache import ResultCache
from .citations import CitationMeta, format_citation, locate_quote
from .config import EngineConfig, ProviderConfig
from .engine import ResearchEngine
from .errors import Cancelled, EdenAiError, InvalidRequestError, NotFoundError
from .functions.catalog import CATALOG
from .images import ImageService, plan_scene_count, split_scenes
from .models import CancelToken, Message, PackContext
from .prompts import PromptOptions
from .providers.registry import ProviderRegistry
from .secrets import MemorySecretStore
from .websearch import SearchService, SourcePolicy

Emitter = Callable[[str, Dict[str, Any]], None]


def _require(params: Dict[str, Any], key: str) -> Any:
    if key not in params or params[key] in (None, ""):
        raise InvalidRequestError(f"missing required parameter: {key}")
    return params[key]


class Application:
    def __init__(self, config: Optional[EngineConfig] = None) -> None:
        self.config = config or EngineConfig.defaults()
        self.secrets = MemorySecretStore()
        self.registry = ProviderRegistry(self.secrets)
        self.registry.configure(self.config.providers)
        self.cache = ResultCache(self.config.cache_path, self.config.cache_ttl_seconds)
        self.policy = SourcePolicy(self.config.allow_domains, self.config.block_domains)
        self.search = SearchService(self.config.search, self.policy)
        self.images: Optional[ImageService] = None
        self.engine = ResearchEngine(self.registry, self.cache, default_provider=self.config.default_provider,
                                     max_workers=self.config.max_workers)
        self._tokens: Dict[str, CancelToken] = {}
        self._token_lock = threading.Lock()
        self._configure_images()

    def _configure_images(self) -> None:
        self.images = ImageService(self.config.images, self.secrets) if self.config.images.cache_dir else None

    def close(self) -> None:
        self.engine.shutdown()
        self.cache.close()

    def token_for(self, request_id: str) -> CancelToken:
        token = CancelToken()
        with self._token_lock:
            self._tokens[request_id] = token
        return token

    def release_token(self, request_id: str) -> None:
        with self._token_lock:
            self._tokens.pop(request_id, None)

    def cancel(self, target: str) -> bool:
        with self._token_lock:
            token = self._tokens.get(target)
        if token is None:
            return False
        token.cancel()
        return True

    def dispatch(self, method: str, params: Dict[str, Any], request_id: str, emit: Emitter) -> Any:
        handler = getattr(self, "rpc_" + method.replace(".", "_"), None)
        if handler is None:
            raise NotFoundError(f"unknown method: {method}")
        return handler(params, request_id, emit)

    def _chunker(self, request_id: str, params: Dict[str, Any], emit: Emitter):
        if not params.get("stream"):
            return None
        return lambda text: emit("chunk", {"id": request_id, "text": text})

    def _pack(self, params: Dict[str, Any]) -> PackContext:
        return PackContext.from_dict(dict(_require(params, "pack")))

    def rpc_ping(self, params, request_id, emit):
        return {"version": __version__, "pid": os.getpid(), "functions": len(CATALOG)}

    def rpc_config_set(self, params, request_id, emit):
        data = dict(_require(params, "config"))
        config = EngineConfig.from_dict(data)
        self.config = config
        self.registry.configure(config.providers)
        self.engine.default_provider = config.default_provider
        self.policy = SourcePolicy(config.allow_domains, config.block_domains)
        self.search = SearchService(config.search, self.policy)
        self._configure_images()
        return {"providers": self.registry.names()}

    def rpc_providers_list(self, params, request_id, emit):
        return {"providers": self.registry.describe(), "default": self.engine.default_provider}

    def rpc_providers_set_key(self, params, request_id, emit):
        name = str(_require(params, "name"))
        self.registry.set_key(name, str(params.get("key", "")))
        return {"name": name, "available": self.registry.get(name).available()}

    def rpc_providers_set_images_key(self, params, request_id, emit):
        key = str(params.get("key", ""))
        if key:
            self.secrets.set("images", key)
        else:
            self.secrets.delete("images")
        return {"configured": bool(key)}

    def rpc_functions_list(self, params, request_id, emit):
        group = params.get("group")
        specs = CATALOG.by_group(str(group)) if group else CATALOG.all()
        return {"functions": [spec.to_dict() for spec in specs], "groups": CATALOG.groups()}

    def rpc_card_run(self, params, request_id, emit):
        ctx = self._pack(params)
        function_id = str(_require(params, "function_id"))
        token = self.token_for(request_id)
        try:
            options = PromptOptions.from_dict(params.get("options"))
            fallback = params.get("fallback")
            chunker = self._chunker(request_id, params, emit)
            if fallback:
                result = self.engine.run_with_fallback(function_id, ctx, [str(p) for p in fallback], options=options,
                                                       use_cache=bool(params.get("use_cache", True)),
                                                       on_chunk=chunker, cancel=token)
            else:
                result = self.engine.run(function_id, ctx, provider=params.get("provider"),
                                         model=str(params.get("model", "")), options=options,
                                         use_cache=bool(params.get("use_cache", True)),
                                         refresh=bool(params.get("refresh", False)), on_chunk=chunker, cancel=token)
            return result.to_dict()
        finally:
            self.release_token(request_id)

    def rpc_card_custom(self, params, request_id, emit):
        ctx = self._pack(params)
        token = self.token_for(request_id)
        try:
            result = self.engine.run_custom(
                str(_require(params, "instruction")), ctx, output=str(params.get("output", "markdown")),
                provider=params.get("provider"), model=str(params.get("model", "")),
                options=PromptOptions.from_dict(params.get("options")), use_cache=bool(params.get("use_cache", True)),
                on_chunk=self._chunker(request_id, params, emit), cancel=token)
            return result.to_dict()
        finally:
            self.release_token(request_id)

    def rpc_card_compare(self, params, request_id, emit):
        ctx = self._pack(params)
        token = self.token_for(request_id)
        try:
            outcome = self.engine.compare(str(_require(params, "function_id")), ctx,
                                          [str(p) for p in _require(params, "providers")],
                                          options=PromptOptions.from_dict(params.get("options")),
                                          use_cache=bool(params.get("use_cache", True)), cancel=token)
            return {"results": {k: v.to_dict() for k, v in outcome["results"].items()}, "errors": outcome["errors"]}
        finally:
            self.release_token(request_id)

    def rpc_card_chain(self, params, request_id, emit):
        ctx = self._pack(params)
        token = self.token_for(request_id)
        try:
            results = self.engine.chain([str(s) for s in _require(params, "steps")], ctx,
                                        provider=params.get("provider"),
                                        options=PromptOptions.from_dict(params.get("options")),
                                        use_cache=bool(params.get("use_cache", True)), cancel=token)
            return {"results": [r.to_dict() for r in results]}
        finally:
            self.release_token(request_id)

    def rpc_card_chat(self, params, request_id, emit):
        ctx = self._pack(params)
        token = self.token_for(request_id)
        try:
            history = [Message(str(m.get("role", "user")), str(m.get("content", "")))
                       for m in params.get("history", [])]
            result = self.engine.chat(ctx, history, str(_require(params, "message")), provider=params.get("provider"),
                                      model=str(params.get("model", "")),
                                      options=PromptOptions.from_dict(params.get("options")),
                                      on_chunk=self._chunker(request_id, params, emit), cancel=token)
            return result.to_dict()
        finally:
            self.release_token(request_id)

    def rpc_card_debate(self, params, request_id, emit):
        ctx = self._pack(params)
        token = self.token_for(request_id)
        try:
            return self.engine.debate(ctx, str(_require(params, "provider_a")), str(_require(params, "provider_b")),
                                      rounds=int(params.get("rounds", 2)), proposition=str(params.get("proposition", "")),
                                      options=PromptOptions.from_dict(params.get("options")),
                                      judge=bool(params.get("judge", True)), cancel=token)
        finally:
            self.release_token(request_id)

    def rpc_search_query(self, params, request_id, emit):
        token = self.token_for(request_id)
        try:
            return self.search.search(str(_require(params, "query")), str(params.get("mode", "web")),
                                      int(params.get("limit", 8)), token)
        finally:
            self.release_token(request_id)

    def rpc_search_discover(self, params, request_id, emit):
        token = self.token_for(request_id)
        try:
            return self.search.discover(str(_require(params, "text")), int(params.get("limit", 5)),
                                        int(params.get("seed", 0)), token)
        finally:
            self.release_token(request_id)

    def rpc_sources_rule(self, params, request_id, emit):
        self.policy.add(str(_require(params, "domain")), str(_require(params, "kind")))
        return self.policy.snapshot()

    def rpc_sources_remove_rule(self, params, request_id, emit):
        self.policy.remove(str(_require(params, "domain")))
        return self.policy.snapshot()

    def rpc_sources_rules(self, params, request_id, emit):
        return self.policy.snapshot()

    def rpc_citation_format(self, params, request_id, emit):
        meta = CitationMeta.from_dict(dict(_require(params, "meta")))
        styles = params.get("styles") or [params.get("style", "apa")]
        return {"citations": {str(s): format_citation(meta, str(s), str(params.get("template", ""))) for s in styles}}

    def rpc_citation_locate(self, params, request_id, emit):
        location = locate_quote(str(_require(params, "text")), str(_require(params, "quote")))
        if location is None:
            return {"found": False}
        return {"found": True, "start": location.start, "end": location.end, "exact": location.exact,
                "score": location.score}

    def rpc_image_generate(self, params, request_id, emit):
        if self.images is None:
            raise InvalidRequestError("image generation is not configured")
        token = self.token_for(request_id)
        try:
            image = self.images.generate(str(_require(params, "prompt")), str(params.get("style", "jewel")),
                                         size=str(params.get("size", "")), refresh=bool(params.get("refresh", False)),
                                         cancel=token)
            return image.to_dict()
        finally:
            self.release_token(request_id)

    def rpc_image_scenes(self, params, request_id, emit):
        text = str(_require(params, "text"))
        count = plan_scene_count(text, int(params.get("max_images", 6)))
        return {"count": count, "scenes": split_scenes(text, count)}

    def rpc_image_set(self, params, request_id, emit):
        if self.images is None:
            raise InvalidRequestError("image generation is not configured")
        prompts = [str(p) for p in _require(params, "prompts")]
        token = self.token_for(request_id)
        try:
            images = self.images.generate_set(
                PackContext.from_dict(dict(params["pack"])) if params.get("pack") else PackContext("", "x"),
                prompts, str(params.get("style", "jewel")), token,
                lambda done, total: emit("progress", {"id": request_id, "done": done, "total": total}))
            return {"images": [image.to_dict() for image in images]}
        finally:
            self.release_token(request_id)

    def rpc_cache_stats(self, params, request_id, emit):
        return self.cache.stats()

    def rpc_cache_clear(self, params, request_id, emit):
        self.cache.clear()
        return {"cleared": True}

    def rpc_cache_invalidate_pack(self, params, request_id, emit):
        return {"removed": self.cache.invalidate_pack(str(_require(params, "pack_id")))}

    def rpc_cancel(self, params, request_id, emit):
        return {"cancelled": self.cancel(str(_require(params, "target")))}


class StdioServer:
    def __init__(self, application: Application, stdin: IO[str], stdout: IO[str], workers: int = 8) -> None:
        self.application = application
        self._stdin = stdin
        self._stdout = stdout
        self._write_lock = threading.Lock()
        self._pool = concurrent.futures.ThreadPoolExecutor(max_workers=workers, thread_name_prefix="eden-rpc")
        self._stop = threading.Event()

    def _write(self, message: Dict[str, Any]) -> None:
        line = json.dumps(message, ensure_ascii=False, separators=(",", ":"))
        with self._write_lock:
            self._stdout.write(line + "\n")
            self._stdout.flush()

    def _emitter(self, request_id: str) -> Emitter:
        def emit(event: str, data: Dict[str, Any]) -> None:
            self._write({"id": request_id, "event": event, "data": data})
        return emit

    def _handle(self, message: Dict[str, Any]) -> None:
        request_id = str(message.get("id", ""))
        method = str(message.get("method", ""))
        params = message.get("params") or {}
        try:
            if not isinstance(params, dict):
                raise InvalidRequestError("params must be an object")
            result = self.application.dispatch(method, params, request_id, self._emitter(request_id))
            self._write({"id": request_id, "ok": True, "result": result})
        except Cancelled as error:
            self._write({"id": request_id, "ok": False, "error": error.to_dict()})
        except EdenAiError as error:
            self._write({"id": request_id, "ok": False, "error": error.to_dict()})
        except Exception as error:
            sys.stderr.write(traceback.format_exc())
            self._write({"id": request_id, "ok": False,
                         "error": {"code": "internal", "message": f"{type(error).__name__}: {error}"}})

    def serve(self) -> None:
        for raw in self._stdin:
            raw = raw.strip()
            if not raw:
                continue
            try:
                message = json.loads(raw)
                if not isinstance(message, dict):
                    raise ValueError("message must be an object")
            except ValueError as error:
                self._write({"id": "", "ok": False, "error": {"code": "parse", "message": str(error)}})
                continue
            if message.get("method") == "shutdown":
                self._write({"id": str(message.get("id", "")), "ok": True, "result": {"bye": True}})
                break
            if message.get("method") == "cancel":
                self._handle(message)
                continue
            self._pool.submit(self._handle, message)
        self._pool.shutdown(wait=True)
        self.application.close()
