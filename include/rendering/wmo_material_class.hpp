#pragma once

/// WMO material classification for the optional glass effect.
///
/// Names and WINDOW flags identify candidate textures. They do not determine
/// opacity: wmoMaterialUsesGlassPass also requires authored alpha blending.
/// Opaque atlases often include both walls and painted window details, so
/// applying glass solely from the name makes whole facades translucent.
/// The named stone exception is retained for transparent variants that share
/// the ruined tower texture.

#include <cstdint>
#include <string>

namespace wowee::rendering {

/// MOMT material flags, as far as the glass path cares.
enum WmoMaterialFlags : uint32_t {
    WMO_MAT_UNLIT    = 0x01,
    WMO_MAT_UNFOGGED = 0x02,
    WMO_MAT_UNCULLED = 0x04,
    WMO_MAT_EXTLIGHT = 0x08,
    WMO_MAT_SIDN     = 0x10,  ///< night glow: windows and lamps
    WMO_MAT_WINDOW   = 0x20,  ///< marked a window by the artist
};

/// True when the texture path names a window or glass surface.
inline bool wmoTextureNamedGlass(const std::string& texturePath) {
    std::string lower = texturePath;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c += 32;
    }
    return lower.find("window") != std::string::npos ||
           lower.find("glass") != std::string::npos;
}

/// Textures that carry a window in their name and are not glass.
///
/// Matched on the file name, so a path in any case and with either separator
/// finds them.
inline bool wmoTextureIsPaintedWall(const std::string& texturePath) {
    static const char* kPaintedWalls[] = {
        // The ruined guard towers of Tirisfal and Elwynn, and the damaged one.
        // MOMT flags 0x0, blend 0, and the BLP has no alpha channel: the whole
        // texture is stone, and the windows in it are holes.
        "tower_window_01.blp",
    };
    std::string lower = texturePath;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c == '\\') c = '/';
    }
    const size_t slash = lower.find_last_of('/');
    const std::string file =
        slash == std::string::npos ? lower : lower.substr(slash + 1);
    for (const char* wall : kPaintedWalls) {
        if (file == wall) return true;
    }
    return false;
}

/// Should this material be drawn as glass?
///
/// The name decides, as it did before, minus the textures named above. A
/// material the artist marked F_WINDOW is glass whatever it is called: five
/// materials in the shipped WMOs carry that flag and a texture name the test
/// above would miss.
inline bool wmoMaterialIsGlass(uint32_t materialFlags,
                               const std::string& texturePath) {
    if (wmoTextureIsPaintedWall(texturePath)) return false;
    if ((materialFlags & WMO_MAT_WINDOW) != 0) return true;
    return wmoTextureNamedGlass(texturePath);
}

/// Only authored alpha-blended surfaces may use the optional glass effect.
/// A window texture can include an entire opaque facade. Its name must never
/// turn the wall into translucent geometry or disable alpha-test cutouts.
/// Other blend modes retain their own material path.
inline bool wmoMaterialUsesGlassPass(uint32_t materialFlags,
                                     const std::string& texturePath,
                                     uint32_t blendMode) {
    return blendMode == 2 && wmoMaterialIsGlass(materialFlags, texturePath);
}

}  // namespace wowee::rendering
