import json
import os
import subprocess
import sys
import tempfile
import threading
import unittest

from eden_ai.cache import ResultCache
from eden_ai.config import EngineConfig, ImageConfig, ProviderConfig, SearchConfig
from eden_ai.engine import ResearchEngine
from eden_ai.errors import AuthError, Cancelled, ConfigError, InvalidRequestError, ProviderError, RateLimitError
from eden_ai.http import HttpClient
from eden_ai.images import ImageService, build_image_prompt, plan_scene_count, split_scenes
from eden_ai.models import CancelToken, Message, PackContext, ProviderRequest
from eden_ai.prompts import PromptOptions
from eden_ai.providers.registry import ProviderRegistry
from eden_ai.secrets import MemorySecretStore
from eden_ai.websearch import SearchService, SourcePolicy, keywords, normalize_url, unwrap_duck_url
from tests.support import FakeApi, PNG_1X1

PASSAGE = "A river went out of Eden to water the garden. From there it divided into four rivers."
CTX = PackContext("pack-1", PASSAGE, "Genesis Notes", "Moses", "Chapter 2", 3)


def build(api, reply=None, names=("claude", "grok", "local")):
    if reply is not None:
        api.reply = reply
    secrets = MemorySecretStore()
    configs = {
        "claude": ProviderConfig("claude", "anthropic", api.url, "claude-test", max_retries=2),
        "grok": ProviderConfig("grok", "xai", api.url, "grok-test", max_retries=0),
        "local": ProviderConfig("local", "openai_compat", api.url, "local-test", requires_key=False, max_retries=0),
    }
    registry = ProviderRegistry(secrets, lambda c: HttpClient(timeout=5, max_retries=c.max_retries, backoff=0.0,
                                                              sleep=lambda s: None))
    registry.configure([configs[n] for n in names])
    secrets.set("claude", "sk-claude")
    secrets.set("grok", "sk-grok")
    return registry, secrets


class ProviderTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeApi(lambda kind, body: "hello from " + kind)
        self.registry, self.secrets = build(self.api)

    def tearDown(self):
        self.api.close()

    def request(self):
        return ProviderRequest([Message("user", "hi")], system="sys", max_tokens=50)

    def test_anthropic_complete_and_headers(self):
        response = self.registry.get("claude").complete(self.request())
        self.assertEqual(response.text, "hello from anthropic")
        self.assertEqual((response.usage.input_tokens, response.usage.output_tokens), (11, 7))
        path, headers, body = self.api.requests[-1]
        self.assertEqual(headers["x-api-key"], "sk-claude")
        self.assertEqual(headers["anthropic-version"], "2023-06-01")
        self.assertEqual(body["system"], "sys")
        self.assertEqual(body["model"], "claude-test")

    def test_anthropic_stream(self):
        chunks = []
        response = self.registry.get("claude").stream(self.request(), chunks.append)
        self.assertEqual("".join(chunks), "hello from anthropic")
        self.assertTrue(len(chunks) > 1)
        self.assertEqual(response.text, "hello from anthropic")
        self.assertEqual((response.usage.input_tokens, response.usage.output_tokens), (9, 4))
        self.assertEqual(response.finish_reason, "end_turn")

    def test_openai_compat_complete_and_stream(self):
        grok = self.registry.get("grok")
        response = grok.complete(self.request())
        self.assertEqual(response.text, "hello from openai")
        self.assertEqual(self.api.requests[-1][1]["authorization"], "Bearer sk-grok")
        self.assertEqual(self.api.requests[-1][2]["messages"][0], {"role": "system", "content": "sys"})
        chunks = []
        streamed = grok.stream(self.request(), chunks.append)
        self.assertEqual("".join(chunks), "hello from openai")
        self.assertEqual(streamed.usage.output_tokens, 2)

    def test_local_provider_needs_no_key(self):
        local = self.registry.get("local")
        self.assertTrue(local.available())
        self.assertNotIn("authorization", (self.api.requests[-1][1] if self.api.requests else {}))
        self.assertEqual(local.complete(self.request()).text, "hello from openai")
        self.assertNotIn("authorization", self.api.requests[-1][1])

    def test_missing_key_and_registry_helpers(self):
        self.secrets.delete("grok")
        self.assertFalse(self.registry.get("grok").available())
        with self.assertRaises(AuthError):
            self.registry.get("grok").complete(self.request())
        self.assertEqual(sorted(self.registry.available()), ["claude", "local"])
        self.registry.set_key("grok", "new")
        self.assertTrue(self.registry.get("grok").available())

    def test_retry_on_rate_limit_then_success(self):
        self.api.fail_next("/v1/messages", 429, 2, {"Retry-After": "0"})
        response = self.registry.get("claude").complete(self.request())
        self.assertEqual(response.text, "hello from anthropic")
        self.assertEqual(self.api.count("/v1/messages"), 3)

    def test_retries_exhausted_and_auth_not_retried(self):
        self.api.fail_next("/v1/messages", 500, 5)
        with self.assertRaises(ProviderError) as caught:
            self.registry.get("claude").complete(self.request())
        self.assertEqual(caught.exception.status, 500)
        self.assertEqual(self.api.count("/v1/messages"), 3)
        self.api.fail_next("/chat/completions", 401, 1)
        with self.assertRaises(AuthError):
            self.registry.get("grok").complete(self.request())
        self.assertEqual(self.api.count("/chat/completions"), 1)
        self.api.fail_next("/chat/completions", 429, 1)
        with self.assertRaises(RateLimitError):
            self.registry.get("grok").complete(self.request())


class EngineTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeApi()
        self.registry, self.secrets = build(self.api)
        self.cache = ResultCache(":memory:")
        self.usage = []
        self.engine = ResearchEngine(self.registry, self.cache, default_provider="claude",
                                     usage_hook=self.usage.append)

    def tearDown(self):
        self.engine.shutdown()
        self.api.close()

    def test_run_caches_results(self):
        self.api.reply = lambda kind, body: "A short summary."
        first = self.engine.run("summary.plain", CTX)
        second = self.engine.run("summary.plain", CTX)
        self.assertFalse(first.cached)
        self.assertTrue(second.cached)
        self.assertEqual(second.text, "A short summary.")
        self.assertEqual(self.api.count("/v1/messages"), 1)
        self.assertEqual(len(self.usage), 1)
        self.engine.run("summary.plain", CTX, refresh=True)
        self.assertEqual(self.api.count("/v1/messages"), 2)
        self.engine.run("summary.plain", CTX, options=PromptOptions(persona="teacher"))
        self.assertEqual(self.api.count("/v1/messages"), 3)
        self.engine.run("summary.plain", CTX, provider="grok")
        self.assertEqual(self.api.count("/chat/completions"), 1)

    def test_streaming_delivers_chunks_and_cached_text(self):
        self.api.reply = lambda kind, body: "streamed answer text"
        chunks = []
        result = self.engine.run("summary.plain", CTX, on_chunk=chunks.append)
        self.assertEqual("".join(chunks), "streamed answer text")
        self.assertEqual(result.usage.input_tokens, 9)
        cached_chunks = []
        self.engine.run("summary.plain", CTX, on_chunk=cached_chunks.append)
        self.assertEqual(cached_chunks, ["streamed answer text"])

    def test_json_output_is_parsed_and_repaired(self):
        replies = iter(["Sorry, here is prose", '```json\n[{"term": "Eden", "definition": "a garden"}]\n```'])
        self.api.reply = lambda kind, body: next(replies)
        result = self.engine.run("glossary", CTX)
        self.assertEqual(result.data, [{"term": "Eden", "definition": "a garden"}])
        self.assertIn("repaired", result.warnings[0])
        self.assertEqual(self.api.count("/v1/messages"), 2)
        self.assertEqual(self.cache.stats()["entries"], 0)

    def test_unparseable_json_returns_text_with_warning(self):
        self.api.reply = lambda kind, body: "never json"
        result = self.engine.run("glossary", CTX)
        self.assertIsNone(result.data)
        self.assertEqual(result.text, "never json")
        self.assertTrue(result.warnings)

    def test_quote_verification_marks_fabricated_quotes(self):
        quotes = [{"quote": "A river went out of Eden", "why": "opens"}, {"quote": "Invented words entirely", "why": "x"}]
        self.api.reply = lambda kind, body: json.dumps(quotes)
        result = self.engine.run("quotes.key", CTX)
        verified, fabricated = result.data
        self.assertTrue(verified["verified"] and verified["exact"])
        self.assertEqual(PASSAGE[verified["start"]:verified["end"]], "A river went out of Eden")
        self.assertFalse(fabricated["verified"])

    def test_list_and_mermaid_outputs(self):
        self.api.reply = lambda kind, body: "- one\n- two"
        self.assertEqual(self.engine.run("keypoints", CTX).data, ["one", "two"])
        self.api.reply = lambda kind, body: "```mermaid\ngraph TD\nA-->B\n```"
        self.assertEqual(self.engine.run("concept_map", CTX).data, "graph TD\nA-->B")

    def test_fallback_skips_failing_provider(self):
        self.api.fail_next("/v1/messages", 500, 10)
        self.api.reply = lambda kind, body: "from fallback"
        result = self.engine.run_with_fallback("summary.plain", CTX, ["claude", "grok"])
        self.assertEqual(result.provider, "grok")
        self.api.fail_next("/v1/messages", 500, 10)
        self.api.fail_next("/chat/completions", 500, 10)
        with self.assertRaises(ProviderError):
            self.engine.run_with_fallback("keypoints", CTX, ["claude", "grok"])
        with self.assertRaises(InvalidRequestError):
            self.engine.run_with_fallback("summary.plain", CTX, [])

    def test_compare_collects_results_and_errors(self):
        self.secrets.delete("grok")
        self.api.reply = lambda kind, body: "answer via " + kind
        outcome = self.engine.compare("summary.plain", CTX, ["claude", "grok", "local"])
        self.assertEqual(sorted(outcome["results"]), ["claude", "local"])
        self.assertEqual(outcome["errors"]["grok"]["code"], "config")
        with self.assertRaises(InvalidRequestError):
            self.engine.compare("summary.plain", CTX, ["claude"])

    def test_chain_passes_prior_output(self):
        self.api.reply = lambda kind, body: "step output"
        results = self.engine.chain(["summary.plain", "implications"], CTX)
        self.assertEqual(len(results), 2)
        last_body = self.api.requests[-1][2]["messages"][0]["content"]
        self.assertIn("<prior_analysis>", last_body)
        self.assertIn("[summary.plain]", last_body)

    def test_chat_and_custom(self):
        self.api.reply = lambda kind, body: "chat reply"
        result = self.engine.chat(CTX, [Message("user", "hi"), Message("assistant", "hello")], "What is Pishon?")
        self.assertEqual(result.text, "chat reply")
        body = self.api.requests[-1][2]
        self.assertIn("<passage>", body["system"])
        self.assertEqual([m["role"] for m in body["messages"]], ["user", "assistant", "user"])
        custom = self.engine.run_custom("List every river named.", CTX)
        again = self.engine.run_custom("List every river named.", CTX)
        self.assertTrue(again.cached and not custom.cached)
        with self.assertRaises(InvalidRequestError):
            self.engine.run_custom("  ", CTX)
        with self.assertRaises(InvalidRequestError):
            self.engine.chat(CTX, [], " ")

    def test_debate_alternates_and_summarizes(self):
        counter = [0]

        def reply(kind, body):
            counter[0] += 1
            return f"turn {counter[0]}"
        self.api.reply = reply
        outcome = self.engine.debate(CTX, "claude", "grok", rounds=2, proposition="the garden was real")
        speakers = [t["speaker"] for t in outcome["turns"]]
        self.assertEqual(speakers, ["A", "B", "A", "B"])
        self.assertEqual([t["provider"] for t in outcome["turns"]], ["claude", "grok", "claude", "grok"])
        self.assertTrue(outcome["summary"])
        self.assertIn("the garden was real", self.api.requests[0][2]["system"])
        with self.assertRaises(InvalidRequestError):
            self.engine.debate(CTX, "claude", "grok", rounds=9)

    def test_cancellation_and_unavailable_provider(self):
        token = CancelToken()
        token.cancel()
        with self.assertRaises(Cancelled):
            self.engine.run("summary.plain", CTX, cancel=token)
        self.secrets.delete("claude")
        with self.assertRaises(ConfigError):
            self.engine.run("summary.plain", CTX)


class SearchAndImageTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeApi()
        self.config = SearchConfig(duckduckgo_url=self.api.url + "/ddg", wikipedia_url=self.api.url + "/wiki",
                                   semantic_scholar_url=self.api.url + "/scholar",
                                   crossref_url=self.api.url + "/crossref", commons_url=self.api.url + "/commons")
        self.http = HttpClient(timeout=5, max_retries=0)

    def tearDown(self):
        self.api.close()

    def test_web_search_merges_filters_and_dedupes(self):
        service = SearchService(self.config, SourcePolicy(block=["blocked.test"]), self.http)
        outcome = service.search("eden river", "web", 10)
        urls = [r["url"] for r in outcome["results"]]
        self.assertEqual(outcome["blocked"], 1)
        self.assertEqual(urls[0], "https://example.org/eden?utm_source=x")
        self.assertEqual(sum(1 for u in urls if u.startswith("https://example.org/eden")), 1)
        self.assertTrue(any("wikipedia" in u or "/wiki/" in u for u in urls))
        self.assertEqual(outcome["results"][0]["title"], "Eden & Gardens")
        self.assertIn("river", outcome["results"][0]["snippet"])

    def test_allow_list_restricts_results(self):
        service = SearchService(self.config, SourcePolicy(allow=["example.org"]), self.http)
        urls = [r["domain"] for r in service.search("eden", "web")["results"]]
        self.assertEqual(set(urls), {"example.org"})

    def test_academic_and_image_modes(self):
        service = SearchService(self.config, http=self.http)
        academic = service.search("eden", "academic")["results"]
        self.assertEqual(academic[0]["doi"], "10.1/abc")
        self.assertEqual(academic[0]["authors"], ["A. Author"])
        self.assertEqual({r["kind"] for r in academic}, {"academic"})
        images = service.search("eden", "image")["results"]
        self.assertEqual(images[0]["image_url"], "https://upload.test/eden.jpg")
        with self.assertRaises(InvalidRequestError):
            service.search("x", "bogus")
        with self.assertRaises(InvalidRequestError):
            service.search("  ")

    def test_backend_errors_are_reported_not_raised(self):
        self.api.fail_next("/scholar", 500, 1)
        outcome = SearchService(self.config, http=self.http).search("eden", "academic")
        self.assertIn("semantic_scholar", outcome["errors"])
        self.assertTrue(outcome["results"])

    def test_url_helpers_and_keywords(self):
        self.assertEqual(unwrap_duck_url("//duckduckgo.com/l/?uddg=https%3A%2F%2Fa.org%2Fx&rut=1"), "https://a.org/x")
        self.assertEqual(normalize_url("https://WWW.A.org/x/?utm_source=q&b=1#f"), "https://a.org/x?b=1")
        self.assertEqual(keywords("River river garden the of Eden eden eden", 2), ["eden", "river"])
        policy = SourcePolicy()
        policy.add("evil.test", "block")
        self.assertFalse(policy.allowed("https://sub.evil.test/x"))
        policy.remove("evil.test")
        self.assertTrue(policy.allowed("https://sub.evil.test/x"))
        outcome = SearchService(self.config, http=self.http).discover(PASSAGE, 3)
        self.assertEqual(outcome["mode"], "discover")

    def test_image_generation_and_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            secrets = MemorySecretStore()
            config = ImageConfig(base_url=self.api.url, cache_dir=directory)
            service = ImageService(config, secrets, http=self.http)
            with self.assertRaises(ConfigError):
                service.generate("a river", "jewel")
            secrets.set("images", "key")
            image = service.generate("a river of gold", "eden")
            self.assertFalse(image.cached)
            with open(image.path, "rb") as handle:
                self.assertEqual(handle.read(), PNG_1X1)
            again = service.generate("a river of gold", "eden")
            self.assertTrue(again.cached)
            self.assertEqual(self.api.count("/images/generations"), 1)
            self.assertIn("Do not render any words", self.api.requests[-1][2]["prompt"])
            with self.assertRaises(InvalidRequestError):
                build_image_prompt("x", "nope")
            batch = service.generate_set(CTX, ["one", "two"], "symbolic")
            self.assertEqual(len(batch), 2)

    def test_scene_planning(self):
        self.assertEqual(plan_scene_count("short"), 1)
        self.assertEqual(plan_scene_count("x" * 1000), 3)
        scenes = split_scenes("One. Two. Three. Four.", 2)
        self.assertEqual(scenes, ["One. Two.", "Three. Four."])


class ServerProcessTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeApi(lambda kind, body: "- first\n- second")
        env = dict(os.environ)
        env["PYTHONPATH"] = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.process = subprocess.Popen([sys.executable, "-m", "eden_ai"], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
        self.lines = []
        self.condition = threading.Condition()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.process.stdout:
            with self.condition:
                self.lines.append(json.loads(line))
                self.condition.notify_all()

    def send(self, request_id, method, params=None):
        self.process.stdin.write(json.dumps({"id": request_id, "method": method, "params": params or {}}) + "\n")
        self.process.stdin.flush()

    def final(self, request_id, timeout=10):
        with self.condition:
            ok = self.condition.wait_for(
                lambda: any(m.get("id") == request_id and "ok" in m for m in self.lines), timeout)
            self.assertTrue(ok, f"no response for {request_id}")
            return next(m for m in self.lines if m.get("id") == request_id and "ok" in m)

    def events(self, request_id):
        with self.condition:
            return [m for m in self.lines if m.get("id") == request_id and "event" in m]

    def tearDown(self):
        try:
            self.send("bye", "shutdown")
            self.process.wait(timeout=10)
        except Exception:
            self.process.kill()
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()
        self.api.close()

    def test_end_to_end_protocol(self):
        self.send("1", "ping")
        pong = self.final("1")
        self.assertTrue(pong["ok"] and pong["result"]["functions"] >= 60)
        self.send("2", "config.set", {"config": {"default_provider": "claude", "providers": [
            {"name": "claude", "kind": "anthropic", "base_url": self.api.url, "model": "m", "max_retries": 0}]}})
        self.assertTrue(self.final("2")["ok"])
        self.send("3", "providers.set_key", {"name": "claude", "key": "sk-test"})
        self.assertTrue(self.final("3")["result"]["available"])
        self.send("4", "card.run", {"function_id": "keypoints", "pack": {"pack_id": "p1", "text": PASSAGE},
                                    "stream": True})
        reply = self.final("4")
        self.assertTrue(reply["ok"], reply)
        self.assertEqual(reply["result"]["data"], ["first", "second"])
        self.assertEqual("".join(e["data"]["text"] for e in self.events("4") if e["event"] == "chunk"), "- first\n- second")
        self.send("5", "card.run", {"function_id": "keypoints", "pack": {"pack_id": "p1", "text": PASSAGE}})
        self.assertTrue(self.final("5")["result"]["cached"])
        self.send("6", "cache.stats")
        self.assertEqual(self.final("6")["result"]["entries"], 1)

    def test_errors_and_utilities(self):
        self.send("1", "no.such.method")
        self.assertEqual(self.final("1")["error"]["code"], "not_found")
        self.send("2", "card.run", {"function_id": "keypoints", "pack": {"text": ""}})
        self.assertEqual(self.final("2")["error"]["code"], "invalid_argument")
        self.send("3", "card.run", {"function_id": "keypoints", "pack": {"text": "x"}})
        self.assertEqual(self.final("3")["error"]["code"], "config")
        self.send("4", "citation.format", {"meta": {"title": "T", "authors": ["A B"], "year": "2001"},
                                           "styles": ["apa", "mla"]})
        self.assertIn("(2001)", self.final("4")["result"]["citations"]["apa"])
        self.send("5", "citation.locate", {"text": PASSAGE, "quote": "water the garden"})
        self.assertTrue(self.final("5")["result"]["found"])
        self.send("6", "sources.rule", {"domain": "bad.test", "kind": "block"})
        self.assertEqual(self.final("6")["result"]["block"], ["bad.test"])
        self.send("7", "functions.list", {"group": "visual"})
        self.assertEqual(len(self.final("7")["result"]["functions"]), 6)
        self.process.stdin.write("not json\n")
        self.process.stdin.flush()
        self.assertEqual(self.final("")["error"]["code"], "parse")
        self.send("8", "cancel", {"target": "ghost"})
        self.assertFalse(self.final("8")["result"]["cancelled"])


if __name__ == "__main__":
    unittest.main()
