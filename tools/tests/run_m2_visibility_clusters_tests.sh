#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
FLAGS=(-std=c++17 -O2)
if [ "${SANITIZE:-0}" = 1 ]; then
    FLAGS+=(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
"${CXX:-g++}" "${FLAGS[@]}" -I"$root/include" -I"$root/extern/glm" "$root/tools/tests/m2_visibility_clusters_test.cpp" "$root/src/rendering/frustum.cpp" -o "$tmpdir/test"
"$tmpdir/test"
