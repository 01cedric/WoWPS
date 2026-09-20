#!/usr/bin/env python3
"""Check mode4 runtime bounds, actual setter and receiver-debug wiring."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
def read(path):
    return (root / path).read_text()

schema = read('src/ui/settings_schema.cpp')
assert '"volumetricdebug", "Lighting inspection", SettingKind::Enum, 0, 4' in schema
assert 'Normal|Linear scene depth|Shadow depth|Scattering|Surface shadow factor' in schema
config = read('src/ui/game_screen_minimap.cpp')
load = config.split('key == "volumetric_debug"', 1)[1].split('}', 1)[0]
assert 'pendingVolumetricDebug =' not in load
assert 'out << "volumetric_debug=0\\n"' in config
lua = read('src/addons/lua_system_api.cpp')
runtime = lua.split('if (key == "extvolumetricdebug") {', 1)[1].split('\n    }', 1)[0]
assert 'std::clamp(std::atoi(value.c_str()), 0, 4)' in runtime
assert '{"extvolumetricdebug", 0.0f, 4.0f}' in lua
renderer = read('src/rendering/renderer.cpp')
assert renderer.count('getVolumetricDebug() == 4') == 2
assert 'inspectSurfaceShadow ? 1.0f : 0.0f' in renderer
assert 'volumeEligibility.allowed && !inspectSurfaceShadow' in renderer
assert 'inspectSurfaceShadow ? "surface-shadow-inspection"' in renderer
for shader in ('terrain', 'wmo', 'm2', 'character'):
    text = read(f'assets/shaders/{shader}.frag.glsl')
    assert re.search(r'if \(shadowParams\.w > 0\.5\) result = \(shadowParams\.x > 0\.5[^\n]*\)\s*\? vec3\((shadow|inspectedShadow)\) : vec3\(1\.0, 0\.0, 1\.0\);', text)

# Compile and execute the production setter verbatim, rather than duplicate its clamp.
inc = read('src/rendering/post_process_volumetric.inc')
setter = re.search(r'void PostProcessPipeline::setVolumetricDebug\(int mode\) \{[^}]*\}', inc).group()
source = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
struct PostProcessPipeline {
    struct { int debug = 0; } volumetric_;
    void setVolumetricDebug(int);
};
''' + setter + r'''
int main() {
    PostProcessPipeline pipeline;
    for (int i = -100; i <= 100; ++i) {
        pipeline.setVolumetricDebug(i);
        assert(pipeline.volumetric_.debug == (i < 0 ? 0 : i > 4 ? 4 : i));
    }
    std::puts("PASS 201 production setter values; modes0..3 preserved, mode4 exposed/session-only, receiver flag wired and volumetric overlay suppressed");
}
'''
with tempfile.TemporaryDirectory(prefix='wowps-surface-inspection--') as tmp:
    p = Path(tmp)
    (p / 'test.cpp').write_text(source)
    flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror']
    if os.environ.get('SANITIZE') == '1':
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True, env={**os.environ,
        'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'halt_on_error=1'})
