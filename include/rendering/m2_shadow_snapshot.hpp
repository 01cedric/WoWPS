#pragma once
#include <glm/glm.hpp>
#include <vector>
namespace wowee::rendering {
// Ephemeral atlas-pair snapshot. The renderer owns its lifetime explicitly:
// no mutation of instances/models may occur between prepare and finish.
// Only cold gather fields are packed; model matrices and animation clocks
// remain at their original addresses and are consumed by the existing draws.
template<class Instance> class M2ShadowSnapshot {
public:
    struct Entry { const Instance* instance; glm::vec3 position; float scaledRadius; };
    template<class Instances, class Order>
    void prepare(const Instances& instances, const Order& order) {
        entries.clear();
        entries.reserve(instances.size());
        for (auto index : order) {
            const auto& instance = instances[index];
            if (!instance.cachedIsValid || instance.cachedIsSmoke || instance.cachedIsInvisibleTrap ||
                !instance.cachedModel || instance.cachedModel->shadowBatches.empty()) continue;
            entries.push_back({&instance, instance.position,
                              instance.cachedModel->boundRadius * instance.scale});
        }
        active = true;
    }
    void finish() { active = false; entries.clear(); }
    void release() { finish(); std::vector<Entry>{}.swap(entries); }
    static bool intersects(const Entry& entry, const glm::mat4& light, float radius) {
        const glm::vec4 clip = light * glm::vec4(entry.position, 1.0f);
        const float margin = entry.scaledRadius / radius * 1.5f;
        return !(std::abs(clip.x) > 1.0f + margin || std::abs(clip.y) > 1.0f + margin ||
                 clip.z < -margin || clip.z > 1.0f + margin);
    }
    std::vector<Entry> entries;
    bool active = false;
};
} // namespace wowee::rendering
