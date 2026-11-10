#!/usr/bin/env bash
# Usage: make_bundle.sh <python-prefix> <host-python-or-empty> <output.zip>
# Packs the CPython runtime and the complete eden_ai sidecar (python/ folder of this repository) into the zip
# that the app unpacks on first start. Entries are stored uncompressed; the APK compresses them itself.
set -euo pipefail
PREFIX="$1"; HOSTPY="${2:-}"; OUT="$3"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"

STAGE="$(mktemp -d)"
STDLIB="$(ls -d "$PREFIX"/lib/python3.* | head -n 1)"
STDNAME="$(basename "$STDLIB")"
mkdir -p "$STAGE/python/lib" "$STAGE/eden-python"
cp -r "$STDLIB" "$STAGE/python/lib/$STDNAME"
# Drop only what Python itself does not need at run time (its test-suite, IDLE, Tk, build configs)
for junk in test idlelib tkinter turtledemo ensurepip lib2to3 turtle.py; do rm -rf "$STAGE/python/lib/$STDNAME/$junk"; done
rm -rf "$STAGE"/python/lib/"$STDNAME"/config-*
# The whole sidecar package and everything beside it
cp -r "$REPO/python/." "$STAGE/eden-python/"
find "$STAGE" -name __pycache__ -prune -exec rm -rf {} +

if [ -n "$HOSTPY" ] && [ -x "$HOSTPY" ]; then
  # Pre-compile with the matching host interpreter; unchecked-hash pycs are trusted regardless of file times
  "$HOSTPY" -m compileall -q -j 0 --invalidation-mode unchecked-hash "$STAGE/python/lib/$STDNAME" || true
  "$HOSTPY" -m compileall -q --invalidation-mode unchecked-hash "$STAGE/eden-python" || true
fi

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
(cd "$STAGE" && zip -0 -r -q "$OUT" python eden-python)
echo "bundle: $(du -h "$OUT" | cut -f1), $(unzip -Z1 "$OUT" | wc -l) entries"
rm -rf "$STAGE"
