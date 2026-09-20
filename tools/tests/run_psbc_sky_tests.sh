#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
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
    "$ROOT/tools/tests/psbc_sky_test.cpp" "$ROOT/tools/tests/psbc_host_compat.cpp" \
    "$ROOT/ps4/third_party/ps4_vulkan/lib/libpsbc.orbis.a" \
    "$ROOT/ps4/third_party/ps4_vulkan/lib/libopengnm.a" \
    "${LINK_FLAGS[@]}" -lpthread -ldl -lm -lz -o "$TEST_DIR/psbc_sky_test"
"$TEST_DIR/psbc_sky_test" "$ROOT/assets/shaders/skybox.vert.spv" vertex
"$TEST_DIR/psbc_sky_test" "$ROOT/assets/shaders/skybox.frag.spv" fragment
"$TEST_DIR/psbc_sky_test" "$ROOT/assets/shaders/clouds.frag.spv" pushonly
rm -rf "$TEST_DIR"
