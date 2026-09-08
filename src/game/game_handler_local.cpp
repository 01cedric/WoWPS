#include "game/local_ui_spell_metadata.hpp"
#include "game/game_handler.hpp"
#include "game/entity_controller.hpp"
#include "game/local_gameplay.hpp"
#include "game/local_mount.hpp"
#include "game/movement_handler.hpp"
#include "game/local_character_visuals.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/coordinates.hpp"
#include "core/logger.hpp"
#include "audio/audio_coordinator.hpp"
#include "audio/npc_voice_manager.hpp"
#include "audio/combat_sound_manager.hpp"
#include "audio/activity_sound_manager.hpp"
#include "audio/spell_sound_manager.hpp"
#include "audio/ui_sound_manager.hpp"
#include "rendering/renderer.hpp"
#include "rendering/animation_controller.hpp"
#include "rendering/spell_visual_system.hpp"
#include <algorithm>

namespace wowee::game {

void GameHandler::setLocalCharacterList(std::vector<Character> list) {
    characters = std::move(list);
}

void GameHandler::resumeLocalTransport(uint32_t entry,uint32_t map,const glm::vec3& headingOffset) {
    if(!entry)return;
    pendingPlayerTransportTransfer_=true;
    pendingPlayerTransportGuid_=0xf1c0000000000000ULL|entry;
    pendingPlayerTransportEntry_=entry;pendingPlayerTransportMapId_=map;
    // Hull assets face local -X; the authoritative passenger frame faces +X.
    pendingPlayerTransportOffset_=glm::vec3(-headingOffset.x,-headingOffset.y,headingOffset.z);
}

void GameHandler::beginLocalExploration(const Character& character, float serverOrientation, bool transportTransfer) {
    const uint64_t riding = transportTransfer ? playerTransportGuid_ : 0;
    const glm::vec3 deckOffset = playerTransportOffset_;
    resetLocalPresentation();
    disconnect();
    localExploration_ = true;
    spellNameCache_.clear();
    spellNameCacheLoaded_ = false;
    localEquipmentVisuals_.clear();
    inventory = Inventory{};
    characters = {character};
    activeCharacterGuid_ = character.guid;
    // The bar the player arranged. setActionBarSlot writes it out through
    // saveCharacterConfig on every change, but nothing read it back on this
    // path - so a local character's action bar was rebuilt from the spell list
    // at every login and anything dragged onto it was gone by the next session.
    loadCharacterConfig();
    playerGuid = character.guid;
    currentMapId_ = character.mapId;
    targetGuid = 0;
    movementInfo = {};
    const auto p = core::coords::serverToCanonical(glm::vec3(character.x, character.y, character.z));
    movementInfo.x = p.x;
    movementInfo.y = p.y;
    movementInfo.z = p.z;
    movementInfo.orientation = core::coords::serverToCanonicalYaw(serverOrientation);
    forceClearTaxiAndMovementState();
    if (riding) {
        pendingPlayerTransportTransfer_ = true;
        pendingPlayerTransportGuid_ = riding;
        pendingPlayerTransportEntry_ = uint32_t(riding);
        pendingPlayerTransportMapId_ = character.mapId;
        pendingPlayerTransportOffset_ = deckOffset;
    }
    setState(WorldState::IN_WORLD);
    syncLocalExplorationPlayer(character, serverOrientation);
    LOG_INFO("[LOCAL_REALM] enter exploration name=", character.name, " map=", character.mapId);
    if (worldEntryCallback_) worldEntryCallback_(character.mapId, character.x, character.y, character.z, true);
}

void GameHandler::syncLocalExplorationPlayer(const Character& character, float serverOrientation) {
    if (!localExploration_ || character.mapId != currentMapId_) return;
    auto& manager = entityController_->getEntityManager();
    auto entity = manager.getEntity(character.guid);
    const bool fresh = !entity;
    if (fresh) {
        entity = std::make_shared<Player>(character.guid);
        manager.addEntity(character.guid, entity);
    }
    const auto p = core::coords::serverToCanonical(glm::vec3(character.x, character.y, character.z));
    const float yaw = core::coords::serverToCanonicalYaw(serverOrientation);
    if (fresh || character.guid == playerGuid) {
        entity->setPosition(p.x, p.y, p.z, yaw);
    } else {
        const glm::vec3 destination(entity->getLatestX(), entity->getLatestY(), entity->getLatestZ());
        const auto change = p - destination;
        if (glm::dot(change, change) > 0.0001f)
            entity->startMoveTo(p.x, p.y, p.z, yaw, 0.1f);
        else
            entity->setOrientation(yaw);
    }
    auto player = std::static_pointer_cast<Player>(entity);
    player->setName(character.name);
    if (fresh) {
        player->setHealth(100);
        player->setMaxHealth(100);
    }
    player->setLevel(character.level);
    uint32_t displayId = player->getDisplayId();
    if (fresh) {
        // ChrRaces.dbc raw fields 4/5 are the male/female display IDs in 3.3.5a.
        // Resolve from the supplied client data so non-human avatars never inherit 49/50.
        if (auto* assets = services_.assetManager) {
            if (const auto dbc = assets->loadDBC("ChrRaces.dbc"); dbc && dbc->isLoaded() && dbc->getFieldCount() >= 6) {
                for (uint32_t row = 0; row < dbc->getRecordCount(); ++row)
                    if (dbc->getUInt32(row, 0) == static_cast<uint8_t>(character.race)) {
                        displayId = dbc->getUInt32(row, character.gender == Gender::FEMALE ? 5 : 4);
                        break;
                    }
            }
        }
        if (!displayId) LOG_WARNING("[LOCAL_WORLD] race display metadata missing race=", static_cast<int>(character.race));
    }
    player->setDisplayId(displayId);
    if (fresh && character.guid != playerGuid && playerSpawnCallback_) {
        playerSpawnCallback_(character.guid, displayId, static_cast<uint8_t>(character.race),
            static_cast<uint8_t>(character.gender), character.appearanceBytes,
            character.facialFeatures, p.x, p.y, p.z, yaw);
        LOG_INFO("[LOCAL_REALM] remote avatar spawn name=", character.name, " guid=", character.guid);
    }
}

void GameHandler::removeLocalExplorationPlayer(uint64_t guid) {
    if (!localExploration_ || guid == playerGuid) return;
    if (playerDespawnCallback_) playerDespawnCallback_(guid);
    entityController_->getEntityManager().removeEntity(guid);
    localEquipmentVisuals_.erase(guid);
    localPresentationStates_.erase(guid);
    LOG_INFO("[LOCAL_REALM] remote avatar removed guid=", guid);
}

bool GameHandler::syncLocalRealmPlayer(const LocalRealmPlayer& snapshot, const LocalWorldContent& content) {
    if (!localExploration_ || snapshot.mapId != currentMapId_) return false;
    const LocalUnitPresentationState current{snapshot.health, snapshot.level, snapshot.attackTarget, snapshot.dead};
    const auto previous = localPresentationStates_.find(snapshot.guid);
    const auto event = localUnitPresentationEvents(
        previous != localPresentationStates_.end() ? &previous->second : nullptr, current);
    localPresentationStates_[snapshot.guid] = current;
    Character character = localCharacterVisual(snapshot, &content, LocalEquipmentSource::AuthoritySnapshot);
    std::array<uint32_t, 19> displays{};
    std::array<uint8_t, 19> types{};
    for (size_t slot = 0; slot < displays.size(); ++slot) {
        displays[slot] = character.equipment[slot].displayModel;
        types[slot] = character.equipment[slot].inventoryType;
    }
    syncLocalExplorationPlayer(character, snapshot.orientation);
    auto unit = std::static_pointer_cast<Unit>(entityController_->getEntityManager().getEntity(snapshot.guid));
    unit->setHealth(snapshot.health);
    unit->setMaxHealth(snapshot.maxHealth);
    unit->setPowerType(static_cast<uint8_t>(snapshot.resourceType));
    unit->setPower(snapshot.mana);
    unit->setMaxPower(snapshot.maxMana);
    unit->setDynamicFlags(snapshot.dead ? UNIT_DYNFLAG_DEAD : 0);
    const auto* mount=localActiveMount(content,snapshot);
    const uint32_t mountDisplay=mount && !snapshot.dead ? mount->mountDisplayId : 0;
    const auto oldMount=unit->getMountDisplayId();unit->setMountDisplayId(mountDisplay);
    if(snapshot.guid!=playerGuid && oldMount!=mountDisplay && otherPlayerMountCallback_)
        otherPlayerMountCallback_(snapshot.guid,mountDisplay);
    if(snapshot.guid==playerGuid && !snapshot.flight.active &&
       (mountAuraSpellId_!=snapshot.mountSpellId || currentMountDisplayId_!=mountDisplay)) {
        mountAuraSpellId_=snapshot.mountSpellId;currentMountDisplayId_=mountDisplay;
        if(mountCallback_)mountCallback_(mountDisplay);
        const float multiplier=mount ? 1.f+mount->mountSpeedPercent/100.f : 1.f;
        if(movementHandler_)movementHandler_->applyServerMovementSpeeds(
            2.5f,7.f*multiplier,4.5f,4.72222f,2.5f,7.f,4.5f,3.141593f,3.141593f);
        LOG_INFO("[LOCAL_MOUNT] spell=",snapshot.mountSpellId," display=",mountDisplay," run=",7.f*multiplier);
    }
    const bool equipmentChanged = !localEquipmentVisuals_.count(snapshot.guid) ||
        localEquipmentVisuals_[snapshot.guid] != snapshot.equipment;
    localEquipmentVisuals_[snapshot.guid] = snapshot.equipment;
    if (snapshot.guid == playerGuid) {
        if (!spellNameCacheLoaded_) {
            auto compact=localUiSpellMetadata<SpellNameEntry>(content);
            spellNameCache_.swap(compact);
            spellNameCacheLoaded_ = true;
            LOG_INFO("[LOCAL_FRAMEXML] compact spell metadata ready: ", spellNameCache_.size(),
                     " entries; no full Spell.dbc UI reload");
        }
        characters = {character};
        movementInfo.x = unit->getX(); movementInfo.y = unit->getY(); movementInfo.z = unit->getZ();
        movementInfo.orientation = unit->getOrientation();
        serverPlayerLevel_ = snapshot.level;
        playerXp_ = snapshot.xp;
        playerNextLevelXp_ = snapshot.xpToLevel;
        playerMoneyCopper_ = snapshot.money;
        if(snapshot.classId==6) for(size_t i=0;i<playerRunes_.size();++i) {
            auto& rune=playerRunes_[i];
            const auto type=static_cast<RuneType>(i/2);
            const auto remaining=std::min(snapshot.runeCooldownMs[i],kLocalRuneRechargeMs);
            const bool ready=remaining==0;
            const float fraction=1.0f-float(remaining)/kLocalRuneRechargeMs;
            const bool changed=rune.type!=type||rune.ready!=ready||fraction+0.05f<rune.readyFraction;
            rune.type=type;rune.ready=ready;rune.readyFraction=fraction;
            if(changed)fireRuneUpdate(static_cast<uint32_t>(i));
        }
        presentLocalCast(snapshot, content);
        if (equipmentChanged) {
            for (size_t i = 0; i < snapshot.equipment.size(); ++i) {
                ItemDef visual{};
                if (const auto* item = localEquippedItem(snapshot, &content, i, LocalEquipmentSource::AuthoritySnapshot)) {
                    visual.itemId = item->id;
                    visual.name = item->name;
                    visual.displayInfoId = item->displayId;
                    visual.inventoryType = types[static_cast<size_t>(kLocalEquipmentVisualSlots[i])];
                    visual.armor = item->armor;
                    visual.damageMin = visual.damageMax = static_cast<float>(item->attack);
                    visual.delayMs = 2000;
                }
                inventory.setEquipSlot(kLocalEquipmentVisualSlots[i], visual);
            }
        }
        if (playerHealthCallback_) playerHealthCallback_(snapshot.health, snapshot.maxHealth);
        if ((event.deathPose || event.respawn) && standStateCallback_)
            standStateCallback_(snapshot.dead ? 7 : 0);
        // Local authority currently has no cast pushback. A nonlethal wound
        // must not visually cancel a wind-up that the authority still runs.
        if (event.wound && !localCastPresentation_.activeSpell && !localCastCommittedThisFrame_ && hitReactionCallback_)
            hitReactionCallback_(playerGuid, HitReaction::WOUND);
        if (event.wound && services_.audioCoordinator)
            if (auto* activity = services_.audioCoordinator->getActivitySoundManager()) activity->playWound();
        if (event.deathSound && services_.audioCoordinator)
            if (auto* activity = services_.audioCoordinator->getActivitySoundManager()) activity->playDeath();
        if (event.levelUp && levelUpCallback_) {
            levelUpCallback_(snapshot.level);
            LOG_INFO("[LOCAL_PRESENTATION] level-up level=", unsigned(snapshot.level));
        }
        const auto progress = localProgressPresentation_.observe(snapshot);
        if (auto* audio = services_.audioCoordinator) if (auto* ui = audio->getUiSoundManager()) {
            if (progress.questRewarded) ui->playQuestComplete();
            else if (progress.questAccepted) ui->playQuestActivate();
            else if (progress.objectiveUpdated) ui->playQuestUpdate();
            if (progress.itemReceived) ui->playLootItem();
            if (progress.moneyReceived) ui->playLootCoinSmall();
        }
        return equipmentChanged;
    }
    if (equipmentChanged && playerEquipmentCallback_) playerEquipmentCallback_(snapshot.guid, displays, types);
    if (event.deathPose && npcDeathCallback_) npcDeathCallback_(snapshot.guid);
    if (event.respawn && npcRespawnCallback_) npcRespawnCallback_(snapshot.guid);
    if (event.wound && hitReactionCallback_) hitReactionCallback_(snapshot.guid, HitReaction::WOUND);
    if (event.levelUp && otherPlayerLevelUpCallback_) otherPlayerLevelUpCallback_(snapshot.guid, snapshot.level);
    return false;
}

void GameHandler::greetLocalRealmNpc(const LocalRealmNpc& npc) {
    if(localExploration_ && !npc.dead && !npc.hostile && npcGreetingCallback_)
        npcGreetingCallback_(npc.guid,core::coords::serverToCanonical(glm::vec3(npc.x,npc.y,npc.z)));
}

void GameHandler::syncLocalRealmNpc(const LocalRealmNpc& npc) {
    if (!localExploration_ || npc.mapId != currentMapId_) return;
    auto& manager = entityController_->getEntityManager();
    auto entity = manager.getEntity(npc.guid);
    const bool fresh = !entity;
    if (fresh) {
        entity = std::make_shared<Unit>(npc.guid);
        manager.addEntity(npc.guid, entity);
    }
    auto unit = std::static_pointer_cast<Unit>(entity);
    const LocalUnitPresentationState current{npc.health, npc.level, npc.targetGuid, npc.dead};
    const auto previous = localPresentationStates_.find(npc.guid);
    const auto event = localUnitPresentationEvents(
        previous != localPresentationStates_.end() ? &previous->second : nullptr, current);
    localPresentationStates_[npc.guid] = current;
    const auto p = core::coords::serverToCanonical(glm::vec3(npc.x, npc.y, npc.z));
    const float yaw = core::coords::serverToCanonicalYaw(npc.orientation);
    if (npc.transportEntry) {
        setTransportAttachment(npc.guid, ObjectType::UNIT,
            0xf1c0000000000000ULL | npc.transportEntry,
            glm::vec3(npc.transportX,npc.transportY,npc.transportZ),true,
            core::coords::serverToCanonicalYaw(npc.transportOrientation));
    } else clearTransportAttachment(npc.guid);
    if (fresh || npc.dead || npc.transportEntry) entity->setPosition(p.x, p.y, p.z, yaw);
    else {
        const glm::vec3 destination(entity->getLatestX(), entity->getLatestY(), entity->getLatestZ());
        if (glm::dot(p - destination, p - destination) > 0.0001f)
            entity->startMoveTo(p.x, p.y, p.z, yaw, 0.1f);
        else entity->setOrientation(yaw);
    }
    unit->setName(npc.name);
    unit->setEntry(npc.entry);
    unit->setDisplayId(npc.displayId);
    unit->setLevel(npc.level);
    unit->setHealth(npc.health);
    unit->setMaxHealth(npc.maxHealth);
    unit->setNpcFlags((npc.questGiver ? 3u : 0u) |
        (npc.auctioneer ? kLocalNpcFlagAuctioneer : 0u) |
        (npc.vendor ? kLocalNpcFlagVendor : 0u) | (npc.repairer ? kLocalNpcFlagRepair : 0u) |
        (npc.classTrainer ? kLocalNpcFlagTrainer | kLocalNpcFlagTrainerClass : 0u) |
        (npc.professionTrainer ? kLocalNpcFlagTrainer | kLocalNpcFlagTrainerProfession : 0u) |
        (npc.innkeeper ? kLocalNpcFlagInnkeeper : 0u) | (npc.flightMaster ? 0x2000u : 0u));
    unit->setHostile(npc.hostile);
    unit->setFactionTemplate(npc.hostile ? 14 : 12);
    unit->setDynamicFlags((npc.dead ? UNIT_DYNFLAG_DEAD : 0) |
                          (npc.lootable ? UNIT_DYNFLAG_LOOTABLE : 0));
    if (fresh && creatureSpawnCallback_) {
        creatureSpawnCallback_(npc.guid, npc.displayId, p.x, p.y, p.z, yaw, 1.0f);
        LOG_INFO("[LOCAL_GAMEPLAY] creature spawn entry=", npc.entry, " name=", npc.name,
                 " guid=", npc.guid, " display=", npc.displayId);
    }
    if (event.deathPose && npcDeathCallback_) npcDeathCallback_(npc.guid);
    if (event.respawn && npcRespawnCallback_) npcRespawnCallback_(npc.guid);
    if (event.wound && hitReactionCallback_)
        hitReactionCallback_(npc.guid, HitReaction::WOUND);
    if (event.aggro && npcAggroCallback_) npcAggroCallback_(npc.guid, p);
    if (auto* audio = services_.audioCoordinator) if (auto* voices = audio->getNpcVoiceManager()) {
        const auto renderPosition = core::coords::canonicalToRender(p);
        if (event.deathSound) voices->playCombatDeath(npc.guid, npc.displayId, renderPosition);
        else if (event.wound) voices->playCombatWound(npc.guid, npc.displayId, renderPosition);
    }
    if (event.deathPose || event.respawn)
        LOG_INFO("[LOCAL_PRESENTATION] npc=", npc.guid, event.deathPose ? " death pose" : " respawn pose");
}

void GameHandler::removeLocalRealmNpc(uint64_t guid) {
    if (!localExploration_) return;
    clearTransportAttachment(guid);
    if (creatureDespawnCallback_) creatureDespawnCallback_(guid);
    entityController_->getEntityManager().removeEntity(guid);
    localPresentationStates_.erase(guid);
    if (targetGuid == guid) targetGuid = 0;
}

void GameHandler::presentLocalMeleeImpact(uint64_t attackerGuid, uint64_t victimGuid) {
    if (!localExploration_ || !attackerGuid || !victimGuid) return;
    const auto attacker = entityController_->getEntityManager().getEntity(attackerGuid);
    if (!attacker || (attacker->getType() != ObjectType::UNIT && attacker->getType() != ObjectType::PLAYER)) return;
    const auto unit = std::static_pointer_cast<Unit>(attacker);
    if (!unit->getHealth()) return;
    const bool localAttacker = attackerGuid == playerGuid;
    // Snapshot damage can also be caused by a spell. Never replace its
    // wind-up/release animation with the old damage-derived melee fallback.
    if (localAttacker && (localCastPresentation_.activeSpell || localCastCommittedThisFrame_)) return;
    if (localAttacker) {
        if (meleeSwingCallback_) meleeSwingCallback_(0);
    } else if (npcSwingCallback_) npcSwingCallback_(attackerGuid);
    auto* audio = services_.audioCoordinator;
    if (!audio) return;
    if (attacker->getType() == ObjectType::UNIT) if (auto* voices = audio->getNpcVoiceManager()) {
        const glm::vec3 canonical(unit->getLatestX(), unit->getLatestY(), unit->getLatestZ());
        voices->playCombatAttack(attackerGuid, unit->getDisplayId(), core::coords::canonicalToRender(canonical));
    }
    if (localAttacker || victimGuid == playerGuid) {
        if (auto* combat = audio->getCombatSoundManager()) {
            combat->playWeaponSwing(audio::CombatSoundManager::WeaponSize::MEDIUM, false);
            combat->playImpact(audio::CombatSoundManager::WeaponSize::MEDIUM,
                audio::CombatSoundManager::ImpactType::FLESH, false);
        }
        if (localAttacker) if (auto* activity = audio->getActivitySoundManager()) activity->playAttackGrunt();
    }
}

namespace {
audio::SpellSoundManager::MagicSchool localSpellSchool(uint32_t mask) {
    using School = audio::SpellSoundManager::MagicSchool;
    if (mask & 4) return School::FIRE;
    if (mask & 16) return School::FROST;
    if (mask & 2) return School::HOLY;
    if (mask & 8) return School::NATURE;
    if (mask & 32) return School::SHADOW;
    if (mask & 64) return School::ARCANE;
    return School::PHYSICAL;
}
}

void GameHandler::presentLocalCast(const LocalRealmPlayer& snapshot, const LocalWorldContent& content) {
    const auto event = localCastPresentation_.observe(snapshot);
    localCastCommittedThisFrame_ = event.completed;
    auto* renderer = services_.renderer;
    auto* visuals = renderer ? renderer->getSpellVisualSystem() : nullptr;
    auto* audio = services_.audioCoordinator;
    auto* sounds = audio ? audio->getSpellSoundManager() : nullptr;
    const auto position = [&](uint64_t guid, glm::vec3& out) {
        if (guid == playerGuid && renderer) { out = renderer->getCharacterPosition(); return true; }
        const auto entity = entityController_->getEntityManager().getEntity(guid);
        if (!entity) return false;
        out = core::coords::canonicalToRender(glm::vec3(entity->getLatestX(), entity->getLatestY(), entity->getLatestZ()));
        return true;
    };
    const auto animate = [&](uint32_t id, uint64_t target, bool start) {
        // Existing callbacks inspect this spell ID for class-specific casting
        // animation selection; the regular getter still exposes the active cast.
        struct ResetId { uint32_t& id; ~ResetId() { id = 0; } } reset{localCastCallbackSpellId_};
        localCastCallbackSpellId_ = id;
        const auto type = !target || target == snapshot.guid ? SpellCastType::OMNI : SpellCastType::DIRECTED;
        if (spellCastAnimCallback_) spellCastAnimCallback_(snapshot.guid, start, false, type);
    };
    if (event.stopPrecast) {
        if (visuals) visuals->cancelAllPrecastVisuals();
        if (sounds) sounds->stopPrecast();
    }
    if (event.interrupted) {
        if (renderer) if (auto* animation = renderer->getAnimationController()) animation->cancelSpellCast();
        LOG_INFO("[LOCAL_PRESENTATION] cast interrupted spell=", event.previousSpell);
    }
    if (event.completed) {
        if (event.previousSpell != event.completedSpell) animate(event.completedSpell, event.completedTarget, true);
        animate(event.completedSpell, event.completedTarget, false);
        if (const auto* spell = content.spell(event.completedSpell)) {
            glm::vec3 casterPosition, targetPosition;
            if (visuals && spell->visualId) {
                if (position(snapshot.guid, casterPosition))
                    visuals->playSpellVisual(spell->visualId, casterPosition, false, resolveUnitRenderInstance(snapshot.guid));
                const auto target = event.completedTarget ? event.completedTarget : snapshot.guid;
                if (position(target, targetPosition)) visuals->playSpellVisual(spell->visualId, targetPosition, true);
            }
            if (sounds) {
                const auto school = localSpellSchool(spell->schoolMask);
                sounds->playCast(school);
                sounds->playImpact(school, audio::SpellSoundManager::SpellPower::MEDIUM);
            }
        }
        LOG_INFO("[LOCAL_PRESENTATION] cast complete spell=", event.completedSpell, " revision=", snapshot.castRevision);
    }
    if (event.started) {
        animate(event.startedSpell, event.startedTarget, true);
        if (const auto* spell = content.spell(event.startedSpell)) {
            glm::vec3 casterPosition;
            if (visuals && spell->visualId && position(snapshot.guid, casterPosition))
                visuals->playSpellVisualPrecast(spell->visualId, casterPosition, snapshot.castRemainingMs,
                    resolveUnitRenderInstance(snapshot.guid));
            if (sounds) sounds->playPrecast(localSpellSchool(spell->schoolMask), audio::SpellSoundManager::SpellPower::MEDIUM);
        }
        LOG_INFO("[LOCAL_PRESENTATION] cast start spell=", event.startedSpell, " remainingMs=", snapshot.castRemainingMs);
    }
}

void GameHandler::resetLocalPresentation() {
    if (localCastPresentation_.activeSpell) {
        if (auto* renderer = services_.renderer) {
            if (auto* visuals = renderer->getSpellVisualSystem()) visuals->cancelAllPrecastVisuals();
            if (auto* animation = renderer->getAnimationController()) animation->cancelSpellCast();
        }
        if (auto* audio = services_.audioCoordinator)
            if (auto* sounds = audio->getSpellSoundManager()) sounds->stopPrecast();
    }
    localCastPresentation_ = {};
    localProgressPresentation_ = {};
    localPresentationStates_.clear();
    localCastCallbackSpellId_ = 0;
    localCastCommittedThisFrame_ = false;
}

} // namespace wowee::game
