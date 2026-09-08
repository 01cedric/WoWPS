#include "rendering/normal_map.hpp"

#include <algorithm>
#include <cmath>

namespace wowee {
namespace rendering {

std::vector<uint8_t> generateNormalHeightMap(const uint8_t* pixels,
                                             uint32_t width, uint32_t height,
                                             float strength, float& outVariance) {
    outVariance = 0.0f;
    if (!pixels || width == 0 || height == 0) return {};

    const uint32_t totalPixels = width * height;

    // Step 1: Compute height from luminance
    constexpr float kInv255 = 1.0f / 255.0f;
    std::vector<float> heightMap(totalPixels);
    double sumH = 0.0, sumH2 = 0.0;
    for (uint32_t i = 0; i < totalPixels; i++) {
        float r = pixels[i * 4 + 0] * kInv255;
        float g = pixels[i * 4 + 1] * kInv255;
        float b = pixels[i * 4 + 2] * kInv255;
        float h = 0.299f * r + 0.587f * g + 0.114f * b;
        heightMap[i] = h;
        sumH += h;
        sumH2 += static_cast<double>(h) * static_cast<double>(h);
    }
    double mean = sumH / totalPixels;
    outVariance = static_cast<float>(sumH2 / totalPixels - mean * mean);

    // Step 1.5: Box blur the height map to reduce noise from diffuse textures
    auto wrapSample = [&](const std::vector<float>& map, int x, int y) -> float {
        x = ((x % static_cast<int>(width)) + static_cast<int>(width)) % static_cast<int>(width);
        y = ((y % static_cast<int>(height)) + static_cast<int>(height)) % static_cast<int>(height);
        return map[y * width + x];
    };

    std::vector<float> blurredHeight(totalPixels);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            int ix = static_cast<int>(x), iy = static_cast<int>(y);
            float sum = 0.0f;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    sum += wrapSample(heightMap, ix + dx, iy + dy);
            blurredHeight[y * width + x] = sum / 9.0f;
        }
    }

    // Step 2: Sobel 3x3 -> normal map
    // Use ORIGINAL height for normals (crisp detail), blurred height for POM alpha only
    std::vector<uint8_t> output(totalPixels * 4);

    auto sampleH = [&](int x, int y) -> float {
        x = ((x % static_cast<int>(width)) + static_cast<int>(width)) % static_cast<int>(width);
        y = ((y % static_cast<int>(height)) + static_cast<int>(height)) % static_cast<int>(height);
        return heightMap[y * width + x];
    };

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            int ix = static_cast<int>(x);
            int iy = static_cast<int>(y);
            // Sobel X
            float gx = -sampleH(ix-1, iy-1) - 2.0f*sampleH(ix-1, iy) - sampleH(ix-1, iy+1)
                       + sampleH(ix+1, iy-1) + 2.0f*sampleH(ix+1, iy) + sampleH(ix+1, iy+1);
            // Sobel Y
            float gy = -sampleH(ix-1, iy-1) - 2.0f*sampleH(ix, iy-1) - sampleH(ix+1, iy-1)
                       + sampleH(ix-1, iy+1) + 2.0f*sampleH(ix, iy+1) + sampleH(ix+1, iy+1);

            float nx = -gx * strength;
            float ny = -gy * strength;
            float nz = 1.0f;
            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 0.0f) { nx /= len; ny /= len; nz /= len; }

            uint32_t idx = (y * width + x) * 4;
            output[idx + 0] = static_cast<uint8_t>(std::clamp((nx * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            output[idx + 1] = static_cast<uint8_t>(std::clamp((ny * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            output[idx + 2] = static_cast<uint8_t>(std::clamp((nz * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            output[idx + 3] = static_cast<uint8_t>(std::clamp(blurredHeight[y * width + x] * 255.0f, 0.0f, 255.0f));
        }
    }
    return output;
}

}  // namespace rendering
}  // namespace wowee
