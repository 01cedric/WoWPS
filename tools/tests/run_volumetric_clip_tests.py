#!/usr/bin/env python3
"""Compile production GLSL slab clipping against geometric endpoint cases.

This exercises the exact current clipAxis function, not a copied algorithm.
It is a numerical CPU regression, not an image or GPU-render acceptance test.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
shader = (root / 'assets/shaders/volumetric.frag.glsl').read_text()
start = shader.index('bool clipAxis(')
brace = shader.index('{', start)
depth = 1
end = brace + 1
while depth:
    depth += (shader[end] == '{') - (shader[end] == '}')
    end += 1
function = shader[start:end].replace('inout float ', 'float& ')
preamble = '''#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using std::abs; using std::min; using std::max;
'''
tests = r'''
void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::abort(); }
}
struct Test {
    const char* name; float origin[3], direction[3], limit;
    bool expected; float enter, leave;
};
int main() {
    // Light clip cuboid is x/y[-1,1], z[0,1], eye interval[0,limit].
    const Test cases[] = {
        {"eye inside, exits side", {0,0,.5f}, {1,0,0}, 10, true,0,1},
        {"ray enters from negative X", {-2,0,.5f}, {1,0,0}, 10, true,1,3},
        {"negative ray enters from positive X", {2,0,.5f}, {-1,0,0}, 10,true,1,3},
        {"parallel outside slab", {2,0,.5f}, {0,1,0}, 10,false,0,0},
        {"source behind ray", {-2,0,.5f}, {-1,0,0},10,false,0,0},
        {"opaque wall before shadow volume", {-2,0,.5f},{1,0,0},.75f,false,0,0},
        {"opaque wall truncates scattering", {-2,0,.5f},{1,0,0},1.25f,true,1,1.25f},
        {"zero length ray at boundary", {-2,0,.5f},{1,0,0},1,false,0,0},
        {"ray parallel on cube boundary", {1,0,.5f},{0,1,0},10,true,0,1},
        {"Vulkan near plane entry", {0,0,-1},{0,0,1},10,true,1,2},
        {"Vulkan far plane entry", {0,0,2},{0,0,-1},10,true,1,2},
        {"three-plane intersection", {-2,-4,-1},{1,2,1},10,true,1.5f,2},
        {"thin but positive interval", {-2,0,.5f},{1,0,0},1.0001f,true,1,1.0001f},
        {"shadow map outside far plane", {0,0,2},{1,0,0},10,false,0,0},
    };
    for (const auto& test : cases) {
        float enter=0, leave=test.limit;
        bool hit=true;
        for (int axis=0; axis<3; ++axis)
            hit=clipAxis(test.origin[axis],test.direction[axis],axis==2?0.f:-1.f,1.f,enter,leave)&&hit;
        require(hit==test.expected,test.name);
        if (hit) require(std::abs(enter-test.enter)<.000001f &&
                         std::abs(leave-test.leave)<.000001f,test.name);
    }
    std::puts("PASS: production volumetric clipAxis: 14 geometric intervals, opaque endpoint, Vulkan Z range, parallel/thin rays");
}
'''
with tempfile.TemporaryDirectory(prefix='wowps-volume-clip-') as temporary:
    path = Path(temporary)
    cpp = path / 'clip.cpp'
    cpp.write_text(preamble + function + tests)
    flags = ['-std=c++17', '-O1', '-g']
    if os.environ.get('SANITIZE') == '1':
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, str(cpp), '-o', str(path/'clip')], check=True)
    environment = dict(os.environ)
    environment.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
    environment.setdefault('UBSAN_OPTIONS', 'halt_on_error=1')
    subprocess.run([str(path/'clip')], check=True, env=environment)
