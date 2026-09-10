#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
python3 - "$ROOT/src/rendering/minimap.cpp" "$TEST_DIR/minimap_render_source.inc" <<'PY'
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
# Function and declaration boundaries are explicit. No expressions or Vulkan
# calls are substituted, so this executes the same body as the game build.
struct_start = source.index('struct MinimapDisplayPush {')
struct_end = source.index('};', struct_start) + 2
function_start = source.index('void Minimap::render(')
function_end = source.index('\n}\n\n} // namespace rendering', function_start) + 2
Path(sys.argv[2]).write_text(source[struct_start:struct_end] + '\n' + source[function_start:function_end] + '\n')
PY
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CXX:-c++}" -std=c++20 -O1 -g "${SAN_FLAGS[@]}" \
    -I"$ROOT/include" -I"$ROOT/extern/glm" -I"$ROOT/ps4/third_party/ps4_vulkan/include" -I"$TEST_DIR" \
    "$ROOT/tools/tests/minimap_display_test.cpp" -o "$TEST_DIR/minimap_display_test"
"$TEST_DIR/minimap_display_test"
printf 'Test executable: %s/minimap_display_test\n' "$TEST_DIR"
