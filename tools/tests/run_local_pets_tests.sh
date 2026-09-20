#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if [ "$#" -ne 1 ]; then
    echo "Usage: run_local_pets_tests.sh DBC_DIRECTORY" >&2
    exit 2
fi
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
# The suite drives the real authority with the real client importer and includes
# src/game/local_realm.cpp directly for the save codec, so that translation unit
# must not also be compiled here.
"${CXX:-c++}" -std=c++20 -O1 -g -ffunction-sections -fdata-sections "${SAN_FLAGS[@]}" \
    -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" -I"$ROOT/tools/tests" \
    "$ROOT/tools/tests/local_pets_test.cpp" \
    "$ROOT/src/game/local_gameplay.cpp" "$ROOT/src/game/local_melee.cpp" \
    "$ROOT/src/game/shapeshift_forms.cpp" \
    "$ROOT/src/game/local_services.cpp" "$ROOT/src/game/local_travel.cpp" "$ROOT/src/game/local_bots.cpp" \
    "$ROOT/src/game/local_world_catalog.cpp" "$ROOT/src/pipeline/dbc_loader.cpp" \
    "$ROOT/src/core/logger.cpp" -Wl,--gc-sections -pthread \
    -o "$TEST_DIR/local_pets_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}" \
    "$TEST_DIR/local_pets_test" "$1"
