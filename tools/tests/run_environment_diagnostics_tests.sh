#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
FLAGS=(-std=c++20 -O1 -g -I"$ROOT/include")
if [ "${SANITIZE:-0}" = 1 ]; then
    FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
fi
"${CXX:-c++}" "${FLAGS[@]}" "$ROOT/tools/tests/g1_environment_diagnostics_test.cpp" -o "$TEMP/test"
"$TEMP/test"
