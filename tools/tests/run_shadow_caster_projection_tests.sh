#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-g++}" -std=c++17 -DGLM_FORCE_DEPTH_ZERO_TO_ONE -fsanitize=address,undefined -fno-omit-frame-pointer -I"$ROOT/include" -I"$ROOT/extern/glm" "$ROOT/tools/tests/shadow_caster_projection_test.cpp" "$ROOT/src/rendering/frustum.cpp" -o "$TEST_DIR/test"
ASAN_OPTIONS=detect_leaks=0 "$TEST_DIR/test"
