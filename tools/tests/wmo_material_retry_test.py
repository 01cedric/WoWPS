#!/usr/bin/env python3
"""Actual WMO material upload phase, allocation-fault injection and ownership checks."""
from pathlib import Path
import os,shlex,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'src/rendering/wmo_renderer.cpp').read_text();h=(ROOT/'include/rendering/wmo_renderer.hpp').read_text()
group=h[h.index('    struct GroupResources {'):h.index('\n    /**\n     * Portal data',h.index('    struct GroupResources {'))]
ubo=h[h.index('    struct WMOMaterialUBO {'):h.index('\n    /**',h.index('    struct WMOMaterialUBO {'))]
phase=s[s.index('    for (size_t materialGroup = modelData.nextMaterialGroupIndex;'):s.index('\n    vkCtx_->endUploadBatch();',s.index('    for (size_t materialGroup = modelData.nextMaterialGroupIndex;'))]
fixture=r'''
#include "rendering/triangle_cell_index.hpp"
#include "rendering/shadow_ranges.hpp"
#include "rendering/wmo_material_class.hpp"
#include "rendering/pom_quality.hpp"
#include <glm/glm.hpp>
#include <chrono>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <memory>
#include <unordered_map>
using namespace wowee::rendering;
#define LOG_WARNING(...) ((void)0)
static long failAfter=-1;
void* operator new(size_t n){if(failAfter==0)throw std::bad_alloc();if(failAfter>0)--failAfter;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
using VkBuffer=uint64_t;using VmaAllocation=uint64_t;using VmaAllocator=uint64_t;using VkDescriptorSet=uint64_t;
constexpr uint64_t VK_NULL_HANDLE=0;
constexpr unsigned VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT=1,VMA_MEMORY_USAGE_CPU_TO_GPU=2,VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET=3,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER=4,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER=5;
struct VkDescriptorImageInfo{};
struct VkDescriptorBufferInfo{VkBuffer buffer=0;size_t offset=0,range=0;};
struct VkWriteDescriptorSet{unsigned sType=0,dstBinding=0,descriptorType=0,descriptorCount=0;VkDescriptorSet dstSet=0;const VkDescriptorImageInfo*pImageInfo=nullptr;const VkDescriptorBufferInfo*pBufferInfo=nullptr;};
struct VkTexture{bool isValid()const{return true;}VkDescriptorImageInfo descriptorInfo()const{return{};}};
static unsigned buffers=0,sets=0,writes=0,failGpuAt=0,gpuAttempts=0;
static unsigned char mapped[32][80]{};
struct AllocatedBuffer{VkBuffer buffer=0;VmaAllocation allocation=0;struct{void*pMappedData=nullptr;}info;};
AllocatedBuffer createBuffer(VmaAllocator,size_t,unsigned,unsigned){if(++gpuAttempts==failGpuAt)throw std::bad_alloc();++buffers;return{buffers,1000+buffers,{mapped[buffers]}};}
void vkFreeDescriptorSets(uint64_t,uint64_t,unsigned,const VkDescriptorSet*){assert(false);}
void vkUpdateDescriptorSets(uint64_t,unsigned,const VkWriteDescriptorSet*,unsigned,void*){++writes;}
struct FakeCtx{VmaAllocator getAllocator()const{return 1;}uint64_t getDevice()const{return 2;}void endUploadBatch(){}};
struct WMORenderer{
'''
after=r'''
struct ModelData{std::vector<GroupResources>groups;size_t nextMaterialGroupIndex=0;std::vector<uint32_t>materialTextureIndices,materialBlendModes,materialFlags;std::vector<VkTexture*>textures;std::vector<std::string>textureNames;glm::vec3 wmoAmbientColor{.5f};};
struct CacheEntry{std::unique_ptr<VkTexture>texture,normalHeightMap;float heightMapVariance=0;};
std::unordered_map<std::string,CacheEntry>textureCache;
FakeCtx context;FakeCtx*vkCtx_=&context;uint64_t materialDescPool_=1;
std::unique_ptr<VkTexture>whiteTexture_=std::make_unique<VkTexture>(),flatNormalTexture_=std::make_unique<VkTexture>();
bool wmoOnlyMap_=false,normalMappingEnabled_=true,pomEnabled_=true;int pomQuality_=2;float normalMapStrength_=1;
enum class ModelLoadResult{Complete,InProgress,Failed};
VkDescriptorSet allocateMaterialSet(){if(++gpuAttempts==failGpuAt)throw std::bad_alloc();return ++sets;}
ModelLoadResult upload(ModelData& modelData){uint32_t id=1;float budgetMs=0;auto loadStepStart=std::chrono::steady_clock::now();
'''
cases=r'''
return ModelLoadResult::Complete;}
};
int main(){
unsigned failedCpu=0,failedGpu=0;
for(unsigned mode=0;mode<2;++mode)for(unsigned fault=0;fault<(mode?7:100);++fault){
 WMORenderer r;WMORenderer::ModelData model;model.groups.emplace_back();auto&g=model.groups[0];
 VkTexture textures[3];
 for(unsigned i=0;i<3;++i){model.textures.push_back(&textures[i]);model.textureNames.push_back(i==0?"lava.blp":"brick.blp");model.materialTextureIndices.push_back(i);model.materialBlendModes.push_back(0);model.materialFlags.push_back(0);g.batches.push_back({i*3,3,uint8_t(i)});g.collisionIndices.insert(g.collisionIndices.end(),{0,1,2});}
 g.collisionVertices={{0,0,0},{1,0,0},{0,1,0}};
 buffers=sets=writes=gpuAttempts=0;failGpuAt=mode?fault:0;failAfter=mode?-1:long(fault);
 bool failed=false;try{r.upload(model);}catch(const std::bad_alloc&){failed=true;if(mode)++failedGpu;else++failedCpu;}
 failAfter=-1;failGpuAt=0;
 unsigned ownedBuffers=0,ownedSets=0;for(auto&mb:g.mergedBatches){ownedBuffers+=bool(mb.materialUBO);ownedSets+=bool(mb.materialSet);}
 assert(ownedBuffers==buffers&&ownedSets==sets);
 r.upload(model);assert(model.nextMaterialGroupIndex==1&&g.mergedBatches.size()==3&&buffers==3&&sets==3&&writes==3);
 size_t draws=0;for(auto&mb:g.mergedBatches){assert(mb.materialReady);draws+=mb.draws.size();}
 assert(draws==3&&g.lavaLights.size()==1&&g.shadowRanges.size()==1&&g.shadowRanges[0].indexCount==9);
 r.upload(model);assert(buffers==3&&sets==3&&writes==3);
}
assert(failedCpu>5&&failedGpu==6);
std::printf("PASS: actual material phase %u CPU and %u GPU allocation faults retain every handle; retries keep exactly3 draw batches/UBOs/sets,1 lava light, unchanged shadow range\n",failedCpu,failedGpu);
}
'''
with tempfile.TemporaryDirectory(prefix='wowps-wmo-material-')as temp:
 path=Path(temp)/'test.cpp';path.write_text(fixture+ubo+group+after+phase+cases);binary=Path(temp)/'test'
 subprocess.run(shlex.split(os.environ.get('CXX','g++'))+['-std=c++20','-O1','-g','-fsanitize=address,undefined','-I'+str(ROOT/'include'),'-I'+str(ROOT/'extern/glm'),str(path),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
