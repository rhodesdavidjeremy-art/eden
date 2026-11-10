# Building Eden Research Reader for Windows on GitHub

1. Create a new GitHub repository and upload the contents of this folder (keep the `.github` folder).
2. Open the **Actions** tab. The *Windows build* workflow starts on every push, or run it by hand with **Run workflow**.
3. When it finishes, download from the run page:
   - `EdenReader-windows-x64-portable` - unzip and run `eden_reader.exe` (Python is bundled).
   - `EdenReader-windows-x64-installer` - `EdenReader-Setup.exe`, a normal installer.
4. To publish a release, push a tag such as `v0.1.0`; both files are attached to the release automatically.

## What the workflow does
- **core-tests (Linux):** runs the complete C++ suite under AddressSanitizer plus the Python suite (`scripts/check.sh`).
- **windows:** builds the Qt 6 GUI with MinGW-w64 (MSYS2), collects the Qt plugins and DLLs, bundles the embedded Python 3.12 with the `eden_ai` sidecar, checks the sidecar answers a JSON request, checks no DLL is missing, then zips it and builds the installer.

## Windows specifics added to the code (nothing was removed)
- `src/core/ai/ChildProcessWin.cpp` - Windows version of the child-process/pipe layer (the POSIX one is unchanged and used on Linux).
- `AppPaths.cpp`, `KeyStore.cpp`, `Clock.cpp` - Windows branches (`%LOCALAPPDATA%\eden-reader`, key file instead of `secret-tool`, `_mkgmtime`).
- `src/ui/main.cpp` - finds the bundled `python-runtime\python.exe` and `python\eden_ai`.

## Known limits
- The Qt interface was never compiled by the original author and I could not compile it either, so the first Actions run may report a compile error in `src/ui`; paste the log and it can be fixed quickly.
- Keys are stored in a private file under `%LOCALAPPDATA%\eden-reader` (Windows has no `secret-tool`).
- The exe is not code-signed, so Windows SmartScreen may warn on first launch.
