#include "rendering/character_placement_cache.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
using namespace wowee::rendering;
static glm::mat4 original(glm::vec3 position, glm::vec3 rotation, float scale) {
    glm::mat4 model(1.0f);
    model=glm::translate(model,position);
    model=glm::rotate(model,rotation.z,glm::vec3(0,0,1));
    model=glm::rotate(model,rotation.x,glm::vec3(1,0,0));
    model=glm::rotate(model,rotation.y,glm::vec3(0,1,0));
    return glm::scale(model,glm::vec3(scale));
}
int main() {
    CharacterPlacementCache cache;
    std::mt19937 rng(277);
    std::uniform_real_distribution<float> position(-15000,15000), angle(-6.28,6.28), scale(.001,100);
    for (unsigned i=0;i<10000;++i) {
        glm::vec3 p(position(rng),position(rng),position(rng));
        glm::vec3 r(angle(rng),angle(rng),angle(rng));
        float s=scale(rng);
        for (unsigned update=0;update<4;++update) {
            if(update==1)p.x+=.125f;
            if(update==2)r.y+=.1f;
            if(update==3)s*=.5f;
            const auto expected=original(p,r,s);
            for(int pass=0;pass<6;++pass) {
                const auto actual=cache.get(p,r,s);
                assert(std::memcmp(&actual,&expected,sizeof(actual))==0);
            }
        }
    }
    assert(cache.rebuilds()==40000);
    std::cout<<"PASS240000 character matrix queries bit-identical to original ZXY composition;40000 recomputations,200000 exact-input reuses; movement,rotation,scale visible immediately\n";
}
