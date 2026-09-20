#include "rendering/shadow_receiver_hull.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>
#include <iostream>
#include <random>
using namespace wowee::rendering;
int main() {
    std::mt19937 rng(274); std::uniform_real_distribution<float> unit(-1,1), depth(0.1f,30000.0f);
    unsigned samples=0, culled=0;
    for(int angle=0;angle<50;++angle) {
        glm::vec3 light=glm::normalize(glm::vec3(unit(rng),unit(rng),unit(rng)));
        const auto view=glm::lookAt(-light*100.0f,glm::vec3(0),std::abs(light.z)>.99f?glm::vec3(0,1,0):glm::vec3(0,0,1));
        std::array<glm::vec3,8> corners; unsigned i=0;
        for(float z:{0.1f,30000.0f})for(float x:{-1.f,1.f})for(float y:{-1.f,1.f}) corners[i++]={x*z*.75f,y*z*.5f,z};
        ShadowReceiverHull hull;hull.build(corners,view);assert(hull.size()>=3);
        for(int j=0;j<2000;++j) {
            float z=depth(rng);glm::vec3 receiver(unit(rng)*z*.75f,unit(rng)*z*.5f,z);
            // Cast from either side along the light axis; camera visibility
            // is intentionally irrelevant. World-space radius expands hull.
            glm::vec3 caster=receiver+light*unit(rng)*100000.0f;
            assert(hull.intersects(caster,0));
            auto offset=glm::normalize(glm::vec3(unit(rng),unit(rng),unit(rng)))*10.f;
            assert(hull.intersects(caster+offset,10.f));
            // A box containing a caster on the same light ray must never
            // disappear, including offscreen casters and sheared transports.
            const glm::vec3 extent(1.0f + std::abs(unit(rng))*20.0f,
                1.0f + std::abs(unit(rng))*20.0f, 1.0f + std::abs(unit(rng))*20.0f);
            assert(hull.intersectsBounds(caster-extent, caster+extent));
            glm::mat4 transform(1.0f);
            transform[0] = glm::vec4(1.0f,unit(rng),unit(rng),0.0f);
            transform[1] = glm::vec4(unit(rng),-2.0f,unit(rng),0.0f);
            transform[2] = glm::vec4(unit(rng),unit(rng),3.0f,0.0f);
            const glm::vec3 localPoint=extent*glm::vec3(unit(rng),unit(rng),unit(rng));
            transform[3] = glm::vec4(caster-glm::mat3(transform)*localPoint,1.0f);
            assert(hull.intersectsTransformedBounds(-extent,extent,transform));
            ++samples;
        }
        for(int j=0;j<100;++j)culled+=!hull.intersects(glm::vec3(unit(rng),unit(rng),unit(rng))*100000.f,0);
        assert(hull.intersects(glm::vec3(1e10f),-1)); // Missing bounds fail open.
    }
    // Far shadow texel width from the DK hardware run. A blocker beyond
    // the geometric projection can still contribute to a diagonal PCF tap.
    std::array<glm::vec3,8> box; unsigned bi=0;
    for(float z:{-1.f,1.f})for(float x:{-1.f,1.f})for(float y:{-1.f,1.f}) box[bi++]={x,y,z};
    ShadowReceiverHull unfiltered,filtered;
    const float farWorldTexel=2.78952f;
    unfiltered.build(box,glm::mat4(1));
    filtered.build(box,glm::mat4(1),2.f*farWorldTexel);
    const glm::vec3 pcfBlocker(1.f+1.4f*farWorldTexel,0,1000);
    assert(!unfiltered.intersects(pcfBlocker,0));
    assert(filtered.intersects(pcfBlocker,0));
    assert(!filtered.intersects(glm::vec3(20,0,1000),0));
    assert(!filtered.intersectsBounds(glm::vec3(20,-1,-10000), glm::vec3(21,1,10000)));
    assert(filtered.intersectsBounds(pcfBlocker,pcfBlocker));
    auto translated=glm::translate(glm::mat4(1),glm::vec3(20,0,1000));
    assert(!filtered.intersectsTransformedBounds(glm::vec3(-1),glm::vec3(1),translated));
    assert(filtered.intersectsBounds(glm::vec3(10),glm::vec3(-10)));
    ShadowReceiverHull invalid;assert(invalid.intersects(glm::vec3(1e10f),0));
    assert(culled>0);
    std::cout << "PASS receiver and upstream coverage samples="<<samples<<" distant exclusions="<<culled<<"\n";
}
