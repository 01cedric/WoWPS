#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
sources=(tools/tests/local_vehicle_effects_test.cpp src/game/local_gameplay.cpp src/game/local_melee.cpp src/game/local_services.cpp src/game/local_travel.cpp src/game/local_bots.cpp src/game/local_world_catalog.cpp src/pipeline/dbc_loader.cpp src/core/logger.cpp)
objects=()
for source in "${sources[@]}"; do
  object="$build/$(basename "$source").o"
  g++ -std=c++20 -O1 -ffunction-sections -fdata-sections -I"$root/include" -I"$root/extern" -I"$root/extern/glm" -c "$root/$source" -o "$object"
  objects+=("$object")
done
g++ "${objects[@]}" -Wl,--gc-sections -pthread -o "$build/test"
"$build/test" "$root/tools/tests/fixtures/vehicle_effects_world.json"
