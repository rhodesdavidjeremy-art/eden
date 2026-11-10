# Architecture

## Data flow

1. `DocumentImporter` reads a file, hashes it, extracts text with the matching reader, splits it into packs with `PackSegmenter`, and stores document, text and packs in one transaction.
2. `ReaderModel` pages the stored text and overlays pack marks; the Qt reader renders a `PageView`.
3. Clicking a pack calls `CardService::run`, which builds a context (pack, neighbouring text, chapter), asks the Python sidecar to run a research function, and stores the result as a card section plus an `ai_sessions` row.
4. `CardPanelModel` turns stored sections into render blocks for display; `ExportBuilder` turns the same data into export bundles.

## Sidecar protocol

Newline-delimited JSON over stdin/stdout. Requests carry `id`, `method`, `params`; replies carry `ok` and `result` or `error`; streaming uses events with the request id. Methods include `card.run`, `card.compare`, `card.chain`, `card.chat`, `card.debate`, `search.query`, `citation.format`, `image.generate`, `cache.stats`, `cancel`.

## Threading

`TaskRunner` executes imports, AI calls and author batches on worker threads and marshals callbacks through a dispatcher (the Qt event loop in the GUI). The database wrapper serializes access with a recursive mutex and supports nested transactions through savepoints.

## Testing

The C++ suite covers every module below the Qt widgets and runs against a real SQLite and a fake sidecar. Generated DOCX, PDF and PPTX files were additionally validated with python-docx, python-pptx, qpdf, pdftotext and LibreOffice.
