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
