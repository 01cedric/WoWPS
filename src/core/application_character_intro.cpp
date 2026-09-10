#include "core/application.hpp"
#include "core/character_intro.hpp"
#include "core/coordinates.hpp"
#include "core/logger.hpp"
#include "core/world_loader.hpp"
#include "audio/audio_engine.hpp"
#include "game/local_realm.hpp"
#include "pipeline/asset_manager.hpp"
#include "rendering/camera.hpp"
#include "rendering/camera_controller.hpp"
#include "rendering/glue_camera.hpp"
#include "rendering/renderer.hpp"
#include "rendering/terrain_manager.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <limits>
#ifdef WOWEE_PS4
#include "platform/ps4/input_ps4.hpp"
#include <orbis/Pad.h>
#endif

namespace wowee::core {
namespace {
constexpr float kStreamTimeoutSeconds = 60.0f;
constexpr uint32_t kLookaheadMs[] = {750, 1500, 3000, 6000, 10000};
constexpr size_t kNoShot = std::numeric_limits<size_t>::max();

bool usablePosition(const glm::vec3& position) {
    // Also bound conversions to integer tile coordinates for damaged tracks.
    return rendering::glue::finite(position) &&
        std::abs(position.x) < 1000000.0f &&
        std::abs(position.y) < 1000000.0f &&
        std::abs(position.z) < 1000000.0f;
}

bool applyFrame(rendering::Camera& camera, const CharacterIntroFrame& frame) {
    const auto position = coords::canonicalToRender(frame.canonicalPosition);
    const auto target = coords::canonicalToRender(frame.canonicalTarget);
    const auto direction = target - position;
    const float horizontal = std::hypot(direction.x, direction.y);
    if (!usablePosition(position) || !usablePosition(target) ||
        !std::isfinite(horizontal) || glm::length(direction) < 0.0001f) return false;
    camera.setPosition(position);
    camera.setRotation(glm::degrees(std::atan2(direction.y, direction.x)),
                       glm::degrees(std::atan2(direction.z, horizontal)));
    camera.setRoll(frame.rollRadians);
    camera.setFov(glm::degrees(rendering::glue::verticalFov(
        frame.diagonalFovRadians, camera.getAspectRatio())));
    return true;
}

bool requestPosition(rendering::TerrainManager& terrain, const glm::vec3& position, bool priority=false) {
    if (!usablePosition(position)) return false;
    const auto tile = coords::worldToTile(position.x, position.y);
    return terrain.enqueueTile(tile.first, tile.second, priority);
}
} // namespace

bool Application::characterIntroOwnsView() const {
    return introReturning_ || (characterIntro_ && characterIntro_->active());
}

void Application::stopCharacterIntro(bool completed) {
    const bool owned = introReturning_ ||
        (characterIntro_ && !characterIntro_->plan().shots.empty());
    if (!owned) return; // Logout before any cinematic must preserve the camera.
    audio::AudioEngine::instance().stopNarration();
    if (characterIntro_) characterIntro_->cancel();
    introNarrationStarted_ = false;
    introNarrationShot_ = kNoShot;
    introSkipRequested_ = false;
    introWaitSeconds_ = 0.0f;
    introWarmup_.reset();
    introPendingAdvanceSeconds_ = 0.0f;
    introLastUpdate_ = std::chrono::steady_clock::now();
    auto* camera = renderer ? renderer->getCamera() : nullptr;
    auto* controller = renderer ? renderer->getCameraController() : nullptr;
    if (completed && camera && controller && state == AppState::IN_GAME) {
        // Keep input ownership while terrain around the unchanged spawn returns.
        // The black overlay hides streaming and the discontinuous camera cut.
        introReturning_ = true;
        introCompleteOnReturn_ = true;
        introBuffering_ = true;
        camera->setPosition(introSavedCameraPosition_);
        return;
    }
    audio::AudioEngine::instance().setCinematicAudioExclusive(false);
    introReturning_ = false;
    introCompleteOnReturn_ = false;
    introBuffering_ = false;
    if (camera) {
        camera->setFov(introSavedFov_);
        camera->setRoll(0.0f);
    }
    if (controller) controller->setScriptedView(false);
}

void Application::updateCharacterIntro(float deltaTime) {
    auto* camera = renderer ? renderer->getCamera() : nullptr;
    auto* controller = renderer ? renderer->getCameraController() : nullptr;
    auto* terrain = renderer ? renderer->getTerrainManager() : nullptr;
    const auto* player = localRealm_ ? localRealm_->localPlayer() : nullptr;
    if (state != AppState::IN_GAME || !localRealmEntered_ || !localRealm_ ||
        !localRealm_->ready() || !player || !camera || !controller || !terrain) {
        stopCharacterIntro(false);
        return;
    }
    // Death, realm relocation or changing character cancels the old flight.
    // CameraController's teleport/reset has already restored normal ownership.
    if (characterIntroOwnsView() && (player->guid != introAttemptedGuid_ ||
        player->positionRevision != introPositionRevision_)) {
        stopCharacterIntro(false);
        return;
    }
    if (characterIntroOwnsView() && player->dead && !introReturning_) {
        stopCharacterIntro(true);
        introCompleteOnReturn_ = false;
    }
    const auto now = std::chrono::steady_clock::now();
    const float measured = introLastUpdate_.time_since_epoch().count() == 0 ? deltaTime :
        std::chrono::duration<float>(now - introLastUpdate_).count();
    introLastUpdate_ = now;
    float elapsed = std::isfinite(measured) && measured > 0.0f ? measured : 0.0f;

    if (introReturning_) {
        camera->setPosition(introSavedCameraPosition_);
        const bool ready = localRealmWmoOnly_ || (terrain &&
            terrain->isTileLoadedAt(introSpawnPosition_.x, introSpawnPosition_.y) &&
            terrain->isTileLoadedAt(introSavedCameraPosition_.x, introSavedCameraPosition_.y));
        if (ready) {
            const bool markComplete = introCompleteOnReturn_;
            stopCharacterIntro(false);
            if (markComplete && !localRealm_->completeIntro())
                LOG_WARNING("[INTRO] Could not persist cinematic completion");
            LOG_INFO("[INTRO] Returned to ordinary spawn camera; completed=", markComplete);
            return;
        }
        if (terrain) {
            requestPosition(*terrain, introSpawnPosition_);
            requestPosition(*terrain, introSavedCameraPosition_);
        }
        introWaitSeconds_ += elapsed;
        if (introWaitSeconds_ >= kStreamTimeoutSeconds) {
            LOG_WARNING("[INTRO] Spawn terrain did not recover before timeout");
            stopCharacterIntro(false);
            disconnectNotice_ = "The starting area could not finish loading. Please try entering the world again.";
            logoutToLoginPending_ = true; // Teardown runs between frames, never here.
        }
        return;
    }

    if (!characterIntro_ || !characterIntro_->active()) {
        if (introEligibilityGuid_ != player->guid && playerCharacterSpawned &&
            worldLoader_ && !worldLoader_->isLoadingWorld()) {
            introEligibilityGuid_ = player->guid;
            LOG_INFO("[INTRO_ENTRY] guid=", player->guid, " race=", unsigned(player->race),
                     " map=", player->mapId, " savedSeen=", player->introSeen,
                     " previousAttempt=", introAttemptedGuid_, " dead=", player->dead,
                     " wmoOnly=", localRealmWmoOnly_);
        }
        if (player->introSeen || player->dead || player->guid == introAttemptedGuid_ ||
            !playerCharacterSpawned || !assetManager || !worldLoader_ ||
            worldLoader_->isLoadingWorld()) return;
        introAttemptedGuid_ = player->guid;
        introPositionRevision_ = player->positionRevision;
        // A root-WMO-only instance does not provide the terrain streaming this
        // flyover uses. There is no invented substitute scene.
        if (localRealmWmoOnly_ || !terrain) return;
        try {
            pipeline::CharacterIntroPlan plan;
            std::string reason;
            const auto read = [this](const std::string& path, size_t maxBytes) {
                beatWatchdog();
                return assetManager->readFileBounded(path, maxBytes);
            };
            if (!pipeline::loadCharacterIntro(player->race, player->classId, player->mapId,
                                              read, plan, reason)) {
                // Which character, not only what went wrong. The flight is
                // chosen entirely out of the player's own tables - ChrClasses
                // column 58, falling back to ChrRaces column 12, then
                // CinematicSequences, CinematicCamera and the Cameras\*.m2 each
                // row names - so the code cannot single a race out and a report
                // that one race has no intro can only be answered from the
                // installed data. A race finding no sequence at all, finding a
                // sequence that is not in the table, and finding a camera whose
                // model is not installed are three different faults with three
                // different fixes, and the reason says which; without the race
                // beside it there is nothing to look up.
                LOG_WARNING("[INTRO] Entering normally: ", reason,
                            " race=", static_cast<int>(player->race),
                            " class=", static_cast<int>(player->classId),
                            " map=", player->mapId);
                return;
            }
            if (!characterIntro_) characterIntro_ = std::make_unique<CharacterIntro>();
            if (!characterIntro_->start(std::move(plan))) {
                LOG_WARNING("[INTRO] No valid first camera sample; entering normally"
                            " race=", static_cast<int>(player->race),
                            " class=", static_cast<int>(player->classId));
                return;
            }
        } catch (const std::exception&) {
            if (characterIntro_) characterIntro_->cancel();
            LOG_WARNING("[INTRO] Setup could not allocate or read assets; entering normally");
            return;
        }
        const auto first = characterIntro_->frame();
        if (!first || !usablePosition(coords::canonicalToRender(first->canonicalPosition))) {
            characterIntro_->cancel();
            return;
        }
        introSavedFov_ = camera->getFovDegrees();
        introSavedCameraPosition_ = camera->getPosition();
        introSpawnPosition_ = renderer->getCharacterPosition();
        introBuffering_ = true;
        introWaitSeconds_ = 0.0f;
        introWarmup_.reset();
        introPendingAdvanceSeconds_ = 0.0f;
        introLastUpdate_ = std::chrono::steady_clock::now();
        // World entry may have blocked this update while loading a new map.
        // That time predates this cinematic: charging it to the first tile
        // wait can exhaust the entire timeout before a single camera frame.
        elapsed = 0.0f;
        introSkipRequested_ = false;
        introNarrationStarted_ = false;
        introNarrationShot_ = kNoShot;
        audio::AudioEngine::instance().setCinematicAudioExclusive(true);
        controller->setScriptedView(true);
        localRealm_->stopAttack();
        localRealm_->cancelCast();
        LOG_INFO("[INTRO] Starting authored sequence=", characterIntro_->plan().sequenceId,
                 " race=", static_cast<int>(player->race),
                 " class=", static_cast<int>(player->classId),
                 " shots=", characterIntro_->plan().shots.size(),
                 " durationMs=", characterIntro_->plan().durationMs,
                 " decodedBytes=", characterIntro_->plan().decodedBytes);
        for (const auto& shot : characterIntro_->plan().shots) {
            LOG_INFO("[INTRO_CAMERA] camera=", shot.cameraId, " model=", shot.modelPath,
                     " positionInterpolation=", shot.camera.positions.interpolationType,
                     " targetInterpolation=", shot.camera.targets.interpolationType,
                     " rollInterpolation=", shot.camera.roll.interpolationType,
                     " curve=stabilized-keys-C1 noHandleOvershoot=1 subMillisecond=1");
        }

    }

    if (Input::getInstance().isKeyJustPressed(SDL_SCANCODE_ESCAPE)) introSkipRequested_ = true;
#ifdef WOWEE_PS4
    if (platform::ps4::padState().pressed & ORBIS_PAD_BUTTON_CIRCLE) introSkipRequested_ = true;
#endif
    if (introSkipRequested_) {
        const auto skipped=characterIntro_->frame();
        LOG_INFO("[INTRO] User skip at tMs=",skipped?skipped->sequenceTimeMs:0);
        stopCharacterIntro(true);
        return;
    }
    const auto beginReturn = [this](bool complete) {
        stopCharacterIntro(true);
        introCompleteOnReturn_ = complete;
    };
    const auto current = characterIntro_->frame();
    if (!current) { beginReturn(false); return; }
    // Retain the proposed step across buffering. Audio had advanced through
    // that interval before it was paused; the waiting interval is excluded.
    // Wall time avoids the game's physics delta clamp slowing the camera while
    // a narration decoder continues at its original rate during a long frame.
    const float step = introNarrationShot_ == kNoShot ? 0.0f :
        introBuffering_ ? introPendingAdvanceSeconds_ : std::min(elapsed, 600.0f);
    introPendingAdvanceSeconds_ = step;
    const uint32_t aheadMs = static_cast<uint32_t>(std::ceil(step * 1000.0f));
    const auto pending = characterIntro_->peekAhead(aheadMs);
    if (!pending || !applyFrame(*camera, *pending)) { beginReturn(false); return; }
    const auto pendingPosition = camera->getPosition();
    const bool requested = requestPosition(*terrain, pendingPosition, true);
    bool ready = requested && terrain->isTileLoadedAt(pendingPosition.x, pendingPosition.y);

    // Require nearby scenery in the viewing direction, not just the tile
    // underneath the camera. A flyover can look into an unfinished neighbour.
    const auto direction=glm::normalize(coords::canonicalToRender(pending->canonicalTarget)-pendingPosition);
    for(float distance:{75.f,150.f}) {
        const auto visible=pendingPosition+direction*distance;
        if(requestPosition(*terrain,visible,true))
            ready=terrain->isTileLoadedAt(visible.x,visible.y)&&ready;
    }
    // Stage the next ten seconds of the authored camera path, but only in
    // adjacent tiles that the active streaming window can retain. There is no
    // full-route preload and no permanently raised world draw distance.
    for (const uint32_t leadMs : kLookaheadMs) {
        if (const auto preview = characterIntro_->peekAhead(aheadMs + leadMs)) {
            const auto position = coords::canonicalToRender(preview->canonicalPosition);
            if (usablePosition(position) && preview->shotIndex == pending->shotIndex) {
                const auto a = coords::worldToTile(pendingPosition.x, pendingPosition.y);
                const auto b = coords::worldToTile(position.x, position.y);
                if (std::abs(a.first-b.first) <= 1 && std::abs(a.second-b.second) <= 1)
                    requestPosition(*terrain, position);
            }
        }
    }
    // Finish initial/cut uploads behind the existing buffering overlay instead
    // of starting narration as soon as the first terrain tile becomes ready.
    ready = introWarmup_.ready(pending->shotIndex, ready,
                                  terrain->hasFinalizationWork(), elapsed);
    const bool wasBuffering = introBuffering_;
    introBuffering_ = !ready;
    if (wasBuffering != introBuffering_) {
        LOG_INFO("[INTRO_STREAM] shot=", pending->shotIndex,
                 " buffering=", introBuffering_, " waitSeconds=", introWaitSeconds_,
                 " tiles=", terrain->getLoadedTileCount(),
                 " pending=", terrain->getPendingTileCount(),
                 " finalizing=", terrain->hasFinalizationWork());
    }
    auto& audio = audio::AudioEngine::instance();
    audio.setNarrationPaused(!ready);
    if (!ready) {
        characterIntro_->advance(0.0f, false);
        introWaitSeconds_ += elapsed;
        if (!requested || introWaitSeconds_ >= kStreamTimeoutSeconds) {
            const auto tile = coords::worldToTile(pendingPosition.x, pendingPosition.y);
            LOG_WARNING("[INTRO] Camera terrain unavailable; returning to spawn: tile=",
                        tile.first, ",", tile.second, " requested=", requested,
                        " waitSeconds=", introWaitSeconds_, " map=", player->mapId);
            beginReturn(false);
        }
        return;
    }
    introWaitSeconds_ = 0.0f;
    characterIntro_->advance(step, true);
    introPendingAdvanceSeconds_ = 0.0f;
    if (!characterIntro_->active()) {
        beginReturn(characterIntro_->finished() && !characterIntro_->failed());
        return;
    }
    const auto frame = characterIntro_->frame();
    if (!frame || !applyFrame(*camera, *frame)) { beginReturn(false); return; }

    if (introNarrationShot_ == kNoShot || current->sequenceTimeMs / 2000 != frame->sequenceTimeMs / 2000) {
        const auto p = camera->getPosition();
        LOG_INFO("[INTRO_CAMERA] tMs=", frame->sequenceTimeMs, " shot=", frame->shotIndex,
                 " position=", p.x, ",", p.y, ",", p.z, " roll=", frame->rollRadians,
                 " deltaMs=", step * 1000.0f, " buffered=", introBuffering_);
    }

    const auto& plan = characterIntro_->plan();
    if (introNarrationShot_ != frame->shotIndex) {
        if (!plan.sequenceSound.path.empty()) {
            // A sequence-level recording spans its shots; do not cut it off at
            // each camera change. Camera-local recordings are used otherwise.
            if (introNarrationShot_ == kNoShot)
                introNarrationStarted_ = audio.playNarration(plan.sequenceSound.path,
                    plan.sequenceSound.volume, frame->sequenceTimeMs / 1000.0f);
        } else {
            audio.stopNarration();
            const auto& sound = plan.shots[frame->shotIndex].narration;
            introNarrationStarted_ = !sound.path.empty() && audio.playNarration(
                sound.path, sound.volume, frame->shotTimeMs / 1000.0f);
        }
        // A missing/disabled audio device must not retry asset reads each frame.
        introNarrationShot_ = frame->shotIndex;
        // Loading a decoder precedes playback; don't charge that read time to
        // the camera clock on the next frame.
        introLastUpdate_ = std::chrono::steady_clock::now();
    }
    audio.setNarrationPaused(false);
}

void Application::renderCharacterIntroOverlay() {
    if (!characterIntroOwnsView() || !ImGui::GetCurrentContext()) return;
    const auto& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) introSkipRequested_ = true;
    auto* draw = ImGui::GetForegroundDrawList();
    const ImVec2 size = io.DisplaySize;
    if (introBuffering_ || introReturning_)
        draw->AddRectFilled(ImVec2(0, 0), size, IM_COL32(0, 0, 0, 255));
    const char* label = introReturning_ ? "Loading starting area..." :
        introBuffering_ ? "Loading cinematic...  Circle / Esc: Skip" : "Circle / Esc: Skip";
    const ImVec2 text = ImGui::CalcTextSize(label);
    const ImVec2 at(std::max(16.0f, (size.x-text.x)*0.5f), std::max(16.0f, size.y-48.0f));
    draw->AddRectFilled(ImVec2(at.x-12, at.y-8), ImVec2(at.x+text.x+12, at.y+text.y+8),
                        IM_COL32(0, 0, 0, 180), 4.0f);
    draw->AddText(at, IM_COL32(235, 220, 175, 255), label);
}
} // namespace wowee::core
