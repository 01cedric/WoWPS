#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
# Deliberate global allocation failure injection uses a standalone allocator.
"${CXX:-c++}" -std=c++20 -O0 -g -pthread -I"$ROOT/include" "$ROOT/tools/tests/memory_recovery_0180_test.cpp" -Wl,--wrap=realloc -o "$TEST_DIR/memory_recovery"
"$TEST_DIR/memory_recovery"
