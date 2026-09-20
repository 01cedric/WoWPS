#include "rendering/m2_visibility_clusters.hpp"
#include "rendering/frustum.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>
#include <iostream>
#include <numeric>
#include <random>
using namespace wowee::rendering;
struct Instance {
    glm::vec3 cachedCullCenter{};
    float cachedPaddedRadius = 1.0f;
    bool cachedIsValid = true, cachedIsSmoke = false, cachedIsInvisibleTrap = false;
    char cold[600]{};
};
int main() {
    std::mt19937 rng(276);
    std::uniform_real_distribution<float> pos(-1000,1000), local(-20,20);
    std::vector<Instance> instances(12500);
    for (size_t i=0; i<instances.size(); ++i) {
        auto& v=instances[i];
        const auto base=glm::vec3(float((i/64)%16)*100-800,float((i/1024)%16)*100-800,10);
        v.cachedCullCenter=base+glm::vec3(local(rng),local(rng),local(rng));
        v.cachedPaddedRadius=1+float(rng()%20);
    }
    M2VisibilityClusters cache;
    uint64_t originalTests=0, broadphaseTests=0, viewChanges=0;
    auto compare=[&](float yaw,float farPlane) {
        const glm::vec3 eye(-80,20,18);
        auto projection=glm::perspective(glm::radians(60.f),1.777f,.1f,farPlane);
        Frustum f;
        f.extractFromMatrix(projection*glm::lookAt(eye,eye+glm::vec3(std::cos(yaw),std::sin(yaw),-.1f),glm::vec3(0,0,1)));
        cache.prepare(instances,[&](auto center,float radius){return f.intersectsSphere(center,radius);});
        std::vector<size_t> full, compact;
        auto visible=[&](const auto& v){return v.cachedIsValid&&!v.cachedIsSmoke&&!v.cachedIsInvisibleTrap&&
            (!(v.cachedPaddedRadius>0)||f.intersectsSphere(v.cachedCullCenter,v.cachedPaddedRadius));};
        for(size_t i=0;i<instances.size();++i) if(visible(instances[i]))full.push_back(i);
        for(size_t block=0;block<cache.clusters().size();++block) {
            if(!cache.clusters()[block].accepted)continue;
            const size_t end=std::min(instances.size(),(block+1)*cache.blockSize);
            broadphaseTests+=end-block*cache.blockSize;
            for(size_t i=block*cache.blockSize;i<end;++i)if(visible(instances[i]))compact.push_back(i);
        }
        originalTests+=instances.size();++viewChanges;
        assert(full==compact);
    };
    for(int frame=0;frame<600;++frame) {
        if(frame%3==0) {
            size_t index=rng()%instances.size();
            instances[index].cachedCullCenter={pos(rng),pos(rng),local(rng)};
            instances[index].cachedPaddedRadius=(frame%27==0)?0.f:float(rng()%40+1);
            cache.invalidate(index);
        }
        if(frame%7==0) {
            size_t index=rng()%instances.size();
            cache.invalidate(index);
            instances[index]=instances.back();instances.pop_back();
        }
        if(frame%5==0) {
            Instance v;v.cachedCullCenter={pos(rng),pos(rng),local(rng)};
            cache.invalidate(static_cast<uint32_t>(instances.size()));
            instances.push_back(v);
        }
        if(frame%47==0) {
            instances.erase(instances.begin()+100,instances.begin()+130);
            cache.invalidateAll();
        }
        // Live look changes every comparison, including the reflection-like opposite view.
        compare(frame*.03f,30000.f);
        compare(frame*.03f+3.14f,500.f);
    }
    // Remove a DIFFERENT block's instance, then append at the old tail before
    // prepare sees any size change. Both the hole and appended slot invalidate.
    instances.assign(128, Instance{});
    for (auto& instance : instances) instance.cachedCullCenter={-1000,-1000,18};
    cache.invalidateAll();compare(0,30000);
    assert(!cache.clusters()[1].accepted);
    cache.invalidate(0);instances[0]=instances.back();instances.pop_back();
    cache.invalidate(static_cast<uint32_t>(instances.size()));
    Instance replacement;replacement.cachedCullCenter={-60,30,18};
    instances.push_back(replacement);compare(0,30000);
    // Cross partial block boundaries and exercise remove+append with unchanged size.
    for(size_t size:{1u,63u,64u,65u,127u,128u,129u}) {
        instances.resize(size);cache.invalidateAll();compare(0,30000);
        cache.invalidate(size-1);instances.pop_back();instances.emplace_back();
        compare(3.14f,30000);
    }
    std::cout<<"PASS exact visible IDs/order across "<<viewChanges<<" camera changes and movement/append/swap-remove/compaction; fullTests="
             <<originalTests<<" clusterSurvivorTests="<<broadphaseTests<<" skipped="<<(originalTests-broadphaseTests)
             <<" (synthetic workload, not console FPS)\n";
}
