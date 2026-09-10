#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-c++}" -std=c++20 -O1 -I"$ROOT/include" -I"$ROOT/extern/glm" \
 "$ROOT/tools/tests/intro_0190_test.cpp" "$ROOT/src/core/character_intro.cpp" -o "$TEST_DIR/intro_test"
"$TEST_DIR/intro_test"
