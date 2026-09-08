#include "rendering/animation/footstep_driver.hpp"
#include "rendering/renderer.hpp"
#include "rendering/camera_controller.hpp"
#include "rendering/character_renderer.hpp"
#include "rendering/terrain_manager.hpp"
#include "rendering/wmo_renderer.hpp"
#include "rendering/water_renderer.hpp"
#include "rendering/swim_effects.hpp"
#include "rendering/footprint_renderer.hpp"
#include "audio/audio_coordinator.hpp"
#include "audio/footstep_manager.hpp"
#include "audio/mount_sound_manager.hpp"
#include "audio/movement_sound_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>

namespace wowee {
namespace rendering {

// ── Footstep event detection (moved from AnimationController) ────────────────

namespace {

bool containsAnyToken(const std::string& text, std::initializer_list<const char*> tokens) {
    for (const char* token : tokens) {
        if (text.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

FootprintFallback fallbackForMount(audio::MountFamily family) {
    switch (family) {
        case audio::MountFamily::HORSE:
        case audio::MountFamily::UNDEAD_HORSE:
        case audio::MountFamily::RAM:
        case audio::MountFamily::MECHANOSTRIDER:
            return FootprintFallback::HOOF;
        case audio::MountFamily::WOLF:
        case audio::MountFamily::TIGER:
            return FootprintFallback::PAW;
        case audio::MountFamily::RAPTOR:
        case audio::MountFamily::TALLSTRIDER:
        case audio::MountFamily::DRAGON:
            return FootprintFallback::CLAW;
        case audio::MountFamily::KODO:
            return FootprintFallback::CLOVEN;
        case audio::MountFamily::UNKNOWN:
            return FootprintFallback::BIPED;
    }
    return FootprintFallback::BIPED;
}

void spawnFootprint(Renderer* renderer, CharacterRenderer* characterRenderer,
                    uint32_t instanceId, bool leftFoot, FootprintFallback fallback) {
    auto* footprints = renderer->getFootprintRenderer();
    if (!footprints || !characterRenderer || instanceId == 0) return;
    std::string modelName;
    characterRenderer->getInstanceModelName(instanceId, modelName);
    footprints->spawn(modelName, renderer->getCharacterPosition(),
                      glm::radians(renderer->getCharacterYaw()), leftFoot, fallback);
}

} // namespace

bool FootstepDriver::shouldTriggerFootstepEvent(uint32_t animationId, float animationTimeMs, float animationDurationMs,
                                                const std::vector<uint32_t>* eventTimesMs) {
    if (animationDurationMs <= 1.0f) {
        footstepNormInitialized_ = false;
        return false;
    }

    float wrappedTime = animationTimeMs;
    while (wrappedTime >= animationDurationMs) {
        wrappedTime -= animationDurationMs;
    }
    if (wrappedTime < 0.0f) wrappedTime += animationDurationMs;
    float norm = wrappedTime / animationDurationMs;

    if (animationId != footstepLastAnimationId_) {
        footstepLastAnimationId_ = animationId;
        footstepLastNormTime_ = norm;
        footstepNormInitialized_ = true;
        return false;
    }

    if (!footstepNormInitialized_) {
        footstepNormInitialized_ = true;
        footstepLastNormTime_ = norm;
        return false;
    }

    auto crossed = [&](float eventNorm) {
        if (footstepLastNormTime_ <= norm) {
            return footstepLastNormTime_ < eventNorm && eventNorm <= norm;
        }
        return footstepLastNormTime_ < eventNorm || eventNorm <= norm;
    };

    bool trigger = false;
    if (eventTimesMs && !eventTimesMs->empty()) {
        // Authored $FSD footfall keyframes from the M2 - exact foot-strike sync.
        for (uint32_t t : *eventTimesMs) {
            float eventNorm = std::min(static_cast<float>(t) / animationDurationMs, 0.999f);
            if (crossed(eventNorm)) {
                trigger = true;
                break;
            }
        }
    } else {
        trigger = crossed(0.22f) || crossed(0.72f);
    }
    footstepLastNormTime_ = norm;
    return trigger;
}

audio::FootstepSurface FootstepDriver::resolveFootstepSurface(Renderer* renderer) const {
    auto* cameraController = renderer->getCameraController();
    if (!cameraController || !cameraController->isThirdPerson()) {
        return audio::FootstepSurface::STONE;
    }

    const glm::vec3& p = renderer->getCharacterPosition();

    float distSq = glm::dot(p - cachedFootstepPosition_, p - cachedFootstepPosition_);
    if (distSq < 2.25f && cachedFootstepUpdateTimer_ < 0.5f) {
        return cachedFootstepSurface_;
    }

    cachedFootstepPosition_ = p;
    cachedFootstepUpdateTimer_ = 0.0f;

    if (cameraController->isSwimming()) {
        cachedFootstepSurface_ = audio::FootstepSurface::WATER;
        return audio::FootstepSurface::WATER;
    }

    auto* waterRenderer = renderer->getWaterRenderer();
    if (waterRenderer) {
        auto waterH = waterRenderer->getWaterHeightAt(p.x, p.y);
        if (waterH && p.z < (*waterH + 0.25f)) {
            cachedFootstepSurface_ = audio::FootstepSurface::WATER;
            return audio::FootstepSurface::WATER;
        }
    }

    auto* wmoRenderer = renderer->getWMORenderer();
    auto* terrainManager = renderer->getTerrainManager();
    if (wmoRenderer) {
        auto wmoFloor = wmoRenderer->getFloorHeight(p.x, p.y, p.z + 1.5f);
        auto terrainFloor = terrainManager ? terrainManager->getHeightAt(p.x, p.y) : std::nullopt;
        const bool standingOnWmo = wmoFloor && std::abs(*wmoFloor - p.z) <= 2.5f;
        if (standingOnWmo && (cameraController->isInsideWMO() ||
                             !terrainFloor || *wmoFloor >= *terrainFloor - 0.1f)) {
            cachedFootstepSurface_ = audio::FootstepSurface::STONE;
            return audio::FootstepSurface::STONE;
        }
    }

    audio::FootstepSurface surface = audio::FootstepSurface::STONE;

    if (terrainManager) {
        auto texture = terrainManager->getDominantTextureAt(p.x, p.y);
        if (texture) {
            std::string t = *texture;
            for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (containsAnyToken(t, {"snow", "ice"})) surface = audio::FootstepSurface::SNOW;
            else if (containsAnyToken(t, {"farm", "field", "soil", "plow", "crop", "wheat", "dirt", "mud", "sand"})) surface = audio::FootstepSurface::DIRT;
            else if (containsAnyToken(t, {"grass", "moss", "leaf"})) surface = audio::FootstepSurface::GRASS;
            else if (containsAnyToken(t, {"wood", "timber"})) surface = audio::FootstepSurface::WOOD;
            else if (containsAnyToken(t, {"metal", "iron"})) surface = audio::FootstepSurface::METAL;
            else if (containsAnyToken(t, {"stone", "rock", "cobble", "brick"})) surface = audio::FootstepSurface::STONE;
        }
    }

    cachedFootstepSurface_ = surface;
    return surface;
}

void FootstepDriver::playWaterStepExtras(Renderer* renderer) const {
    if (renderer->getAudioCoordinator()->getMovementSoundManager()) {
        renderer->getAudioCoordinator()->getMovementSoundManager()->playWaterFootstep(audio::MovementSoundManager::CharacterSize::MEDIUM);
    }
    auto* swimEffects = renderer->getSwimEffects();
    auto* waterRenderer = renderer->getWaterRenderer();
    if (swimEffects && waterRenderer) {
        const glm::vec3& characterPosition = renderer->getCharacterPosition();
        auto wh = waterRenderer->getWaterHeightAt(characterPosition.x, characterPosition.y);
        if (wh) {
            swimEffects->spawnFootSplash(characterPosition, *wh);
        }
    }
}

// ── Footstep update (moved from AnimationController::updateFootsteps) ────────

void FootstepDriver::update(float deltaTime, Renderer* renderer,
                             bool mounted, uint32_t mountInstanceId, bool taxiFlight,
                             bool isFootstepState) {
    auto* footstepManager = renderer->getAudioCoordinator()->getFootstepManager();
    if (!footstepManager) return;

    auto* characterRenderer = renderer->getCharacterRenderer();
    auto* cameraController = renderer->getCameraController();
    uint32_t characterInstanceId = renderer->getCharacterInstanceId();

    footstepManager->update(deltaTime);
    cachedFootstepUpdateTimer_ += deltaTime;

    bool canPlayFootsteps = characterRenderer && characterInstanceId > 0 &&
        cameraController && cameraController->isThirdPerson() &&
        cameraController->isGrounded() && !cameraController->isSwimming();

    if (canPlayFootsteps && mounted && mountInstanceId > 0 && !taxiFlight) {
        // Mount footsteps: use mount's animation for timing
        uint32_t animId = 0;
        float animTimeMs = 0.0f, animDurationMs = 0.0f;
        if (characterRenderer->getAnimationState(mountInstanceId, animId, animTimeMs, animDurationMs) &&
            animDurationMs > 1.0f && cameraController->isMoving()) {
            float wrappedTime = animTimeMs;
            while (wrappedTime >= animDurationMs) {
                wrappedTime -= animDurationMs;
            }
            if (wrappedTime < 0.0f) wrappedTime += animDurationMs;
            float norm = wrappedTime / animDurationMs;

            if (animId != mountFootstepLastAnimId_) {
                mountFootstepLastAnimId_ = animId;
                mountFootstepLastNormTime_ = norm;
                mountFootstepNormInitialized_ = true;
            } else if (!mountFootstepNormInitialized_) {
                mountFootstepNormInitialized_ = true;
                mountFootstepLastNormTime_ = norm;
            } else {
                auto crossed = [&](float eventNorm) {
                    if (mountFootstepLastNormTime_ <= norm) {
                        return mountFootstepLastNormTime_ < eventNorm && eventNorm <= norm;
                    }
                    return mountFootstepLastNormTime_ < eventNorm || eventNorm <= norm;
                };

                auto* mountSounds = renderer->getAudioCoordinator()->getMountSoundManager();
                const auto family = mountSounds
                    ? mountSounds->getCurrentMountFamily()
                    : audio::MountFamily::UNKNOWN;

                // Bank: hooves for horse-like mounts, heavy thuds for kodos,
                // softened character steps for padded paws and everything else.
                audio::FootstepBank bank = audio::FootstepBank::CHARACTER;
                switch (family) {
                    case audio::MountFamily::HORSE:
                    case audio::MountFamily::UNDEAD_HORSE:
                    case audio::MountFamily::RAM:
                        bank = audio::FootstepBank::HORSE;
                        break;
                    case audio::MountFamily::KODO:
                        bank = audio::FootstepBank::HEAVY;
                        break;
                    default:
                        break;
                }

                // Beat timing: prefer the model's authored $FSD footfall
                // keyframes (exact hoof-strike sync). Models without them fall
                // back to a synthetic gait: quadrupeds gallop - four beats
                // clustered in the first half of the stride, then airborne
                // suspension; two-legged striders alternate biped steps.
                bool beat = false;
                const std::vector<uint32_t>* eventTimes =
                    characterRenderer->getFootstepEventTimes(mountInstanceId);
                if (eventTimes && !eventTimes->empty()) {
                    for (uint32_t t : *eventTimes) {
                        float eventNorm = std::min(static_cast<float>(t) / animDurationMs, 0.999f);
                        if (crossed(eventNorm)) {
                            beat = true;
                            break;
                        }
                    }
                } else {
                    const bool biped = family == audio::MountFamily::MECHANOSTRIDER ||
                                       family == audio::MountFamily::TALLSTRIDER ||
                                       family == audio::MountFamily::DRAGON;
                    beat = biped
                        ? (crossed(0.25f) || crossed(0.75f))
                        : (crossed(0.05f) || crossed(0.20f) || crossed(0.35f) || crossed(0.50f));
                }

                if (beat) {
                    const auto surface = resolveFootstepSurface(renderer);
                    footstepManager->playMountFootstep(surface, bank);
                    if (surface == audio::FootstepSurface::WATER) {
                        playWaterStepExtras(renderer);
                    } else {
                        spawnFootprint(renderer, characterRenderer, mountInstanceId,
                                       nextMountFootLeft_, fallbackForMount(family));
                        nextMountFootLeft_ = !nextMountFootLeft_;
                    }
                }
                mountFootstepLastNormTime_ = norm;
            }
        } else {
            mountFootstepNormInitialized_ = false;
        }
        footstepNormInitialized_ = false;
    } else if (canPlayFootsteps && isFootstepState) {
        uint32_t animId = 0;
        float animTimeMs = 0.0f;
        float animDurationMs = 0.0f;
        if (characterRenderer->getAnimationState(characterInstanceId, animId, animTimeMs, animDurationMs) &&
            shouldTriggerFootstepEvent(animId, animTimeMs, animDurationMs,
                                       characterRenderer->getFootstepEventTimes(characterInstanceId))) {
            auto surface = resolveFootstepSurface(renderer);
            footstepManager->playFootstep(surface, cameraController->isSprinting());
            if (surface == audio::FootstepSurface::WATER) {
                playWaterStepExtras(renderer);
            } else {
                spawnFootprint(renderer, characterRenderer, characterInstanceId,
                               nextPlayerFootLeft_, FootprintFallback::BIPED);
                nextPlayerFootLeft_ = !nextPlayerFootLeft_;
            }
        }
        mountFootstepNormInitialized_ = false;
    } else {
        footstepNormInitialized_ = false;
        mountFootstepNormInitialized_ = false;
    }
}

} // namespace rendering
} // namespace wowee
