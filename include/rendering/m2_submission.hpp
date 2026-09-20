#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

namespace wowee::rendering {

// M2 distance selection produces exactly four LODs. Counting/scattering is
// linear, preserves order within each LOD, and reuses both vectors' storage.
template<class Entry>
void m2GroupOpaqueLods(std::vector<Entry>& entries, std::vector<Entry>& scratch) {
    std::array<std::size_t, 4> counts{};
    for (const auto& entry : entries) {
        assert(entry.targetLOD < counts.size());
        ++counts[entry.targetLOD];
    }
    // Most props expose only LOD 0; avoid even copying that common case.
    unsigned occupied = 0;
    for (const auto count : counts) occupied += count != 0;
    if (occupied < 2) return;
    std::array<std::size_t, 4> next{};
    for (std::size_t i = 1; i < next.size(); ++i)
        next[i] = next[i - 1] + counts[i - 1];
    scratch.resize(entries.size());
    for (auto& entry : entries)
        scratch[next[entry.targetLOD]++] = std::move(entry);
    entries.swap(scratch);
}

// Scope this to ONE transparent instance inside ONE render call. All instance
// fields other than the UV transform are then invariant (model matrix, fade,
// skinning flags/range). Material/tint/UV-set remain per-batch draw state.
// Never inspect mapped upload memory to detect reusable records.
class M2TransparentRecordReuse {
public:
    bool find(const glm::vec2& offset, const glm::vec4& linear,
              std::uint32_t& slot) const {
        if (!valid_ || offset != offset_ || linear != linear_) return false;
        slot = slot_;
        return true;
    }
    void remember(const glm::vec2& offset, const glm::vec4& linear,
                  std::uint32_t slot) {
        offset_ = offset;
        linear_ = linear;
        slot_ = slot;
        valid_ = true;
    }
private:
    glm::vec2 offset_{0.0f};
    glm::vec4 linear_{1.0f, 0.0f, 0.0f, 1.0f};
    std::uint32_t slot_ = 0;
    bool valid_ = false;
};

} // namespace wowee::rendering
