# Eden Research Reader

A desktop research reader. Every meaningful passage of a PDF, DOCX, EPUB or text file becomes a Research Pack that opens a Research Card with AI summaries, evidence, sources, multi-model dialogue, generated images and rich-media discussion. Cards export to Word, PDF, PowerPoint, Markdown, HTML, slides and Anki flashcards, and authors can publish pre-researched Enhanced Eden documents.

## Layout

    src/core/common   utilities, JSON, hashing, logging
    src/core/db       SQLite wrapper, migrations (schema v4)
    src/core/model    domain types
    src/core/repo     one repository per table
    src/core/doc      inflate/deflate, zip, xml, TXT/DOCX/EPUB/PDF readers, pack segmenter, importer, Enhanced Eden format
    src/core/ai       child process, sidecar client, card service
    src/core/app      task runner, key store, themes, library/reader/card-panel models, comments, background simulation, app controller
    src/core/export   exporters (docx, pdf, pptx, md, html, slides, anki) and the export builder
    src/ui            Qt 6 widgets (library, reader, card panel, settings, export, animated background)
    python/eden_ai    standard-library AI sidecar: providers, 62 research functions, cache, search, images, stdio server
    tests             C++ tests (test_*.cpp) and a fake sidecar; Python tests live in python/tests

## Build

Requirements: C++20 compiler, CMake 3.20+, SQLite3 development files, Python 3.10+. The GUI additionally needs Qt 6 Widgets (Qt Multimedia is optional and enables voice and camera recording).

    cmake -S . -B build
    cmake --build build -j
    ./build/bin/eden_tests
    (cd python && python3 -m unittest discover -s tests -t .)
    ./build/bin/eden_reader

Use `-DEDEN_BUILD_UI=OFF` for a core-only build and `-DEDEN_SANITIZE=ON` for AddressSanitizer and UBSan.

## Running with AI

Open Settings, paste an API key for Claude or Grok (stored in the system keyring through `secret-tool` when present, otherwise in a private file under the data folder). A local OpenAI-compatible server such as Ollama needs no key. Keys are sent to the Python sidecar in memory only.

## Enhanced Eden documents

An author prepares research with "Prepare research for every passage", then publishes with "Publish Enhanced Eden document". The `.eden` file is a zip holding the extracted text, pack boundaries, research cards, sources, Writer-Original comments with media, pre-rendered images and suggested prompts. Readers import it like any other file. Files are not cryptographically signed, so authorship is as trustworthy as the source of the file.

## Known limits

- Scanned PDFs without a text layer and encrypted PDFs are rejected; multi-column PDFs follow the order stored in the file.
- Exported PDFs use the built-in Helvetica and Courier fonts, so characters outside Western European text are replaced with `?`.
- The Qt interface has not been compiled in the environment this code was written in; the logic beneath it is covered by tests.
- Collaboration, notifications and cloud sync are not implemented.
