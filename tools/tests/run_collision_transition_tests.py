#!/usr/bin/env python3
"""2.12 collision/water/interior transition contracts."""
from pathlib import Path
import os, subprocess, tempfile

root = Path(__file__).resolve().parents[2]
cam = (root / 'src/rendering/camera_controller.cpp').read_text()
water_cpp = (root / 'src/rendering/water_renderer.cpp').read_text()
water_h = (root / 'include/rendering/water_renderer.hpp').read_text()
renderer = (root / 'src/rendering/renderer.cpp').read_text()

# Production movement constants/helpers execute under sanitizers.
code = r'''
#include <cassert>
#include <cstdio>
#include "rendering/movement_limits.hpp"
using namespace wowee::rendering::movement;
int main() {
    assert(isReachableStep(-0.69f));
    assert(isReachableStep(-0.70f));
    assert(!isReachableStep(-0.71f));
    assert(isReachableStep(0.60f));
    assert(!isReachableStep(0.61f));
    assert(bridgeMissingLiquidSample(true, 0.0f, false));
    assert(bridgeMissingLiquidSample(true, 0.20f, false));
    assert(!bridgeMissingLiquidSample(true, 0.201f, false));
    assert(!bridgeMissingLiquidSample(true, 0.10f, true));
    assert(!bridgeMissingLiquidSample(false, 0.10f, false));
    puts("PASS movement transition helpers: step-down/up bounds + bounded liquid-gap bridge");
}
'''
with tempfile.TemporaryDirectory() as td:
    p = Path(td) / 'movement_transition.cpp'
    p.write_text(code)
    exe = Path(td) / 'movement_transition'
    subprocess.run([
        os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-g',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-I' + str(root / 'include'), str(p), '-o', str(exe)
    ], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = 'detect_leaks=0'
    env['UBSAN_OPTIONS'] = 'halt_on_error=1'
    subprocess.run([str(exe)], check=True, env=env)

# One coherent nearest-liquid sample owns height, liquid type and WMO identity.
assert 'struct WaterQuerySample' in water_h
assert 'getNearestWaterSampleAt' in water_h
sample_start = water_cpp.index('WaterRenderer::getNearestWaterSampleAt')
sample_end = water_cpp.index('WaterRenderer::getNearestWaterHeightAt', sample_start)
sample_body = water_cpp[sample_start:sample_end]
for token in ('surface.liquidType', 'surface.wmoId', 'wateredGridPosition', 'sampleGridHeight'):
    assert token in sample_body, token

# Camera movement must consume that one sample; no highest-surface type lookup.
move_start = cam.index('glm::vec3 CameraController::moveFollowedCharacter')
move_end = cam.index('void CameraController::groundFollowedCharacter', move_start)
move = cam[move_start:move_end]
assert 'getNearestWaterSampleAt' in move
assert 'waterSample->liquidType' in move
assert 'waterSample->fromWmo()' in move
assert 'MAX_ACTIVE_SWIM_SURFACE_ABOVE = 4096.0f' in move
assert 'getWaterTypeAt(targetPos.x, targetPos.y)' not in move
assert 'bridgeMissingLiquidSample' in move
assert 'waterSampleGapSeconds_ += f.physicsDeltaTime' in move

# Portal transitions sample lower torso + chest and use one missed-check grace.
refresh_start = cam.index('void CameraController::refreshWmoContainment')
refresh_end = cam.index('// The camera that orbits a character', refresh_start)
refresh = cam[refresh_start:refresh_end]
for token in ('lowerZ', 'torsoZ', 'insideWMOMissCount_', 'insideInteriorMissCount_',
              'if (cached && misses == 0)'):
    assert token in refresh, token

tele_start = cam.index('void CameraController::teleportTo')
tele_end = cam.index('bool CameraController::groundNotStreamedYet', tele_start)
tele = cam[tele_start:tele_end]
assert 'refreshWmoContainment(pos, true);' in tele

# Stair descent keeps continuous grounded support within shared step-down limit.
assert 'dz >= -movement::kMaxStepDown' in cam
assert 'grounded && !f.nowJump' in cam

# WMO liquid presence must respect the rendered mask, not only the rectangle.
wmo_start = water_cpp.index('bool WaterRenderer::isWmoWaterAt')
wmo_end = water_cpp.index('glm::vec4 WaterRenderer::getLiquidColor', wmo_start)
assert 'wateredGridPosition(surface, glX, glY)' in water_cpp[wmo_start:wmo_end]

assert renderer.count('getNearestWaterSampleAt') >= 4
assert 'cameraIndoors_ && !waterSample->fromWmo()' in renderer
assert 'waterSample->liquidType' in renderer

print('PASS 2.12 source contracts: coherent vertical liquid, bounded seam swim, WMO portal hysteresis, teleport refresh, stair descent, masked WMO liquid')
