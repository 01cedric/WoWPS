#!/usr/bin/env python3
"""Execute production CreateFrame init + complete options Lua with Lua 5.1."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/addons/lua_engine.cpp').read_text()
start = source.index('if (auto* w = tree->get(id)) {', source.index('static int lua_CreateFrame('))
pos = source.index('{', start)
depth = 1
end = pos + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
block = source[start:end]
assert 'canonicalFrameType' in block
flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if os.getenv('SANITIZE') == '1' else []
with tempfile.TemporaryDirectory(prefix='wowps-settings-') as temp:
    out = Path(temp)
    (out / 'frame_create_init.inc').write_text(block)
    lua = root / 'extern/lua-5.1.5/src'
    c_sources = sorted(str(p) for p in lua.glob('*.c') if p.name not in ('lua.c', 'luac.c', 'print.c'))
    subprocess.run([os.getenv('CC', 'cc'), '-O1', '-g', '-w', *flags, '-c', *c_sources], cwd=out, check=True)
    exe = out / 'settings-access'
    subprocess.run([os.getenv('CXX', 'c++'), '-std=c++20', '-O1', '-g', *flags,
        '-I'+str(root/'include'), '-I'+str(lua), '-I'+str(out),
        str(root/'tools/tests/settings_access_test.cpp'), str(root/'src/ui/widget_tree.cpp'),
        str(root/'src/ui/settings_schema.cpp'), *map(str, out.glob('*.o')), '-lm', '-ldl', '-o', str(exe)], check=True)
    subprocess.run([str(exe), str(root/'tools/tests/settings_access_fixture.lua')], check=True)
