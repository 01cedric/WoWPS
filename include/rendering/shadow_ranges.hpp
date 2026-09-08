#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace wowee::rendering {
struct ShadowRange { uint32_t firstIndex, indexCount; };
// Depth-only draws with identical state can share one triangle-list range.
// Keep gaps and different triangle alignment: joining them would add geometry.
inline void coalesceShadowRanges(std::vector<ShadowRange>& ranges) {
    std::sort(ranges.begin(), ranges.end(), [](auto a, auto b) {
        return a.firstIndex < b.firstIndex;
    });
    size_t output = 0;
    for (auto range : ranges) {
        if (!range.indexCount) continue;
        if (output) {
            auto& last = ranges[output - 1];
            const uint64_t end = uint64_t(last.firstIndex) + last.indexCount;
            const uint64_t nextEnd = uint64_t(range.firstIndex) + range.indexCount;
            if (last.indexCount % 3 == 0 && range.indexCount % 3 == 0 &&
                last.firstIndex % 3 == range.firstIndex % 3 && range.firstIndex <= end &&
                std::max(end, nextEnd) - last.firstIndex <= UINT32_MAX) {
                last.indexCount = uint32_t(std::max(end, nextEnd) - last.firstIndex);
                continue;
            }
        }
        ranges[output++] = range;
    }
    ranges.resize(output);
}
}
