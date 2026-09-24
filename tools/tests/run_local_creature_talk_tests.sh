#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-c++}" -std=c++20 -O1 -g -ffunction-sections -fdata-sections \
    -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" \
    "$ROOT/tools/tests/local_creature_talk_test.cpp" "$ROOT/src/game/local_melee.cpp" \
    "$ROOT/src/game/local_services.cpp" "$ROOT/src/game/local_travel.cpp" \
    "$ROOT/src/game/local_bots.cpp" "$ROOT/src/game/local_world_catalog.cpp" \
    "$ROOT/src/pipeline/dbc_loader.cpp" "$ROOT/src/core/logger.cpp" \
    -Wl,--gc-sections -pthread -o "$TEST_DIR/local_creature_talk_test"
"$TEST_DIR/local_creature_talk_test" "$ROOT/assets/local_realm"
