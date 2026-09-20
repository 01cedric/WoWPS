#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/../.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -f "$test_dir/wmo-bounds"; rmdir "$test_dir"' EXIT
"${CXX:-c++}" -std=c++17 -O1 -g -fsanitize=address,undefined \
  -DGLM_FORCE_DEPTH_ZERO_TO_ONE -I"$project_dir/include" -I"$project_dir/extern/glm" \
  "$project_dir/tools/tests/wmo_draw_bounds_test.cpp" \
  "$project_dir/src/rendering/frustum.cpp" -o "$test_dir/wmo-bounds"
"$test_dir/wmo-bounds"
