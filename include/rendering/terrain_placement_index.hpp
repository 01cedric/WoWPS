#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace wowee::rendering {
// Placement membership is immutable for the lifetime of a resident ADT.
// Sorted IDs use four bytes per placement, avoiding per-node heap allocations
// on the constrained PS4 CPU heap while turning neighbor scans into log N.
class TerrainPlacementIndex {
public:
    template<class Placements>
    void assign(const Placements& placements) {
        ids_.clear();
        ids_.reserve(placements.size());
        for (const auto& placement : placements)
            if (placement.uniqueId) ids_.push_back(placement.uniqueId);
        std::sort(ids_.begin(), ids_.end());
        ids_.erase(std::unique(ids_.begin(), ids_.end()), ids_.end());
    }
    bool contains(uint32_t id) const {
        return id != 0 && std::binary_search(ids_.begin(), ids_.end(), id);
    }
private:
    std::vector<uint32_t> ids_;
};
} // namespace wowee::rendering
