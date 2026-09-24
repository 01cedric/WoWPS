#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if [ "$#" -ne 1 ]; then echo "Usage: $(basename "$0") DBC_DIRECTORY" >&2; exit 2; fi
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-c++}" -std=c++20 -O1 -g -ffunction-sections -fdata-sections \
    -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" -I"$ROOT/tools/tests" \
    "$ROOT/tools/tests/local_npc_spell_family_test.cpp" "$ROOT/src/game/local_gameplay.cpp" "$ROOT/src/game/local_melee.cpp" \
    "$ROOT/src/game/local_services.cpp" "$ROOT/src/game/local_travel.cpp" \
    "$ROOT/src/game/local_bots.cpp" "$ROOT/src/game/local_world_catalog.cpp" \
    "$ROOT/src/pipeline/dbc_loader.cpp" "$ROOT/src/core/logger.cpp" \
    -Wl,--gc-sections -pthread -o "$TEST_DIR/local_npc_spell_family_test"
"$TEST_DIR/local_npc_spell_family_test" "$1"
