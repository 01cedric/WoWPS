#!/usr/bin/env python3
"""Execute production GLSL transport and exposure helper for sun/moon colors."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
fixture = root/'tools/tests/run_volumetric_reconstruction_resolve_tests.py'
namespace = {'__file__': str(fixture)}
exec(compile(fixture.read_text().split('with tempfile.TemporaryDirectory')[0], str(fixture), 'exec'), namespace)
volume = (root/'src/rendering/post_process_volumetric.inc').read_text()
assert 'data.sunColor = glm::vec4(v.lightColor * effectiveVolumetricIntensity(v.intensity), float(v.debug));' in volume
# Reuse the texture fixture and exact production GLSL functions, not a rewritten
# approximation. Vary only authored source color and the production exposure.
checks = r'''
int main() {
 using namespace wowee::rendering;
 v.inverseRelativeViewProjection=mat4(1);v.inverseRelativeViewProjection[2][3]=-.99f;
 v.lightMatrix=mat4(.005f);v.lightMatrix[3][3]=1;v.lightMatrix[3][2]=.1f;
 v.camera=vec4(0,0,0,1);v.sunDirection=vec4(0,0,1,4);v.parameters=vec4(160,.008f,8,.9f);
 extent(1,1);depths[0]=encode(80);
 for(int samples:{8,12}) for(vec3 source:{vec3(1,.8f,.5f),vec3(.03f,.045f,.075f),vec3(.001f,.002f,.004f)}) {
  v.parameters.z=float(samples);
  v.sunColor=vec4(source*kDefaultVolumetricIntensity,0);
  vec3 oldRay=integrateScattering(vec2(.5f),depths[0]);
  v.sunColor=vec4(source*effectiveVolumetricIntensity(kDefaultVolumetricIntensity),0);
  vec3 strongRay=integrateScattering(vec2(.5f),depths[0]);
  check(length(strongRay-oldRay*kVolumetricDisplayExposure)<.000001f,"source transport uses current explicit exposure before display mapping");
  vec3 oldDisplay=oldRay*volumeDisplayScale(max(oldRay.r,max(oldRay.g,oldRay.b)));
  vec3 newDisplay=strongRay*volumeDisplayScale(max(strongRay.r,max(strongRay.g,strongRay.b)));
  check(all(greaterThan(newDisplay,oldDisplay)),"sun and dim moon sources visibly increase numerically");
  check(all(lessThan(newDisplay,vec3(1))),"display remains bounded");
  closedShadow=true;check(length(integrateScattering(vec2(.5f),depths[0]))==0,"occluded light remains exactly zero");closedShadow=false;
  v.sunColor=vec4(source*effectiveVolumetricIntensity(0),0);check(length(integrateScattering(vec2(.5f),depths[0]))==0,"saved zero remains off");
 }
 v.sunColor=vec4(0);check(length(integrateScattering(vec2(.5f),depths[0]))==0,"no invented light for black source");
 check(abs(effectiveVolumetricIntensity(1.35f)-1.35f*kVolumetricDisplayExposure)<.000001f,"default upgrade applies without migration");
 check(effectiveVolumetricIntensity(2.f)==2.f*kVolumetricDisplayExposure,"custom maximum stays in supported range");
 puts("PASS: actual GLSL sun/dim moon transport explicit exposure, bounded display, fully occluded zero, saved zero/off and custom control preserved; CPU fixture, not PS4 pixels");
}
'''
source = '#include "rendering/volumetric_intensity.hpp"\n'+namespace['preamble']+namespace['translate'](namespace['common']+namespace['march']+namespace['comp'])+checks
with tempfile.TemporaryDirectory(prefix='wowps-exposure-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(source)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'include'),'-I'+str(root/'extern/glm'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
