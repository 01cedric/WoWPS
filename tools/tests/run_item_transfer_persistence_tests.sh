#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-c++}" -std=c++20 -O1 -g -I"$ROOT/include" \
    "$ROOT/tools/tests/item_transfer_persistence_test.cpp" "$ROOT/src/game/local_bots.cpp" \
    -o "$TEST_DIR/item_transfer_persistence_test"
"$TEST_DIR/item_transfer_persistence_test"
