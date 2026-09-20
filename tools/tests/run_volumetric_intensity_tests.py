#!/usr/bin/env python3
"""Bounded setting round-trip and production setter; no GPU-image claim."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
volume=(root/'src/rendering/post_process_volumetric.inc').read_text()
setter=volume[volume.index('void PostProcessPipeline::setVolumetricIntensity'):volume.index('void PostProcessPipeline::setVolumetricDebug')]
assert 'data.sunColor = glm::vec4(v.lightColor * effectiveVolumetricIntensity(v.intensity), float(v.debug));' in volume
schema=(root/'src/ui/settings_schema.cpp').read_text()
assert '"volumetricintensity", "Light shaft intensity", SettingKind::Float, 0, 2, 0.05f' in schema
assert '"", 1.35f, "volumetricquality"' in schema
lua=(root/'src/addons/lua_system_api.cpp').read_text()
assert '{"extvolumetricintensity", "volumetricintensity"}' in lua
assert '{"extvolumetricintensity", 0.0f, 2.0f}' in lua
assert 'n == "extvolumetricintensity") lua_pushstring(L, "1.35")' in lua
assert 'sink(rendering::clampVolumetricIntensity(static_cast<float>(std::atof(value.c_str()))))' in lua
persist=(root/'src/ui/game_screen_minimap.cpp').read_text()
assert '"volumetric_intensity=" << rendering::clampVolumetricIntensity(settingsPanel_.pendingVolumetricIntensity)' in persist
assert 'rendering::clampVolumetricIntensity(std::stof(val))' in persist
for path in ('src/ui/settings_panel.cpp','src/ui/game_screen.cpp','src/rendering/renderer.cpp'):
    assert 'setVolumetricIntensity' in (root/path).read_text()
assert 'else if (key == "volumetricintensity")' in (root/'src/ui/settings_panel.cpp').read_text()
code='''
#include "rendering/volumetric_intensity.hpp"
#include <cassert>
#include <limits>
#include <sstream>
#include <cstdio>
using namespace wowee::rendering;
struct PostProcessPipeline {
 struct {float intensity=kDefaultVolumetricIntensity;} volumetric_;
 void setVolumetricIntensity(float intensity);
};
'''+setter+'''
int main() {
 PostProcessPipeline p; assert(p.volumetric_.intensity==1.35f);
 for(int i=-100;i<=300;++i) {
  float x=i*.01f; p.setVolumetricIntensity(x);
  assert(p.volumetric_.intensity==std::clamp(x,0.f,2.f));
  std::ostringstream saved; saved<<clampVolumetricIntensity(x);
  float restored=clampVolumetricIntensity(std::stof(saved.str()));
  assert(std::abs(restored-p.volumetric_.intensity)<.00001f);
 }
 p.setVolumetricIntensity(std::numeric_limits<float>::infinity()); assert(p.volumetric_.intensity==1.35f);
 p.setVolumetricIntensity(std::numeric_limits<float>::quiet_NaN()); assert(p.volumetric_.intensity==1.35f);
 p.setVolumetricIntensity(0); assert(p.volumetric_.intensity==0);
 puts("PASS: 401 intensity round trips, bounds/default/nonfinite guards, production setter and complete UI/CVar/persistence wiring");
}
'''
with tempfile.TemporaryDirectory(prefix='intensity261-') as tmp:
    source=Path(tmp)/'test.cpp'; source.write_text(code)
    binary=Path(tmp)/'test'
    subprocess.run(['c++','-std=c++17','-I'+str(root/'include'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
