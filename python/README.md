# eden_ai

Standard-library Python sidecar for the Eden Research Reader. It speaks newline-delimited JSON over stdin/stdout.

    python -m eden_ai [--config config.json]
    python -m unittest discover -s tests -t .

Request: `{"id": "1", "method": "card.run", "params": {...}}`
Response: `{"id": "1", "ok": true, "result": {...}}` or `{"id": "1", "ok": false, "error": {"code": "...", "message": "..."}}`
Events: `{"id": "1", "event": "chunk", "data": {"text": "..."}}`
