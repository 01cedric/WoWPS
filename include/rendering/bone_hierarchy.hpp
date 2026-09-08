#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wowee::rendering {

// Cached per model. The on-disk bone order is not required to be topological.
// Runtime pose evaluation follows this plan so every parent comes from the
// current frame, even when the file lists a child before its parent.
struct BoneHierarchy {
    std::vector<std::size_t> order;
    std::vector<int32_t> parents;
    std::size_t invalidParents = 0;
    std::size_t cycleBreaks = 0;
};

template <class Bones>
BoneHierarchy buildBoneHierarchy(const Bones& bones) {
    BoneHierarchy result;
    const std::size_t count = bones.size();
    result.order.reserve(count);
    result.parents.reserve(count);
    for (const auto& bone : bones) {
        const int32_t parent = bone.parentBone;
        if (parent < -1 || (parent >= 0 && static_cast<std::size_t>(parent) >= count)) {
            result.parents.push_back(-1);
            ++result.invalidParents;
        } else {
            result.parents.push_back(parent);
        }
    }

    // Iterative depth-first traversal, O(bones), without using the call stack.
    // A malformed cyclic edge is cut at the last node of the current path;
    // unwinding that path then gives a valid parent-before-child order.
    std::vector<uint8_t> state(count, 0); // unseen, on current path, complete
    std::vector<std::size_t> path;
    path.reserve(count);
    for (std::size_t start = 0; start < count; ++start) {
        if (state[start] == 2) continue;
        path.clear();
        int32_t current = static_cast<int32_t>(start);
        while (current >= 0 && state[static_cast<std::size_t>(current)] == 0) {
            const std::size_t index = static_cast<std::size_t>(current);
            state[index] = 1;
            path.push_back(index);
            current = result.parents[index];
        }
        if (current >= 0 && state[static_cast<std::size_t>(current)] == 1) {
            result.parents[path.back()] = -1;
            ++result.cycleBreaks;
        }
        while (!path.empty()) {
            const std::size_t index = path.back();
            path.pop_back();
            result.order.push_back(index);
            state[index] = 2;
        }
    }
    return result;
}

} // namespace wowee::rendering
