#include "rendering/shadow_instances.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

using wowee::rendering::ShadowInstanceOrder;

// Use the PS4 instance stride measured in the session-release diagnostic,
// with model/position separated from trailing cached flags as in M2Instance.
struct Instance {
    uint32_t id, modelId;
    glm::vec3 position;
    float scale;
    glm::mat4 matrix;
    std::array<char, 384> animationAndParticleStorage{};
    bool valid = true, smoke = false, invisible = false;
    float radius = 1;
    uint64_t unused = 0;
};
static_assert(sizeof(Instance) == 488);

static bool visible(const Instance& instance, const glm::mat4& light, float radius) {
    if (!instance.valid || instance.smoke || instance.invisible) return false;
    const glm::vec4 clip = light * glm::vec4(instance.position, 1.f);
    const float margin = instance.radius * instance.scale / radius * 1.5f;
    return !(std::abs(clip.x) > 1.f + margin || std::abs(clip.y) > 1.f + margin ||
             clip.z < -margin || clip.z > 1.f + margin);
}

static void reference(const std::vector<Instance>& instances, const glm::mat4& light,
                      float radius, std::vector<const Instance*>& out, uint64_t& comparisons) {
    out.clear();
    for (const auto& instance : instances)
        if (visible(instance, light, radius)) out.push_back(&instance);
    std::sort(out.begin(), out.end(), [&](auto* a, auto* b) {
        ++comparisons;
        return a->modelId < b->modelId;
    });
}

static void cached(const std::vector<Instance>& instances, const glm::mat4& light,
                   float radius, ShadowInstanceOrder& order,
                   std::vector<const Instance*>& out, uint64_t& modelReads) {
    const auto& indices = order.prepare(static_cast<uint32_t>(instances.size()), [&](uint32_t i) {
        ++modelReads;
        return instances[i].modelId;
    });
    out.clear();
    for (uint32_t i : indices)
        if (visible(instances[i], light, radius)) out.push_back(&instances[i]);
}

static void equivalent(const std::vector<Instance>& instances, const glm::mat4& light,
                       float radius, ShadowInstanceOrder& order) {
    std::vector<const Instance*> oldCasters, newCasters;
    uint64_t comparisons = 0, modelReads = 0;
    reference(instances, light, radius, oldCasters, comparisons);
    cached(instances, light, radius, order, newCasters, modelReads);
    // Instanced draws use one group per model. Match group sizes and expand
    // both groups back to the exact instance IDs and current matrix bytes.
    assert(oldCasters.size() == newCasters.size());
    for (size_t begin = 0; begin < oldCasters.size();) {
        const auto oldEnd = wowee::rendering::shadowInstanceGroupEnd(begin, oldCasters.size(),
            [&](size_t i) { return oldCasters[i]->modelId; });
        const auto newEnd = wowee::rendering::shadowInstanceGroupEnd(begin, newCasters.size(),
            [&](size_t i) { return newCasters[i]->modelId; });
        assert(oldEnd == newEnd && oldCasters[begin]->modelId == newCasters[begin]->modelId);
        const auto byId = [](auto* a, auto* b) { return a->id < b->id; };
        std::sort(oldCasters.begin() + begin, oldCasters.begin() + oldEnd, byId);
        std::sort(newCasters.begin() + begin, newCasters.begin() + newEnd, byId);
        for (size_t i = begin; i < oldEnd; ++i) assert(oldCasters[i] == newCasters[i]);
        begin = oldEnd;
    }
}

int main() {
    std::mt19937 rng(20260908);
    std::uniform_real_distribution<float> pos(-800.f, 800.f);
    std::vector<Instance> instances;
    for (uint32_t i = 0; i < 17332; ++i) {
        Instance instance{};
        instance.id = i;
        instance.modelId = rng() % 511;
        instance.position = {pos(rng), pos(rng), pos(rng) * .05f};
        instance.scale = .2f + (rng() % 100) * .03f;
        instance.radius = .5f + (rng() % 100) * .2f;
        instance.matrix = glm::translate(glm::mat4(1.f), instance.position);
        instance.valid = i % 37 != 0;
        instance.smoke = i % 43 == 0;
        instance.invisible = i % 47 == 0;
        instances.push_back(instance);
    }
    ShadowInstanceOrder order;
    glm::mat4 light(1.f);
    light[0][0] = light[1][1] = 1.f / 650.f;
    light[2][2] = 1.f / 2000.f;
    light[3][2] = .5f;
    equivalent(instances, light, 650.f, order);
    assert(order.rebuilds() == 1);
    // Visibility and transforms must never be cached with model membership.
    for (int frame = 0; frame < 24; ++frame) {
        light[3][0] = std::sin(frame * .3f) * .75f;
        light[3][1] = std::cos(frame * .2f) * .75f;
        for (size_t i = 0; i < instances.size(); i += 31) {
            instances[i].position.x += 11.f;
            instances[i].matrix = glm::translate(glm::mat4(1.f), instances[i].position);
        }
        equivalent(instances, light, 650.f, order);
    }
    assert(order.rebuilds() == 1);
    // Reallocation alone cannot stale the cached indices.
    instances.reserve(instances.capacity() * 2);
    equivalent(instances, light, 650.f, order);
    assert(order.rebuilds() == 1);
    // Swap-remove and append together retain the count but change membership.
    order.invalidate();
    instances[3] = instances.back();
    instances.pop_back();
    Instance replacement = instances[4];
    replacement.id = 20000;
    replacement.modelId = 999;
    instances.push_back(replacement);
    equivalent(instances, light, 650.f, order);
    assert(order.rebuilds() == 2);
    order.invalidate();
    instances.erase(std::remove_if(instances.begin(), instances.end(),
        [](const Instance& i) { return i.id % 5 == 0; }), instances.end());
    equivalent(instances, light, 650.f, order);
    assert(order.rebuilds() == 3);
    // An interrupted rebuild must retry; it must never reuse the partial sort.
    order.invalidate();
    try {
        order.prepare(static_cast<uint32_t>(instances.size()), [](uint32_t) -> uint32_t {
            throw std::runtime_error("injected rebuild failure");
        });
        assert(false);
    } catch (const std::runtime_error&) {}
    equivalent(instances, light, 650.f, order);
    assert(order.rebuilds() == 4);
    std::cout << "PASS shadow groups: 24 moving views, moving transforms, reallocation, same-count replacement, bulk removal, failed rebuild\n";

    std::vector<const Instance*> oldCasters, newCasters;
    uint64_t comparisons = 0, modelReads = 0, checksum = 0;
    constexpr int frames = 120;
    const auto startOld = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
        reference(instances, light, 650.f, oldCasters, comparisons);
        checksum += oldCasters.size();
    }
    const auto startNew = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
        cached(instances, light, 650.f, order, newCasters, modelReads);
        checksum += newCasters.size();
    }
    const auto end = std::chrono::steady_clock::now();
    assert(modelReads == 0 && comparisons > 0 && checksum > 0);
    std::cout << "PASS steady shadow grouping: comparisons=" << comparisons
              << " cachedModelReads=" << modelReads << " checksum=" << checksum << '\n';
    std::cout << "HOST benchmark only, " << frames << " frames: previousMs="
              << std::chrono::duration<double, std::milli>(startNew - startOld).count()
              << " cachedMs=" << std::chrono::duration<double, std::milli>(end - startNew).count() << '\n';

    instances.clear();
    order.release();
    equivalent(instances, light, 650.f, order);
    instances.push_back(replacement);
    order.invalidate();
    equivalent(instances, light, 650.f, order);
    std::cout << "PASS full reset, empty scene and new scene\n";
}
