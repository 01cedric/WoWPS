#pragma once
namespace wowee::rendering {
enum class WMOShadowMaterial { Opaque, Cutout, None };
// Only authored alpha-test or opaque layers write binary shadow depth. Glass
// classification by texture name must not erase an opaque wall atlas.
constexpr WMOShadowMaterial wmoShadowMaterial(bool alphaTest, bool transparent) {
    return alphaTest ? WMOShadowMaterial::Cutout
                    : transparent ? WMOShadowMaterial::None : WMOShadowMaterial::Opaque;
}
}
