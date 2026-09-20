#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CXX:-c++}" -std=c++20 -O1 -g -ffunction-sections -fdata-sections "${SAN_FLAGS[@]}" \
    -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" -I"$ROOT/tools/tests" \
    "$ROOT/tools/tests/local_binary_resist_test.cpp" \
    "$ROOT/src/game/local_gameplay.cpp" "$ROOT/src/game/local_melee.cpp" \
    "$ROOT/src/game/local_services.cpp" "$ROOT/src/game/local_travel.cpp" \
    "$ROOT/src/game/local_world_catalog.cpp" "$ROOT/src/pipeline/dbc_loader.cpp" \
    "$ROOT/src/core/logger.cpp" -Wl,--gc-sections -pthread -o "$TEST_DIR/binary_resist"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}" \
    "$TEST_DIR/binary_resist" "$1" "$ROOT/assets/local_realm/catalog"
