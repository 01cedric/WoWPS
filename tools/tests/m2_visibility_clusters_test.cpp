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
    float cachedVisualRadius = 0.5f;
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
    uint64_t originalTests=0, broadphaseTests=0, frustumOnlyTests=0, viewChanges=0;
    auto compare=[&](float yaw,float farPlane,float maxDistance=2000.f) {
        const glm::vec3 eye(-80,20,18);
        auto projection=glm::perspective(glm::radians(60.f),1.777f,.1f,farPlane);
        Frustum f;
        f.extractFromMatrix(projection*glm::lookAt(eye,eye+glm::vec3(std::cos(yaw),std::sin(yaw),-.1f),glm::vec3(0,0,1)));
        cache.prepare(instances,[&](auto center,float radius){return m2ClusterWithinDistance(center,radius,eye,maxDistance) && f.intersectsSphere(center,radius);});
        std::vector<size_t> full, compact;
        auto visible=[&](const auto& v){const float surface=std::max(0.f,glm::length(v.cachedCullCenter-eye)-v.cachedVisualRadius); return surface*surface<=maxDistance*maxDistance && v.cachedIsValid&&!v.cachedIsSmoke&&!v.cachedIsInvisibleTrap&&
            (!(v.cachedPaddedRadius>0)||f.intersectsSphere(v.cachedCullCenter,v.cachedPaddedRadius));};
        for(size_t i=0;i<instances.size();++i) if(visible(instances[i]))full.push_back(i);
        for(size_t block=0;block<cache.clusters().size();++block) {
            const auto& cluster=cache.clusters()[block];
            const size_t end=std::min(instances.size(),(block+1)*cache.blockSize);
            if(cluster.bypass || f.intersectsSphere(cluster.center,cluster.radius))
                frustumOnlyTests+=end-block*cache.blockSize;
            if(!cluster.accepted)continue;
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
        compare(frame*.03f,30000.f,50.f+float(frame%20)*20.f);
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
    // All blocks are inside the view cone but beyond the distance cap.
    // The original frustum-only broad phase visits every one of these objects.
    instances.assign(6400,Instance{});
    for(size_t i=0;i<instances.size();++i)
        instances[i].cachedCullCenter={1000.f+float(i/64),20,18};
    cache.invalidateAll(); compare(0,30000,100);
    size_t survivors=0;
    for(const auto& cluster:cache.clusters()) survivors+=cluster.accepted;
    assert(survivors==0);
    // A huge/offset object whose surface reaches the camera survives.
    instances[0].cachedCullCenter={1000,20,18};
    instances[0].cachedVisualRadius=1100;
    instances[0].cachedPaddedRadius=1200;
    cache.invalidate(0); compare(0,30000,100);
    assert(cache.clusters()[0].accepted);
    // Tangency, large coordinates, invalid inputs and overflow fail open.
    assert(m2ClusterWithinDistance({110,0,0},10,{0,0,0},100));
    assert(!m2ClusterWithinDistance({111,0,0},10,{0,0,0},100));
    assert(m2ClusterWithinDistance({30110,30000,0},10,{30000,30000,0},100));
    const float nan=std::numeric_limits<float>::quiet_NaN();
    assert(m2ClusterWithinDistance({nan,0,0},10,{0,0,0},100));
    assert(m2ClusterWithinDistance({0,0,0},nan,{0,0,0},100));
    assert(m2ClusterWithinDistance({0,0,0},10,{0,0,0},nan));
    assert(m2ClusterWithinDistance({1e30f,0,0},10,{0,0,0},100));
    cache.release(); assert(cache.clusters().empty());
    std::cout<<"PASS distance-only workload: 6400 individual tests avoided; tangency, large objects and invalid bounds preserved\n";
    std::cout<<"PASS exact visible IDs/order across "<<viewChanges<<" camera changes and movement/append/swap-remove/compaction; fullTests="
             <<originalTests<<" clusterSurvivorTests="<<broadphaseTests<<" skipped="<<(originalTests-broadphaseTests)
             <<" additionalDistanceSkips="<<(frustumOnlyTests-broadphaseTests)
             <<" (synthetic workload, not console FPS)\n";
}
