#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CXX:-c++}" -std=c++20 -O1 -g "${SAN_FLAGS[@]}" -I"$ROOT/include" \
    "$ROOT/tools/tests/framexml_pad_0178_test.cpp" "$ROOT/src/ui/widget_tree.cpp" -o "$TEST_DIR/pad_test"
"$TEST_DIR/pad_test"
