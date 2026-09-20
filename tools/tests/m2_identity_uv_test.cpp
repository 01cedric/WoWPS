#include "rendering/m2_identity_uv.hpp"
#include "rendering/m2_texture_transform.hpp"
#include "rendering/ordered_triangle_range.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <random>
using namespace wowee;
using namespace rendering;
int main() {
    pipeline::M2TextureTransform transform;
    assert(m2TextureTransformIsIdentity(transform));
    for (auto* track : {&transform.translation, &transform.rotation, &transform.scale}) {
        track->sequences.resize(3);
        track->globalSequence = 0;
        for (auto& keys : track->sequences) keys.timestamps = {0, 10, 20, 100};
    }
    for (auto& keys : transform.translation.sequences) keys.vec3Values.assign(4, glm::vec3(0));
    for (auto& keys : transform.scale.sequences) keys.vec3Values.assign(4, glm::vec3(1));
    for (auto& keys : transform.rotation.sequences)
        keys.quatValues = {glm::quat(1,0,0,0),glm::quat(-1,0,0,0),glm::quat(1,0,0,0),glm::quat(-1,0,0,0)};
    std::mt19937 rng(276);
    std::uniform_real_distribution<float> times(-1000,10000);
    for (unsigned type=0; type<4; ++type) {
        transform.translation.interpolationType=type;
        transform.scale.interpolationType=type;
        transform.rotation.interpolationType=type;
        for (int gs : {-1,0,99}) {
            transform.translation.globalSequence=gs;
            transform.scale.globalSequence=gs;
            transform.rotation.globalSequence=gs;
            assert(m2TextureTransformIsIdentity(transform));
            for (int i=0;i<10000;++i) {
                const auto uv=sampleM2Uv(&transform, int(rng()%6)-2, times(rng), times(rng), {100});
                assert(uv.offset==glm::vec2(0));
                assert(uv.linear==glm::vec4(1,0,0,1));
            }
        }
    }
    transform.translation.sequences[2].vec3Values[3].x=0.001f;
    assert(!m2TextureTransformIsIdentity(transform));
    transform.translation.sequences[2].vec3Values[3].x=0;
    transform.scale.sequences[0].vec3Values[0].z=2;
    assert(!m2TextureTransformIsIdentity(transform));
    transform.scale.sequences[0].vec3Values[0].z=1;
    transform.rotation.sequences[1].quatValues[1].x=0.001f;
    assert(!m2TextureTransformIsIdentity(transform));
    transform.rotation.sequences[1].quatValues[1].x=std::numeric_limits<float>::quiet_NaN();
    assert(!m2TextureTransformIsIdentity(transform));
    uint32_t count=6;
    assert(appendOrderedTriangleRange(5,count,11,9) && count==15);
    assert(!appendOrderedTriangleRange(5,count,19,3)); // overlapping triangle retained separately
    assert(!appendOrderedTriangleRange(5,count,21,3)); // gap never filled
    assert(!appendOrderedTriangleRange(5,count,20,2)); // incomplete triangle cannot combine
    count=UINT32_MAX-3;
    assert(!appendOrderedTriangleRange(0,count,UINT32_MAX-3,6));
    // Preserve the exact ordered index stream, including overlaps and gaps.
    for(int trial=0;trial<1000;++trial) {
        std::vector<uint32_t> original, merged;
        uint32_t first=0, n=0;
        auto flush=[&]{for(uint32_t j=0;j<n;++j)merged.push_back(first+j);n=0;};
        uint32_t next=0;
        for(int i=0;i<100;++i) {
            uint32_t c=3*(1+rng()%5);
            for(uint32_t j=0;j<c;++j)original.push_back(next+j);
            if(!appendOrderedTriangleRange(first,n,next,c)){flush();first=next;n=c;}
            next+=c;
            if(rng()%3==0)next-=std::min(next,3u);
            if(rng()%3==0)next+=3;
        }
        flush();assert(original==merged);
    }
    std::cout << "PASS identity UV proof vs sampler: 120000 local/global samples, all interpolation modes, nonidentity/NaN retained; 100000 ordered range submissions preserve exact geometry and duplicates\n";
}
