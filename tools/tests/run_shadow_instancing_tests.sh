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
    "$ROOT/tools/tests/psbc_shadow_test.cpp" "$ROOT/tools/tests/psbc_host_compat.cpp" \
    "$ROOT/ps4/third_party/ps4_vulkan/lib/libpsbc.orbis.a" \
    "$ROOT/ps4/third_party/ps4_vulkan/lib/libopengnm.a" \
    "${LINK_FLAGS[@]}" -lpthread -ldl -lm -lz -o "$TEST_DIR/psbc_shadow_test"
python3 - "$ROOT" <<'PY'
import re,sys
from pathlib import Path
p=Path(sys.argv[1])/'assets/shaders'
old=(p/'shadow.vert.glsl').read_text().split('void main() {',1)[1]
new=(p/'shadow_instanced.vert.glsl').read_text().split('void main() {',1)[1]
new=new.replace('    mat4 instanceModel = instances.models[push.instanceDataOffset + gl_InstanceIndex];\n','')
new=new.replace('instanceModel','push.model')
assert new==old, 'Instanced shadow geometry/wind/UV differs from individual shader'
print('PASS unchanged shadow geometry, wind and alpha UV computation')
PY
python3 "$ROOT/tools/ps4/verify_shaders.py"
if command -v spirv-val >/dev/null; then
    spirv-val --target-env vulkan1.0 "$ROOT/assets/shaders/shadow_instanced.vert.spv"
    spirv-val --target-env vulkan1.0 "$ROOT/assets/shaders/shadow.frag.spv"
fi
"$TEST_DIR/psbc_shadow_test" "$ROOT/assets/shaders/shadow_instanced.vert.spv"
"$TEST_DIR/psbc_shadow_test" "$ROOT/assets/shaders/shadow.frag.spv" fragment
printf 'Test executable: %s/psbc_shadow_test\n' "$TEST_DIR"
