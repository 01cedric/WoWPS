#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
test_bin=$(mktemp /tmp/wowps-palette-XXXXXX)
trap 'rm -f "$test_bin"' EXIT
"${CXX:-c++}" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I"$root/include" -I"$root/extern/glm" "$root/tools/tests/celestial_palette_test.cpp" "$root/src/rendering/zone_ambience.cpp" -o "$test_bin"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$test_bin"
