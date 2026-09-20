#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
LINK_FLAGS=()
for family in compress_in_place compress_xof hash_many; do
    case "$family" in
        hash_many) variants=(sse2 sse41 avx2 avx512);;
        *) variants=(sse2 sse41 avx512);;
    esac
    for variant in "${variants[@]}"; do
        LINK_FLAGS+=("-Wl,--defsym=blake3_${family}_${variant}=blake3_${family}_portable")
    done
done
"${CXX:-clang++-18}" -std=c++17 -O2 -I"$ROOT/ps4/third_party/ps4_vulkan/include" \
    "$ROOT/tools/tests/psbc_water_test.cpp" "$ROOT/tools/tests/psbc_host_compat.cpp" \
    "$ROOT/ps4/third_party/ps4_vulkan/lib/libpsbc.orbis.a" \
    "$ROOT/ps4/third_party/ps4_vulkan/lib/libopengnm.a" \
    "${LINK_FLAGS[@]}" -lpthread -ldl -lm -lz -o "$TEMP/shadow-material"
for kind in vert frag; do
    "$TEMP/shadow-material" "$ROOT/assets/shaders/water.$kind.spv" "$kind"
done
