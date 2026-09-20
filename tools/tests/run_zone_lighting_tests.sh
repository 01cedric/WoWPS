#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
FLAGS=(-std=c++20 -O1 -g -ffunction-sections -fdata-sections -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" -pthread)
if [ "${SANITIZE:-0}" = 1 ]; then
 FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
 export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
 export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
"${CXX:-c++}" "${FLAGS[@]}" "$ROOT/tools/tests/zone_lighting_test.cpp" \
 "$ROOT/src/rendering/lighting_manager.cpp" "$ROOT/src/rendering/zone_ambience.cpp" "$ROOT/src/core/logger.cpp" \
 -Wl,--gc-sections -o "$TEMP/zone-lighting"
"$TEMP/zone-lighting"
