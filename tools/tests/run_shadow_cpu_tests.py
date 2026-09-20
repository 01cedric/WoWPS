#!/usr/bin/env python3
"""Compile actual production gather body and test exact caster equivalence."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'src/rendering/m2_renderer_render.cpp').read_text()
start=source.index('    shadowCasters_.clear();', source.index('void M2Renderer::renderShadow'))
end=source.index('    if (profileShadow)',start)
new=source[start:end]
# Original ordering: subtexel rejection before footprint rejection. Use the
# exact old operations (copied into this fixture so the test is standalone).
old='''
shadowCasters_.clear();
for (uint32_t index:casterOrder) {
 const auto& instance=instances[index];
 if(!instance.cachedIsValid || instance.cachedIsSmoke || instance.cachedIsInvisibleTrap)continue;
 if(!instance.cachedModel)continue;
 const M2ModelGPU& model=*instance.cachedModel;
 if(model.shadowBatches.empty())continue;
 if(shadowPassIndex==1 && minCasterDiameter>0) {
  float normSquared=0;
  for(int column=0;column<3;++column)for(int row=0;row<3;++row)
   normSquared+=instance.modelMatrix[column][row]*instance.modelMatrix[column][row];
  if(m2FarShadowSubtexel(shadowPassIndex,minCasterDiameter,model.shadowVertexRadius,normSquared,model.shadowWindFoliage))continue;
 }
 const glm::vec4 clip=lightSpaceMatrix*glm::vec4(instance.position,1);
 const float margin=(model.boundRadius*instance.scale)/shadowRadius*1.5f;
 if(std::abs(clip.x)>1+margin || std::abs(clip.y)>1+margin)continue;
 if(clip.z < -margin || clip.z>1+margin)continue;
 shadowCasters_.push_back(&instance);
}
'''
header=r'''
#include "rendering/m2_shadow.hpp"
#include "rendering/m2_shadow_cpu.hpp"
#include "rendering/m2_shadow_lod.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>
using namespace wowee::rendering;
struct M2ModelGPU {std::vector<int> shadowBatches{1};float shadowVertexRadius=1,boundRadius=1;bool shadowWindFoliage=false;};
struct Instance { bool cachedIsValid=true,cachedIsSmoke=false,cachedIsInvisibleTrap=false;const M2ModelGPU* cachedModel=nullptr;glm::mat4 modelMatrix{1};glm::vec3 position{0};float scale=1;int currentSequenceIndex=0;float animTime=0,globalSequenceTime=0;};
std::vector<Instance> instances;std::vector<uint32_t> casterOrder;
'''
sig='std::vector<const Instance*> NAME(glm::mat4 lightSpaceMatrix,float shadowRadius,uint32_t shadowPassIndex,float minCasterDiameter){std::vector<const Instance*> shadowCasters_;\n'
main=r'''
struct UvModel {
 struct Batch {uint16_t textureAnimIndex=0;};std::vector<Batch>batches{{0}};
 bool hasTextureAnimation=true,isLavaModel=false;
 std::vector<uint16_t>textureTransformLookup{0};
 std::vector<wowee::pipeline::M2TextureTransform>textureTransforms{1};
 std::vector<uint32_t>globalSequenceDurations{1000};
};
struct Batch{uint32_t materialBatch=0,maskMode=1;};
int main(){
 std::mt19937 random(268);std::uniform_real_distribution<float>p(-3000,3000),scale(.0001f,8);
 std::vector<M2ModelGPU>models(31);
 for(size_t i=0;i<models.size();++i){models[i].boundRadius=models[i].shadowVertexRadius=scale(random);models[i].shadowWindFoliage=i%2; if(i%13==0)models[i].shadowBatches.clear();}
 instances.resize(12000);casterOrder.resize(instances.size());std::iota(casterOrder.begin(),casterOrder.end(),0);
 for(size_t i=0;i<instances.size();++i){auto&x=instances[i];x.cachedModel=i%17?&models[i%models.size()]:nullptr;x.position={p(random),p(random),p(random)};x.scale=scale(random);x.modelMatrix=glm::scale(glm::mat4(1),glm::vec3(x.scale,scale(random),scale(random)));x.modelMatrix[0][1]=.7f;x.cachedIsValid=i%29;x.cachedIsSmoke=i%101==0;x.cachedIsInvisibleTrap=i%97==0;}
 size_t checked=0;
 for(unsigned frame=0;frame<40;++frame){
  const auto light=glm::ortho(-400.f,400.f,-400.f,400.f,1.f,7000.f)*glm::lookAt(glm::vec3(p(random),p(random),3500),glm::vec3(0),glm::vec3(0,1,0));
  for(unsigned pass=0;pass<2;++pass)for(float threshold:{0.f,.1f,2.f,10.f}) {assert(oldGather(light,400,pass,threshold)==newGather(light,400,pass,threshold));checked+=instances.size();}
 }
 // Explicit boundary and degenerate inputs keep conservative behavior.
 for(auto&x:instances){x.cachedModel=&models[1];x.cachedIsValid=true;x.cachedIsSmoke=x.cachedIsInvisibleTrap=false;x.position={1.f+models[1].boundRadius*x.scale/400.f*1.5f,0,0};}
 for(float threshold:{0.f,1.f,INFINITY,NAN})assert(oldGather(glm::mat4(1),400,1,threshold)==newGather(glm::mat4(1),400,1,threshold));
 UvModel model; Batch batch; auto&t=model.textureTransforms[0];
 t.translation.interpolationType=1;t.translation.sequences.resize(2);
 for(auto&s:t.translation.sequences){s.timestamps={0,500,1000};s.vec3Values={{0,0,0},{1,2,0},{0,3,0}};}
 t.rotation.sequences.resize(1);t.rotation.sequences[0].timestamps={0};t.rotation.sequences[0].quatValues={glm::angleAxis(.7f,glm::vec3(0,0,1))};
 t.scale.sequences.resize(1);t.scale.sequences[0].timestamps={0};t.scale.sequences[0].vec3Values={{2,.5f,1}};
 for(unsigned mode=0;mode<4;++mode){t.translation.globalSequence=mode==1?0:-1;model.isLavaModel=mode==2;model.textureTransformLookup[0]=mode==3?100:0;
  M2ShadowUvClockCache cache;
  for(unsigned i=0;i<12000;++i){Instance x;x.animTime=float((i/20)%1000);x.globalSequenceTime=float((i/20)%2000);x.currentSequenceIndex=(i/200)%2;
   auto ref=sampleM2ShadowUv(model,batch,x,7);
   auto got=cache.sample(x,[&]{return sampleM2ShadowUv(model,batch,x,7);});
   assert(ref.linear==got.linear && ref.offset==got.offset);
  }
  assert(cache.samples()==600 && cache.reused()==11400);
 }
 M2ShadowUvClockCache exact;Instance x;unsigned calls=0;auto sample=[&]{++calls;return M2UvTransform{};};
 exact.sample(x,sample);exact.sample(x,sample);assert(calls==1);
 x.animTime=-0.f;exact.sample(x,sample);assert(calls==2);
 x.globalSequenceTime=std::nextafter(0.f,1.f);exact.sample(x,sample);assert(calls==3);
 x.currentSequenceIndex=1;exact.sample(x,sample);assert(calls==4);
 std::cout<<"PASS "<<checked<<" gather decisions, exact caster order and coverage; boundary/invalid thresholds\n";
 std::cout<<"PASS 48000 UV samples: ordinary/global clocks, rotated/scaled UVs, lava/invalid lookup; exact input key changes\n";
 std::cout<<"Synthetic repeated-clock fixture: 12000 requests -> 600 evaluations + 11400 exact reuses (95%); not console FPS evidence\n";
}
'''
with tempfile.TemporaryDirectory(prefix='shadow268-') as d:
 cpp=Path(d)/'test.cpp';exe=Path(d)/'test'
 cpp.write_text(header+sig.replace('NAME','oldGather')+old+'return shadowCasters_;}\n'+sig.replace('NAME','newGather')+new+'return shadowCasters_;}\n'+main)
 subprocess.run(['g++','-std=c++17','-O2','-DGLM_FORCE_DEPTH_ZERO_TO_ONE','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'include'),'-I'+str(root/'extern/glm'),str(cpp),'-o',str(exe)],check=True)
 env=os.environ.copy();env.setdefault('ASAN_OPTIONS','detect_leaks=0');env.setdefault('UBSAN_OPTIONS','halt_on_error=1')
 subprocess.run([str(exe)],check=True,env=env)
