#!/usr/bin/env python3
"""Run extracted production GLSL optical helpers; not GPU/image acceptance."""
from pathlib import Path
import re, subprocess, tempfile, os
root=Path(__file__).resolve().parents[2]
s=(root/'assets/shaders/water.frag.glsl').read_text()
s=s[s.index('float waterFresnel('):s.index('// ============================================================\n// Noise functions for foam')]
s=re.sub(r'(?<![\w.])(\d+\.\d+)(?![\w.])',r'\1f',s)
code='''#include <glm/glm.hpp>
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace glm;
'''+s+'''
int main(){
 assert(abs(waterFresnel(1)-.02f)<1e-6f);
 assert(abs(waterFresnel(0)-1.f)<1e-6f);
 for(int i=0;i<1000;++i){float c=i/1000.f;assert(waterFresnel(c)>=waterFresnel(c+.001f));}
 for(float t:{0.f,1.f}){
 vec3 previous(1);
 for(int i=0;i<=1200;++i){vec3 v=waterTransmission(i*.1f,t);
 assert(all(lessThanEqual(v,previous))&&all(greaterThanEqual(v,vec3(0))));
 assert(v.r<=v.g&&v.g<=v.b);previous=v;
 vec3 blackUnderwater=vec3(0)*v+vec3(.02f)*(vec3(1)-v);
 assert(all(lessThanEqual(blackUnderwater,vec3(.02f))));}
 assert(length(waterTransmission(0,t)-vec3(1))==0);
 }
 assert(waterRayDistance(12,10,1)==2);
 assert(waterRayDistance(12,10,.5f)==4);
 assert(waterRayDistance(2,10,.5f)==0);
 assert(waterRayDistance(10000,10,0)==120);
 assert(waterRefractionForeground(9,10));
 assert(waterRefractionForeground(10,10));
 assert(!waterRefractionForeground(12,10));
 puts("PASS production water optical helpers: Fresnel endpoints/1000 angles, 2402 transmission samples, oblique rays, foreground rejection, dark scene preservation");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td)/'test.cpp';p.write_text(code)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'extern/glm'),str(p),'-o',td+'/test'],check=True)
 subprocess.run([td+'/test'],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
