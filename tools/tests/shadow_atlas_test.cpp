#include "rendering/shadow_atlas.hpp"
#include "rendering/shadow_fit.hpp"
#include <cassert>
#include <cstdio>
#include <glm/gtc/constants.hpp>

using namespace wowee::rendering;
int main() {
    unsigned checks=0;
    for (uint32_t side : {512u, 1024u, 2048u, 4096u}) {
        const auto near = shadowAtlasRegion(side,0);
        const auto far = shadowAtlasRegion(side,1);
        assert(near.x+near.side <= far.x);
        assert(far.x+far.side <= 2*side && far.y+far.side <= side);
        assert(near.side*near.side + far.side*far.side == side*side*5/4);
        for (float fov : {35.f,45.f,60.f,70.f,90.f}) {
            for (float aspect : {1.f, 16.f/9.f, 21.f/9.f}) {
                const auto nf = fitShadowFrustum({1,2,3},{0,1,0},glm::radians(fov),aspect,0.1f,48.f);
                const auto ff = fitShadowFrustum({1,2,3},{0,1,0},glm::radians(fov),aspect,0.1f,300.f);
                // New near texels resolve at least six times more finely than
                // the previous full-resolution 300-yard receiver map.
                assert(nf.radius*6.f < ff.radius);
                for (float d : {0.1f,24.f,48.f}) {
                    const float y = std::tan(glm::radians(fov)*.5f)*d;
                    for (int sx : {-1,1}) for (int sz : {-1,1}) {
                        glm::vec3 corner = glm::vec3(1,2,3)+glm::vec3(sx*y*aspect,d,sz*y);
                        assert(glm::length(corner-nf.center) <= nf.radius+0.001f);
                        ++checks;
                    }
                }
            }
        }
    }
    std::printf("PASS atlas disjoint regions, 1.25x raster area, %u near-frustum corners, >6x linear near precision\n",checks);
}
