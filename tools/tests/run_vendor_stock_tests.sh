#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
# Only the merchant stock entry points are under test; the catalog-backed
# service lookups in the same unit are dropped by linker GC, as in the other
# narrow service suites, rather than by linking the whole world catalog.
"${CXX:-c++}" -std=c++20 -O1 -g -ffunction-sections -fdata-sections "${SAN_FLAGS[@]}" -I"$ROOT/include" \
    "$ROOT/tools/tests/vendor_stock_test.cpp" "$ROOT/src/game/local_services.cpp" \
    -Wl,--gc-sections -o "$TEST_DIR/vendor_stock_test"
"$TEST_DIR/vendor_stock_test"
printf 'Test executable: %s/vendor_stock_test\n' "$TEST_DIR"
