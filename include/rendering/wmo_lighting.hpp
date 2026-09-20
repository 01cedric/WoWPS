#pragma once
#include <cstdint>

namespace wowee::rendering {
// WotLK MOGP: 0x8 exterior, 0x40 exterior_lit, 0x2000 indoor.
// Indoor describes spatial classification; it does not override exterior_lit.
// A missing MOCV uses white vertex defaults, which are albedo multipliers,
// never authored full-bright interior lighting.
constexpr bool wmoUsesBakedInteriorLighting(uint32_t flags, bool hasVertexColors) {
    return (flags & 0x2000u) != 0 && (flags & 0x48u) == 0 && hasVertexColors;
}
// MOMT 0=Diffuse, 1=Specular, 2=Metal. Preserve the existing approximation
// only for authored single-texture specular variants; other variants need
// their own texture/shader implementation, not generic wall gloss.
constexpr float wmoMaterialSpecularIntensity(uint32_t shader) {
    return shader == 1u || shader == 2u ? 0.5f : 0.0f;
}
}
