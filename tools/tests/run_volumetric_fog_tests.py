#!/usr/bin/env python3
"""Execute production fog GLSL against numerical integration and blend invariants."""
from pathlib import Path
import os, subprocess, tempfile, re
root=Path(__file__).resolve().parents[2]
fixture=root/'tools/tests/run_volumetric_reconstruction_resolve_tests.py'
n={'__file__':str(fixture)}
exec(compile(fixture.read_text().split('with tempfile.TemporaryDirectory')[0],str(fixture),'exec'),n)
expected = [('mat4','inverseRelativeViewProjection'),('mat4','lightMatrix'),('vec4','camera'),('vec4','sunDirection'),('vec4','sunColor'),('vec4','parameters'),('mat4','nearLightMatrix'),('vec4','fogParameters'),('vec4','fogColor')]
for name in ['volumetric.frag.glsl','volumetric_composite.frag.glsl']:
 shader=(root/'assets/shaders'/name).read_text()
 block=shader.split('uniform Volume {')[1].split('} v;')[0]
 assert re.findall(r'(mat4|vec4)\s+(\w+)\s*;',block)==expected, 'GLSL ABI field order differs: '+name
production=(root/'src/rendering/post_process_volumetric.inc').read_text()
block=production.split('struct alignas(16) VolumetricUniforms {')[1].split('};')[0]
assert re.findall(r'glm::(mat4|vec4)\s+(\w+)\s*;',block)==expected, 'C++/GLSL ABI field order mismatch'
checks=r'''
int main(){
 static_assert(sizeof(Volume)==288);static_assert(offsetof(Volume,fogParameters)==256);static_assert(offsetof(Volume,fogColor)==272);
 v.fogParameters=vec4(.0021f,20.f,.1f,160.f);
 unsigned cases=0;
 for(float start:{-40.f,-1.f,0.f,1.f,20.f,80.f}) for(float dz:{-90.f,-1.f,0.f,.0001f,1.f,90.f}) for(float distance:{1.f,30.f,160.f,500.f}) {
  v.camera.z=20+start; vec3 endpoint(distance,0,dz);float len=length(endpoint),limit=min(len,160.f);double reference=0;
  for(int j=0;j<10000;++j){double z=start+endpoint.z*((j+.5)/10000.0)*limit/len;reference+=exp(-.1*std::max(z,0.0))*limit/10000.0;}
  reference=std::min(reference*.0021,.65);float actual=heightFogOpticalDepth(endpoint);
  check(std::isfinite(actual)&&abs(actual-reference)<.00001,"analytic density agrees with independent 10000-step integration");++cases;
 }
 v.camera.z=20;check(heightFogOpticalDepth(vec3(0))==0,"surface at camera contains no fog");
 float ground=heightFogOpticalDepth(vec3(100,0,0));v.camera.z=70;check(heightFogOpticalDepth(vec3(100,0,0))<ground*.01,"equal-distance high air less dense than ground air");
 v.fogParameters.x=0;check(heightFogOpticalDepth(vec3(100,0,0))==0,"fog off no extinction");
 v.sunColor=vec4(0);nearReads=farReads=0;check(length(integrateScattering(vec2(.5f),.5f))==0&&nearReads==0&&farReads==0,"rays off no shadow texture reads");
 v.inverseRelativeViewProjection=mat4(1);v.inverseRelativeViewProjection[2][3]=-.99f;
 v.parameters=vec4(160,.002f,8,.9f);v.sunDirection=vec4(0,0,1,4);v.camera=vec4(0,0,0,1);
 v.fogParameters=vec4(.0021f,0,.1f,160);v.fogColor=vec4(.1f,.3f,.5f,0);
 extent(1,1);depths[0]=encode(30);samples[0]=vec4(0);TexCoord=vec2(.5f);
 composite();vec4 endpoint=v.inverseRelativeViewProjection*vec4(0,0,depths[0],1);float T=exp(-heightFogOpticalDepth(vec3(endpoint)/endpoint.w));
 check(length(vec3(outColor)-vec3(v.fogColor)*(1-T))<.000001f&&abs(outColor.a-(1-T))<.000001f,"production composite fog-only outputs actual Beer-Lambert in-scatter and transmission");
 v.fogParameters.x=0;composite();check(length(vec3(outColor))==0&&outColor.a==1,"fog off original screen-blend shader output retained");
 for(float tau:{0.f,.05f,.35f,.65f}) for(vec3 ray:{vec3(0),vec3(.99f,.1f,.4f),vec3(.5f)}) for(vec3 fog:{vec3(0),vec3(1),vec3(.1f,.3f,.8f)}) for(vec3 scene:{vec3(0),vec3(1),vec3(.4f,.2f,.8f)}) {
  float T=exp(-tau),q=max(ray.r,max(ray.g,ray.b));vec3 src=fog*(1-T)+ray*T;float alpha=1-T*(1-q);vec3 result=src+scene*(1-alpha);
  check(all(greaterThanEqual(result,vec3(0)))&&all(lessThanEqual(result,vec3(1.000001f))),"premultiplied fog/ray blend bounded for white scene and strong rays");
 }
 printf("PASS %u analytic fog cases, finite endpoints, horizon, ground crossings, sky cap, no ray reads, bounded blend; ABI 288 bytes offsets 256/272. CPU GLSL only.\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='wowps-fog268-') as tmp:
 p=Path(tmp);s=n['preamble'].replace('#include <cmath>','#include <cmath>\n#include <cstddef>')+n['translate'](n['common']+n['march']+n['comp'])+checks
 (p/'test.cpp').write_text(s)
 subprocess.run(['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'extern/glm'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
