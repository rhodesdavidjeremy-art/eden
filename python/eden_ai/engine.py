from __future__ import annotations

import concurrent.futures
import threading
from typing import Any, Callable, Dict, List, Optional, Sequence

from .cache import ResultCache
from .citations import locate_quote
from .errors import (AuthError, Cancelled, ConfigError, EdenAiError, InvalidRequestError, NetworkError,
                     ParseError, ProviderError)
from .functions.catalog import CATALOG, FunctionCatalog, FunctionSpec
from .models import (CancelToken, ChunkCallback, FunctionResult, Message, PackContext, ProviderRequest,
                     ProviderResponse, Usage)
from .parsing import extract_json, extract_mermaid, split_list
from .prompts import PromptOptions, build_chat_request, build_request, context_block, system_prompt
from .providers.base import Provider
from .providers.registry import ProviderRegistry

UsageHook = Callable[[FunctionResult], None]

_FALLBACK_ERRORS = (ProviderError, NetworkError, AuthError, ConfigError)


class ResearchEngine:
    def __init__(self, registry: ProviderRegistry, cache: ResultCache, *, catalog: FunctionCatalog = CATALOG,
                 default_provider: str = "claude", max_workers: int = 8,
                 usage_hook: Optional[UsageHook] = None) -> None:
        self.registry = registry
        self.cache = cache
        self.catalog = catalog
        self.default_provider = default_provider
        self.usage_hook = usage_hook
        self._pool = concurrent.futures.ThreadPoolExecutor(max_workers=max_workers, thread_name_prefix="eden-ai")
        self._lock = threading.Lock()

    def shutdown(self) -> None:
        self._pool.shutdown(wait=False, cancel_futures=True)

    def resolve_provider(self, name: Optional[str]) -> Provider:
        provider = self.registry.get(name or self.default_provider)
        if not provider.available():
            raise ConfigError(f"provider '{provider.name}' is not available; check its api key and settings")
        return provider

    def _record(self, result: FunctionResult) -> None:
        if self.usage_hook is not None and not result.cached:
            try:
                self.usage_hook(result)
            except Exception:
                pass

    def _complete(self, provider: Provider, request: ProviderRequest, on_chunk: Optional[ChunkCallback],
                  cancel: Optional[CancelToken]) -> ProviderResponse:
        if cancel is not None:
            cancel.check()
        if on_chunk is not None:
            return provider.stream(request, on_chunk, cancel)
        return provider.complete(request, cancel)

    def _post_process(self, spec: FunctionSpec, ctx: PackContext, text: str) -> Any:
        if spec.output == "json":
            data = extract_json(text)
            if spec.verify_quotes and isinstance(data, list):
                self._verify_quotes(ctx, data)
            return data
        if spec.output == "mermaid":
            return extract_mermaid(text)
        if spec.output == "list":
            return split_list(text)
        return None

    @staticmethod
    def _verify_quotes(ctx: PackContext, items: List[Any]) -> None:
        for item in items:
            if not isinstance(item, dict):
                continue
            quote = str(item.get("quote", ""))
            location = locate_quote(ctx.text, quote) if quote else None
            item["verified"] = location is not None
            if location is not None:
                item["start"] = location.start
                item["end"] = location.end
                item["exact"] = location.exact

    def _execute(self, spec: FunctionSpec, ctx: PackContext, provider: Provider, options: PromptOptions,
                 model: str, on_chunk: Optional[ChunkCallback], cancel: Optional[CancelToken],
                 prior: str) -> FunctionResult:
        request = build_request(spec, ctx, options, model, prior)
        response = self._complete(provider, request, on_chunk, cancel)
        usage = response.usage
        latency = response.latency_ms
        text = response.text
        warnings: List[str] = []
        data: Any = None
        try:
            data = self._post_process(spec, ctx, text)
        except ParseError:
            if spec.output == "json":
                repair = ProviderRequest(
                    messages=request.messages + [
                        Message("assistant", text),
                        Message("user", "That was not valid JSON. Reply again with only the corrected JSON."),
                    ],
                    system=request.system, model=request.model, max_tokens=request.max_tokens, temperature=0.0)
                retry = self._complete(provider, repair, None, cancel)
                usage = usage.add(retry.usage)
                latency += retry.latency_ms
                try:
                    data = self._post_process(spec, ctx, retry.text)
                    text = retry.text
                    warnings.append("response was repaired after an invalid json reply")
                except ParseError:
                    warnings.append("response could not be parsed as json")
            else:
                warnings.append("response could not be parsed")
        return FunctionResult(spec.id, provider.name, response.model, text, usage, latency, False, data, warnings)

    def run(self, function_id: str, ctx: PackContext, *, provider: Optional[str] = None, model: str = "",
            options: Optional[PromptOptions] = None, use_cache: bool = True, refresh: bool = False,
            on_chunk: Optional[ChunkCallback] = None, cancel: Optional[CancelToken] = None,
            prior: str = "") -> FunctionResult:
        spec = self.catalog.get(function_id)
        options = options or PromptOptions()
        return self._run_spec(spec, ctx, provider, model, options, use_cache, refresh, on_chunk, cancel, prior)

    def run_custom(self, instruction: str, ctx: PackContext, *, output: str = "markdown", max_tokens: int = 900,
                   temperature: float = 0.4, provider: Optional[str] = None, model: str = "",
                   options: Optional[PromptOptions] = None, use_cache: bool = True,
                   on_chunk: Optional[ChunkCallback] = None, cancel: Optional[CancelToken] = None) -> FunctionResult:
        if not instruction.strip():
            raise InvalidRequestError("custom prompt is empty")
        identifier = "custom:" + CATALOG_DIGEST(instruction, output)
        spec = FunctionSpec(identifier, "custom", "Custom prompt", instruction, output, max_tokens, temperature)
        return self._run_spec(spec, ctx, provider, model, options or PromptOptions(), use_cache, False, on_chunk,
                              cancel, "")

    def _run_spec(self, spec: FunctionSpec, ctx: PackContext, provider_name: Optional[str], model: str,
                  options: PromptOptions, use_cache: bool, refresh: bool, on_chunk: Optional[ChunkCallback],
                  cancel: Optional[CancelToken], prior: str) -> FunctionResult:
        provider = self.resolve_provider(provider_name)
        effective_model = model or provider.config.model
        key = ResultCache.make_key(provider.name, effective_model, spec.id, ctx.fingerprint(),
                                   options.cache_key_part() + "\x1e" + prior)
        if use_cache and not refresh:
            hit = self.cache.get(key)
            if hit is not None:
                if on_chunk is not None:
                    on_chunk(hit["text"])
                return FunctionResult(spec.id, hit["provider"], hit["model"], hit["text"],
                                      Usage(**hit["usage"]), 0, True, hit.get("data"), hit.get("warnings", []))
        result = self._execute(spec, ctx, provider, options, model, on_chunk, cancel, prior)
        if use_cache and result.text and not result.warnings:
            self.cache.put(key, result.to_dict(), pack_id=ctx.pack_id, function_id=spec.id,
                           provider=provider.name, model=effective_model)
        self._record(result)
        return result

    def run_with_fallback(self, function_id: str, ctx: PackContext, providers: Sequence[str], *,
                          options: Optional[PromptOptions] = None, use_cache: bool = True,
                          on_chunk: Optional[ChunkCallback] = None,
                          cancel: Optional[CancelToken] = None) -> FunctionResult:
        if not providers:
            raise InvalidRequestError("no providers listed for fallback")
        failures: List[str] = []
        last: Optional[Exception] = None
        for name in providers:
            try:
                return self.run(function_id, ctx, provider=name, options=options, use_cache=use_cache,
                                on_chunk=on_chunk, cancel=cancel)
            except _FALLBACK_ERRORS as error:
                failures.append(f"{name}: {error}")
                last = error
        raise ProviderError("all providers failed: " + "; ".join(failures),
                            details={"failures": failures}) from last

    def compare(self, function_id: str, ctx: PackContext, providers: Sequence[str], *,
                options: Optional[PromptOptions] = None, use_cache: bool = True,
                cancel: Optional[CancelToken] = None) -> Dict[str, Any]:
        if len(providers) < 2:
            raise InvalidRequestError("comparison needs at least two providers")
        futures = {name: self._pool.submit(self.run, function_id, ctx, provider=name, options=options,
                                           use_cache=use_cache, cancel=cancel) for name in providers}
        results: Dict[str, Any] = {}
        errors: Dict[str, Any] = {}
        for name, future in futures.items():
            try:
                results[name] = future.result()
            except Cancelled:
                raise
            except EdenAiError as error:
                errors[name] = error.to_dict()
        return {"results": results, "errors": errors}

    def chain(self, steps: Sequence[str], ctx: PackContext, *, provider: Optional[str] = None,
              options: Optional[PromptOptions] = None, use_cache: bool = True,
              cancel: Optional[CancelToken] = None) -> List[FunctionResult]:
        if not steps:
            raise InvalidRequestError("chain needs at least one step")
        results: List[FunctionResult] = []
        prior = ""
        for step in steps:
            result = self.run(step, ctx, provider=provider, options=options, use_cache=use_cache,
                              cancel=cancel, prior=prior)
            results.append(result)
            prior = (prior + "\n\n" if prior else "") + f"[{step}]\n{result.text}"
        return results

    def chat(self, ctx: PackContext, history: Sequence[Message], message: str, *,
             provider: Optional[str] = None, model: str = "", options: Optional[PromptOptions] = None,
             on_chunk: Optional[ChunkCallback] = None, cancel: Optional[CancelToken] = None) -> FunctionResult:
        if not message.strip():
            raise InvalidRequestError("message is empty")
        resolved = self.resolve_provider(provider)
        request = build_chat_request(ctx, list(history), message, options, model)
        response = self._complete(resolved, request, on_chunk, cancel)
        result = FunctionResult("chat", resolved.name, response.model, response.text, response.usage,
                                response.latency_ms)
        self._record(result)
        return result

    def debate(self, ctx: PackContext, provider_a: str, provider_b: str, *, rounds: int = 2,
               proposition: str = "", options: Optional[PromptOptions] = None, judge: bool = True,
               cancel: Optional[CancelToken] = None) -> Dict[str, Any]:
        if rounds < 1 or rounds > 6:
            raise InvalidRequestError("rounds must be between 1 and 6")
        options = options or PromptOptions()
        first = self.resolve_provider(provider_a)
        second = self.resolve_provider(provider_b)
        claim = proposition.strip() or "the main claim of the passage is well supported"
        roles = {
            "A": (first, f"You are Debater A. Argue that {claim}. Use the passage as evidence, answer your opponent's "
                         "points directly, concede what is genuinely right, and keep each turn under 180 words."),
            "B": (second, f"You are Debater B. Argue that it is not the case that {claim}. Use the passage as evidence, "
                          "answer your opponent's points directly, concede what is genuinely right, and keep each turn "
                          "under 180 words."),
        }
        turns: List[Dict[str, Any]] = []
        usage = Usage()
        transcript: List[str] = []
        for round_number in range(1, rounds + 1):
            for speaker in ("A", "B"):
                if cancel is not None:
                    cancel.check()
                provider, role = roles[speaker]
                opponent = "\n\n".join(transcript) if transcript else "(no arguments yet; open the debate)"
                request = ProviderRequest(
                    messages=[Message("user", f"Debate so far:\n{opponent}\n\nGive Debater {speaker}'s turn "
                                              f"{round_number}.")],
                    system=system_prompt(options) + " " + role + "\n" + context_block(ctx, options),
                    max_tokens=500, temperature=0.5)
                response = self._complete(provider, request, None, cancel)
                usage = usage.add(response.usage)
                turns.append({"speaker": speaker, "round": round_number, "provider": provider.name,
                              "model": response.model, "text": response.text})
                transcript.append(f"Debater {speaker}: {response.text}")
        summary = ""
        if judge:
            request = ProviderRequest(
                messages=[Message("user", "Debate transcript:\n" + "\n\n".join(transcript) +
                                  "\n\nSummarize the strongest point on each side and what remains unresolved, "
                                  "without declaring a winner.")],
                system=system_prompt(options), max_tokens=500, temperature=0.2)
            response = self._complete(first, request, None, cancel)
            usage = usage.add(response.usage)
            summary = response.text
        return {"turns": turns, "summary": summary, "usage": usage.to_dict()}


def CATALOG_DIGEST(instruction: str, output: str) -> str:
    import hashlib
    return hashlib.sha256((output + "\x00" + instruction).encode("utf-8")).hexdigest()[:16]
