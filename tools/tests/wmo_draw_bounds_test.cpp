#include "rendering/wmo_draw_bounds.hpp"
#include "rendering/frustum.hpp"
#include "rendering/shadow_caster_bounds.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <random>
#include <limits>
#include <cassert>
#include <iostream>

using namespace wowee::rendering;

// Independent eight-corner homogeneous clip test. No normal normalization or
// affine shortcut shared with either production visibility implementation.
static bool reference(const glm::mat4& m, const WmoDrawBounds& b) {
    if (!b.valid) return true;
    bool outside[6] = {true, true, true, true, true, true};
    for (int c = 0; c < 8; ++c) {
        const glm::vec4 p = m * glm::vec4(
            (c & 1) ? b.high.x : b.low.x,
            (c & 2) ? b.high.y : b.low.y,
            (c & 4) ? b.high.z : b.low.z, 1.0f);
        const float d[6] = {p.w+p.x, p.w-p.x, p.w+p.y,
                            p.w-p.y, p.z, p.w-p.z};
        for (int i = 0; i < 6; ++i) outside[i] &= d[i] < 0.0f;
    }
    for (bool o : outside) if (o) return false;
    return true;
}

int main() {
    std::vector<glm::vec3> v{{-1,-1,-5},{1,-1,-5},{0,1,-5},
                            {90,-1,-5},{92,-1,-5},{91,1,-5}};
    std::vector<uint16_t> ix{0,1,2,3,4,5};
    const auto a = wmoDrawBounds(v,ix,0,3);
    const auto b = wmoDrawBounds(v,ix,3,3);
    assert(a.valid && b.valid && a.low.x == -1.5f && a.high.x == 1.5f);
    const auto projection = glm::perspective(glm::radians(60.0f),1.7f,0.1f,1000.0f);
    Frustum f; f.extractFromMatrix(projection);
    assert(f.intersectsAABB(a.low,a.high));
    assert(!f.intersectsAABB(b.low,b.high));
    // Huge group crosses the view; per-range culling removes the off-screen
    // street while retaining visible faces and the existing submission order.
    assert(f.intersectsAABB(a.low,b.high));
    assert(!wmoDrawBounds(v,ix,0,0).valid);
    assert(!wmoDrawBounds(v,ix,UINT32_MAX,3).valid);
    assert(!wmoDrawBounds(v,ix,3,UINT32_MAX).valid);
    assert(!wmoDrawBounds(v,ix,4,3).valid);
    ix[0]=65535; assert(!wmoDrawBounds(v,ix,0,3).valid); ix[0]=0;
    v[0].x=std::numeric_limits<float>::quiet_NaN();
    assert(!wmoDrawBounds(v,ix,0,3).valid); v[0].x=-1;

    // Main, cutout and blended draws have no index edits or material reordering.
    // Filter a mixed transparent/opaque sequence; visible relative order remains.
    const std::vector<int> order{9,7,4,3,1};
    std::vector<int> visible;
    for (int id : order) {
        const auto& bounds = id == 7 ? b : a;
        if (f.intersectsAABB(bounds.low,bounds.high)) visible.push_back(id);
    }
    assert((visible == std::vector<int>{9,4,3,1}));

    std::mt19937 random(261);
    std::uniform_real_distribution<float> pos(-300.0f,300.0f);
    std::uniform_real_distribution<float> angle(-3.14f,3.14f);
    std::uniform_real_distribution<float> scale(.1f,4.0f);
    const glm::mat4 light = glm::ortho(-32.0f,32.0f,-32.0f,32.0f,0.0f,400.0f);
    unsigned culled=0, shadowCulled=0;
    for (unsigned i=0; i<50000; ++i) {
        glm::mat4 model = glm::translate(glm::mat4(1),glm::vec3(pos(random),pos(random),pos(random)));
        model = glm::rotate(model,angle(random),glm::normalize(glm::vec3(1,2,3)));
        model = glm::scale(model,glm::vec3((i&1 ? -1:1)*scale(random),scale(random),scale(random)));
        const glm::mat4 clip = projection*model;
        f.extractFromMatrix(clip);
        const bool visible = f.intersectsAABB(a.low,a.high);
        assert(visible == reference(clip,a));
        culled += !visible;
        const glm::mat4 ls = light*model;
        const bool litVolume = shadowIntersectsWorldBounds(ls,a.low,a.high);
        assert(litVolume == reference(ls,a));
        shadowCulled += !litVolume;
    }
    // A caster behind the camera remains present in the finite light volume.
    const glm::mat4 upstream = glm::translate(glm::mat4(1),glm::vec3(0,0,15));
    f.extractFromMatrix(projection*upstream);
    assert(!f.intersectsAABB(a.low,a.high));
    const glm::mat4 lightOtherSide = glm::ortho(-32.f,32.f,-32.f,32.f,-100.f,100.f);
    assert(shadowIntersectsWorldBounds(lightOtherSide*upstream,a.low,a.high));
    std::cout << "PASS: exact range bounds, malformed fail-open, visible material order, "
                 "50000 rotated/reflected/scaled Vulkan camera and light comparisons; "
              << "cameraCulled=" << culled << " shadowCulled=" << shadowCulled << '\n';
}
