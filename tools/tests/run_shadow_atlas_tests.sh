#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
"${CXX:-g++}" -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$ROOT/include" -I"$ROOT/extern/glm" \
    "$ROOT/tools/tests/shadow_atlas_test.cpp" -o "$TEST_DIR/shadow_atlas_test"
ASAN_OPTIONS=detect_leaks=0 "$TEST_DIR/shadow_atlas_test"
