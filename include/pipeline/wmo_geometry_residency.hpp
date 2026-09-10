#pragma once

#include "pipeline/wmo_loader.hpp"
#include <algorithm>
#include <cstddef>
#include <type_traits>

namespace wowee::pipeline {

// Uploaded vertices have a GPU owner, and collision keeps its own positions,
// indices, flags and grid. Only retire the committed prefix: liquids and portal
// ranges must survive until instance/portal creation, as must unuploaded groups.
inline size_t releaseWmoGeometryPrefix(WMOModel& model, size_t committedGroups) noexcept {
    size_t bytes = 0;
    for (size_t i = 0; i < std::min(committedGroups, model.groups.size()); ++i) {
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

} // namespace wowee::pipeline
