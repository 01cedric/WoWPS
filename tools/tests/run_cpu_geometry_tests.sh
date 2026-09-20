#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
FLAGS=(-std=c++20 -O1 -g -pthread -DWOWEE_PS4
    -I"$ROOT/tools/tests/stubs/cpu_geometry" -I"$ROOT/include")
if [ "${SANITIZE:-0}" = 1 ]; then
    FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
"${CXX:-c++}" "${FLAGS[@]}" "$ROOT/tools/tests/cpu_geometry_test.cpp" \
    "$ROOT/src/platform/ps4/cpu_geometry.cpp" -o "$TEST_DIR/cpu-geometry"
"$TEST_DIR/cpu-geometry"
