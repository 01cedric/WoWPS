#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
cxx="${CXX:-g++}"
"$cxx" -std=c++17 -O2 -I"$root/include" -I"$root/extern/glm" "$root/tools/tests/m2_shadow_clock_cache_test.cpp" -o "$tmpdir/cache"
"$tmpdir/cache"
"$cxx" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I"$root/include" -I"$root/extern/glm" "$root/tools/tests/m2_shadow_clock_cache_test.cpp" -o "$tmpdir/cache-sanitized"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$tmpdir/cache-sanitized"
