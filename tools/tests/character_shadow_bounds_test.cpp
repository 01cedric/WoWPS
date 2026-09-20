#include "rendering/visibility_bounds.hpp"
#include "rendering/shadow_fit.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>
#include <cstdio>

int main() {
    using namespace wowee::rendering;
    const glm::vec3 travel = glm::normalize(glm::vec3(1, 0, -.1f));
    const glm::vec3 center(0);
    const glm::mat4 view = glm::lookAt(center - travel * 720.0f, center, glm::vec3(0, 0, 1));
    glm::mat4 projection = glm::ortho(-100.f, 100.f, -100.f, 100.f, 1.f, 1560.f);
    projection[1][1] *= -1;
    Frustum light;
    light.extractFromMatrix(projection * view);
    const glm::vec3 low(-1,-1,0), high(1,1,3);
    auto accepts = [&](glm::vec3 location, glm::vec3 scale = glm::vec3(1)) {
        auto model = glm::scale(glm::translate(glm::mat4(1), location), scale);
        return modelBoundsInFrustum(light, model, low, high, 2.f);
    };
    const glm::vec3 upstream = -travel * 220.f;
    assert(glm::dot(upstream, upstream) > 135.f * 135.f); // Former origin sphere drops it.
    assert(accepts(upstream)); // Its projection overlaps the receiver center.
    assert(accepts(glm::vec3(0)));
    assert(!accepts(glm::vec3(0, 160, 0))); // Lateral rejection is retained.
    assert(!accepts(-travel * 800.f)); // Before the light near plane.
    assert(!accepts(travel * 900.f)); // Beyond the light far plane.
    assert(accepts(glm::vec3(0, 108, 0), glm::vec3(4))); // Scaled attachment intersects edge.
    std::puts("PASS character light-frustum bounds: formerly rejected upstream caster retained; lateral/depth rejection; scaled attachment. CPU geometry evidence only.");
}
