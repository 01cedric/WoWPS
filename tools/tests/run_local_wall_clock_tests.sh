#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror "${SAN_FLAGS[@]}" \
  -I"$ROOT/include" "$ROOT/tools/tests/local_wall_clock_test.cpp" -o "$TEST_DIR/test"
"$TEST_DIR/test"
