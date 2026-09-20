#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="${STORMSTRIKE_CODEC_TEST_DIR:-$(mktemp -d)}"
mkdir -p "$TEST_DIR"
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
RUNTIME_INPUTS=()
if [ -n "${RUNTIME_OBJECT_DIR:-}" ]; then
    for UNIT in local_gameplay local_melee local_services local_travel local_world_catalog dbc_loader logger; do
        RUNTIME_INPUTS+=("$RUNTIME_OBJECT_DIR/$UNIT.o")
    done
else
    for UNIT in local_gameplay local_melee local_services local_travel local_world_catalog; do
        RUNTIME_INPUTS+=("$ROOT/src/game/$UNIT.cpp")
    done
    RUNTIME_INPUTS+=("$ROOT/src/pipeline/dbc_loader.cpp" "$ROOT/src/core/logger.cpp")
fi
"${CXX:-c++}" -std=c++20 -O1 -g1 -ffunction-sections -fdata-sections "${SAN_FLAGS[@]}" \
    -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" \
    "$ROOT/tools/tests/local_stormstrike_codec_test.cpp" "${RUNTIME_INPUTS[@]}" \
    -pthread -Wl,--gc-sections -o "$TEST_DIR/local_stormstrike_codec_test"
"$TEST_DIR/local_stormstrike_codec_test"
printf 'Test executable: %s/local_stormstrike_codec_test\n' "$TEST_DIR"
