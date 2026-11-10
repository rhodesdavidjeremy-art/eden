#!/bin/sh
set -e
cd "$(dirname "$0")/.."
cmake -S . -B build -DEDEN_BUILD_UI=OFF -DEDEN_SANITIZE=ON
cmake --build build -j
ASAN_OPTIONS=detect_leaks=0 ./build/bin/eden_tests
(cd python && python3 -m unittest discover -s tests -t .)
