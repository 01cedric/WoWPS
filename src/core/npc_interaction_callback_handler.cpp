#include "core/npc_interaction_callback_handler.hpp"
#include "core/entity_spawner.hpp"
#include "core/coordinates.hpp"
#include "core/logger.hpp"
#include "rendering/renderer.hpp"
#include "rendering/character_renderer.hpp"
#include "rendering/animation/animation_ids.hpp"
#include "game/game_handler.hpp"
#include "audio/audio_coordinator.hpp"
#include "audio/npc_voice_manager.hpp"

namespace wowee { namespace core {

NPCInteractionCallbackHandler::NPCInteractionCallbackHandler(
    EntitySpawner& entitySpawner,
    rendering::Renderer* renderer,
    game::GameHandler& gameHandler,
    audio::AudioCoordinator* audioCoordinator)
    : entitySpawner_(entitySpawner)
    , renderer_(renderer)
    , gameHandler_(gameHandler)
    , audioCoordinator_(audioCoordinator)
{
}

audio::VoiceType NPCInteractionCallbackHandler::resolveNpcVoiceType(uint64_t guid) const {
    audio::VoiceType voiceType = audio::VoiceType::GENERIC;
    auto entity = gameHandler_.getEntityManager().getEntity(guid);
    if (entity && entity->getType() == game::ObjectType::UNIT) {
        auto unit = std::static_pointer_cast<game::Unit>(entity);
        uint32_t displayId = unit->getDisplayId();
        voiceType = entitySpawner_.detectVoiceTypeFromDisplayId(displayId);
    }
    return voiceType;
}

void NPCInteractionCallbackHandler::setupCallbacks() {
    // NPC greeting callback - play voice line
    gameHandler_.setNpcGreetingCallback([this](uint64_t guid, const glm::vec3& position) {
        // Play NPC_WELCOME animation on the NPC
        if (renderer_) {
            auto* cr = renderer_->getCharacterRenderer();
            if (cr) {
                uint32_t instanceId = entitySpawner_.getCreatureInstanceId(guid);
                if (instanceId != 0) cr->playAnimation(instanceId, rendering::anim::NPC_WELCOME, false);
            }
        }
        if (audioCoordinator_ && audioCoordinator_->getNpcVoiceManager()) {
            // Convert canonical to render coords for 3D audio
            glm::vec3 renderPos = core::coords::canonicalToRender(position);
            audio::VoiceType voiceType = resolveNpcVoiceType(guid);
            audioCoordinator_->getNpcVoiceManager()->playGreeting(guid, voiceType, renderPos);
        }
    });

    // NPC farewell callback - play farewell voice line
    gameHandler_.setNpcFarewellCallback([this](uint64_t guid, const glm::vec3& position) {
        if (audioCoordinator_ && audioCoordinator_->getNpcVoiceManager()) {
            glm::vec3 renderPos = core::coords::canonicalToRender(position);
            audio::VoiceType voiceType = resolveNpcVoiceType(guid);
            audioCoordinator_->getNpcVoiceManager()->playFarewell(guid, voiceType, renderPos);
        }
    });

    // NPC vendor callback - play vendor voice line
    gameHandler_.setNpcVendorCallback([this](uint64_t guid, const glm::vec3& position) {
        if (audioCoordinator_ && audioCoordinator_->getNpcVoiceManager()) {
            glm::vec3 renderPos = core::coords::canonicalToRender(position);
            audio::VoiceType voiceType = resolveNpcVoiceType(guid);
            audioCoordinator_->getNpcVoiceManager()->playVendor(guid, voiceType, renderPos);
        }
    });

    // NPC aggro callback - play combat start voice line
    gameHandler_.setNpcAggroCallback([this](uint64_t guid, const glm::vec3& position) {
        if (audioCoordinator_ && audioCoordinator_->getNpcVoiceManager()) {
            glm::vec3 renderPos = core::coords::canonicalToRender(position);
            audio::VoiceType voiceType = resolveNpcVoiceType(guid);
            uint32_t displayId = 0;
            auto entity = gameHandler_.getEntityManager().getEntity(guid);
            if (entity && entity->getType() == game::ObjectType::UNIT) {
                displayId = std::static_pointer_cast<game::Unit>(entity)->getDisplayId();
            }
            audioCoordinator_->getNpcVoiceManager()->playAggro(
                guid, displayId, voiceType, renderPos);
        }
    });
}

}} // namespace wowee::core
