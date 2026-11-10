# Eden Research Reader as a website

This is the real application (C++ core, Qt interface and the Python AI sidecar with all 62 functions) running
unchanged inside a container, shown in your browser through noVNC behind a password-protected gateway.

## Why a container and not a static page on GitHub Pages
A pure in-browser build would have to drop functions: browsers forbid the cross-origin requests that web search and
page/citation fetching rely on, give no separate processes or free threads for the AI sidecar, and have no real file
system. Running the app itself on a server keeps every function and every line of code. The cost: the website needs a
host that can run a container (your own PC, a VPS, Fly.io, Render, Railway, Cloud Run ...). GitHub Pages cannot.

## 1. Build it on GitHub
1. Create a repository, upload the contents of this folder (keep the `.github` folder).
2. The *Website (container image)* workflow runs on every push (Actions tab). It runs all tests, builds the image (the
   build fails if the app does not compile), starts it, checks that Eden is running on the virtual screen, logs in,
   reaches the desktop through the WebSocket proxy and round-trips a file. On success it publishes
   `ghcr.io/<your-github-name>/<repo-name>` (latest). In the repo's *Packages* settings you can make it public.

## 2. Run it
```
docker run -d --name eden -p 8080:8080 -e EDEN_WEB_PASSWORD='choose-a-password' -v eden-data:/data \
  ghcr.io/<your-github-name>/<repo-name>:latest
```
or `EDEN_WEB_PASSWORD=choose-a-password docker compose up -d --build` in this folder. Open http://localhost:8080,
type the password and Eden appears in the page. On a public server put it behind HTTPS (Caddy, nginx, your host's
TLS) - the gateway sets secure cookies automatically when it sees `X-Forwarded-Proto: https`.

Everything Eden stores (library, notes, API keys) lives in the `/data` volume, so it survives restarts and updates.

## Using it
- **Eden** fills the page; it restarts by itself if you close it.
- **Files** (link above the desktop): upload documents/media from your computer, download exports. Eden's file dialogs
  open in that same folder, so Import/Attach/Save find your files directly.
- **Record**: records audio/video with your own microphone/camera in the browser and saves the file to the same folder;
  then use Eden's attach button. (Needs https or localhost.)
- Settings: `EDEN_WEB_SCREEN` (default `1360x860x24`), `EDEN_WEB_MAX_UPLOAD` (bytes, default 2 GB).

## Security
The site is protected by the password (rate-limited, signed HttpOnly cookie, WebSocket requires login). Choose a long
password, use HTTPS on the internet, and remember that anyone with the password has the same access as you, including
your stored API keys.

## Verified here vs. not
Verified by running it on Linux: the gateway (15 tests: login, rate-limit, forged cookies, upload/download, path
traversal, size limit, static files, WebSocket proxy incl. close/ping, desktop-down case), a second independent check
of the WebSocket proxy with Node's standards-compliant client (100 KB echo, `binary` subprotocol), the startup script
syntax and the virtual-screen start, plus the Python suite (45 tests).
Not verifiable without Docker/Qt in my environment: the image build itself and the Qt interface code (the original
README says the UI was never compiled). If the first Actions run stops on an error, send me the log.
