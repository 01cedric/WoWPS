#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
# Temporary binaries are intentionally kept for inspection; no user data removed.
mapfile -t LUA_SOURCES < <(rg --files "$ROOT/extern/lua-5.1.5/src" -g '*.c' -g '!lua.c' -g '!luac.c' -g '!print.c')
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
(
    cd "$TEST_DIR"
    "${CC:-cc}" -O1 -g -w -c "${LUA_SOURCES[@]}"
    "${CXX:-c++}" -std=c++20 -O1 -g -pthread "${SAN_FLAGS[@]}" \
        -I"$ROOT/include" -I"$ROOT/extern/glm" -I"$ROOT/extern/lua-5.1.5/src" \
        "$ROOT/tools/tests/memory_performance_test.cpp" ./*.o -lm -ldl -o memory_performance_test
    ./memory_performance_test
)
printf 'Test executable: %s/memory_performance_test\n' "$TEST_DIR"
