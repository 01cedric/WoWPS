#!/usr/bin/env python3
"""Exercise the production eligibility policy and its renderer call site."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
renderer = (root / 'src/rendering/renderer.cpp').read_text()
end_frame = renderer.split('void Renderer::endFrame()', 1)[1].split('void Renderer::', 1)[0]
assert 'volumetricEligibility(cameraSubmerged,' in end_frame
assert 'volumeEligibility.allowed && !inspectSurfaceShadow' in end_frame
assert 'inspectSurfaceShadow ? "surface-shadow-inspection" : volumeEligibility.reason' in end_frame
assert 'shadowDepthView[frame] != VK_NULL_HANDLE' in end_frame
assert 'shadowDepthLayout_[frame] == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL' in end_frame
assert 'cameraIndoors_' not in end_frame, 'indoor bounds must not disable shadowed rays'
assert 'cameraIndoors_ = canQueryWmo &&' in renderer, 'retain existing indoor classification'

source = r'''
#include "rendering/volumetric_eligibility.hpp"
#include "rendering/volumetric_status.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
    using namespace wowee::rendering;
    unsigned cases = 0;
    for (bool submerged : {false, true})
    for (bool shadows : {false, true})
    for (int quality : {0, 1, 2, 3})
    for (bool ready : {false, true}) {
        const auto policy = volumetricEligibility(submerged, shadows, quality, ready);
        assert(policy.allowed == (!submerged && shadows && quality > 0 && ready));
        const char* expected = submerged ? "camera-underwater" :
            !shadows || quality == 0 ? "shadows-off" : !ready ? "shadow-not-ready" : "ready";
        assert(std::strcmp(policy.reason, expected) == 0);
        assert(std::strcmp(volumetricFrameStatus(1, true, true, false, false,
            policy.allowed, policy.reason, false, true, true, true), expected) == 0);
        ++cases;
    }
    // Restoring eligibility must not bypass quality/resource/light gates.
    assert(!std::strcmp(volumetricFrameStatus(0,true,true,false,false,true,"ready",false,true,true,true),"off"));
    assert(!std::strcmp(volumetricFrameStatus(1,true,true,false,false,true,"ready",true,true,true,true),"resource-failure"));
    assert(!std::strcmp(volumetricFrameStatus(1,true,true,false,false,true,"ready",false,true,true,false),"no-key-light"));
    std::printf("PASS %u eligibility cases and production renderer wiring; indoor bounds no longer disable shadowed rays\n", cases);
}
'''
source = '#include <initializer_list>\n' + source
with tempfile.TemporaryDirectory(prefix='wowps-volume-eligibility--') as tmp:
    folder = Path(tmp)
    cpp = folder / 'test.cpp'
    cpp.write_text(source)
    flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + str(root / 'include')]
    if os.environ.get('SANITIZE') == '1':
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, str(cpp), '-o', str(folder / 'test')], check=True)
    subprocess.run([str(folder / 'test')], check=True, env={**os.environ,
        'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'halt_on_error=1'})
