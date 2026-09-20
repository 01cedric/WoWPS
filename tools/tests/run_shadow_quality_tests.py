#!/usr/bin/env python3
"""Execute production GLSL plane/PCF math as C++; simulate bilinear depth comparisons."""
from pathlib import Path
import os,re,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
def extract(path):
 s=path.read_text();s=s[s.index('vec2 shadowAtlasUV('):s.index('vec3 localLightContribution(')]
 s=s.replace('projected.xy = projected.xy * 0.5 + 0.5;', 'projected.x = projected.x * 0.5 + 0.5; projected.y = projected.y * 0.5 + 0.5;')
 s=s.replace('lightPos.xyz','vec3(lightPos)').replace('coords.xy','vec2(coords)')
 return re.sub(r'(?<![\w.])(\d+\.\d+|\d+e-\d+)(?![\w.])',r'\1f',s.strip())
code=extract(root/'assets/shaders/terrain.frag.glsl')
for n in ('wmo','m2','character'): assert code==extract(root/f'assets/shaders/{n}.frag.glsl')
header=r'''
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
using namespace glm;
using sampler2DShadow=int;
vec4 shadowAtlasParams(.133897f,1.67371f,48.f,1.f),shadowParams(1,1,1.f/1024,0);
mat4 nearLightSpaceMatrix(1),lightSpaceMatrix(1);
int uShadowMap=0,reads=0;
float activeTexel=1.f/1024, occluder=0;
vec2 planeGradient(0),center(.5f);
float texture(int,vec3 p){
 ++reads;
 vec2 uv=p.x<.5f ? vec2(p.x*2,p.y):vec2((p.x-.5f)*4,p.y*2);
 vec2 q=uv/activeTexel-.5f,base=floor(q),f=q-base;
 float value=0;
 for(int y=0;y<2;++y)for(int x=0;x<2;++x){
  vec2 sample=(base+vec2(x,y)+.5f)*activeTexel;
  float z=.5f+dot(planeGradient,sample-center)-occluder;
  value+=(p.z<=z?1.f:0.f)*(x?f.x:1-f.x)*(y?f.y:1-f.y);
 }
 return value;
}
float shadowTexelSize(){return shadowParams.z;}
'''
# Frozen the implementation comparison kernel, copied verbatim from its GLSL and namespace-isolated.
baseline=r'''
float oldPCF(int smap,vec3 coords,float texel,bool nearCascade){
 vec2 texelPos=vec2(coords)/texel-.5f,base=floor(texelPos),f=texelPos-base;
 vec2 weightLo=2.f-f,weightHi=1.f+f;
 vec2 posLo=(base-.5f+1.f/weightLo)*texel,posHi=(base+1.5f+f/weightHi)*texel;
 float shadow=texture(smap,vec3(shadowAtlasUV(posLo,texel,nearCascade),coords.z))*weightLo.x*weightLo.y;
 shadow+=texture(smap,vec3(shadowAtlasUV(vec2(posHi.x,posLo.y),texel,nearCascade),coords.z))*weightHi.x*weightLo.y;
 shadow+=texture(smap,vec3(shadowAtlasUV(vec2(posLo.x,posHi.y),texel,nearCascade),coords.z))*weightLo.x*weightHi.y;
 shadow+=texture(smap,vec3(shadowAtlasUV(posHi,texel,nearCascade),coords.z))*weightHi.x*weightHi.y;
 return shadow/9.f;
}
'''
tests=r'''
int main(){
 int cases=0,oldAcne=0;float newMin=1,oldMin=1;
 for(bool near:{true,false})for(float sx:{-7.f,-.9f,-.3f,0.f,.3f,.9f,7.f})for(float sy:{-7.f,-3.f,-.7f,0.f,.7f,3.f,7.f}){
  float worldTexel=near?.133897f:1.67371f;
  activeTexel=near?1.f/1024:1.f/512;
  float width=worldTexel/activeTexel,depthRange=1950.f;
  mat4 m(1);m[0][0]=2.f/width;m[1][1]=2.f/width;m[2][2]=1.f/depthRange;
  vec3 normal=normalize(vec3(-sx,-sy,1));
  planeGradient=vec2(sx,sy)*width/depthRange;
  vec2 actual=shadowDepthGradient(m,normal);
  assert(length(actual-planeGradient)<1e-5f);
  for(int x=0;x<16;++x)for(int y=0;y<16;++y){
   center=vec2(.5f)+(vec2(x,y)/16.f)*activeTexel;
   // Maximum existing residual bias (0.04 yards) must retain 5 cm blockers.
   vec3 coords(center,.5f-.04f/depthRange);
   reads=0;occluder=0;
   float old=oldPCF(0,coords,activeTexel,near);
   float now=sampleShadowPCF(0,coords,activeTexel,near,actual);
   assert(reads==8);assert(now>.9999f);newMin=std::min(newMin,now);oldMin=std::min(oldMin,old);
   if(old<.9999f)++oldAcne;
   // Thin parallel blockers retain contact at either cascade resolution.
   for(float separation:{.05f/.9144f,.1f/.9144f,.25f/.9144f,4.f}){
    occluder=separation/depthRange;reads=0;
    assert(sampleShadowPCF(0,coords,activeTexel,near,actual)<.0001f);assert(reads==4);
   }
   ++cases;
  }
 }
 assert(oldAcne>0);
 printf("PASS: %d planar receiver cases: the implementation acne=%d, the implementation acne=0; minimum visibility old=%.6f new=%.6f; blockers retained; four reads per cascade.\n",cases,oldAcne,oldMin,newMin);
 // Rotated light matrices give the same analytic gradient as their local plane.
 for(int angle=0;angle<360;angle+=7){
  mat4 rotation=rotate(mat4(1),radians(float(angle)),normalize(vec3(1,2,3)));
  mat4 scale(1);scale[0][0]=.02f;scale[1][1]=.03f;scale[2][2]=.0005f;
  vec3 localNormal=normalize(vec3(.3f,-.7f,1));
  vec3 worldNormal=vec3(transpose(rotation)*vec4(localNormal,0));
  assert(length(shadowDepthGradient(scale*rotation,worldNormal)-shadowDepthGradient(scale,localNormal))<1e-6f);
 }
 // Grazing/degenerate normals produce finite bounded gradients.
 mat4 m(1);for(vec3 n:{vec3(1,0,0),vec3(0),vec3(0,1,-1e-8f)}){
  vec2 g=shadowDepthGradient(m,n);assert(std::isfinite(g.x)&&std::isfinite(g.y));assert(length(g)<=20.001f);
 }
}
'''
with tempfile.TemporaryDirectory(prefix='shadow267-') as d:
 p=Path(d)/'test.cpp';p.write_text(header+code+baseline+tests);b=Path(d)/'test'
 subprocess.run(['c++','-std=c++17','-I'+str(root/'extern/glm'),'-fsanitize=address,undefined',str(p),'-o',str(b)],check=True)
 subprocess.run([str(b)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
