#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace wowee::core {
// Small presentation rosters are capped by the realm. Keep their contiguous
// storage across frames instead of allocating a hash node for every actor.
// No gameplay ownership lives here; the realm remains authoritative.
class RetainedGuidSet {
public:
    void clear() noexcept { values_.clear(); }
    void release() noexcept { std::vector<uint64_t>().swap(values_); }
    bool count(uint64_t guid) const noexcept {
        return std::find(values_.begin(), values_.end(), guid) != values_.end();
    }
    void insert(uint64_t guid) {
        if (!count(guid)) values_.push_back(guid);
    }
    void swap(RetainedGuidSet& other) noexcept { values_.swap(other.values_); }
    auto begin() const noexcept { return values_.begin(); }
    auto end() const noexcept { return values_.end(); }
    size_t size() const noexcept { return values_.size(); }
private:
    std::vector<uint64_t> values_;
};
} // namespace wowee::core
