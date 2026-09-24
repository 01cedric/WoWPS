#pragma once

#include "pipeline/wmo_loader.hpp"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace wowee::pipeline {

// Geometry whose GPU copy has already committed, detached from its owning WMO
// so the expensive allocator releases can happen on a terrain worker instead
// of the render thread. Moving these vectors is O(1) with both the normal and
// PS4 CpuGeometryAllocator vector types; destruction is the part that can cost
// tens of milliseconds when a city WMO owns many large source buffers.
struct DetachedWmoGeometry {
    platform::CpuGeometryVector<WMOVertex> vertices;
    platform::CpuGeometryVector<uint16_t> indices;
    std::vector<WMOBatch> batches;
    std::vector<uint8_t> triFlags;
    std::vector<uint8_t> bspNodes;
    size_t bytes = 0;
};

inline size_t detachWmoGeometryGroup(WMOModel& model, size_t groupIndex,
                                     DetachedWmoGeometry& out) noexcept {
    if (groupIndex >= model.groups.size()) return 0;
    auto& group = model.groups[groupIndex];
    out.bytes = group.vertices.capacity() * sizeof(WMOVertex) +
                group.indices.capacity() * sizeof(uint16_t) +
                group.batches.capacity() * sizeof(WMOBatch) +
                group.triFlags.capacity() * sizeof(uint8_t) +
                group.bspNodes.capacity() * sizeof(uint8_t);
    out.vertices = std::move(group.vertices);
    out.indices = std::move(group.indices);
    out.batches = std::move(group.batches);
    out.triFlags = std::move(group.triFlags);
    out.bspNodes = std::move(group.bspNodes);
    return out.bytes;
}

// Uploaded vertices have a GPU owner, and collision keeps its own positions,
// indices, flags and grid. Only retire the committed prefix: liquids and portal
// ranges must survive until instance/portal creation, as must unuploaded groups.
inline size_t releaseWmoGeometryRange(WMOModel& model, size_t firstGroup, size_t committedGroups) noexcept {
    size_t bytes = 0;
    const size_t endGroup = std::min(committedGroups, model.groups.size());
    for (size_t i = std::min(firstGroup, endGroup); i < endGroup; ++i) {
        auto& group = model.groups[i];
        const auto release = [&](auto& values) {
            bytes += values.capacity() * sizeof(typename std::decay_t<decltype(values)>::value_type);
            std::decay_t<decltype(values)>{}.swap(values);
        };
        release(group.vertices);
        release(group.indices);
        release(group.batches);
        release(group.triFlags);
        release(group.bspNodes);
    }
    return bytes;
}

inline size_t releaseWmoGeometryPrefix(WMOModel& model, size_t committedGroups) noexcept {
    return releaseWmoGeometryRange(model, 0, committedGroups);
}

} // namespace wowee::pipeline
