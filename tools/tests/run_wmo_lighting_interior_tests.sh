#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
python3 - "$ROOT" "$TEMP" <<'PYGEN'
from pathlib import Path
import re,sys
root,temp=map(Path,sys.argv[1:]); source=(root/'assets/shaders/wmo.frag.glsl').read_text()
result='#include <glm/glm.hpp>\nusing vec3=glm::vec3; using glm::max;\n'
for name in ['bakedInteriorLighting','outdoorGlassReflection']:
 start=source.index('vec3 '+name+'(');end=source.index('}',start)+1
 text=source[start:end]
 result+=re.sub(r'(?<![\w.])(\d+\.\d+)(?![\w.])',r'\1f',text)+'\n'
(temp/'wmo_shader_math.generated.hpp').write_text(result)
PYGEN
FLAGS=(-I"$TEMP" -std=c++20 -O1 -g -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" -pthread)
if [ "${SANITIZE:-0}" = 1 ]; then
 FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
 export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
 export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
"${CXX:-c++}" "${FLAGS[@]}" "$ROOT/tools/tests/wmo_lighting_interior_test.cpp" \
 "$ROOT/src/pipeline/wmo_loader.cpp" "$ROOT/src/core/logger.cpp" -o "$TEMP/wmo-lighting"
"$TEMP/wmo-lighting"
