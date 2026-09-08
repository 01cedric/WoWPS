// ============================================================================
// SfxStateDriver - extracted from AnimationController
//
// Tracks state transitions for activity SFX (jump, landing, swim) and
// mount ambient sounds.  Moved from AnimationController::updateSfxState().
// ============================================================================

#include "rendering/animation/sfx_state_driver.hpp"
#include "rendering/animation/footstep_driver.hpp"
#include "rendering/renderer.hpp"
#include "audio/audio_coordinator.hpp"
#include "audio/activity_sound_manager.hpp"
#include "audio/mount_sound_manager.hpp"
#include "audio/music_manager.hpp"
#include "rendering/camera_controller.hpp"

namespace wowee {
namespace rendering {

void SfxStateDriver::update(float deltaTime, Renderer* renderer,
                            bool mounted, bool taxiFlight,
                            FootstepDriver& footstepDriver) {
    auto* activitySoundManager = renderer->getAudioCoordinator()->getActivitySoundManager();
    if (!activitySoundManager) return;

    auto* cameraController = renderer->getCameraController();

    activitySoundManager->update(deltaTime);
    if (cameraController && cameraController->isThirdPerson()) {
        bool grounded = cameraController->isGrounded();
        bool jumping = cameraController->isJumping();
        bool falling = cameraController->isFalling();
        bool swimming = cameraController->isSwimming();
        bool moving = cameraController->isMoving();

        if (!initialized_) {
            prevGrounded_ = grounded;
            prevJumping_ = jumping;
            prevFalling_ = falling;
            prevSwimming_ = swimming;
            initialized_ = true;
        }

        if (!swimming && prevSwimming_) sinceWaterExit_ = 0.0f;
        else if (sinceWaterExit_ < kWaterExitSfxSuppress) sinceWaterExit_ += deltaTime;
        const bool justLeftWater = sinceWaterExit_ < kWaterExitSfxSuppress;

        // Jump detection. Wading ashore lifts the character without a jump, and
        // jumpClips are the character's exertion vocalisations, so a grunt there
        // sounds like it jumped out of the lake.
        if (jumping && !prevJumping_ && !swimming && !justLeftWater) {
            activitySoundManager->playJump();
        }

        // Landing detection. Wading out of water reaches the ground without a
        // fall, and retail marks that with the water-exit splash below rather
        // than a landing thud, so a swim-to-ground transition is not a landing.
        if (grounded && !prevGrounded_ && !justLeftWater) {
            bool hardLanding = prevFalling_;
            activitySoundManager->playLanding(
                footstepDriver.resolveFootstepSurface(renderer), hardLanding);
        }

        // Water transitions
        if (swimming && !prevSwimming_) {
            activitySoundManager->playWaterEnter();
        } else if (!swimming && prevSwimming_) {
            activitySoundManager->playWaterExit();
        }

        activitySoundManager->setSwimmingState(swimming, moving);

        if (renderer->getAudioCoordinator()->getMusicManager()) {
            renderer->getAudioCoordinator()->getMusicManager()->setUnderwaterMode(swimming);
        }

        prevGrounded_ = grounded;
        prevJumping_ = jumping;
        prevFalling_ = falling;
        prevSwimming_ = swimming;
    } else {
        activitySoundManager->setSwimmingState(false, false);
        if (renderer->getAudioCoordinator()->getMusicManager()) {
            renderer->getAudioCoordinator()->getMusicManager()->setUnderwaterMode(false);
        }
        initialized_ = false;
    }

    // Mount ambient sounds
    if (renderer->getAudioCoordinator()->getMountSoundManager()) {
        renderer->getAudioCoordinator()->getMountSoundManager()->update(deltaTime);
        if (cameraController && mounted) {
            bool isMoving = cameraController->isMoving();
            bool flying = taxiFlight || !cameraController->isGrounded();
            renderer->getAudioCoordinator()->getMountSoundManager()->setMoving(isMoving);
            renderer->getAudioCoordinator()->getMountSoundManager()->setFlying(flying);
        }
    }
}

} // namespace rendering
} // namespace wowee
