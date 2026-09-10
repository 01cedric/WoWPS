#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
mapfile -t LUA_SOURCES < <(rg --files "$ROOT/extern/lua-5.1.5/src" -g '*.c' -g '!lua.c' -g '!luac.c' -g '!print.c')
(cd "$TEST_DIR"
 "${CC:-cc}" -O1 -w -c "${LUA_SOURCES[@]}"
 "${CXX:-c++}" -std=c++20 -O1 -I"$ROOT/include" -I"$ROOT/extern/lua-5.1.5/src" "$ROOT/tools/tests/quest_rewards_framexml_test.cpp" ./*.o -lm -ldl -o services_test
 ./services_test "$ROOT/tools/tests/quest_rewards_framexml_test.lua"
)
