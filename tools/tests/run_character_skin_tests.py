#!/usr/bin/env python3
"""Execute production GLSL skin-matrix blocks with GLM float arithmetic."""
import pathlib,sys,zipfile,tempfile,subprocess
root=pathlib.Path(__file__).resolve().parents[2]
archive=pathlib.Path(sys.argv[1])
with zipfile.ZipFile(archive) as z, tempfile.TemporaryDirectory() as t:
    funcs=[]
    for kind in ['character','character_shadow']:
        rel=f'assets/shaders/{kind}.vert.glsl'
        old=z.read(next(n for n in z.namelist() if n.endswith('/'+rel))).decode()
        new=(root/rel).read_text()
        for label,src in [('old',old),('new',new)]:
            start=src.index('    uvec4 bi =');end=src.index('    vec4 skinnedPos',start)
            funcs.append('mat4 '+kind+'_'+label+'(vec4 aBoneWeights,uvec4 aBoneIndices){\n'+src[start:end]+'return skinMat;}\n')
    src=r"""
#include <glm/glm.hpp>
#include <random>
#include <cstdio>
#include <cassert>
#include <cmath>
using namespace glm;
const unsigned MAX_BONES=512;
mat4 bones[MAX_BONES];
"""+''.join(funcs)+r"""
int main(){
 std::mt19937 rng(276);std::uniform_real_distribution<float>d(-32,32);unsigned checks=0;float maxError=0;
 for(unsigned sample=0;sample<20000;++sample){
  for(auto&b:bones)for(int c=0;c<4;c++)for(int r=0;r<4;r++)b[c][r]=d(rng);
  unsigned count=sample%5; unsigned raw[4]={0,0,0,0},remaining=255;
  for(unsigned j=0;j<count;j++){raw[j]=(j==count-1)?remaining:1+rng()%(remaining-(count-j-1));remaining-=raw[j];}
  // Rotate sparse lanes so a nonzero first influence is not assumed.
  vec4 w(0);for(unsigned j=0;j<4;j++)w[(j+(sample/5)%4)%4]=float(raw[j])/255.0f;
  uvec4 idx(rng()%512,rng()%512,rng()%512,rng()%512);
  if(sample%3==0)idx[sample%4]=512+sample;if(sample%7==0)idx[(sample+1)%4]=0xffffffffu;
  mat4 before=character_old(w,idx),after=character_new(w,idx),shadowBefore=character_shadow_old(w,idx),shadowAfter=character_shadow_new(w,idx);
  for(int c=0;c<4;c++)for(int r=0;r<4;r++){
   assert(std::isfinite(before[c][r])&&std::isfinite(after[c][r]));
   float err=std::abs(before[c][r]-after[c][r]);maxError=std::max(maxError,err);
   assert(err<=1e-5f);assert(std::abs(shadowBefore[c][r]-shadowAfter[c][r])<=1e-5f);
   assert(after[c][r]==shadowAfter[c][r]);
  }checks++;
 }
 printf("PASS production GLSL blocks: %u cases, all-zero and 1-4 normalized U8 influences, rotated zero lanes, clamped indices, finite matrices; maxAbsError=%g\n",checks,maxError);
}
"""
    source=pathlib.Path(t)/'skin.cpp';source.write_text(src)
    exe=pathlib.Path(t)/'skin'
    subprocess.run(['clang++-18','-std=c++17','-O2','-I'+str(root/'extern/glm'),str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
