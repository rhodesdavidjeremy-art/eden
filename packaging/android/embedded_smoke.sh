#!/usr/bin/env bash
# Builds and runs embedded_smoke.cpp against this machine's CPython (needs python3-dev / libpython3-dev).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WORK="$(mktemp -d)"
STDLIB="$(python3 -c 'import sysconfig; print(sysconfig.get_path("stdlib"))')"
NAME="$(basename "$STDLIB")"
mkdir -p "$WORK/prefix/lib"
cp -r "$STDLIB" "$WORK/prefix/lib/$NAME"
"$HERE/make_bundle.sh" "$WORK/prefix" "$(command -v python3)" "$WORK/python-bundle.zip"
CFLAGS="$(python3-config --includes)"
LDFLAGS="$(python3-config --embed --ldflags 2>/dev/null || python3-config --ldflags)"
S="$REPO/src/core"
g++ -std=c++20 -DEDEN_EMBED_PYTHON -I"$S" $CFLAGS "$HERE/embedded_smoke.cpp" \
  "$S/app/BundleExtractor.cpp" "$S/doc/Zip.cpp" "$S/doc/Inflate.cpp" "$S/common/Hash.cpp" "$S/common/Strings.cpp" \
  "$S/ai/ChildProcess.cpp" "$S/ai/EmbeddedPython.cpp" $LDFLAGS -lpthread -o "$WORK/smoke"
"$WORK/smoke" "$WORK/python-bundle.zip" "$WORK/runtime"
rm -rf "$WORK"
