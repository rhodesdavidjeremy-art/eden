# Eden Research Reader - one repository, three builds

Upload this whole folder (keep `.github`) to ONE GitHub repository. Open the **Actions** tab; three workflows run
independently on every push (or start any with **Run workflow**):

| Workflow | Produces | Details |
|---|---|---|
| **Windows build** | `EdenReader-windows-x64-portable` (zip) and `EdenReader-windows-x64-installer` (`EdenReader-Setup.exe`) | `docs/build/WINDOWS.md` |
| **Android APK** | `EdenReader-android-arm64-apk` (`EdenReader-android-arm64.apk`) | `docs/build/ANDROID.md` |
| **Website (container image)** | image `ghcr.io/<you>/<repo>` - run it, open http://localhost:8080 | `docs/build/WEBSITE.md` |

Push a tag such as `v0.1.0` to attach the Windows and Android files to a GitHub release.

All three build from the same source (`src/`, `python/`); nothing was removed from the original project. Platform
differences live in small guarded additions: `ChildProcessWin.cpp` (Windows), `EmbeddedPython.cpp` +
`AndroidBootstrap.cpp` + `PlatformFiles.hpp` (Android), `web/` + `Dockerfile` (website).

Not verifiable in my environment: the three CI builds themselves and the Qt interface code (the original README says it
was never compiled). If a run stops with an error, send the log.
