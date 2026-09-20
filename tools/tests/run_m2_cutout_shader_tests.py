#!/usr/bin/env python3
"""Execute production source-alpha and coverage blocks with sanitizers."""
from pathlib import Path
import re,subprocess,tempfile,os
root=Path(__file__).resolve().parents[2]
s=(root/'assets/shaders/m2.frag.glsl').read_text()
a=s.index('    int alphaMode = alphaTest & 7;');b=s.index('    bool isFoliage',a)
sourceGate=s[a:b]
a=s.index('    if (alphaMode != 0) {',b);b=s.index('    if (colorKeyBlack != 0)',a)
coverage=s[a:b]
# Sky never enters either alpha path, and binary coverage cannot use mip boost.
assert s.index('if (vSkyMode != 0)')<s.index('int alphaMode = alphaTest & 7;')
assert 'if (!hardCutout && isFoliage && hasTexture != 0)' in s
assert s.index('if (hardCutout && alphaMode')<s.index('// Fix DXT fringe')
body=re.sub(r'(?<![\w.])(\d+\.\d+)(?![\w.])',r'\1f',sourceGate+coverage).replace('discard;','return false;')
source=r'''
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdio>
#include <limits>
using std::clamp;using std::max;
float derivative;float fwidth(float){return derivative;}
bool shade(int alphaTest,float sourceAlpha,float& result) {
 struct {float a;}texColor{sourceAlpha};
'''+body+r'''
 result=texColor.a;return true;
}
int main(){
 for(int mode:{1,2,3})for(float d:{0.f,.001f,.1f,1.f,2.f,16.f}){
  derivative=d;float out=0;float cutoff=mode==3?.25f:.4f;
  for(float alpha:{-1.f,0.f,.0001f,.1f,.24f,.24999f,.25f,.3f,.39999f,.4f,.9f,1.f}){
   bool survives=shade(mode|8,alpha,out);
   assert(survives==(alpha>=cutoff));if(survives)assert(out==1.f);
  }
  assert(!shade(mode|8,std::numeric_limits<float>::quiet_NaN(),out));
 }
 // Reproduce old zero-alpha resurrection: large derivative admits completely
 // transparent texels; binary path now kills before that remapping.
 derivative=2.f;float oldAlpha=0,newAlpha=0;
 assert(shade(2,0,oldAlpha)&&oldAlpha>.39f);
 assert(!shade(2|8,0,newAlpha));
 // Multisample path deliberately retains its previous fractional coverage.
 derivative=.2f;assert(shade(2,.4f,oldAlpha)&&std::abs(oldAlpha-.5f)<1.e-6f);
 assert(shade(2|8,.4f,newAlpha)&&newAlpha==1.f);
 // Non-cutout materials are unchanged.
 assert(shade(0,.12f,newAlpha)&&newAlpha==.12f);
 puts("PASS production M2 blocks: alpha-zero old regression reproduced; hard raw cutoff precedes remapping; negative/NaN rejected; all authored modes and derivative ranges; soft MSAA/noncutout unchanged");
}
'''
with tempfile.TemporaryDirectory(prefix='m2-cutout0278-') as d:
 p=Path(d);(p/'test.cpp').write_text(source)
 subprocess.run([os.getenv('CXX','c++'),'-std=c++20','-O1','-g','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
