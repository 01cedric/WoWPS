#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
python3 - "$ROOT" "$TEMP" <<'PYGEN'
from pathlib import Path
import re,sys
root,temp=map(Path,sys.argv[1:]); source=(root/'assets/shaders/wmo.frag.glsl').read_text()
result='#include <glm/glm.hpp>\n#include <cmath>\nusing vec3=glm::vec3; using glm::max; using glm::dot; using glm::inversesqrt; using std::pow;\n'
for name in ['bakedInteriorLighting','outdoorGlassReflection','directionalSpecular','outdoorUnlitLighting']:
 start=source.index(('float ' if name=='directionalSpecular' else 'vec3 ')+name+'(');end=source.index('}',start)+1
 text=source[start:end]
 result+=re.sub(r'(?<![\w.])(\d+\.\d+)(?![\w.])',r'\1f',text)+'\n'
(temp/'wmo_shader_math.generated.hpp').write_text(result)
renderer=(root/'src/rendering/wmo_renderer.cpp').read_text()
start=renderer.index('struct BatchKey {');end=renderer.index('        std::unordered_map<BatchKey',start)
header=(root/'include/rendering/wmo_renderer.hpp').read_text()
a=header.index('struct WMOMaterialUBO {');b=header.index('};',a)+2
(temp/'wmo_material.generated.hpp').write_text('#include <functional>\n#include <cstddef>\n#include <cstdint>\n'+renderer[start:end]+header[a:b])
PYGEN
FLAGS=(-I"$TEMP" -std=c++20 -O1 -g -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" -pthread)
if [ "${SANITIZE:-0}" = 1 ]; then
 FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
 export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
 export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
"${CXX:-c++}" "${FLAGS[@]}" "$ROOT/tools/tests/wmo_lighting_material_test.cpp" \
 "$ROOT/src/pipeline/wmo_loader.cpp" "$ROOT/src/core/logger.cpp" -o "$TEMP/wmo-lighting"
"$TEMP/wmo-lighting"
