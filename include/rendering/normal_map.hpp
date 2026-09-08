#pragma once

/**
 * normal_map.hpp - a normal map derived from a diffuse texture.
 *
 * The character renderer and the WMO renderer both build one, from the same
 * eighty lines: luminance to height, a box blur to take the noise off it, then
 * a Sobel pass for the normals with the blurred height kept in alpha for
 * parallax. The two copies differed in exactly one value, the Sobel strength,
 * which is now the parameter it always was.
 *
 * Pure on purpose. Both copies lived inside a renderer class and needed a
 * Vulkan context to reach, so neither had ever been tested; this takes pixels
 * and answers pixels.
 */

#include <cstdint>
#include <vector>

namespace wowee {
namespace rendering {

/// RGBA8 normal map from an RGBA8 diffuse texture.
///
/// The result is width*height*4 bytes: the normal in RGB, and the blurred
/// height in alpha for parallax occlusion mapping. outVariance answers how
/// much height there was to work with, which the callers use to decide whether
/// the map is worth keeping at all.
///
/// strength scales the Sobel gradient. The character renderer uses 5, which
/// suits skin and cloth; the WMO renderer uses 2, which suits stone and wood
/// at the size a wall is seen from.
///
/// Answers an empty vector when there is nothing to read.
std::vector<uint8_t> generateNormalHeightMap(const uint8_t* pixels,
                                             uint32_t width, uint32_t height,
                                             float strength, float& outVariance);

}  // namespace rendering
}  // namespace wowee
