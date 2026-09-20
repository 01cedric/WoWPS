#!/usr/bin/env python3
"""Execute production gather body against its full-scan reference; no Vulkan needed."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[2]
s = (root/'src/rendering/m2_renderer.cpp').read_text()
body = s[s.index('uint32_t M2Renderer::gatherLocalLights('):s.index('/// The nine main-pass pipelines')]
a=body.index('    // Keep original instance order')
b=body.index('        const M2ModelGPU* model = instance.cachedModel;',a)
reference=body[:a]+'    for (const auto& instance : instances) {\n'+body[b:]
reference=reference.replace('M2Renderer::gatherLocalLights(', 'M2Renderer::reference(')
prefix=r'''
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include <algorithm>
#include <limits>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <iostream>
struct Batch { bool glowCardLike=true,lanternGlowHint=true; float glowSize=1; int glowTint=0; };
struct Emitter { size_t bone=0; glm::vec3 position{0}; };
struct M2ModelGPU { bool isLanternLike=false,isTorch=false,isBrazierOrFire=false,isForge=false,isLavaModel=false,isGroundFire=false; std::string name; std::vector<Batch> batches; std::vector<Emitter> particleEmitters; };
struct M2Instance { const M2ModelGPU* cachedModel=nullptr; glm::vec3 cachedCullCenter{0},position{0}; float cachedVisualRadius=10,scale=1; glm::mat4 modelMatrix{1}; std::vector<glm::mat4> boneMatrices; };
float clockSeconds=0;
float lampFlickerClockSeconds(){return clockSeconds;}
float lampFlicker(glm::vec3 p,float t,float a,float b,float c){return a+b*std::sin(t+p.x)+c*std::cos(t+p.y);}
glm::vec3 animatedBatchLightWorldCenter(const M2Instance& i,const Batch& b){return glm::vec3(i.modelMatrix*(i.boneMatrices.empty()?glm::mat4(1):i.boneMatrices[0])*glm::vec4(b.glowSize,0,0,1));}
struct M2Renderer {
std::vector<M2Instance> instances;
mutable std::vector<size_t> localLightInstanceIndices_;
mutable bool localLightInstancesDirty_=true;
uint32_t gatherLocalLights(const glm::vec3&,glm::vec4*,glm::vec4*,uint32_t)const;
uint32_t reference(const glm::vec3&,glm::vec4*,glm::vec4*,uint32_t)const;
};
'''
main=r'''
int main(){
M2ModelGPU models[7]; models[0].isLanternLike=true; models[0].batches.push_back({});
models[1].isTorch=true; models[1].particleEmitters.push_back({});
models[2].isLavaModel=true; models[3].isForge=true; models[3].particleEmitters.push_back({});
models[4].isBrazierOrFire=true; models[4].isGroundFire=true; models[4].batches.push_back({}); models[4].particleEmitters.push_back({});
models[5].isLanternLike=true; models[5].name="Chandelier"; models[5].particleEmitters.push_back({});
M2Renderer r;
for(int i=0;i<20000;++i){ M2Instance v; v.cachedModel=&models[i%101<6?i%101:6]; v.position=glm::vec3(i%83-41,i%127-63,i%7); v.cachedCullCenter=v.position; v.modelMatrix=glm::translate(glm::mat4(1),v.position); v.boneMatrices.push_back(glm::mat4(1)); r.instances.push_back(v); }
int comparisons=0;
auto check=[&](){for(unsigned limit: {0u,1u,8u,32u,256u}){glm::vec4 ap[256]{},ac[256]{},bp[256]{},bc[256]{};auto n=r.gatherLocalLights(glm::vec3(clockSeconds,0,0),ap,ac,limit);auto m=r.reference(glm::vec3(clockSeconds,0,0),bp,bc,limit);assert(n==m);assert(!std::memcmp(ap,bp,n*sizeof(glm::vec4)));assert(!std::memcmp(ac,bc,n*sizeof(glm::vec4)));++comparisons;}};
for(int frame=0;frame<30;++frame){clockSeconds=frame*13.7f; for(auto& v:r.instances)v.boneMatrices[0]=glm::translate(glm::mat4(1),glm::vec3(0,frame*0.3f,0));check();}
assert(r.localLightInstanceIndices_.size()<1200);
// Mirror every topology invalidation: add, swap-remove, erase, model pointer refresh, clear.
r.instances.push_back(r.instances[0]);r.localLightInstancesDirty_=true;check();
r.instances[0]=std::move(r.instances.back());r.instances.pop_back();r.localLightInstancesDirty_=true;check();
r.instances.erase(r.instances.begin()+10,r.instances.begin()+800);r.localLightInstancesDirty_=true;check();
r.instances[0].cachedModel=nullptr;r.instances[1].cachedModel=&models[2];r.localLightInstancesDirty_=true;check();
r.instances.clear();r.localLightInstancesDirty_=true;check();
std::cout<<"PASS: "<<comparisons<<" bit-exact production/full-scan comparisons; 20000 instances, animated bones, camera, flicker, all fixture types, light limits, topology and null models\n";
}
'''
# Pin the mutation boundaries that must invalidate the index before references change.
for file, names in {'src/rendering/m2_renderer_render.cpp':['commitInstance'], 'src/rendering/m2_renderer_instance.cpp':['removeInstance','removeInstances','clearInstances','rebuildSpatialIndex']}.items():
    text=(root/file).read_text()
    for name in names:
        start=text.index('M2Renderer::'+name+'(')
        end=text.find('\n}',start)
        assert 'localLightInstancesDirty_ = true;' in text[start:end], name
with tempfile.TemporaryDirectory() as d:
    source=Path(d)/'test.cpp';source.write_text(prefix+body+reference+main)
    binary=Path(d)/'test'
    subprocess.run(['c++','-std=c++20','-O2','-I'+str(root/'extern/glm'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
