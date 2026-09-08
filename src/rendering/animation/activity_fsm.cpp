#include "rendering/animation/activity_fsm.hpp"
#include "rendering/animation/animation_ids.hpp"

namespace wowee {
namespace rendering {

// ── One-shot completion helper ───────────────────────────────────────────────

bool ActivityFSM::oneShotComplete(const Input& in, uint32_t expectedAnimId) const {
    if (!in.haveAnimState) return false;
    return in.currentAnimId != expectedAnimId ||
           (in.currentAnimDuration > 0.1f && in.currentAnimTime >= in.currentAnimDuration - 0.05f);
}

// ── Event handling ───────────────────────────────────────────────────────────

void ActivityFSM::onEvent(AnimEvent event) {
    switch (event) {
        case AnimEvent::EMOTE_START:
            // Handled by startEmote() with animId
            break;
        case AnimEvent::EMOTE_STOP:
            cancelEmote();
            break;
        case AnimEvent::LOOT_START:
            startLooting();
            break;
        case AnimEvent::LOOT_STOP:
            stopLooting();
            break;
        case AnimEvent::SIT:
            // Handled by setStandState()
            break;
        case AnimEvent::STAND_UP:
            if (state_ == State::SITTING || state_ == State::SIT_DOWN) {
                if (sitUpAnim_ != 0)
                    state_ = State::SIT_UP;
                else
                    state_ = State::NONE;
            }
            break;
        case AnimEvent::MOVE_START:
        case AnimEvent::JUMP:
        case AnimEvent::SWIM_ENTER:
            // Movement cancels all activities
            if (state_ == State::EMOTE) cancelEmote();
            if (state_ == State::LOOTING || state_ == State::LOOT_KNEELING || state_ == State::LOOT_END)
                state_ = State::NONE;
            if (state_ == State::SIT_UP || state_ == State::SIT_DOWN) {
                state_ = State::NONE;
                standState_ = 0;
            }
            break;
        default:
            break;
    }
}

// ── Emote management ─────────────────────────────────────────────────────────

void ActivityFSM::startEmote(uint32_t animId, bool loop) {
    emoteActive_ = true;
    emoteAnimId_ = animId;
    emoteLoop_ = loop;
    state_ = State::EMOTE;
}

void ActivityFSM::cancelEmote() {
    emoteActive_ = false;
    emoteAnimId_ = 0;
    emoteLoop_ = false;
    if (state_ == State::EMOTE) state_ = State::NONE;
}

// ── Sit/sleep/kneel ──────────────────────────────────────────────────────────

void ActivityFSM::setStandState(uint8_t standState) {
    if (standState == standState_) return;
    standState_ = standState;

    if (standState == STAND_STATE_STAND) {
        // Standing up - exit via SIT_UP if we have an exit animation
        return;
    }

    if (standState == STAND_STATE_SIT) {
        sitDownAnim_ = anim::SIT_GROUND_DOWN;
        sitLoopAnim_ = seatedLoopOverride_ != 0 ? seatedLoopOverride_ : anim::SITTING;
        sitUpAnim_   = anim::SIT_GROUND_UP;
        sitDownAnimSeen_ = false;
        sitDownFrames_ = 0;
        state_ = State::SIT_DOWN;
    } else if (standState == STAND_STATE_SLEEP) {
        sitDownAnim_ = anim::SLEEP_DOWN;
        sitLoopAnim_ = anim::SLEEP;
        sitUpAnim_   = anim::SLEEP_UP;
        sitDownAnimSeen_ = false;
        sitDownFrames_ = 0;
        state_ = State::SIT_DOWN;
    } else if (standState == STAND_STATE_KNEEL) {
        sitDownAnim_ = anim::KNEEL_START;
        sitLoopAnim_ = anim::KNEEL_LOOP;
        sitUpAnim_   = anim::KNEEL_END;
        sitDownAnimSeen_ = false;
        sitDownFrames_ = 0;
        state_ = State::SIT_DOWN;
    } else if (standState >= STAND_STATE_SIT_CHAIR && standState <= STAND_STATE_SIT_HIGH) {
        // Chair variants - no transition animation, go directly to loop
        sitDownAnim_ = 0;
        sitUpAnim_   = 0;
        if (standState == STAND_STATE_SIT_LOW) {
            sitLoopAnim_ = anim::SIT_CHAIR_LOW;
        } else if (standState == STAND_STATE_SIT_HIGH) {
            sitLoopAnim_ = anim::SIT_CHAIR_HIGH;
        } else {
            sitLoopAnim_ = anim::SIT_CHAIR_MED;
        }
        state_ = State::SITTING;
    } else if (standState == STAND_STATE_DEAD) {
        sitDownAnim_ = 0;
        sitLoopAnim_ = 0;
        sitUpAnim_   = 0;
        return;
    }
}

void ActivityFSM::setSeatedLoopAnimation(uint32_t animationId) {
    seatedLoopOverride_ = animationId;
    if (standState_ == STAND_STATE_SIT) {
        sitLoopAnim_ = animationId != 0 ? animationId : anim::SITTING;
    }
}

// ── Loot management ──────────────────────────────────────────────────────────

void ActivityFSM::startLooting() {
    state_ = State::LOOTING;
    lootAnimSeen_ = false;
    lootFrames_ = 0;
}

void ActivityFSM::stopLooting() {
    if (state_ == State::LOOTING || state_ == State::LOOT_KNEELING) {
        state_ = State::LOOT_END;
        lootEndAnimSeen_ = false;
        lootEndFrames_ = 0;
    }
}

// ── State transitions ────────────────────────────────────────────────────────

void ActivityFSM::updateTransitions(const Input& in) {
    switch (state_) {
        case State::NONE:
            break;

        case State::EMOTE:
            if (in.swimming || (in.jumping && !in.grounded) || in.moving || in.sitting) {
                cancelEmote();
            } else if (!emoteLoop_ && in.haveAnimState) {
                if (oneShotComplete(in, emoteAnimId_)) {
                    cancelEmote();
                }
            }
            break;

        case State::LOOTING:
            if (in.swimming || (in.jumping && !in.grounded) || in.moving) {
                state_ = State::NONE;
            } else if (in.haveAnimState) {
                if (in.currentAnimId == anim::LOOT) lootAnimSeen_ = true;
                if (lootAnimSeen_ && oneShotComplete(in, anim::LOOT)) {
                    state_ = State::LOOT_KNEELING;
                }
                // Safety: if anim never seen (model lacks it), advance after a timeout
                if (!lootAnimSeen_ && ++lootFrames_ > 10) {
                    state_ = State::LOOT_KNEELING;
                }
            }
            break;

        case State::LOOT_KNEELING:
            if (in.swimming || (in.jumping && !in.grounded) || in.moving) {
                state_ = State::NONE;
            }
            // Stays in LOOT_KNEELING until stopLooting() transitions to LOOT_END
            break;

        case State::LOOT_END:
            if (in.swimming || (in.jumping && !in.grounded) || in.moving) {
                state_ = State::NONE;
            } else if (in.haveAnimState) {
                if (in.currentAnimId == anim::KNEEL_END) lootEndAnimSeen_ = true;
                if (lootEndAnimSeen_ && oneShotComplete(in, anim::KNEEL_END)) {
                    state_ = State::NONE;
                }
                // Safety timeout
                if (!lootEndAnimSeen_ && ++lootEndFrames_ > 10) {
                    state_ = State::NONE;
                }
            }
            break;

        case State::SIT_DOWN:
            if (in.swimming) {
                state_ = State::NONE;
                standState_ = 0;
            } else if (!in.sitting) {
                // Stand up requested
                if (sitUpAnim_ != 0 && !in.moving) {
                    sitUpAnimSeen_ = false;
                    sitUpFrames_ = 0;
                    state_ = State::SIT_UP;
                } else {
                    state_ = State::NONE;
                    standState_ = 0;
                }
            } else if (sitDownAnim_ != 0 && in.haveAnimState) {
                // Track whether the sit-down anim has started playing
                if (in.currentAnimId == sitDownAnim_) sitDownAnimSeen_ = true;
                // Only detect completion after the anim has been seen at least once
                if (sitDownAnimSeen_ && oneShotComplete(in, sitDownAnim_)) {
                    state_ = State::SITTING;
                }
                // Safety: if animation was never seen after enough frames (model
                // may lack it), fall through to the sitting loop.
                if (!sitDownAnimSeen_ && ++sitDownFrames_ > 10) {
                    state_ = State::SITTING;
                }
            }
            break;

        case State::SITTING:
            if (in.swimming) {
                state_ = State::NONE;
                standState_ = 0;
            } else if (!in.sitting) {
                if (sitUpAnim_ != 0 && !in.moving) {
                    sitUpAnimSeen_ = false;
                    sitUpFrames_ = 0;
                    state_ = State::SIT_UP;
                } else {
                    state_ = State::NONE;
                    standState_ = 0;
                }
            }
            break;

        case State::SIT_UP:
            if (in.swimming || in.moving) {
                state_ = State::NONE;
                standState_ = 0;
            } else if (in.haveAnimState) {
                uint32_t expected = sitUpAnim_ ? sitUpAnim_ : anim::SIT_GROUND_UP;
                // Track whether the sit-up anim has started playing
                if (in.currentAnimId == expected) sitUpAnimSeen_ = true;
                // Only detect completion after the anim has been seen at least once
                if (sitUpAnimSeen_ && oneShotComplete(in, expected)) {
                    state_ = State::NONE;
                    standState_ = 0;
                }
                // Safety: if animation was never seen after enough frames, finish
                if (!sitUpAnimSeen_ && ++sitUpFrames_ > 10) {
                    state_ = State::NONE;
                    standState_ = 0;
                }
            }
            break;
    }
}

// ── Animation resolution ─────────────────────────────────────────────────────

AnimOutput ActivityFSM::resolve(const Input& in, const AnimCapabilitySet& /*caps*/) {
    updateTransitions(in);

    if (state_ == State::NONE) return AnimOutput::stay();

    uint32_t animId = 0;
    bool loop = true;

    switch (state_) {
        case State::NONE:
            return AnimOutput::stay();

        case State::EMOTE:
            animId = emoteAnimId_;
            loop = emoteLoop_;
            break;

        case State::LOOTING:
            animId = anim::LOOT;
            loop = false;
            break;

        case State::LOOT_KNEELING:
            animId = anim::KNEEL_LOOP;
            loop = true;
            break;

        case State::LOOT_END:
            animId = anim::KNEEL_END;
            loop = false;
            break;

        case State::SIT_DOWN:
            animId = sitDownAnim_ ? sitDownAnim_ : anim::SIT_GROUND_DOWN;
            loop = false;
            break;

        case State::SITTING:
            animId = sitLoopAnim_ ? sitLoopAnim_ : anim::SITTING;
            loop = true;
            break;

        case State::SIT_UP:
            animId = sitUpAnim_ ? sitUpAnim_ : anim::SIT_GROUND_UP;
            loop = false;
            break;
    }

    if (animId == 0) return AnimOutput::stay();
    return AnimOutput::ok(animId, loop);
}

// ── Reset ────────────────────────────────────────────────────────────────────

void ActivityFSM::reset() {
    state_ = State::NONE;
    cancelEmote();
    standState_ = 0;
    sitDownAnim_ = 0;
    sitLoopAnim_ = 0;
    seatedLoopOverride_ = 0;
    sitUpAnim_ = 0;
    sitDownAnimSeen_ = false;
    sitUpAnimSeen_ = false;
    sitDownFrames_ = 0;
    sitUpFrames_ = 0;
    lootAnimSeen_ = false;
    lootFrames_ = 0;
    lootEndAnimSeen_ = false;
    lootEndFrames_ = 0;
}

} // namespace rendering
} // namespace wowee
