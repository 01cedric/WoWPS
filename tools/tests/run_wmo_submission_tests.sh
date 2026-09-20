#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
python3 - "$ROOT" "$TEMP" <<'PY'
from pathlib import Path
import sys
root,temp=map(Path,sys.argv[1:]); s=(root/'src/rendering/wmo_renderer.cpp').read_text()
a=s.index('int WMORenderer::findContainingGroup('); b=s.index('\nvoid WMORenderer::WMOInstance::updateModelMatrix()',a)
(temp/'wmo_traversal.generated.inc').write_text(s[a:b])
PY
FLAGS=(-std=c++20 -O1 -g -I"$TEMP" -I"$ROOT/include" -I"$ROOT/extern/glm")
if [ "${SANITIZE:-0}" = 1 ]; then
 FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
 export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
 export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
"${CXX:-c++}" "${FLAGS[@]}" "$ROOT/tools/tests/wmo_submission_test.cpp" -o "$TEMP/wmo-submission"
"$TEMP/wmo-submission"
