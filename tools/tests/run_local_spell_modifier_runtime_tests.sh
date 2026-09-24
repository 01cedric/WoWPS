#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"; trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-c++}" -std=c++20 -O1 -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" \
  "$ROOT/tools/tests/local_spell_modifier_runtime_test.cpp" -o "$TEST_DIR/local_spell_modifier_runtime_test"
"$TEST_DIR/local_spell_modifier_runtime_test"
