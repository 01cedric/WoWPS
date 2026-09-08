#pragma once

#include <cstdint>

namespace wowee::rendering {

// Version 264+ loads one external skin (currently skin00) for the whole model.
// A section's `level` extends its first triangle index; it does not identify
// a different LOD. All sections in that selected skin must draw together.
inline constexpr uint16_t m2SkinSectionLod(uint32_t modelVersion, uint16_t sectionLevel) {
    return modelVersion >= 264 ? 0 : sectionLevel;
}

} // namespace wowee::rendering
