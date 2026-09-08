// Renamed from player_animator.cpp → character_animator.cpp
// Class renamed: PlayerAnimator → CharacterAnimator
// All animations are now generic (character-based, not player-specific).
#include "rendering/animation/character_animator.hpp"
#include "rendering/animation/animation_ids.hpp"
#include "game/inventory.hpp"

namespace wowee {
namespace rendering {

CharacterAnimator::CharacterAnimator() = default;

// ── IAnimator ────────────────────────────────────────────────────────────────

void CharacterAnimator::onEvent(AnimEvent event) {
    locomotion_.onEvent(event);
    combat_.onEvent(event);
    activity_.onEvent(event);
    mount_.onEvent(event);
}

void CharacterAnimator::update(float dt) {
    lastDt_ = dt;
    lastOutput_ = resolveAnimation();
}

// ── ICharacterAnimator ──────────────────────────────────────────────────────

void CharacterAnimator::startSpellCast(uint32_t precast, uint32_t cast, bool loop, uint32_t finalize) {
    combat_.startSpellCast(precast, cast, loop, finalize);
}

void CharacterAnimator::stopSpellCast() {
    combat_.stopSpellCast();
}

void CharacterAnimator::cancelSpellCast() {
    combat_.cancelSpellCast();
}

void CharacterAnimator::triggerMeleeSwing() {
    // Melee is handled via timer in FrameInput - CombatFSM transitions automatically
}

void CharacterAnimator::triggerRangedShot() {
    // Ranged is handled via timer in FrameInput - CombatFSM transitions automatically
}

void CharacterAnimator::triggerHitReaction(uint32_t animId) {
    combat_.triggerHitReaction(animId);
}

void CharacterAnimator::triggerSpecialAttack(uint32_t /*spellId*/) {
    // Special attack animation is injected via FrameInput::specialAttackAnimId
}

void CharacterAnimator::setEquippedWeaponType(const WeaponLoadout& loadout) {
    loadout_ = loadout;
}

void CharacterAnimator::setEquippedRangedType(RangedWeaponType type) {
    loadout_.rangedType = type;
}

void CharacterAnimator::setRangedWeaponActive(bool active) {
    rangedWeaponActive_ = active;
}

void CharacterAnimator::playEmote(uint32_t animId, bool loop) {
    activity_.startEmote(animId, loop);
}

void CharacterAnimator::cancelEmote() {
    activity_.cancelEmote();
}

void CharacterAnimator::startLooting() {
    activity_.startLooting();
}

void CharacterAnimator::stopLooting() {
    activity_.stopLooting();
}

void CharacterAnimator::setStunned(bool stunned) {
    combat_.setStunned(stunned);
}

void CharacterAnimator::setCharging(bool charging) {
    combat_.setCharging(charging);
}

void CharacterAnimator::setStandState(uint8_t state) {
    activity_.setStandState(state);
}

void CharacterAnimator::setStealthed(bool stealth) {
    stealthed_ = stealth;
}

void CharacterAnimator::setInCombat(bool combat) {
    inCombat_ = combat;
}

void CharacterAnimator::setLowHealth(bool low) {
    lowHealth_ = low;
}

void CharacterAnimator::setSprintAuraActive(bool active) {
    sprintAura_ = active;
}

// ── Mount ────────────────────────────────────────────────────────────────────

void CharacterAnimator::configureMountFSM(const MountFSM::MountAnimSet& anims, bool taxiFlight) {
    mount_.configure(anims, taxiFlight);
}

void CharacterAnimator::clearMountFSM() {
    mount_.clear();
}

// ── Priority resolver ────────────────────────────────────────────────────────

AnimOutput CharacterAnimator::resolveAnimation() {
    const auto& fi = frameInput_;

    // ── Mount takes over everything ─────────────────────────────────────
    if (mount_.isActive()) {
        // MountFSM returns mount-specific output; rider anim is separate
        // For the main character animation, we return MOUNT (or flight variant)
        uint32_t riderAnim = caps_.resolvedMount ? caps_.resolvedMount : anim::MOUNT;
        return AnimOutput::ok(riderAnim, true);
    }

    // ── Build combat input ──────────────────────────────────────────────
    CombatFSM::Input combatIn;
    combatIn.inCombat = inCombat_;
    combatIn.grounded = fi.grounded;
    combatIn.jumping = fi.jumping;
    combatIn.swimming = fi.swimming;
    combatIn.moving = fi.moving;
    combatIn.sprinting = fi.sprinting;
    combatIn.lowHealth = lowHealth_;
    combatIn.rangedWeaponActive = rangedWeaponActive_;
    combatIn.meleeSwingTimer = fi.meleeSwingTimer;
    combatIn.rangedShootTimer = fi.rangedShootTimer;
    combatIn.meleeAnimId = fi.meleeAnimId;
    combatIn.specialAttackAnimId = fi.specialAttackAnimId;
    combatIn.rangedAnimId = fi.rangedAnimId;
    combatIn.currentAnimId = fi.currentAnimId;
    combatIn.currentAnimTime = fi.currentAnimTime;
    combatIn.currentAnimDuration = fi.currentAnimDuration;
    combatIn.haveAnimState = fi.haveAnimState;
    // A character model supporting draw/sheath animations does not mean the
    // character currently has something to draw. Empty-handed combat must go
    // directly to READY_UNARMED / ATTACK_UNARMED. Equipped fist weapons still
    // have inventoryType ONE_HAND and retain their dedicated fist animation path.
    const bool hasDrawableWeapon = loadout_.inventoryType != game::InvType::NON_EQUIP ||
        (rangedWeaponActive_ && loadout_.rangedType != RangedWeaponType::NONE);
    combatIn.hasUnsheathe = hasDrawableWeapon && caps_.resolvedUnsheathe != 0;
    combatIn.hasSheathe = hasDrawableWeapon && caps_.resolvedSheathe != 0;

    // ── Combat FSM (highest priority for non-mount) ─────────────────────
    auto combatOut = combat_.resolve(combatIn, caps_, loadout_);
    if (combatOut.valid) return applyOverlays(combatOut);

    // ── Activity FSM (emote, loot, sit) ─────────────────────────────────
    ActivityFSM::Input actIn;
    actIn.moving = fi.moving;
    actIn.sprinting = fi.sprinting;
    actIn.jumping = fi.jumping;
    actIn.grounded = fi.grounded;
    actIn.swimming = fi.swimming;
    actIn.sitting = fi.sitting;
    actIn.currentAnimId = fi.currentAnimId;
    actIn.currentAnimTime = fi.currentAnimTime;
    actIn.currentAnimDuration = fi.currentAnimDuration;
    actIn.haveAnimState = fi.haveAnimState;

    auto actOut = activity_.resolve(actIn, caps_);
    if (actOut.valid) return actOut;

    // ── Locomotion FSM (lowest priority) ────────────────────────────────
    LocomotionFSM::Input locoIn;
    locoIn.moving = fi.moving;
    locoIn.movingForward = fi.movingForward;
    locoIn.sprinting = fi.sprinting;
    locoIn.movingBackward = fi.movingBackward;
    locoIn.strafeLeft = fi.strafeLeft;
    locoIn.strafeRight = fi.strafeRight;
    locoIn.grounded = fi.grounded;
    locoIn.jumping = fi.jumping;
    locoIn.swimming = fi.swimming;
    locoIn.sitting = fi.sitting;
    locoIn.sprintAura = sprintAura_;
    locoIn.deltaTime = lastDt_;
    // Animation state for one-shot completion detection (jump start/end)
    locoIn.currentAnimId = fi.currentAnimId;
    locoIn.currentAnimTime = fi.currentAnimTime;
    locoIn.currentAnimDuration = fi.currentAnimDuration;
    locoIn.haveAnimState = fi.haveAnimState;

    auto locoOut = locomotion_.resolve(locoIn, caps_);
    if (locoOut.valid) return applyOverlays(locoOut);

    // All FSMs returned invalid → STAY (keep last animation)
    return AnimOutput::stay();
}

// ── Overlay application ──────────────────────────────────────────────────────

AnimOutput CharacterAnimator::applyOverlays(AnimOutput base) const {
    if (!stealthed_) return base;

    // Stealth substitution based on locomotion state
    auto locoState = locomotion_.getState();
    if (locoState == LocomotionFSM::State::IDLE) {
        if (caps_.resolvedStealthIdle) base.animId = caps_.resolvedStealthIdle;
    } else if (locoState == LocomotionFSM::State::WALK) {
        if (caps_.resolvedStealthWalk) base.animId = caps_.resolvedStealthWalk;
    } else if (locoState == LocomotionFSM::State::RUN) {
        if (caps_.resolvedStealthRun) base.animId = caps_.resolvedStealthRun;
        else if (caps_.resolvedStealthWalk) base.animId = caps_.resolvedStealthWalk;
    }

    return base;
}

} // namespace rendering
} // namespace wowee
