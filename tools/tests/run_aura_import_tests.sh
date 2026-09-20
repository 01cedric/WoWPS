#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-c++}" -std=c++20 -O1 -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" \
 "$ROOT/tools/tests/aura_import_test.cpp" "$ROOT/src/pipeline/dbc_loader.cpp" "$ROOT/src/core/logger.cpp" -pthread -o "$TEST_DIR/test"
"$TEST_DIR/test"
