#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
FLAGS=(-std=c++20 -O1 -g -DGLM_FORCE_DEPTH_ZERO_TO_ONE -I"$ROOT/include" -I"$ROOT/extern/glm")
if [ "${SANITIZE:-0}" = 1 ]; then
    FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
"${CXX:-c++}" "${FLAGS[@]}" "$ROOT/tools/tests/m2_shadow_test.cpp" -o "$TEMP/m2-shadow"
"$TEMP/m2-shadow"
python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys
root=Path(sys.argv[1]); shaders=root/'assets/shaders'
original=(shaders/'shadow.vert.glsl').read_text()
wind=original.split('    // Wind vertex displacement',1)[1].split('    vec4 worldPos',1)[0]
for name in ('m2_shadow.vert.glsl','m2_shadow_instanced.vert.glsl'):
    source=(shaders/name).read_text()
    actual=source.split('    // Wind vertex displacement',1)[1].split('    vec4 worldPos',1)[0]
    assert actual.replace('worldRefOrigin','push.model[3].xyz') == wind, name+' altered wind geometry'
    assert 'push.texCoordSet == 1u ? aTexCoord1 : aTexCoord' in source
    assert 'mat2(push.uvLinear.xy, push.uvLinear.zw) * uv + push.uvOffset' in source
    assert 'layout(offset = 124) uint maskMode' in source
fragment=(shaders/'m2_shadow.frag.glsl').read_text()
assert 'alphaMode == 3u ? 0.25 : 0.4' in fragment
assert 'if (push.maskMode == 0u) return;' in fragment
assert 'layout(set = 1, binding = 0)' in fragment
cpp=(root/'src/rendering/m2_renderer_render.cpp').read_text()
assert 'constexpr auto pushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;' in cpp
assert 'shadowInstancedLayout_, pushStages, 0, sizeof(push), &push' in cpp
assert 'shadowPipelineLayout_, pushStages, 0, sizeof(push), &push' in cpp
assert 'shadowTextureCache_' not in cpp
print('PASS shader wind parity, both UV channels, full UV transform, mask ABI/cutoffs and immutable dual-stage pushes')
PY
