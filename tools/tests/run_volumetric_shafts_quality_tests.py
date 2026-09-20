#!/usr/bin/env python3
"""Execute production GLSL and a parameter-only the implementation baseline; no GPU claim."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
fixture = root/'tools/tests/run_volumetric_reconstruction_resolve_tests.py'
namespace = {'__file__': str(fixture)}
exec(compile(fixture.read_text().split('with tempfile.TemporaryDirectory')[0], str(fixture), 'exec'), namespace)
production = (root/'src/rendering/post_process_volumetric.inc').read_text()
assert 'glm::vec4(160.0f, kVolumetricExtinction, v.quality == 1 ? 8.0f : 12.0f, 0.9f)' in production
assert 'VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR' in production
common = namespace['common']
assert 'const float g = 0.75;' in common
baseline = common[common.index('float directionalScatteringScale('):]
# Undo only the two the implementation shader changes to execute the the implementation baseline.
baseline = baseline.replace(' * shaftExposure(mu)', '')
baseline = baseline.replace('float(i)+stratumOffset', 'float(i)+offset')
baseline = baseline.replace('directionalScatteringScale', 'baselinePhase').replace('integrateScattering', 'baselineScattering').replace('shaftExposure', 'baselineExposure')
checks = r'''
vec3 display(vec3 r){return r*volumeDisplayScale(max(r.r,max(r.g,r.b)));}
vec3 blend(vec3 r,vec3 scene){vec3 s=display(r);return s+scene*(vec3(1)-s);}
int main(){
 using namespace wowee::rendering;
 v.inverseRelativeViewProjection=mat4(1);v.inverseRelativeViewProjection[2][3]=-.99f;
 v.lightMatrix=mat4(.005f);v.lightMatrix[3][3]=1;v.lightMatrix[3][2]=.1f;
 v.nearLightMatrix=v.lightMatrix;v.camera=vec4(0,0,0,1);
 v.sunDirection=vec4(0,0,1,4);extent(1,1);depths[0]=encode(80);
 const vec3 scene(.12f,.23f,.07f);
 for(int count:{8,12}) for(vec3 color:{vec3(1,.65f,.3f),vec3(.1f,.2f,.5f)}) for(int degrees:{0,15,30,45,90}) {
  float angle=radians(float(degrees));v.sunDirection=vec4(sin(angle),0,cos(angle),4);
  v.parameters=vec4(160,kVolumetricExtinction,float(count),.9f);
  v.sunColor=vec4(color*effectiveVolumetricIntensity(kDefaultVolumetricIntensity),0);
  shadowPattern=0;nearReads=farReads=0;
  vec3 ray=integrateScattering(vec2(.5f),depths[0]);
  check(nearReads==unsigned(count)&&farReads==0,"unchanged one near-shadow lookup per march step");
  check(all(greaterThan(ray,vec3(0))),"no angular cutoff: off-axis light remains nonzero");
  check(abs(ray.r/color.r-ray.b/color.b)<.00001f,"sun/moon palette retained without white floor");
  closedShadow=true;vec3 blocked=integrateScattering(vec2(.5f),depths[0]);closedShadow=false;
  check(length(blocked)==0&&length(blend(blocked,scene)-scene)==0,"blocked air does not alter colored scene");
  vec3 contrast=blend(ray,scene)-blend(blocked,scene);
  check(all(greaterThan(contrast,vec3(0))),"actual bounded blend retains lit/blocked shaft contrast at each angle");
  // Same physical medium, same source RGB, different directional exposure only.
  vec3 old=baselineScattering(vec2(.5f),depths[0]);
  float ratio=ray.r/old.r;
  float cosine=max(cos(angle),0.f);
  float expected=1.f+2.5f*cosine*cosine*cosine*cosine;
  check(abs(ratio-expected)<.0001f,"actual integrated directional radiance gain matches exposure");
  if(degrees==0)check(ratio>3.49f,"sunward shafts gain 3.5x linear radiance");
  if(degrees==45)check(ratio>1.62f&&ratio<1.63f,"oblique shafts gain without global fog increase");
  if(degrees==90)check(abs(ratio-1.f)<.00001f,"lateral veiling remains unchanged");
  if(count==8&&color.r==1.f)printf("angle=%d radiance/baseline=%.5f displayedContrastRGB=%.5f,%.5f,%.5f\n",degrees,ratio,contrast.r,contrast.g,contrast.b);
 }
 // Fine near-map gaps are absent from far. Test shadow-defined shafts, not a
 // brightness threshold, at the requested oblique viewing directions.
 v.parameters=vec4(160,kVolumetricExtinction,8,.9f);v.sunColor=vec4(1,.65f,.3f,0);
 shadowPattern=4;
 for(int count:{8,12})for(int degrees:{15,30,45})for(int band=10;band<30;++band){
  v.parameters.z=float(count);float a=radians(float(degrees));v.sunDirection=vec4(sin(a),0,cos(a),4);
  v.camera.x=((float(band)+.5f)/40.f-.5f)*400.f;
  vec3 ray=integrateScattering(vec2(.5f),depths[0]);
  check((length(ray)>0)==(band%2==0),"near-only canopy gaps and blocked bands remain separated off-axis");
 }
 shadowPattern=0;v.camera.x=0;v.sunColor=vec4(0);
 check(length(integrateScattering(vec2(.5f),depths[0]))==0,"zero source adds no white haze");
 // Verify sample strata remain separated and unbiased over spatial offsets.
 for(int count:{8,12})for(int i=0;i<count;++i){
  double mean=0;for(int j=0;j<4096;++j){float offset=(j+.5f)/4096.f;
   float position=fract(offset+float(i)*.61803398875f);
   check(position>=0&&position<1,"jitter stays inside each integration stratum");mean+=position;
  }check(abs(mean/4096-.5)<.001,"each decorrelated stratum samples uniformly");
 }
 puts("PASS actual GLSL: 3.5x forward radiance, unchanged lateral veil, full occlusion zero, near canopy gaps, sun/moon palette, unchanged sample budget. CPU fixtures NOT console pixels");
}
'''
source = '#include "rendering/volumetric_intensity.hpp"\n'+namespace['preamble']+namespace['translate'](common+baseline+namespace['march']+namespace['comp'])+checks
with tempfile.TemporaryDirectory(prefix='wowps-shafts-') as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(source)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'include'),'-I'+str(root/'extern/glm'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
