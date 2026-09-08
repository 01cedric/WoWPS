#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace wowee::rendering {

struct TextureUploadLevel {
    uint64_t offset;
    uint64_t bytes;
};

// Validate source byte ranges before either the CPU copy or GPU upload. Every
// level begins at a 4-byte boundary (also a block boundary for BC textures).
inline bool textureUploadLayout(uint32_t width, uint32_t height,
                               uint32_t blockWidth, uint32_t blockHeight,
                               uint32_t blockBytes, const uint8_t* const* data,
                               const uint32_t* sizes, uint32_t count,
                               std::vector<TextureUploadLevel>& levels,
                               uint64_t& total) {
    levels.clear();
    total = 0;
    if (!width || !height || width > 16384 || height > 16384 ||
        !blockWidth || !blockHeight || !blockBytes || !data || !sizes || !count)
        return false;
    uint32_t maxLevels = 1;
    for (uint32_t size = std::max(width, height); size > 1; size /= 2)
        ++maxLevels;
    if (count > maxLevels) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t required = ((uint64_t(width) + blockWidth - 1) / blockWidth) *
            ((uint64_t(height) + blockHeight - 1) / blockHeight) * blockBytes;
        if (!data[i] || sizes[i] < required) {
            levels.clear();
            total = 0;
            return false;
        }
        total = (total + 3) & ~uint64_t(3);
        levels.push_back({total, required});
        total += required;
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
    return true;
}

} // namespace wowee::rendering
