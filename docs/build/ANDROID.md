# Building Eden Research Reader as an Android APK on GitHub

1. Create a new GitHub repository and upload the contents of this folder (keep the `.github` folder).
2. Open the **Actions** tab. The *Android APK* workflow starts on every push, or start it with **Run workflow**.
   The first run takes about 40-60 minutes (it compiles Python for Android); later runs reuse a cache.
3. Download `EdenReader-android-arm64-apk` from the run page, unzip it, copy `EdenReader-android-arm64.apk` to the
   phone and open it (allow "install unknown apps" for your file manager or browser). Needs Android 6.0+ and a 64-bit
   (arm64) phone, which is virtually every phone made since 2016.
4. To publish a release, push a tag such as `v0.1.0`; the APK is attached automatically.

## Keeping updates installable
Without extra setup each build is signed with a fresh throw-away key, so a new APK can only be installed after
uninstalling the old one (your library is lost). To avoid that, create one keystore and add three repository
secrets: `ANDROID_KEYSTORE_BASE64` (`base64 -w0 my.jks`), `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`.

## How all the functions run on Android (nothing was removed)
- **C++ core and Qt interface:** built unchanged with Qt 6.7 for Android.
- **Python AI sidecar (all 62 research functions):** Android cannot launch a separate Python process, so CPython is built
  for Android in CI and runs *inside the app* on its own thread. The sidecar's stdin/stdout are wired to pipes, so the
  C++ side and the JSON protocol are exactly the same as on desktop (`src/core/ai/EmbeddedPython.cpp`).
  On first start the app unpacks Python plus the complete `eden_ai` package to its private storage (`BundleExtractor`).
- **SQLite with FTS5:** the official amalgamation is compiled into the app (Android has no SQLite headers).
- **Files:** Android's picker returns `content://` addresses; `PlatformFiles.hpp` copies picked files into app storage
  before the core reads them, and copies exports back to the location you choose.
- **Storage / temp / keys:** private app folders; API keys go in the private key file (no `secret-tool` on Android).
- **Permissions:** internet, microphone and camera (asked at the moment you first record).
- Only Python's own test-suite, IDLE, Tk and build-config folders are left out of the bundled standard library.

## Verified here vs. not
Verified by running it on Linux: the bundle -> unpack -> in-process Python -> JSON round-trip chain (including restart,
clean exit and bad-module error), the zip unpacker (stored/deflated/malicious paths), the Python test-suite (45 tests).
Not verifiable without Android tooling: the Qt/Gradle/NDK build itself and the Qt interface code (the original README says
the UI was never compiled). The first Actions run may stop on a build error; send me the log and I will fix it.

## Known limits
- The interface is the desktop layout (library, reader, card panel); it works on a tablet or a large phone but is
  cramped on a small phone.
- arm64 only (add `x86_64` to Qt/Python builds if you need an emulator).
