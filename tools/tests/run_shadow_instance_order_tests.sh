#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CXX:-c++}" -std=c++17 -O2 -g "${SAN_FLAGS[@]}" -I"$ROOT/include" -I"$ROOT/extern/glm" \
    "$ROOT/tools/tests/shadow_instance_order_test.cpp" -o "$TEST_DIR/shadow_instance_order_test"
"$TEST_DIR/shadow_instance_order_test"
printf 'Test executable: %s/shadow_instance_order_test\n' "$TEST_DIR"
