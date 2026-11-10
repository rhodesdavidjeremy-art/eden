#!/usr/bin/env bash
# Builds CPython for Android (arm64-v8a) with CPython's own Android tooling and prints the prefix path on the
# last line of output ("PYTHON_PREFIX=..."). Run from any directory; needs ANDROID_HOME (Android SDK).
set -euo pipefail

PYTHON_TAG="${PYTHON_TAG:-v3.13.5}"
HOST="${ANDROID_PYTHON_HOST:-aarch64-linux-android}"
WORK="${WORK:-$PWD/cpython-android}"

# Re-use a previous build (restored from the CI cache) when there is one
if [ ! -d "$WORK/.git" ]; then
  rm -rf "$WORK"
  git clone --depth 1 --branch "$PYTHON_TAG" https://github.com/python/cpython.git "$WORK"
fi
cd "$WORK"

test -f Android/android.py || { echo "This CPython tag has no Android/android.py (need 3.13 or newer)"; exit 1; }

# CPython pins the NDK it is built with; install exactly that one.
NDK_VERSION="$(grep -oE 'ndk_version=[0-9.]+' Android/android-env.sh | head -n 1 | cut -d= -f2 || true)"
if [ -n "$NDK_VERSION" ] && [ ! -d "$ANDROID_HOME/ndk/$NDK_VERSION" ]; then
  yes | "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --licenses >/dev/null || true
  "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" "ndk;$NDK_VERSION"
fi

if ! find cross-build -path "*prefix/lib/libpython3*.so" 2>/dev/null | grep -q .; then
  python3 Android/android.py build "$HOST"
else
  echo "Using cached CPython Android build"
fi

LIB="$(find cross-build -path "*prefix/lib/libpython3*.so" | head -n 1)"
if [ -z "$LIB" ]; then
  echo "libpython was not produced; build tree:"; find cross-build -maxdepth 4 | head -80
  exit 1
fi
PREFIX="$(cd "$(dirname "$LIB")/.." && pwd)"
test -f "$PREFIX"/include/python3*/Python.h || { echo "Python.h missing under $PREFIX/include"; exit 1; }
STDLIB="$(ls -d "$PREFIX"/lib/python3.* | head -n 1)"
test -f "$STDLIB/os.py" || { echo "stdlib missing under $PREFIX/lib"; exit 1; }
test -d "$STDLIB/lib-dynload" || { echo "lib-dynload missing under $STDLIB"; exit 1; }
echo "--- libraries shipped with Python:"; ls -1 "$PREFIX"/lib/*.so
echo "PYTHON_PREFIX=$PREFIX"
echo "PYTHON_HOST_BIN=$(find "$PWD/cross-build/build" -maxdepth 1 -name python -type f | head -n 1)"
