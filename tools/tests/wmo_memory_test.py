#!/usr/bin/env python3
"""Execute actual WMO collision and retirement methods with deterministic GPU fences."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/rendering/wmo_renderer.cpp').read_text()
header = (ROOT / 'include/rendering/wmo_renderer.hpp').read_text()
group = header[header.index('    struct GroupResources {'):header.index('\n    /**\n     * Portal data', header.index('    struct GroupResources {'))]
methods = source[source.index('void WMORenderer::GroupResources::buildCollisionGrid()'):source.index('std::optional<float> WMORenderer::getFloorHeight(')]
methods += source[source.index('bool WMORenderer::isModelLoaded('):source.index('bool WMORenderer::instanceHasCollisionGeometry(')]
methods += source[source.index('void WMORenderer::unloadModel('):source.index('uint32_t WMORenderer::createInstance(')]
methods += source[source.index('bool WMORenderer::createGroupResources('):source.index('// renderGroup removed')]
methods += source[source.index('void WMORenderer::destroyGroupGPU('):source.index('VkDescriptorSet WMORenderer::allocateMaterialSet()')]
prefix = r'''
#include "rendering/triangle_cell_index.hpp"
#include "rendering/spatial_grid.hpp"
#include "rendering/shadow_ranges.hpp"
#include "rendering/wmo_vertex.hpp"
#include "rendering/deferred_cleanup.hpp"
#include "pipeline/wmo_geometry_residency.hpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <random>
using namespace wowee::rendering;
namespace pipeline = wowee::pipeline;
static long failAfter = -1;
void* operator new(size_t n) {
    if (failAfter == 0) throw std::bad_alloc();
    if (failAfter > 0) --failAfter;
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
using VkBuffer = uint64_t;
using VmaAllocation = uint64_t;
using VkDevice = uint64_t;
using VmaAllocator = uint64_t;
using VkDescriptorSet = uint64_t;
using VkDescriptorPool = uint64_t;
constexpr uint64_t VK_NULL_HANDLE = 0;
struct VkTexture {};
static unsigned releasedBuffers[64]{}, releasedSets[64]{};
void vmaDestroyBuffer(VmaAllocator, VkBuffer buffer, VmaAllocation allocation) {
    assert(allocation == buffer + 1000); ++releasedBuffers[buffer];
}
void vkFreeDescriptorSets(VkDevice, VkDescriptorPool, int, const VkDescriptorSet* set) { ++releasedSets[*set]; }
void destroy(VmaAllocator a, VkBuffer& b, VmaAllocation& alloc) {
    if (b) vmaDestroyBuffer(a, b, alloc); b = alloc = 0;
}
struct FakeVkContext {
    std::vector<std::function<void()>> queues[2];
    VkDevice getDevice() const { return 1; }
    VmaAllocator getAllocator() const { return 2; }
    unsigned deferCalls=0,failDefer=0;
    void deferAfterAllFrameFences(std::function<void()>&& fn) { if(++deferCalls==failDefer)throw std::bad_alloc();enqueueAfterAllFences(queues, std::move(fn)); }
    void finish(unsigned f) { auto q = std::move(queues[f]); for (auto& fn : q) fn(); }
};
namespace core {
struct Logger {
    static Logger& getInstance() { static Logger logger; return logger; }
    template<class... T> void warning(T&&...) {}
    template<class... T> void info(T&&...) {}
};
}
#define LOG_WARNING(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
constexpr unsigned VK_BUFFER_USAGE_VERTEX_BUFFER_BIT = 1, VK_BUFFER_USAGE_INDEX_BUFFER_BIT = 2;
struct AllocatedBuffer { VkBuffer buffer; VmaAllocation allocation; };
static unsigned uploadCalls=0,failUpload=0;
AllocatedBuffer uploadBuffer(FakeVkContext&, const void*, size_t, unsigned usage) {
    if (++uploadCalls == failUpload) throw std::bad_alloc();
    return {10+usage,1010+usage};
}
constexpr float kWallMaxAbsNormalZ = 0.65f;
struct WMORenderer {
FakeVkContext* vkCtx_;
VkDescriptorPool materialDescPool_ = 7;
'''
suffix = r'''
bool createGroupResources(const wowee::pipeline::WMOGroup&, GroupResources&, uint32_t);
void destroyGroupGPU(GroupResources&, bool);
struct ModelData { bool terrainManaged=false,retiring=false; std::vector<GroupResources> groups; std::vector<VkTexture*> textures; };
struct Instance { uint32_t modelId; };
struct TextureCacheEntry { std::unique_ptr<VkTexture> texture,normalHeightMap; size_t approxBytes=0; };
std::unordered_map<uint32_t,ModelData> loadedModels,loadingModels_;
std::vector<Instance> instances;
std::unordered_map<std::string,TextureCacheEntry> textureCache;
size_t textureCacheBytes_=0;
bool isModelLoaded(uint32_t) const;
void unloadModel(uint32_t);
void cleanupUnusedModels(const std::unordered_set<uint32_t>&);
};
'''
cases = r'''
int main() {
    using G = WMORenderer::GroupResources;
    G g;
    g.boundingBoxMin = {0,0,0}; g.boundingBoxMax = {256,256,30};
    std::mt19937 rng(162);
    for (unsigned i = 0; i < 3000; ++i) {
        const float x = float(rng()%250), y = float(rng()%250), z = float(rng()%20);
        const float span = 1 + float(rng()%24);
        glm::vec3 p(x,y,z), q(std::min(256.f,x+span),y,z);
        glm::vec3 r = i%2 ? glm::vec3(x,y,z+8) : glm::vec3(x,std::min(256.f,y+span),z);
        for (auto v : {p,q,r}) { g.collisionIndices.push_back(g.collisionVertices.size()); g.collisionVertices.push_back(v); }
    }
    g.buildCollisionGrid();
    std::vector<std::vector<uint32_t>> old[3];
    for (auto& cells : old) cells.resize(4096);
    for (size_t i=0; i<g.collisionIndices.size(); i+=3) {
        const auto a=g.collisionVertices[g.collisionIndices[i]], b=g.collisionVertices[g.collisionIndices[i+1]], c=g.collisionVertices[g.collisionIndices[i+2]];
        const int minx=std::max(0,int(std::min({a.x,b.x,c.x})/4)), miny=std::max(0,int(std::min({a.y,b.y,c.y})/4));
        const int maxx=std::min(63,int(std::max({a.x,b.x,c.x})/4)), maxy=std::min(63,int(std::max({a.y,b.y,c.y})/4));
        auto n=glm::cross(b-a,c-a); const float len=glm::length(n); n = len>0.001f ? n/len : glm::vec3(0,0,1);
        const unsigned type=std::abs(n.z)>=0.65f ? 1 : 2;
        for(int y=miny;y<=maxy;++y) for(int x=minx;x<=maxx;++x) {old[0][y*64+x].push_back(i);old[type][y*64+x].push_back(i);}
    }
    const TriangleCellIndex* compact[3]={&g.cellTriangles,&g.cellFloorTriangles,&g.cellWallTriangles};
    size_t oldBytes=0,newBytes=0;
    for(unsigned type=0;type<3;++type) {
        oldBytes += old[type].capacity()*sizeof(std::vector<uint32_t>);
        newBytes += compact[type]->storageBytes();
        for(unsigned cell=0;cell<4096;++cell) {
            oldBytes += old[type][cell].capacity()*4;
            auto result=(*compact[type])[cell];
            assert(std::equal(result.begin(),result.end(),old[type][cell].begin(),old[type][cell].end()));
        }
        for(unsigned qi=0;qi<500;++qi) {
            float x=float(int(rng()%300)-22),y=float(int(rng()%300)-22),w=float(rng()%35);
            auto range=cellRangeCovering(64,64,256,256,glm::vec2(0),x,y,x+w,y+w);
            std::vector<uint32_t> expected,actual;
            std::vector<bool> seen(3000,false);
            if(range)for(int cy=range->minY;cy<=range->maxY;++cy)for(int cx=range->minX;cx<=range->maxX;++cx)
                for(uint32_t tri:old[type][cy*64+cx])if(!seen[tri/3]){seen[tri/3]=true;expected.push_back(tri);}
            g.gatherCellTriangles(*compact[type],x,y,x+w,y+w,actual);
            assert(actual==expected);
        }
    }
    assert(newBytes < oldBytes);
    // A failed query never poisons the reusable visited bits.
    std::vector<uint32_t> query;
    failAfter=0;
    bool threw=false;
    try { g.getTrianglesInRange(0,0,256,256,query); } catch(const std::bad_alloc&) { threw=true; }
    failAfter=-1;
    assert(threw && std::all_of(g.triVisited.begin(),g.triVisited.end(),[](auto v){return !v;}));
    g.getTrianglesInRange(0,0,256,256,query); assert(query.size()==3000);
    // A two-pass index rebuild is transactional across all three allocations.
    unsigned buildFailures=0;
    for(long fail=0;fail<8;++fail) {
        TriangleCellIndex index; index.build(2,[](auto emit){emit(1,42);});
        failAfter=fail; bool success=false;
        try { index.build(4,[](auto emit){emit(0,3);emit(3,6);});success=true; } catch(const std::bad_alloc&) {++buildFailures;}
        failAfter=-1;
        if(!success)assert(index[1].size()==1&&index[1][0]==42);
    }
    assert(buildFailures==3);
    // Retirement retains every handle until both queues are committed.
    unsigned retirementFailures=0;
    for(long fail=0;fail<16;++fail) {
        std::fill(std::begin(releasedBuffers),std::end(releasedBuffers),0);
        std::fill(std::begin(releasedSets),std::end(releasedSets),0);
        FakeVkContext ctx; WMORenderer renderer{&ctx}; G resource;
        resource.vertexBuffer=11;resource.vertexAlloc=1011;resource.indexBuffer=12;resource.indexAlloc=1012;
        resource.mergedBatches.resize(2);
        for(unsigned i=0;i<2;++i){auto& b=resource.mergedBatches[i];b.materialSet=21+i;b.materialUBO=31+i;b.materialUBOAlloc=1031+i;}
        failAfter=fail;bool success=false;
        try {renderer.destroyGroupGPU(resource,true);success=true;}catch(const std::bad_alloc&){++retirementFailures;}
        failAfter=-1;
        if(!success){assert(resource.vertexBuffer==11&&resource.indexBuffer==12&&resource.mergedBatches[1].materialUBO==32);assert(ctx.queues[0].empty()&&ctx.queues[1].empty());renderer.destroyGroupGPU(resource,true);}
        assert(!resource.vertexBuffer&&!resource.indexBuffer&&!resource.mergedBatches[1].materialUBO);
        ctx.finish(1);assert(!releasedBuffers[11]);ctx.finish(0);
        assert(releasedBuffers[11]==1&&releasedBuffers[12]==1&&releasedBuffers[31]==1&&releasedBuffers[32]==1&&releasedSets[21]==1&&releasedSets[22]==1);
    }
    assert(retirementFailures>=4);
    // Partial retirement is never advertised as a ready model. Resume the
    // owned remainder after a second-group queue failure, without double-free.
    {
        std::fill(std::begin(releasedBuffers),std::end(releasedBuffers),0);
        FakeVkContext ctx;WMORenderer renderer{&ctx};auto& model=renderer.loadedModels[7];model.groups.resize(2);
        for(unsigned i=0;i<2;++i){model.groups[i].vertexBuffer=11+i;model.groups[i].vertexAlloc=1011+i;}
        ctx.failDefer=2;bool caught=false;try{renderer.unloadModel(7);}catch(const std::bad_alloc&){caught=true;}
        assert(caught&&renderer.loadedModels.count(7)&&!renderer.isModelLoaded(7)&&!model.groups[0].vertexBuffer&&model.groups[1].vertexBuffer==12);
        ctx.failDefer=0;renderer.unloadModel(7);assert(!renderer.loadedModels.count(7));
        ctx.finish(0);assert(!releasedBuffers[11]&&!releasedBuffers[12]);ctx.finish(1);assert(releasedBuffers[11]==1&&releasedBuffers[12]==1);
    }
    // Actual periodic retirement protects both active instances and prepared
    // models, while canceling orphan uploads across repeated area turnovers.
    for(unsigned cycle=0;cycle<100;++cycle) {
        std::fill(std::begin(releasedBuffers),std::end(releasedBuffers),0);
        FakeVkContext ctx;WMORenderer renderer{&ctx};
        for(uint32_t id=1;id<=5;++id){auto& models=id<=3?renderer.loadedModels:renderer.loadingModels_;auto& model=models[id];model.terrainManaged=true;model.groups.emplace_back();model.groups.back().vertexBuffer=id;model.groups.back().vertexAlloc=id+1000;model.groups.back().collisionVertices.resize(100);}
        renderer.instances.push_back({1});renderer.cleanupUnusedModels({2,4});
        assert(renderer.loadedModels.size()==2&&renderer.loadedModels.count(1)&&renderer.loadedModels.count(2)&&renderer.loadingModels_.size()==1&&renderer.loadingModels_.count(4));
        ctx.finish(0);assert(!releasedBuffers[3]&&!releasedBuffers[5]);ctx.finish(1);assert(releasedBuffers[3]==1&&releasedBuffers[5]==1&&!releasedBuffers[1]&&!releasedBuffers[2]&&!releasedBuffers[4]);
        renderer.instances.clear();renderer.cleanupUnusedModels({});ctx.finish(0);ctx.finish(1);assert(renderer.loadedModels.empty()&&renderer.loadingModels_.empty());
    }
    // Inject a GPU allocation failure after the vertex upload. The actual
    // group builder keeps that handle and resumes only the missing index.
    wowee::pipeline::WMOGroup uploadSource{};
    uploadSource.vertices.resize(3);
    uploadSource.vertices[0].position={0,0,0};uploadSource.vertices[1].position={1,0,0};uploadSource.vertices[2].position={0,1,0};
    for(auto& v:uploadSource.vertices){v.normal={0,0,1};v.texCoord={v.position.x,v.position.y};v.color={1,1,1,1};}
    uploadSource.indices={0,1,2};uploadSource.boundingBoxMin={0,0,0};uploadSource.boundingBoxMax={1,1,0};
    FakeVkContext uploadCtx;WMORenderer uploader{&uploadCtx};G partial;
    uploadCalls=0;failUpload=2;
    threw=false;try{uploader.createGroupResources(uploadSource,partial,0);}catch(const std::bad_alloc&){threw=true;}
    assert(threw&&partial.vertexBuffer==11&&!partial.indexBuffer&&uploadCalls==2);
    failUpload=0;assert(uploader.createGroupResources(uploadSource,partial,0));
    assert(uploadCalls==3&&partial.vertexBuffer==11&&partial.indexBuffer==12&&partial.collisionVertices.size()==3&&partial.batches.size()==1);
    uploader.destroyGroupGPU(partial,true);uploadCtx.finish(0);uploadCtx.finish(1);
    // Retire uploaded geometry with all allocations disabled; pending geometry,
    // portals and liquids remain owned for the later instance phase.
    wowee::pipeline::WMOModel model{};model.groups.resize(4);
    for(auto& group:model.groups){group.vertices.resize(100);group.indices.resize(120);group.batches.resize(2);group.triFlags.resize(40);group.bspNodes.resize(64);group.liquid.heights={1,2};group.portalStart=3;group.portalCount=5;}
    failAfter=0;auto bytes=wowee::pipeline::releaseWmoGeometryPrefix(model,2);failAfter=-1;
    assert(bytes>0&&model.groups[0].vertices.capacity()==0&&model.groups[1].indices.capacity()==0&&model.groups[2].vertices.size()==100);
    for(auto& group:model.groups)assert(group.portalStart==3&&group.portalCount==5&&group.liquid.heights.size()==2);
    for(unsigned cycle=0;cycle<100;++cycle){G copy=g;copy.buildCollisionGrid();assert(copy.cellTriangles.storageBytes()==g.cellTriangles.storageBytes());}
    std::printf("PASS: 12,288 cell lists and 1,500 collision queries match reference order; 100 residency rebuilds stable\n");
    std::printf("collision fixture: old=%zu compact=%zu saved=%zu bytes (%.1f%%)\n",oldBytes,newBytes,oldBytes-newBytes,100.*double(oldBytes-newBytes)/oldBytes);
    std::printf("PASS: %u index allocation faults, %u retirement allocation faults, query OOM, pending/orphan residency, partial upload retry, two independent fences, committed source prefix/liquids\n",buildFailures,retirementFailures);
}
'''
with tempfile.TemporaryDirectory(prefix='wowps-wmo-memory-') as temp:
    sourcefile=Path(temp)/'wmo_memory.cpp'; sourcefile.write_text(prefix+group+suffix+methods+cases)
    cmd=shlex.split(os.environ.get('CXX','g++'))+['-std=c++20','-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined','-I'+str(ROOT/'include'),'-I'+str(ROOT/'extern/glm'),str(sourcefile),'-o',str(Path(temp)/'test')]
    subprocess.run(cmd,check=True)
    subprocess.run([str(Path(temp)/'test')],check=True)
