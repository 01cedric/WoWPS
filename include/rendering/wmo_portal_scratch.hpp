#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wowee::rendering {
// Sequential WMO traversal scratch, retained across instances and frames. Group
// indices are dense; hashing and allocating a node for each visible group adds
// no information. Separate bits retain the distinction between visible exterior
// groups and groups already queued for traversal.
class WMOPortalScratch {
public:
    void reset(std::size_t groups) {
        flags_.resize(groups);
        std::fill(flags_.begin(), flags_.end(), uint8_t{0});
        queue.clear();
        queue.reserve(groups);
    }
    void show(uint32_t group) { flags_[group] |= 1; }
    bool visible(uint32_t group) const { return (flags_[group] & 1) != 0; }
    bool visited(uint32_t group) const { return (flags_[group] & 2) != 0; }
    void enqueue(uint32_t group) {
        if (visited(group)) return;
        flags_[group] |= 3;
        queue.push_back(group);
    }
    std::vector<uint32_t> queue;
private:
    std::vector<uint8_t> flags_;
};
} // namespace wowee::rendering
