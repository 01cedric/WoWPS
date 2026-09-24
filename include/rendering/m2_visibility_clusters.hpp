#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace wowee::rendering {
// Conservative distance broad phase using the same enclosing spheres as the
// frustum broad phase. Invalid inputs fail open; the per-instance predicate
// remains authoritative. Padding protects tangencies from float rounding.
inline bool m2ClusterWithinDistance(const glm::vec3& center, float radius,
                                    const glm::vec3& camera, float maxDistance) {
    if (!(radius >= 0.0f) || !(maxDistance >= 0.0f) ||
        !std::isfinite(radius) || !std::isfinite(maxDistance)) return true;
    const glm::vec3 delta = center - camera;
    const float distanceSquared = glm::dot(delta, delta);
    const float reach = radius + maxDistance;
    const float paddedReach = reach + 0.01f + 0.00001f * reach;
    if (!std::isfinite(distanceSquared) || !std::isfinite(paddedReach)) return true;
    return distanceSquared <= paddedReach * paddedReach;
}

// Placement-order blocks keep streamed tile doodads together. Bounds enclose
// the SAME padded spheres used by the final cull, with outward rounding slack.
// They reject work only; final per-instance predicates and draw order survive.
class M2VisibilityClusters {
public:
    static constexpr uint32_t blockSize = 64;
    struct Cluster {
        glm::vec3 minimum{}, maximum{}, center{};
        float radius = 0.0f;
        bool dirty = true, bypass = false, accepted = true;
    };
    void invalidate(uint32_t index) {
        if (index / blockSize < clusters_.size()) clusters_[index / blockSize].dirty = true;
    }
    void invalidateAll() { dirtyAll_ = true; }
    void release() { std::vector<Cluster>{}.swap(clusters_); previousSize_ = 0; dirtyAll_ = true; }
    template<class Instances, class Intersects>
    void prepare(const Instances& instances, Intersects&& intersects) {
        const size_t count = instances.size();
        // The partial old tail changes on append/remove even when the number
        // of blocks does not. Swap-removal additionally invalidates its hole.
        if (count != previousSize_) {
            if (previousSize_) invalidate(static_cast<uint32_t>(previousSize_ - 1));
            if (count) invalidate(static_cast<uint32_t>(count - 1));
        }
        clusters_.resize((count + blockSize - 1) / blockSize);
        for (size_t block = 0; block < clusters_.size(); ++block) {
            auto& cluster = clusters_[block];
            if (dirtyAll_ || cluster.dirty) {
                cluster.minimum = glm::vec3(std::numeric_limits<float>::max());
                cluster.maximum = glm::vec3(std::numeric_limits<float>::lowest());
                cluster.bypass = false;
                bool any = false;
                const size_t end = std::min(count, (block + 1) * blockSize);
                for (size_t index = block * blockSize; index < end; ++index) {
                    const auto& instance = instances[index];
                    if (!instance.cachedIsValid || instance.cachedIsSmoke || instance.cachedIsInvisibleTrap) continue;
                    const auto center = instance.cachedCullCenter;
                    const float radius = instance.cachedPaddedRadius;
                    if (!(radius > 0.0f) || !std::isfinite(radius) ||
                        !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) {
                        cluster.bypass = true;
                        break;
                    }
                    const float slack = 0.01f + 0.00001f * std::max(radius,
                        std::max(std::abs(center.x), std::max(std::abs(center.y), std::abs(center.z))));
                    cluster.minimum = glm::min(cluster.minimum, center - glm::vec3(radius + slack));
                    cluster.maximum = glm::max(cluster.maximum, center + glm::vec3(radius + slack));
                    any = true;
                }
                if (!any) cluster.bypass = true; // fail open for empty/invalid blocks
                if (!cluster.bypass) {
                    cluster.center = (cluster.minimum + cluster.maximum) * 0.5f;
                    cluster.radius = glm::length(cluster.maximum - cluster.minimum) * 0.5f;
                    if (!std::isfinite(cluster.radius)) cluster.bypass = true;
                }
                cluster.dirty = false;
            }
            cluster.accepted = cluster.bypass || intersects(cluster.center, cluster.radius);
        }
        previousSize_ = count;
        dirtyAll_ = false;
    }
    const std::vector<Cluster>& clusters() const { return clusters_; }
private:
    std::vector<Cluster> clusters_;
    size_t previousSize_ = 0;
    bool dirtyAll_ = true;
};
}
