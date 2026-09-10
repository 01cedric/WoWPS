#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <vector>

namespace wowee::rendering {
// Model membership changes when instances are added/removed; visibility,
// transforms and wind change every frame. Cache only the former. Indices,
// rather than pointers, survive a vector reallocation. Call invalidate()
// before changing membership/order (including a same-size replacement).
class ShadowInstanceOrder {
public:
    void invalidate() { dirty_ = true; }
    void release() {
        std::vector<uint32_t>{}.swap(indices_);
        dirty_ = true;
        rebuilds_ = 0;
    }

    template<class ModelAt>
    const std::vector<uint32_t>& prepare(uint32_t count, ModelAt modelAt) {
        if (dirty_ || indices_.size() != count) {
            // Keep dirty set until sorting succeeds, so allocation failure
            // cannot leave a partly built order marked reusable.
            dirty_ = true;
            indices_.resize(count);
            std::iota(indices_.begin(), indices_.end(), uint32_t{0});
            std::sort(indices_.begin(), indices_.end(), [&](uint32_t a, uint32_t b) {
                return modelAt(a) < modelAt(b);
            });
            dirty_ = false;
            ++rebuilds_;
        }
        return indices_;
    }

    uint32_t rebuilds() const { return rebuilds_; }

private:
    std::vector<uint32_t> indices_;
    bool dirty_ = true;
    uint32_t rebuilds_ = 0;
};

// All members in [begin,end) share one model and therefore its vertex/index
// buffers, depth ranges and alpha textures. Draw order changes only inside the
// depth-only pass; each instance keeps its own model transform and wind origin.
template<class ModelAt>
size_t shadowInstanceGroupEnd(size_t begin, size_t count, ModelAt modelAt) {
    if (begin >= count) return count;
    const auto model = modelAt(begin);
    size_t end = begin + 1;
    while (end < count && modelAt(end) == model) ++end;
    return end;
}

inline bool shadowInstanceStorageFits(size_t count, uint32_t capacity) {
    return count != 0 && count <= capacity;
}
} // namespace wowee::rendering
