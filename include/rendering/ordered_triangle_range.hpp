#pragma once
#include <cstdint>
namespace wowee::rendering {
// Unlike a depth-only union, color draws must retain duplicates and submission
// order. Only concatenate exactly adjacent complete triangle-list primitives.
inline bool appendOrderedTriangleRange(uint32_t first, uint32_t& count,
                                       uint32_t nextFirst, uint32_t nextCount) {
    if (!count || !nextCount || count % 3 || nextCount % 3 ||
        uint64_t(first) + count != nextFirst || uint64_t(count) + nextCount > UINT32_MAX)
        return false;
    count += nextCount;
    return true;
}
}
