#!/usr/bin/env bash
# Ignite source admission (a standalone harness with its own content lookup)
# and the Ignite runtime (talent 34) with the Molten Armor / Molten Shields
# interaction; both need the client's own DBC directory.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DBC="${1:?usage: $0 <dbc directory>}"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
COMMON=("$ROOT/src/pipeline/dbc_loader.cpp" "$ROOT/src/core/logger.cpp")
GAMEPLAY=("$ROOT/src/game/local_gameplay.cpp" "$ROOT/src/game/local_melee.cpp" "$ROOT/src/game/local_services.cpp"
          "$ROOT/src/game/local_travel.cpp" "$ROOT/src/game/local_bots.cpp" "$ROOT/src/game/local_world_catalog.cpp")
"${CXX:-c++}" -std=c++20 -O1 -g -ffunction-sections -fdata-sections \
    -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" -I"$ROOT/tools/tests" \
    "$ROOT/tools/tests/local_ignite_source_test.cpp" "${COMMON[@]}" \
    -Wl,--gc-sections -pthread -o "$TEST_DIR/local_ignite_source_test"
"$TEST_DIR/local_ignite_source_test" "$DBC"
"${CXX:-c++}" -std=c++20 -O1 -g -ffunction-sections -fdata-sections \
    -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" -I"$ROOT/tools/tests" \
    "$ROOT/tools/tests/local_ignite_runtime_test.cpp" "${GAMEPLAY[@]}" "${COMMON[@]}" \
    -Wl,--gc-sections -pthread -o "$TEST_DIR/local_ignite_runtime_test"
"$TEST_DIR/local_ignite_runtime_test" "$DBC"
