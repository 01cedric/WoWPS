#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
# The bundled PSBC is x86-64 SysV, built with libc++. The adapter supplies
# libc++ hash/error symbols and refuses console OS operations on the host.
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
    "$ROOT/tools/tests/psbc_bloom_test.cpp" "$ROOT/tools/tests/psbc_host_compat.cpp" \
    "$ROOT/ps4/third_party/ps4_vulkan/lib/libpsbc.orbis.a" \
    "$ROOT/ps4/third_party/ps4_vulkan/lib/libopengnm.a" \
    "${LINK_FLAGS[@]}" -lpthread -ldl -lm -lz -o "$TEST_DIR/psbc_bloom_test"
for shader in bloom_blur.frag bloom_composite.frag; do
    if command -v spirv-val >/dev/null; then
        spirv-val --target-env vulkan1.0 "$ROOT/assets/shaders/$shader.spv"
    fi
    "$TEST_DIR/psbc_bloom_test" "$ROOT/assets/shaders/$shader.spv"
done
rm -rf "$TEST_DIR"
