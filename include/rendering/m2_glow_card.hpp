#pragma once

/// Deciding what to do with a batch that is a glow card.
///
/// A lamp's halo, a brazier's flame, an elf building's window bloom: a small
/// quad whose texture is bright in the middle and black at the edges. Drawn as
/// a mesh at any distance it reads as a card; the renderer replaces it with a
/// billboarded sprite instead.
///
/// Three decisions go into that, and the renderer makes all three twice -
/// once while recording the opaque pass and once for the transparent one,
/// three hundred and fifty lines apart, in expressions that were identical
/// down to the constants. A batch is meant to be skipped by exactly one pass
/// and drawn by the other, so the two copies drifting apart does not produce a
/// warning or a crash. It produces a card that is drawn twice, or not at all.

#include <cstdint>

namespace wowee::rendering {

/// What the passes know about one batch and the model it belongs to.
struct M2GlowCardBatch {
    // The batch
    float glowSize = 0.0f;
    uint8_t blendMode = 0;
    bool lanternGlowHint = false;
    bool glowCardLike = false;
    bool colorKeyBlack = false;
    bool unlit = false;
    bool preserveGlowMesh = false;

    // The model it is part of
    bool modelIsElvenLike = false;
    bool modelIsLanternLike = false;
    bool modelIsTorch = false;
    bool modelIsBrazierOrFire = false;
    bool modelIsSpellEffect = false;
    bool modelIsKoboldFlame = false;
};

/// Small enough that a sprite can stand in for the mesh.
///
/// A lantern's own glow is allowed to be larger than a general card, because
/// the fixture it belongs to is bigger than the light it gives off.
inline bool m2IsSmallCardLikeBatch(const M2GlowCardBatch& b) {
    return b.glowSize <= 1.35f || (b.lanternGlowHint && b.glowSize <= 6.0f);
}

/// Should a billboarded sprite be emitted for this batch?
inline bool m2WantsGlowSprite(const M2GlowCardBatch& b) {
    const bool koboldFlameCard = b.colorKeyBlack && b.modelIsKoboldFlame;
    const bool fixtureWithHint =
        (b.modelIsLanternLike || b.modelIsTorch || b.modelIsBrazierOrFire) &&
        b.lanternGlowHint;
    return !koboldFlameCard &&
           (b.modelIsElvenLike || fixtureWithHint) &&
           !b.modelIsSpellEffect &&
           m2IsSmallCardLikeBatch(b) &&
           (b.lanternGlowHint || b.blendMode >= 3 ||
            (b.colorKeyBlack && b.unlit && b.blendMode >= 1));
}

/// Once a sprite stands in for it, should the mesh itself be dropped?
///
/// A model that is a light fixture keeps its mesh unless the card is small
/// enough to be entirely replaced - the fixture is the thing you see, and
/// dropping it leaves a lamp post with no lamp.
inline bool m2GlowSpriteReplacesMesh(const M2GlowCardBatch& b) {
    const bool cardLikeSkipMesh =
        !b.preserveGlowMesh &&
        (b.glowCardLike || b.blendMode >= 3 || b.colorKeyBlack || b.unlit);
    const bool lanternGlowCardSkip =
        (b.modelIsLanternLike || b.modelIsTorch || b.modelIsBrazierOrFire) &&
        b.lanternGlowHint && m2IsSmallCardLikeBatch(b) && cardLikeSkipMesh;
    return lanternGlowCardSkip || (cardLikeSkipMesh && !b.modelIsLanternLike);
}

}  // namespace wowee::rendering
