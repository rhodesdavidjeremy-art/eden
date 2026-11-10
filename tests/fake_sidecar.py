import json
import os
import sys
import threading
import time

write_lock = threading.Lock()
cancelled = set()
state = {"last_pack": None, "calls": 0}


def emit(message):
    with write_lock:
        sys.stdout.write(json.dumps(message) + "\n")
        sys.stdout.flush()


def respond(request_id, result=None, error=None):
    if error is not None:
        emit({"id": request_id, "ok": False, "error": error})
    else:
        emit({"id": request_id, "ok": True, "result": result})


def handle(message):
    request_id = message["id"]
    method = message["method"]
    params = message.get("params") or {}
    state["calls"] += 1
    if method == "ping":
        respond(request_id, {"version": "fake", "functions": 62})
    elif method == "echo":
        respond(request_id, params)
    elif method == "stream":
        parts = []
        for index in range(int(params.get("count", 3))):
            piece = "part%d " % index
            parts.append(piece)
            emit({"id": request_id, "event": "chunk", "data": {"text": piece}})
        respond(request_id, {"text": "".join(parts)})
    elif method == "slow":
        deadline = time.time() + float(params.get("seconds", 5))
        while time.time() < deadline:
            if request_id in cancelled:
                respond(request_id, error={"code": "cancelled", "message": "operation cancelled"})
                return
            time.sleep(0.02)
        respond(request_id, {"finished": True})
    elif method == "fail":
        respond(request_id, error={"code": params.get("code", "provider"), "message": "boom"})
    elif method == "crash":
        os._exit(3)
    elif method == "stderr":
        sys.stderr.write("diagnostic line\n")
        sys.stderr.flush()
        respond(request_id, {"ok": True})
    elif method == "cancel":
        cancelled.add(params.get("target"))
        respond(request_id, {"cancelled": True})
    elif method == "card.run":
        state["last_pack"] = params.get("pack")
        function_id = params.get("function_id")
        provider = params.get("provider") or "claude"
        if params.get("stream"):
            for piece in ("- first\n", "- second"):
                emit({"id": request_id, "event": "chunk", "data": {"text": piece}})
        if function_id == "keypoints":
            result = {"function_id": function_id, "provider": provider, "model": "fake-model",
                      "text": "- first\n- second", "usage": {"input_tokens": 10, "output_tokens": 5},
                      "latency_ms": 12, "cached": False, "data": ["first", "second"]}
        else:
            result = {"function_id": function_id, "provider": provider, "model": "fake-model",
                      "text": "Summary: " + params["pack"]["text"][:20], "usage": {"input_tokens": 4, "output_tokens": 2},
                      "latency_ms": 3, "cached": params.get("use_cache") is True and state["calls"] > 50}
        respond(request_id, result)
    elif method == "card.compare":
        results = {}
        for name in params["providers"]:
            results[name] = {"function_id": params["function_id"], "provider": name, "model": "m-" + name,
                             "text": "answer from " + name, "usage": {"input_tokens": 1, "output_tokens": 1},
                             "latency_ms": 1, "cached": False}
        respond(request_id, {"results": results, "errors": {}})
    elif method == "card.chat":
        respond(request_id, {"provider": "claude", "model": "fake-model",
                             "text": "reply to: %s (history %d)" % (params["message"], len(params.get("history", []))),
                             "usage": {"input_tokens": 2, "output_tokens": 2}, "latency_ms": 1})
    elif method == "card.debate":
        turns = [{"speaker": s, "round": 1, "provider": p, "model": "m", "text": "turn " + s}
                 for s, p in (("A", params["provider_a"]), ("B", params["provider_b"]))]
        respond(request_id, {"turns": turns, "summary": "summary", "usage": {"input_tokens": 0, "output_tokens": 0}})
    elif method == "search.query":
        respond(request_id, {"query": params["query"], "mode": params["mode"], "blocked": 0, "errors": {},
                             "results": [{"title": "Eden", "url": "https://example.org/eden", "snippet": "s",
                                          "domain": "example.org"},
                                         {"title": "Bad", "url": "https://bad.test/x", "snippet": "t",
                                          "domain": "bad.test"}]})
    elif method == "sources.rule":
        respond(request_id, {"allow": [], "block": []})
    elif method == "citation.format":
        respond(request_id, {"citations": {s: "CITE-" + s for s in params["styles"]}})
    elif method == "citation.locate":
        position = params["text"].find(params["quote"])
        respond(request_id, {"found": position >= 0, "start": position, "end": position + len(params["quote"])})
    elif method in ("config.set", "providers.set_key"):
        respond(request_id, {"providers": ["claude"], "available": True})
    elif method == "providers.list":
        respond(request_id, {"providers": [{"name": "claude", "available": True}], "default": "claude"})
    elif method == "debug.last_pack":
        respond(request_id, state["last_pack"])
    else:
        respond(request_id, error={"code": "not_found", "message": "unknown method: " + method})


def main():
    for raw in sys.stdin:
        raw = raw.strip()
        if not raw:
            continue
        message = json.loads(raw)
        if message["method"] == "shutdown":
            respond(message["id"], {"bye": True})
            return
        if message["method"] == "cancel":
            handle(message)
            continue
        threading.Thread(target=handle, args=(message,), daemon=True).start()


main()
