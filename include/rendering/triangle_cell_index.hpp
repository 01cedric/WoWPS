#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace wowee::rendering {

// A cell index with one exact-sized triangle array. A city WMO can have
// hundreds of 64x64 grids; a vector object per cell consumed more memory than
// the actual collision entries, even for empty cells. Enumeration order is
// preserved, including triangles referenced by several cells.
class TriangleCellIndex {
public:
    template<class Enumerate>
    void build(size_t cells, Enumerate&& enumerate) {
        std::vector<uint32_t> offsets(cells + 1, 0);
        enumerate([&](size_t cell, uint32_t) {
            if (cell >= cells || offsets[cell + 1] == std::numeric_limits<uint32_t>::max())
                throw std::length_error("collision cell index overflow");
            ++offsets[cell + 1];
        });
        uint64_t total = 0;
        for (size_t cell = 0; cell < cells; ++cell) {
            total += offsets[cell + 1];
            if (total > std::numeric_limits<uint32_t>::max())
                throw std::length_error("collision triangle index overflow");
            offsets[cell + 1] = static_cast<uint32_t>(total);
        }
        std::vector<uint32_t> triangles(static_cast<size_t>(total));
        auto cursor = offsets;
        enumerate([&](size_t cell, uint32_t triangle) {
            if (cell >= cells || cursor[cell] >= offsets[cell + 1])
                throw std::length_error("collision enumeration changed");
            triangles[cursor[cell]++] = triangle;
        });
        for (size_t cell = 0; cell < cells; ++cell)
            if (cursor[cell] != offsets[cell + 1])
                throw std::length_error("collision enumeration changed");
        // Commit only after both passes succeed. An allocation failure leaves
        // the previous index intact and safe to query or rebuild.
        offsets_.swap(offsets);
        triangles_.swap(triangles);
    }

    [[nodiscard]] bool empty() const noexcept { return offsets_.size() <= 1; }
    [[nodiscard]] std::span<const uint32_t> operator[](size_t cell) const noexcept {
        return std::span<const uint32_t>(triangles_).subspan(
            offsets_[cell], offsets_[cell + 1] - offsets_[cell]);
    }
    [[nodiscard]] size_t storageBytes() const noexcept {
        return (offsets_.capacity() + triangles_.capacity()) * sizeof(uint32_t);
    }

private:
    std::vector<uint32_t> offsets_;
    std::vector<uint32_t> triangles_;
};

} // namespace wowee::rendering
