#pragma once

#include <cstddef>
#include <cstdint>

namespace wowee::rendering {
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
