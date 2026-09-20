#include "rendering/wmo_portal_scratch.hpp"
#include <glm/glm.hpp>
#include <unordered_set>
#include <limits>
#include <random>
#include <cassert>
#include <iostream>
#include <cstdlib>
#include <new>
static size_t allocations = 0;
void* operator new(std::size_t n) { ++allocations; if(auto p=std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
namespace wowee::rendering {
// CPU fixture data matches the fields read by the extracted production methods.
// Frustum varies by test; actual portal bounds transformation and validation run.
struct Frustum {
    glm::vec3 min{-100}, max{100};
    bool intersectsAABB(glm::vec3 lo, glm::vec3 hi) const {
        return glm::all(glm::lessThanEqual(lo,max)) && glm::all(glm::greaterThanEqual(hi,min));
    }
};
struct WMORenderer {
    struct Group { uint32_t groupFlags; glm::vec3 boundingBoxMin, boundingBoxMax; };
    struct Portal { uint16_t startVertex, vertexCount; };
    struct Ref { uint16_t portalIndex, groupIndex; };
    struct ModelData {
        std::vector<Group> groups;
        std::vector<Portal> portals;
        std::vector<glm::vec3> portalVertices;
        std::vector<Ref> portalRefs;
        std::vector<std::pair<uint16_t,uint16_t>> groupPortalRefs;
    };
    int findContainingGroup(const ModelData&,const glm::vec3&) const;
    bool isPortalVisible(const ModelData&,uint16_t,const glm::vec3&,const Frustum&,const glm::mat4&) const;
    void getVisibleGroupsViaPortals(const ModelData&,const glm::vec3&,const glm::vec3&,const Frustum&,const glm::mat4&,WMOPortalScratch&) const;
    void reference(const ModelData&,const glm::vec3&,const glm::vec3&,const Frustum&,const glm::mat4&,std::unordered_set<uint32_t>&) const;
};
#include "wmo_traversal.generated.inc"
#include "fixtures/wmo_portals.inc"
}
int main() {
    using namespace wowee::rendering;
    WMORenderer renderer;
    WMOPortalScratch scratch;
    scratch.reset(128); // warm capacity; all following streamed models are smaller
    std::mt19937 rng(258);
    size_t comparisons=0, oldAllocations=0;
    for (unsigned trial=0;trial<4000;++trial) {
        WMORenderer::ModelData m;
        const unsigned n=rng()%90;
        for(unsigned i=0;i<n;++i) {
            const uint32_t flags[]={0x2000,0x8,0x2008,0};
            m.groups.push_back({trial%3 ? 0x2000u : flags[rng()%4],glm::vec3(float(i)*3,0,0),glm::vec3(float(i)*3+2,2,2)});
            if (i && trial%7==0) m.groups.back().boundingBoxMin.x=0; // overlapping doorway boxes
            m.portals.push_back({static_cast<uint16_t>(m.portalVertices.size()),static_cast<uint16_t>(trial%13==0 ? 2:3)});
            m.portalVertices.insert(m.portalVertices.end(),{glm::vec3(i*3,0,0),glm::vec3(i*3+1,1,0),glm::vec3(i*3,1,1)});
            auto start=static_cast<uint16_t>(m.portalRefs.size());
            for(unsigned j=0,count=rng()%8;j<count;++j) m.portalRefs.push_back({static_cast<uint16_t>(rng()%(n+3)),static_cast<uint16_t>(rng()%(n+3))});
            m.groupPortalRefs.emplace_back(start,static_cast<uint16_t>(m.portalRefs.size()-start));
        }
        if(trial%11==0 && n) m.groupPortalRefs.resize(n/2); // missing refs
        if(trial%17==0 && !m.groupPortalRefs.empty()) m.groupPortalRefs[0]={65000,7}; // invalid ref range
        if(trial%19==0 && n) m.portals[0].startVertex=65530;
        Frustum f;
        f.min.x=float(rng()%100)-50; f.max.x=f.min.x+float(rng()%250);
        glm::mat4 transform(1);
        if(trial%2) { transform[0][0]=0; transform[0][1]=1;transform[1][0]=-1;transform[1][1]=0; }
        glm::vec3 cam(float(rng()%(n+2))*3+1,1,1), viewer(float(rng()%(n+2))*3+1,1,1);
        std::unordered_set<uint32_t> reference;
        size_t before=allocations;
        renderer.reference(m,cam,viewer,f,transform,reference);
        oldAllocations+=allocations-before;
        before=allocations;
        scratch.reset(n);
        renderer.getVisibleGroupsViaPortals(m,cam,viewer,f,transform,scratch);
        assert(allocations==before); // actual extracted production traversal, after warmup
        for(unsigned i=0;i<n;++i) { assert(scratch.visible(i)==reference.contains(i)); ++comparisons; }
        assert(scratch.queue.size()<=n); // cycles/duplicate seeds cannot enqueue twice
    }
    assert(oldAllocations>0);
    std::cout << "WMO the reference: 4000 generated portal graphs / " << comparisons
              << " visibility decisions match frozen the reference traversal; warm traversal allocations=0 versus "
              << oldAllocations << " oracle allocations. CPU fixture only; no GPU/FPS claim.\n";
}
