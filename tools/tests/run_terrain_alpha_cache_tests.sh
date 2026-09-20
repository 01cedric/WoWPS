#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
binary=$(mktemp /tmp/wowps-alpha-.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-c++} -std=c++20 -O1 -g -fsanitize=address,undefined -I"$root/include" "$root/tools/tests/terrain_alpha_cache_test.cpp" -o "$binary"
"$binary"
python3 - "$root" <<'PY'
import pathlib,sys
s=(pathlib.Path(sys.argv[1])/'src/rendering/terrain_renderer.cpp').read_text()
assert s.count('alphaReuseCache_.clear();')==2
for part in s.split('textureCache.clear();')[:-1]:
    assert 'alphaReuseCache_.clear();' in part
assert 'alphaReuseCache_.remember(mask, raw);' in s
assert 'VK_FORMAT_R8_UNORM' in s
print('PASS renderer cache invalidation at shutdown and world clear; successful alpha upload remembered')
PY
