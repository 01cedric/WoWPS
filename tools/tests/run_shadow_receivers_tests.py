#!/usr/bin/env python3
"""Compile production receiver GLSL math as C++/GLM; no GPU-output claim."""
from pathlib import Path
import re, subprocess, tempfile, os
root = Path(__file__).resolve().parents[2]
sources = [(root/'assets/shaders'/f'{n}.frag.glsl').read_text() for n in ('terrain','wmo','m2','character')]
blocks = [s[s.index('vec2 shadowAtlasUV('):s.index('vec3 localLightContribution(')].strip() for s in sources]
assert all(b == blocks[0] for b in blocks), 'all four receivers must use identical atlas/bias math'
for s in sources:
    assert s.index('distSquared >= radius * radius') < s.index('float dist = sqrt(distSquared)')
    assert '0.0005 *' not in s
    assert 'ivec4 localLightMeta;\n    mat4 nearLightSpaceMatrix;' in s
code = blocks[0]
code = code.replace('projected.xy = projected.xy * 0.5 + 0.5;', 'projected.x = projected.x * 0.5 + 0.5; projected.y = projected.y * 0.5 + 0.5;')
code = code.replace('lightPos.xyz', 'vec3(lightPos)').replace('coords.xy', 'vec2(coords)')
code = re.sub(r'(?<![\w.])(\d+\.\d+|\d+e-\d+)(?![\w.])', r'\1f', code)
header = r'''
#include <glm/glm.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
using namespace glm;
using sampler2DShadow = int;
vec4 shadowAtlasParams(.1f, 1.f, 48.f, 1.f), shadowParams(1.f,1.f,1.f/1024.f,0.f);
mat4 nearLightSpaceMatrix(1.f), lightSpaceMatrix(1.f);
sampler2DShadow uShadowMap=0;
int nearReads=0, farReads=0;
float texture(int, vec3 p) {
    assert(p.x >= 0 && p.x <= .75f && p.y >= 0 && p.y <= 1);
    if(p.x < .5f) {++nearReads; return .25f;}
    assert(p.y <= .5f); ++farReads; return .75f;
}
float shadowTexelSize(){return shadowParams.z;}
'''
tests = r'''
int main() {
 for(int size: {512,1024,2048}) {
  shadowParams.z=1.f/size;
  for(bool near: {false,true}) for(int x=-8;x<=108;++x) for(int y=-8;y<=108;++y) {
   float texel=shadowParams.z*(near?1.f:2.f);
   vec2 p=shadowAtlasUV(vec2(x,y)/100.f,texel,near);
   if(near) assert(p.x>0 && p.x<.5f && p.y>0 && p.y<1);
   else assert(p.x>.5f && p.x<.75f && p.y>0 && p.y<.5f);
  }
 }
 // World-space bias remains 5-40 milliyards regardless of depth range.
 for(float span: {32.f,312.f,1560.f,1950.f}) for(int angle=0;angle<=100;++angle) {
  mat4 m(1.f);m[2][2]=1.f/span;
  float cosine=angle/100.f;vec3 normal(std::sqrt(1-cosine*cosine),0,cosine);
  vec3 p=shadowReceiverCoords(m,vec3(0,0,10),normal,vec3(0,0,1),0.f);
  float worldBias=10-p.z*span;
  assert(std::abs(worldBias-(.005f+.035f*(1-cosine)))<.000003f);
 }
 shadowParams.z=1.f/1024.f;
 vec3 n(0,0,1), l(0,0,1);
 float center=outdoorShadow(vec3(0,0,.5f),n,l);
 assert(std::abs(center-.25f)<.00001f && nearReads==4 && farReads==0);
 nearReads=farReads=0;
 nearLightSpaceMatrix[3][0]=5.f;
 float far=outdoorShadow(vec3(0,0,.5f),n,l);
 assert(std::abs(far-.75f)<.00001f && nearReads==0 && farReads==4);
 nearLightSpaceMatrix=mat4(1.f);nearReads=farReads=0;
 float edge=outdoorShadow(vec3(.92f,0,.5f),n,l);
 assert(edge>.25f && edge<.75f && nearReads==4 && farReads==4);
 nearReads=farReads=0;
 assert(outdoorShadow(vec3(10,10,.5f),n,l)==1.f && nearReads==0 && farReads==0);
 // Squared-radius rejection matches original finite nonnegative distances.
 for(int r=1;r<=64;++r) for(int d=0;d<=1000;++d) {
  float distance=d*.1f, squared=distance*distance;
  assert((std::sqrt(squared)>=r)==(squared>=float(r*r)));
 }
 puts("PASS: four production GLSL receivers identical; 82134 atlas bounds; 404 world-bias cases; near/far/blend/outside read counts; 64064 light rejections");
}
'''
with tempfile.TemporaryDirectory(prefix='shadow261-') as tmp:
    source=Path(tmp)/'receiver.cpp'; source.write_text(header+code+tests)
    binary=Path(tmp)/'receiver'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-I'+str(root/'extern/glm'),'-fsanitize=address,undefined','-fno-omit-frame-pointer',str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
