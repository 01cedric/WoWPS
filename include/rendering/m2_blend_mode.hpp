#pragma once

/// What an M2 material's blend mode means for the pipeline it draws on.
///
/// A batch whose texture carries no alpha channel used to be forced onto the
/// cutout pipeline whenever its blend mode was 2 or higher. For blend mode 2 -
/// alpha blend - that is reasonable: blending by an alpha that is 1 everywhere
/// is the same as drawing opaque, so cutting out at least gives the artist's
/// silhouette a chance.
///
/// For the additive modes it is wrong, and destroys the effect. Additive does
/// not use alpha for transparency at all - black is what disappears, because
/// adding zero changes nothing. A glow card is authored exactly that way: an
/// opaque texture, bright in the middle, black at the edges.
///
/// Orgrimmar's bonfire is the case that showed it. Its glow is
/// GENERICGLOW_ALPHA_128.BLP - named for an alpha it does not have, alphaDepth
/// 0 in the file - on a material with blend mode 4. Forced to cutout, every
/// texel passed the test and the flame rendered as a flat opaque grey disc
/// sitting behind the logs.

#include <cstdint>

namespace wowee::rendering {

/// M2 material blend modes, as stored.
enum M2BlendMode : uint8_t {
    M2_BLEND_OPAQUE = 0,
    M2_BLEND_ALPHA_KEY = 1,   ///< cutout: alpha tested against a threshold
    M2_BLEND_ALPHA = 2,       ///< blended by the texture's alpha
    M2_BLEND_ADD = 3,         ///< added to what is behind; black is invisible
    M2_BLEND_ADD_ALPHA = 4,   ///< additive, scaled by alpha where there is one
    M2_BLEND_MODULATE = 5,
    M2_BLEND_MODULATE2X = 6,
};

/// True when the mode adds to the framebuffer rather than covering it.
///
/// These never need an alpha channel: black is the transparent colour.
inline bool m2BlendIsAdditive(uint8_t blendMode) {
    return blendMode == M2_BLEND_ADD || blendMode == M2_BLEND_ADD_ALPHA;
}

/// Should this batch be alpha tested?
///
/// Alpha key always is, that being what it means. A blended batch whose
/// texture has no alpha is tested as a fallback, because blending by a missing
/// alpha draws it solid. An additive batch never is: it has nothing to test
/// and does not need one.
inline bool m2BatchNeedsAlphaTest(uint8_t blendMode, bool hasAlpha) {
    if (blendMode == M2_BLEND_ALPHA_KEY) return true;
    if (m2BlendIsAdditive(blendMode)) return false;
    return blendMode >= M2_BLEND_ALPHA && !hasAlpha;
}

// Low three bits retain the authored alpha mode (ordinary/foliage/ground).
// A single-sample target cannot realize fractional alpha-to-coverage: request
// a binary shader test instead. Mode zero must stay zero for unmasked effects.
inline int m2EncodeAlphaTest(int mode, bool singleSample) {
    return mode == 0 ? 0 : mode | (singleSample ? 8 : 0);
}


/// Static draw/material state for one M2 batch.
///
/// All inputs are immutable for the lifetime of a loaded model. Keeping this
/// decision beside the blend-mode rules lets model upload resolve it once,
/// rather than re-running the same branches and rewriting the same mapped UBO
/// fields for every visible LOD group on every frame.
struct M2StaticMaterialState {
    uint8_t effectiveBlendMode = M2_BLEND_OPAQUE;
    uint8_t alphaMode = 0;            ///< pre-single-sample encoding: 0/1/2/3
    bool forceCutout = false;
    int32_t unlit = 0;
    float colorKeyThreshold = 0.08f;
};

inline M2StaticMaterialState m2StaticMaterialState(
    uint8_t blendMode, bool hasAlpha, bool colorKeyBlack, uint16_t materialFlags,
    bool modelIsGroundDetail, bool modelIsFoliageLike, bool modelIsSpellEffect,
    bool batchForgeFireCard) {
    M2StaticMaterialState state{};
    state.effectiveBlendMode = blendMode;

    // Pass selection is fixed too. Spell effects are handled by the blended
    // pass even when the file calls a layer opaque; forge fire cards stay in
    // the first pass but intentionally use the additive pipeline there.
    const bool rawTransparent = blendMode >= M2_BLEND_ALPHA || modelIsSpellEffect;
    if (modelIsSpellEffect || batchForgeFireCard) {
        if (state.effectiveBlendMode <= M2_BLEND_ALPHA_KEY ||
            state.effectiveBlendMode == M2_BLEND_ADD_ALPHA ||
            state.effectiveBlendMode == M2_BLEND_MODULATE) {
            state.effectiveBlendMode = M2_BLEND_ADD;
        }
    }

    if (!rawTransparent) {
        const bool foliageCutout = modelIsFoliageLike && !modelIsSpellEffect &&
                                   blendMode <= M2_BLEND_ADD;
        state.forceCutout = !modelIsSpellEffect && !batchForgeFireCard &&
            !m2BlendIsAdditive(blendMode) &&
            (modelIsGroundDetail || foliageCutout ||
             m2BatchNeedsAlphaTest(blendMode, hasAlpha) || colorKeyBlack);
        if (state.forceCutout) state.effectiveBlendMode = M2_BLEND_ALPHA_KEY;
        state.alphaMode = static_cast<uint8_t>(state.forceCutout
            ? (modelIsGroundDetail ? 3 : (foliageCutout ? 2 : 1))
            : (m2BatchNeedsAlphaTest(blendMode, hasAlpha) ? 1 : 0));
        state.unlit = modelIsGroundDetail && state.forceCutout
            ? 0 : ((materialFlags & 0x01u) ? 1 : 0);
    } else {
        state.alphaMode = m2BatchNeedsAlphaTest(blendMode, hasAlpha) ? 1 : 0;
        state.unlit = (materialFlags & 0x01u) ? 1 : 0;
    }

    if (colorKeyBlack &&
        (state.effectiveBlendMode == M2_BLEND_ADD_ALPHA ||
         state.effectiveBlendMode == M2_BLEND_MODULATE)) {
        state.colorKeyThreshold = 0.7f;
    }
    return state;
}

/// Should this batch have its black keyed out?
///
/// The key discards every texel darker than a threshold, and exists so a card
/// authored with a black backing can be drawn opaquely without the backing
/// showing. An additive batch has no such problem - black adds nothing - and
/// the key actively destroys it: a glow card is a radial gradient from black
/// to white, so discarding everything below the threshold turns a soft falloff
/// into a hard-edged disc. Orgrimmar's bonfires used a threshold of 0.7 on
/// exactly such a card.

inline bool m2BatchWantsColorKey(uint8_t blendMode, bool textureIsKeyed) {
    return textureIsKeyed && !m2BlendIsAdditive(blendMode);
}

}  // namespace wowee::rendering
