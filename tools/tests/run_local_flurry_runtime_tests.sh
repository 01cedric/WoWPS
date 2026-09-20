#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="${FLURRY_TEST_DIR:-$(mktemp -d)}"
mkdir -p "$TEST_DIR"
if [ -z "${FLURRY_TEST_DIR:-}" ]; then trap 'rm -rf "$TEST_DIR"' EXIT; fi
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
FLAGS=(-std=c++20 -O1 -g1 -ffunction-sections -fdata-sections "${SAN_FLAGS[@]}" -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm")
# Explicit reuse is intended only for relinking fixture corrections against
# an unchanged production freeze, compiler and sanitizer flags.
OBJECTS=()
for unit in game/local_gameplay game/local_melee game/local_services game/local_travel game/local_world_catalog pipeline/dbc_loader core/logger; do
    object="$TEST_DIR/${unit##*/}.o"
    if [ "${FLURRY_REUSE_FROZEN_OBJECTS:-0}" != 1 ] || [ ! -f "$object" ]; then
        "${CXX:-c++}" "${FLAGS[@]}" -c "$ROOT/src/$unit.cpp" -o "$object"
    fi
    OBJECTS+=("$object")
done
for test in local_flurry_runtime_test local_bloodthirst_runtime_test local_proc_runtime_test; do
    "${CXX:-c++}" "${FLAGS[@]}" "$ROOT/tools/tests/$test.cpp" "${OBJECTS[@]}" -Wl,--gc-sections -pthread -o "$TEST_DIR/$test"
    ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}" "$TEST_DIR/$test"
done
