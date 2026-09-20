#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
FLAGS=(-std=c++20 -O1 -g -ffunction-sections -fdata-sections
    -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/ps4/third_party/ps4_vulkan/include")
if [ "${SANITIZE:-0}" = 1 ]; then
    FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
    # LeakSanitizer cannot inspect threads under the workspace tracer.
    # Address/undefined-behavior instrumentation remains enabled.
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
for platform in desktop ps4; do
    PLATFORM_FLAGS=()
    if [ "$platform" = ps4 ]; then PLATFORM_FLAGS=(-DWOWEE_PS4); fi
    "${CXX:-c++}" "${FLAGS[@]}" "${PLATFORM_FLAGS[@]}" \
        "$ROOT/tools/tests/volumetric_settings_test.cpp" "$ROOT/src/ui/settings_schema.cpp" \
        -Wl,--gc-sections -o "$TEST_DIR/settings-$platform"
    "$TEST_DIR/settings-$platform"
done
"${CXX:-c++}" "${FLAGS[@]}" -DWOWEE_PS4 \
    "$ROOT/tools/tests/volumetric_depth_barrier_test.cpp" "$ROOT/src/rendering/vk_utils.cpp" \
    -Wl,--gc-sections -o "$TEST_DIR/depth-barrier"
"$TEST_DIR/depth-barrier"
