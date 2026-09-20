#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
FLAGS=(-std=c++20 -O1 -DGLM_FORCE_DEPTH_ZERO_TO_ONE -I"$ROOT/include" -I"$ROOT/extern/glm")
if [ "${SANITIZE:-0}" = 1 ]; then
  FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
  # LeakSanitizer cannot inspect this ptrace-managed test environment.
  export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
  export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
"${CXX:-c++}" "${FLAGS[@]}" "$ROOT/tools/tests/character_shadow_bounds_test.cpp" \
  "$ROOT/src/rendering/frustum.cpp" -o "$TEMP/character-shadow"
"$TEMP/character-shadow"
