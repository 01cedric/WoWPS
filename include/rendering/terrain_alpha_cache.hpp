#pragma once
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>

namespace wowee::rendering {
// Non-owning, direct-mapped lookup only. TextureCache retains the GPU owner.
// Collisions evict lookup entries; full bytes, never the hash, decide reuse.
template<class Texture, size_t Capacity = 128>
class TerrainAlphaCache {
public:
    static constexpr size_t Bytes = 64 * 64;
    using Mask = std::array<uint8_t, Bytes>;
    static Mask normalize(std::span<const uint8_t> input) {
        Mask result;
        result.fill(255);
        std::copy_n(input.begin(), std::min(input.size(), Bytes), result.begin());
        return result;
    }
    static bool opaque(const Mask& mask) {
        return std::all_of(mask.begin(), mask.end(), [](uint8_t v) { return v == 255; });
    }
    static size_t slot(const Mask& mask) {
        uint64_t hash = 14695981039346656037ull;
        for (uint8_t v : mask) { hash ^= v; hash *= 1099511628211ull; }
        return static_cast<size_t>(hash % Capacity);
    }
    Texture* find(const Mask& mask) {
        if (entries_) {
            const auto& entry = (*entries_)[slot(mask)];
            if (entry.texture && entry.mask == mask) { ++hits; return entry.texture; }
        }
        ++misses;
        return nullptr;
    }
    void remember(const Mask& mask, Texture* texture) {
        if (!texture) return;
        if (!entries_) {
            entries_.reset(new (std::nothrow) Table{});
            if (!entries_) return; // Optional lookup must not make uploads fail.
        }
        auto& entry = (*entries_)[slot(mask)];
        if (entry.texture && entry.mask != mask) ++replacements;
        entry.mask = mask;
        entry.texture = texture;
    }
    // Ownership is elsewhere. Invalidate aliases before their owners retire,
    // retaining the other hot masks and the lookup table allocation.
    template<class Predicate>
    void forgetIf(Predicate shouldForget) {
        if (!entries_) return;
        for (auto& entry : *entries_)
            if (entry.texture && shouldForget(entry.texture)) entry.texture = nullptr;
    }
    void clear() { entries_.reset(); hits = misses = replacements = 0; }
    size_t allocatedBytes() const { return entries_ ? sizeof(Table) : 0; }
    uint64_t hits = 0, misses = 0, replacements = 0;
private:
    struct Entry { Mask mask{}; Texture* texture = nullptr; };
    using Table = std::array<Entry, Capacity>;
    std::unique_ptr<Table> entries_;
};
} // namespace wowee::rendering
