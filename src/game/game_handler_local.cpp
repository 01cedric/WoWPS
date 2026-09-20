#include "game/local_npc_auras.hpp"
#include "game/local_forms.hpp"
#include "game/local_feral_talents.hpp"
#include "game/local_melee.hpp"
#include "game/local_regeneration_rates.hpp"
#include "game/combat_handler.hpp"
#include "game/local_realm.hpp"
#include "game/local_threat_view.hpp"
#include "game/local_action_spell_ranks.hpp"
#include <chrono>
#include "game/local_aura_presentation.hpp"
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
    localActionKnownSpells_.clear();localActionRanksInitialized_=false;
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
        try {
            playerSpawnCallback_(character.guid, displayId, static_cast<uint8_t>(character.race),
                static_cast<uint8_t>(character.gender), character.appearanceBytes,
                character.facialFeatures, p.x, p.y, p.z, yaw);
        } catch (...) {
            manager.removeEntity(character.guid);
            throw;
        }
        LOG_INFO("[LOCAL_REALM] remote avatar spawn name=", character.name, " guid=", character.guid);
    }
}

void GameHandler::removeLocalExplorationPlayer(uint64_t guid) {
    if (!localExploration_ || guid == playerGuid) return;
    if (playerDespawnCallback_) playerDespawnCallback_(guid);
    entityController_->getEntityManager().removeEntity(guid);
    localEquipmentVisuals_.erase(guid);localFormVisuals_.erase(guid);
    localPresentationStates_.erase(guid);
    localGhostUnits_.erase(guid);
    if(spellHandler_)spellHandler_->removeUnitAuraCache(guid);
    LOG_INFO("[LOCAL_REALM] remote avatar removed guid=", guid);
}

bool GameHandler::syncLocalRealmPlayer(const LocalRealmPlayer& snapshot, const LocalWorldContent& content) {
    if (!localExploration_ || snapshot.mapId != currentMapId_) return false;
    if (snapshot.ghost) localGhostUnits_.insert(snapshot.guid);
    else localGhostUnits_.erase(snapshot.guid);
    if (snapshot.guid == playerGuid) {
        const bool ghostChanged = releasedSpirit_ != snapshot.ghost;
        const bool wasDead = playerDead_;
        playerDead_ = snapshot.dead;
        releasedSpirit_ = snapshot.dead && snapshot.ghost;
        corpsePositionValid_ = snapshot.corpseValid;
        corpseMapId_ = snapshot.corpseMapId;
        corpseX_ = snapshot.corpseX;
        corpseY_ = snapshot.corpseY;
        corpseZ_ = snapshot.corpseZ;
        corpseReclaimAvailableMs_ = 0;
        // Deliver the state transition before optional body allocation; an
        // exhausted heap must not swallow PLAYER_ALIVE/PLAYER_UNGHOST events.
        if (ghostChanged && ghostStateCallback_) ghostStateCallback_(releasedSpirit_);
        if (wasDead != playerDead_ || ghostChanged)
            fireAddonEvent(playerDead_ ? (releasedSpirit_ ? "PLAYER_ALIVE" : "PLAYER_DEAD") : "PLAYER_UNGHOST", {});
        // Keep a separate body at the death position while the avatar travels
        // as a ghost. The corpse uses the same equipment/appearance callbacks
        // as local players, with a reserved corpse GUID namespace.
        const bool showBody = snapshot.dead && snapshot.ghost && snapshot.corpseValid &&
                              snapshot.corpseMapId == snapshot.mapId &&
                              snapshot.corpseInstanceId == snapshot.instanceId;
        if (!showBody && localCorpseVisualGuid_) {
            removeLocalExplorationPlayer(localCorpseVisualGuid_);
            localCorpseVisualGuid_ = 0;
        }
        if (showBody) {
            localCorpseVisualGuid_ = 0xF101000000000000ULL | (snapshot.guid & 0x0000FFFFFFFFFFFFULL);
            corpseGuid_ = localCorpseVisualGuid_;
            // Only instantiate/update on appearance changes or initial return
            // to this map: no whole-player snapshot copy every ghost frame.
            if (!entityController_->getEntityManager().getEntity(localCorpseVisualGuid_) ||
                !localPresentationStates_.count(localCorpseVisualGuid_) ||
                localEquipmentVisuals_[localCorpseVisualGuid_] != snapshot.equipment) {
                LocalRealmPlayer body = snapshot;
                body.guid = localCorpseVisualGuid_;
                body.ghost = false; body.corpseValid = false;
                body.x = snapshot.corpseX; body.y = snapshot.corpseY; body.z = snapshot.corpseZ;
                body.orientation = snapshot.corpseOrientation;
                body.health = 0; body.attackTarget = 0;
                syncLocalRealmPlayer(body, content);
            }
        } else if (!snapshot.corpseValid) corpseGuid_ = 0;
    }
    const LocalUnitPresentationState current{snapshot.health, snapshot.level, snapshot.attackTarget,
                                            snapshot.dead && !snapshot.ghost};
    const auto previous = localPresentationStates_.find(snapshot.guid);
    const auto event = localUnitPresentationEvents(
        previous != localPresentationStates_.end() ? &previous->second : nullptr, current);
    Character character = localCharacterVisual(snapshot, &content, LocalEquipmentSource::AuthoritySnapshot);
    std::array<uint32_t, 19> displays{};
    std::array<uint8_t, 19> types{};
    for (size_t slot = 0; slot < displays.size(); ++slot) {
        displays[slot] = character.equipment[slot].displayModel;
        types[slot] = character.equipment[slot].inventoryType;
    }
    syncLocalExplorationPlayer(character, snapshot.orientation);
    // Publish the event baseline only after entity creation/spawn succeeds,
    // so allocation failure remains retryable on the next authority snapshot.
    localPresentationStates_[snapshot.guid] = current;
    auto unit = std::static_pointer_cast<Unit>(entityController_->getEntityManager().getEntity(snapshot.guid));
    unit->setHealth(snapshot.health);
    unit->setMaxHealth(snapshot.maxHealth);
    const auto oldMana=unit->getPowerByType(0),oldManaMax=unit->getMaxPowerByType(0);
    for(uint8_t type=0;type<7;++type){unit->setPowerByType(type,0);unit->setMaxPowerByType(type,0);}
    unit->setPowerType(static_cast<uint8_t>(snapshot.resourceType));
    unit->setPower(snapshot.mana);
    unit->setMaxPower(snapshot.maxMana);
    if(snapshot.guid==playerGuid&&snapshot.classId==11){unit->setPowerByType(0,localAvailableMana(snapshot));unit->setMaxPowerByType(0,localManaCapacity(snapshot));if(oldMana!=localAvailableMana(snapshot))fireAddonEvent("UNIT_MANA",{"player"});if(oldManaMax!=localManaCapacity(snapshot))fireAddonEvent("UNIT_MAXMANA",{"player"});}
    unit->setDynamicFlags(snapshot.dead && !snapshot.ghost ? UNIT_DYNFLAG_DEAD : 0);
    const auto* mount=localActiveMount(content,snapshot);
    const uint32_t mountDisplay=mount && !snapshot.dead ? mount->mountDisplayId : 0;
    const auto* form=localActiveForm(snapshot);
    const uint8_t formId=form?form->form:0;
    const uint32_t formDisplay=localFormDisplay(snapshot);
    const auto oldDisplay=unit->getDisplayId();
    if(formDisplay)unit->setDisplayId(formDisplay);
    else if(localFormVisuals_[snapshot.guid]){
        if(auto* assets=services_.assetManager)if(auto races=assets->loadDBC("ChrRaces.dbc"))
            for(uint32_t row=0;row<races->getRecordCount();++row)if(races->getUInt32(row,0)==snapshot.race){unit->setDisplayId(races->getUInt32(row,(snapshot.gender==1||snapshot.useFemaleModel)?5:4));break;}
    }
    const bool formChanged=localFormVisuals_[snapshot.guid]!=snapshot.formSpellId || (snapshot.guid==playerGuid&&shapeshiftFormId_!=formId);
    localFormVisuals_[snapshot.guid]=snapshot.formSpellId;
    if(formChanged){
        if(snapshot.guid==playerGuid){
            shapeshiftFormId_=formId;
            for(const char* name:{"UPDATE_SHAPESHIFT_FORM","UPDATE_SHAPESHIFT_FORMS","UPDATE_BONUS_ACTIONBAR","ACTIONBAR_PAGE_CHANGED","ACTIONBAR_UPDATE_USABLE"})fireAddonEvent(name,{});
            fireAddonEvent("UNIT_DISPLAYPOWER",{"player"});fireAddonEvent("UNIT_MODEL_CHANGED",{"player"});fireAddonEvent("UNIT_PORTRAIT_UPDATE",{"player"});
            if(playerModelRebuildCallback_)playerModelRebuildCallback_();
        }else if(oldDisplay!=unit->getDisplayId()){
            if(playerDespawnCallback_)playerDespawnCallback_(snapshot.guid);
            if(playerSpawnCallback_)playerSpawnCallback_(snapshot.guid,unit->getDisplayId(),snapshot.race,snapshot.gender,character.appearanceBytes,character.facialFeatures,unit->getX(),unit->getY(),unit->getZ(),unit->getOrientation());
        }
    }
    const auto oldMount=unit->getMountDisplayId();unit->setMountDisplayId(mountDisplay);
    if(snapshot.guid!=playerGuid && oldMount!=mountDisplay && otherPlayerMountCallback_)
        otherPlayerMountCallback_(snapshot.guid,mountDisplay);
    const float runMultiplier=mount ? 1.f+mount->mountSpeedPercent/100.f : localFormRunPercent(snapshot,content)/100.f;
    if(snapshot.guid==playerGuid && !snapshot.flight.active &&
       (formChanged || mountAuraSpellId_!=snapshot.mountSpellId || currentMountDisplayId_!=mountDisplay || localRealmRunMultiplier_!=runMultiplier)) {
        localRealmRunMultiplier_=runMultiplier;
        mountAuraSpellId_=snapshot.mountSpellId;currentMountDisplayId_=mountDisplay;
        if(mountCallback_)mountCallback_(mountDisplay);
        const float multiplier=runMultiplier;
        if(movementHandler_)movementHandler_->applyServerMovementSpeeds(
            2.5f,7.f*multiplier,4.5f,4.72222f*(form?form->swimPercent/100.f:1.f),2.5f,7.f,4.5f,3.141593f,3.141593f);
        LOG_INFO("[LOCAL_MOUNT] spell=",snapshot.mountSpellId," display=",mountDisplay," run=",7.f*multiplier);
    }
    const bool equipmentChanged = formChanged || !localEquipmentVisuals_.count(snapshot.guid) ||
        localEquipmentVisuals_[snapshot.guid] != snapshot.equipment;
    localEquipmentVisuals_[snapshot.guid] = snapshot.equipment;
    if (snapshot.guid == playerGuid) {
        const auto regen=localRegenerationRates(snapshot,content);
        const auto auraMana=localRegenerationAuraManaPer5(snapshot,content)/5.0;
        const float manaRegen=float(regen.manaPerSecond+auraMana),manaInterrupted=float(regen.manaInterruptedPerSecond+auraMana);
        const bool regenChanged=playerManaRegen_!=manaRegen||playerManaRegenCasting_!=manaInterrupted;
        playerManaRegen_=manaRegen;playerManaRegenCasting_=manaInterrupted;
        if(regenChanged)LOG_INFO("[LOCAL_REGEN] class=",unsigned(snapshot.classId)," level=",unsigned(snapshot.level),
            " form=",snapshot.formSpellId," source=",regen.sourceValues," manaPerSecond=",manaRegen,
            " interruptedPerSecond=",manaInterrupted," timedAuraPerSecond=",auraMana," talents=",snapshot.talents.size());
        const auto melee=localMeleeStats(snapshot,content);const auto armor=int32_t(localMeleeArmor(snapshot,content));
        const std::array<float,2> meleeSpeeds{localMeleeSpeed(snapshot,content),melee.offHand?localMeleeSpeed(snapshot,content,true):0.f};
        bool meleeChanged=localMeleePresentationSpeeds_!=meleeSpeeds||playerArmorRating_!=armor||equipmentChanged||playerMeleeAP_!=int32_t(melee.attackPower)||playerCritPct_!=melee.crit||
            playerDodgePct_!=melee.dodge||playerParryPct_!=melee.parry||playerBlockPct_!=melee.block||serverPlayerLevel_!=snapshot.level;
        for(size_t i=0;i<5;++i){meleeChanged=meleeChanged||playerStats_[i]!=melee.attributes[i];playerStats_[i]=melee.attributes[i];}
        localMeleePresentationSpeeds_=meleeSpeeds;
        playerArmorRating_=armor;playerMeleeAP_=int32_t(melee.attackPower);playerCritPct_=melee.crit;playerDodgePct_=melee.dodge;
        playerParryPct_=melee.parry;playerBlockPct_=melee.block;
        constexpr size_t ratingSlots[]={1,2,3,4,5,8,17,23};
        for(size_t i=0;i<8;++i){meleeChanged=meleeChanged||playerCombatRatings_[ratingSlots[i]]!=melee.ratings[i];playerCombatRatings_[ratingSlots[i]]=melee.ratings[i];}
        const auto spellCrit=localSpellCritStats(snapshot,content);
        const auto rangedCritRating=localRangedCritRating(snapshot,content);
        const auto rangedCrit=localRangedCritChance(snapshot,content);
        bool spellCritChanged=playerCombatRatings_[10]!=spellCrit.itemRating+spellCrit.auraRating||playerCombatRatings_[9]!=rangedCritRating||playerRangedCritPct_!=rangedCrit;
        playerRangedCritPct_=rangedCrit;
        playerCombatRatings_[9]=rangedCritRating;
        playerCombatRatings_[10]=spellCrit.itemRating+spellCrit.auraRating;
        for(size_t school=0;school<7;++school){const float crit=school?spellCrit.crit:0.f;
            spellCritChanged=spellCritChanged||playerSpellCritPct_[school]!=crit;playerSpellCritPct_[school]=crit;}
        if(spellCritChanged)fireAddonEvent("COMBAT_RATING_UPDATE",{});
        for(const auto& v:snapshot.meleeViews){
            if(snapshot.meleeViewPositionRevision!=snapshot.positionRevision||!v.serial||(localMeleePresentationSerial_&&int32_t(v.serial-localMeleePresentationSerial_)<=0))continue;
            localMeleePresentationSerial_=v.serial;if(!combatHandler_)continue;
            using T=CombatTextEntry::Type;T type=v.healing?T::HEAL:(v.spell?T::SPELL_DAMAGE:T::MELEE_DAMAGE);
            switch(v.outcome){case LocalMeleeOutcome::Miss:type=T::MISS;break;case LocalMeleeOutcome::Dodge:type=T::DODGE;break;
                case LocalMeleeOutcome::Parry:type=T::PARRY;break;case LocalMeleeOutcome::Critical:type=v.healing?T::CRIT_HEAL:T::CRIT_DAMAGE;break;
                case LocalMeleeOutcome::Glancing:type=T::GLANCING;break;case LocalMeleeOutcome::Crushing:type=T::CRUSHING;break;
                // The original combat-text vocabulary already declares these.
                case LocalMeleeOutcome::Resist:type=T::RESIST;break;case LocalMeleeOutcome::Immune:type=T::IMMUNE;break;
                case LocalMeleeOutcome::Deflect:type=T::DEFLECT;break;default:break;}
            // A zero-amount outcome still renders: the damage-nullifying
            // predicate, not the melee-avoidance one, decides that.
            // A full resist is one RESIST line carrying the resisted amount,
            // as the network realm's SMSG_SPELLNONMELEEDAMAGELOG renders it
            // (combat_handler.cpp:780-781); a partial resist is the damage line
            // followed by a RESIST line, exactly like a partial block.
            const bool fullResistLine=v.outcome==LocalMeleeOutcome::Resist&&v.resisted;
            if((v.amount||localOutcomeNullifiesDamage(v.outcome))&&!fullResistLine)combatHandler_->addCombatText(type,int32_t(v.amount),v.spell,v.source==playerGuid,0,v.source,v.target);
            if(v.blocked)combatHandler_->addCombatText(T::BLOCK,int32_t(v.blocked),v.spell,v.source==playerGuid,0,v.source,v.target);
            if(v.resisted)combatHandler_->addCombatText(T::RESIST,int32_t(v.resisted),v.spell,v.source==playerGuid,0,v.source,v.target);
        }
        if(comboPoints_!=snapshot.comboPoints||comboTarget_!=snapshot.comboTarget){
            comboPoints_=snapshot.comboPoints;comboTarget_=snapshot.comboTarget;
            fireAddonEvent("PLAYER_COMBO_POINTS",{});fireAddonEvent("UNIT_COMBO_POINTS",{"player"});
            fireAddonEvent("ACTIONBAR_UPDATE_USABLE",{});fireAddonEvent("SPELL_UPDATE_USABLE",{});
        }
        if(formChanged&&form&&form->bar){
            const size_t first=(6+form->bar-1)*SLOTS_PER_BAR;
            bool empty=first+SLOTS_PER_BAR<=actionBar.size();
            for(size_t i=first;empty&&i<first+SLOTS_PER_BAR;++i)empty=actionBar[i].id==0;
            if(empty){
                actionBar[first].type=ActionBarSlot::SPELL;actionBar[first].id=SPELL_ID_ATTACK;
                size_t slot=first+1;
                for(auto id:snapshot.knownSpells){const auto* d=content.spell(id);if(!d||d->passive||d->triggeredOnly||d->formId||!d->unsupportedReason.empty()||!localSpellFormReady(snapshot,*d)||
                    (d->resourceType!=255&&d->resourceType!=uint8_t(snapshot.resourceType)))continue;
                    if(slot>=first+SLOTS_PER_BAR)break;actionBar[slot].type=ActionBarSlot::SPELL;actionBar[slot++].id=id;
                }
                saveCharacterConfig();for(size_t i=first;i<first+SLOTS_PER_BAR;++i)fireAddonEvent("ACTIONBAR_SLOT_CHANGED",{std::to_string(i+1)});
            }
        }
        if(content.talentIndexReady&&(!localActionRanksInitialized_||localActionKnownSpells_!=snapshot.knownSpells)) {
            localActionRanksInitialized_=true;localActionKnownSpells_=snapshot.knownSpells;
            std::vector<size_t> changedSlots;
            for(size_t i=0;i<actionBar.size();++i) {
                auto& action=actionBar[i];if(action.type!=ActionBarSlot::SPELL||!action.id)continue;
                const auto replacement=localActionSpellRank(content,snapshot.knownSpells,action.id);
                if(replacement==action.id)continue;
                action=ActionBarSlot{};
                if(replacement){action.type=ActionBarSlot::SPELL;action.id=replacement;}
                changedSlots.push_back(i);
            }
            if(!changedSlots.empty()) {
                saveCharacterConfig();
                for(auto slot:changedSlots)fireAddonEvent("ACTIONBAR_SLOT_CHANGED",{std::to_string(slot+1)});
                fireAddonEvent("ACTIONBAR_UPDATE_STATE",{});
            }
        }
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
        // Publish stat events after all source fields and the level are installed.
        // Regen-only talent/aura changes still refresh the original paperdoll.
        if(meleeChanged){for(const char* event:{"UNIT_RESISTANCES","UNIT_STATS","UNIT_ATTACK_POWER","UNIT_DAMAGE","UNIT_ATTACK_SPEED"})fireAddonEvent(event,{"player"});fireAddonEvent("COMBAT_RATING_UPDATE",{});}
        else if(regenChanged)fireAddonEvent("UNIT_STATS",{"player"});
        if(spellHandler_){
            spellHandler_->syncLocalTalents(snapshot.talents,snapshot.level);
            auto& list=spellHandler_->getPlayerAurasMut();
            const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
            // Timed stat auras, healing views and derived area aura applications,
            // in that fixed order and bounded by the presentation (see
            // include/game/local_aura_presentation.hpp). An area aura row carries
            // zero durations - the buff has no timer - and its own flag byte,
            // whose effect bits are empty when a dominant source stripped it.
            const auto count=localOwnerAuraCount(snapshot);
            bool changed=list.size()!=count;
            for(size_t i=0;i<count&&!changed;++i){const auto a=localOwnerAuraAt(snapshot,content,i);
                changed=list[i].spellId!=a.spellId||list[i].casterGuid!=a.casterGuid||list[i].charges!=a.stacks||
                    list[i].flags!=a.flags||std::abs(int64_t(list[i].getRemainingMs(now))-a.remainingMs)>1500;
            }
            list.resize(count);
            for(size_t i=0;i<count;++i){const auto a=localOwnerAuraAt(snapshot,content,i);auto& slot=list[i];
                slot.spellId=a.spellId;slot.flags=a.flags;slot.level=snapshot.level;slot.charges=a.stacks;
                slot.durationMs=a.remainingMs;slot.maxDurationMs=a.durationMs;slot.casterGuid=a.casterGuid;slot.receivedAtMs=now;
            }
            spellHandler_->mirrorAurasByGuid(snapshot.guid,list);
            if(changed&&addonEventCallback_){addonEventCallback_("UNIT_AURA",{"player"});addonEventCallback_("PLAYER_AURAS_CHANGED",{});}
        }
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
                    if(const auto* source=localMeleeItem(item->id);source&&source->itemClass==2&&!source->scaling&&source->damage[0]<=source->damage[1]&&source->damage[2]<=source->damage[3]){
                        visual.damageMin=source->damage[0]+source->damage[2];visual.damageMax=source->damage[1]+source->damage[3];visual.delayMs=source->delay;
                    }
                }
                inventory.setEquipSlot(kLocalEquipmentVisualSlots[i], visual);
            }
        }
        if (playerHealthCallback_) playerHealthCallback_(snapshot.health, snapshot.maxHealth);
        if ((event.deathPose || event.respawn) && standStateCallback_)
            standStateCallback_(snapshot.dead && !snapshot.ghost ? 7 : 0);
        // A nonlethal wound must not visually cancel a wind-up that the
        // authority still runs, including its replicated pushback delay.
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
    if(spellHandler_) {
        const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        const auto* prior=spellHandler_->getUnitAuras(npc.guid);
        std::vector<AuraSlot> auras;auras.reserve(npc.snares.size()+npc.damageAuras.size()+npc.stormstrikeAuras.size()+npc.controls.size());
        for(const auto& a:npc.controls) {
            const auto* realm=localServiceRealm();const auto* d=realm?realm->content().spell(a.spellId):nullptr;
            AuraSlot slot{};slot.spellId=a.spellId;slot.flags=0x9f;slot.level=npc.level;slot.charges=1;
            slot.durationMs=a.remainingMs;slot.maxDurationMs=d?d->durationMs:a.remainingMs;
            slot.casterGuid=a.casterGuid;slot.receivedAtMs=now;auras.push_back(slot);
        }
        for(const auto& a:npc.snares) {
            const auto* realm=localServiceRealm();const auto* d=realm?realm->content().spell(a.spellId):nullptr;
            AuraSlot slot{};slot.spellId=a.spellId;slot.flags=0x9f;slot.level=npc.level;slot.charges=1;
            slot.durationMs=a.remainingMs;slot.maxDurationMs=d?d->durationMs:a.remainingMs;
            slot.casterGuid=a.casterGuid;slot.receivedAtMs=now;auras.push_back(slot);
        }
        for(const auto& a:npc.damageAuras) {
            AuraSlot slot{};slot.spellId=a.spellId;slot.flags=0x9f;slot.level=npc.level;slot.charges=a.stacks;
            slot.durationMs=a.remainingMs;slot.maxDurationMs=a.durationMs;slot.casterGuid=a.casterGuid;slot.receivedAtMs=now;auras.push_back(slot);
        }
        for(const auto& a:npc.stormstrikeAuras) {
            AuraSlot slot{};slot.spellId=a.spellId;slot.flags=0x9f;slot.level=npc.level;slot.charges=a.charges;
            slot.durationMs=a.remainingMs;slot.maxDurationMs=12000;slot.casterGuid=a.casterGuid;slot.receivedAtMs=now;auras.push_back(slot);
        }
        bool changed=!prior?!auras.empty():prior->size()!=auras.size();
        if(!changed&&prior)for(size_t i=0;i<auras.size();++i) {
            const auto& old=(*prior)[i];const auto& a=auras[i];
            if(old.spellId!=a.spellId||old.casterGuid!=a.casterGuid||old.charges!=a.charges||old.maxDurationMs!=a.maxDurationMs||
               std::abs(int64_t(old.getRemainingMs(now))-a.durationMs)>1500){changed=true;break;}
        }
        spellHandler_->mirrorAurasByGuid(npc.guid,auras);
        if(changed&&addonEventCallback_&&getTargetGuid()==npc.guid)addonEventCallback_("UNIT_AURA",{"target"});
    }
    if(getTargetGuid()==npc.guid)if(const auto* realm=localServiceRealm())if(const auto* player=realm->localPlayer()) {
        const auto threat=localThreatView(npc,*player);
        const std::array<uint64_t,4> signature{threat.amount,threat.rawBasisPoints,threat.scaledBasisPoints,uint64_t(threat.status)|(uint64_t(threat.present)<<8)};
        if(localThreatTargetGuid_!=npc.guid||localThreatTargetSignature_!=signature) {
            localThreatTargetGuid_=npc.guid;localThreatTargetSignature_=signature;
            if(addonEventCallback_){addonEventCallback_("UNIT_THREAT_LIST_UPDATE",{"target"});addonEventCallback_("UNIT_THREAT_SITUATION_UPDATE",{"player"});}
        }
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
        (npc.banker ? kLocalNpcFlagBanker : 0u) | (npc.innkeeper ? kLocalNpcFlagInnkeeper : 0u) | (npc.flightMaster ? 0x2000u : 0u));
    unit->setHostile(npc.hostile);
    unit->setFactionTemplate(npc.hostile ? 14 : 12);
    unit->setDynamicFlags((npc.dead ? UNIT_DYNFLAG_DEAD : 0) |
                          (npc.lootable ? UNIT_DYNFLAG_LOOTABLE : 0));
    // P04: the authority's control list, expressed in the field the client
    // already carries. Nothing else is needed - the entity has the field and
    // the pick and nameplate code already read it.
    unit->setUnitFlags((localNpcStunned(npc) ? UNIT_FLAG_STUNNED : 0u) |
                       (localNpcSilenced(npc) ? UNIT_FLAG_SILENCED : 0u));
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

void GameHandler::syncLocalRealmPet(const LocalRealmPet& pet) {
    if (!localExploration_ || pet.mapId != currentMapId_) return;
    auto& manager = entityController_->getEntityManager();
    auto entity = manager.getEntity(pet.guid);
    const bool fresh = !entity;
    if (fresh) {
        entity = std::make_shared<Unit>(pet.guid);
        manager.addEntity(pet.guid, entity);
    }
    auto unit = std::static_pointer_cast<Unit>(entity);
    const LocalUnitPresentationState current{pet.health, pet.level, pet.targetGuid, pet.dead};
    const auto previous = localPresentationStates_.find(pet.guid);
    const auto event = localUnitPresentationEvents(
        previous != localPresentationStates_.end() ? &previous->second : nullptr, current);
    localPresentationStates_[pet.guid] = current;
    const auto p = core::coords::serverToCanonical(glm::vec3(pet.x, pet.y, pet.z));
    const float yaw = core::coords::serverToCanonicalYaw(pet.orientation);
    if (fresh || pet.dead) entity->setPosition(p.x, p.y, p.z, yaw);
    else {
        const glm::vec3 destination(entity->getLatestX(), entity->getLatestY(), entity->getLatestZ());
        if (glm::dot(p - destination, p - destination) > 0.0001f)
            entity->startMoveTo(p.x, p.y, p.z, yaw, 0.1f);
        else entity->setOrientation(yaw);
    }
    unit->setName(pet.name);
    unit->setEntry(pet.entry);
    unit->setDisplayId(pet.displayId);
    unit->setLevel(pet.level);
    unit->setHealth(pet.health);
    unit->setMaxHealth(pet.maxHealth);
    // A summon offers no service, is never attackable by its owner and carries
    // no loot: no npc flags, the friendly faction template, no lootable bit.
    unit->setNpcFlags(0);
    unit->setHostile(false);
    unit->setFactionTemplate(12);
    // resourceType 255 is a summon with no bar. Clearing all seven slots first
    // and leaving max power at zero is what makes FrameXML hide it; the player
    // path at the top of this file does the same for the same reason.
    for(uint8_t type=0;type<7;++type){unit->setPowerByType(type,0);unit->setMaxPowerByType(type,0);}
    unit->setPowerType(pet.resourceType == 255 ? 0 : pet.resourceType);
    unit->setPower(pet.power);
    unit->setMaxPower(pet.maxPower);
    // UNIT_FLAG_PLAYER_CONTROLLED is what separates the pet the owner commands
    // from a guardian that merely belongs to them.
    unit->setUnitFlags(pet.kind == LocalPetKind::Controlled ? UNIT_FLAG_PLAYER_CONTROLLED : 0u);
    unit->setDynamicFlags(pet.dead ? UNIT_DYNFLAG_DEAD : 0);
    // Who summoned it. The unit APIs read the owner out of these two halves;
    // an expansion whose table has no wire index for them cannot carry the
    // relationship at all, so write nothing rather than field index 0xFFFF.
    const auto summonedByLo = fieldIndex(UF::UNIT_FIELD_SUMMONEDBY_LO);
    const auto summonedByHi = fieldIndex(UF::UNIT_FIELD_SUMMONEDBY_HI);
    if (summonedByLo != 0xFFFF && summonedByHi != 0xFFFF) {
        entity->setField(summonedByLo, static_cast<uint32_t>(pet.ownerGuid & 0xFFFFFFFFULL));
        entity->setField(summonedByHi, static_cast<uint32_t>(pet.ownerGuid >> 32));
    }
    if (fresh && creatureSpawnCallback_) {
        creatureSpawnCallback_(pet.guid, pet.displayId, p.x, p.y, p.z, yaw, 1.0f);
        LOG_INFO("[LOCAL_GAMEPLAY] pet spawn entry=", pet.entry, " name=", pet.name,
                 " guid=", pet.guid, " display=", pet.displayId, " owner=", pet.ownerGuid);
    }
    if (event.deathPose && npcDeathCallback_) npcDeathCallback_(pet.guid);
    if (event.respawn && npcRespawnCallback_) npcRespawnCallback_(pet.guid);
    if (event.wound && hitReactionCallback_)
        hitReactionCallback_(pet.guid, HitReaction::WOUND);
    if (event.aggro && npcAggroCallback_) npcAggroCallback_(pet.guid, p);
    if (auto* audio = services_.audioCoordinator) if (auto* voices = audio->getNpcVoiceManager()) {
        const auto renderPosition = core::coords::canonicalToRender(p);
        if (event.deathSound) voices->playCombatDeath(pet.guid, pet.displayId, renderPosition);
        else if (event.wound) voices->playCombatWound(pet.guid, pet.displayId, renderPosition);
    }
    if (event.deathPose || event.respawn)
        LOG_INFO("[LOCAL_PRESENTATION] pet=", pet.guid, event.deathPose ? " death pose" : " respawn pose");
}

void GameHandler::removeLocalRealmNpc(uint64_t guid) {
    if (!localExploration_) return;
    clearTransportAttachment(guid);
    if (creatureDespawnCallback_) creatureDespawnCallback_(guid);
    entityController_->getEntityManager().removeEntity(guid);
    localPresentationStates_.erase(guid);
    localGhostUnits_.erase(guid);
    if(spellHandler_)spellHandler_->removeUnitAuraCache(guid);
    if (targetGuid == guid) targetGuid = 0;
}

void GameHandler::removeLocalRealmPet(uint64_t guid) {
    if (!localExploration_) return;
    if (creatureDespawnCallback_) creatureDespawnCallback_(guid);
    entityController_->getEntityManager().removeEntity(guid);
    localPresentationStates_.erase(guid);
    localGhostUnits_.erase(guid);
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
    // UnitCastingInfo and FrameXML use the spell ID as their cast token.
    // The owner wire sequence separately distinguishes an actual recast from
    // pushback, even if an intermediate cancellation snapshot was not received.
    const auto castEvent = [&](const char* name, uint32_t id) {
        fireAddonEvent(name, {"player", getSpellName(id), getSpellRank(id),
            std::to_string(id), std::to_string(id)});
    };
    if (event.stopPrecast) {
        if (visuals) visuals->cancelAllPrecastVisuals();
        if (sounds) sounds->stopPrecast();
    }
    if (event.interrupted) {
        if (renderer) if (auto* animation = renderer->getAnimationController()) animation->cancelSpellCast();
        castEvent(snapshot.castStatus == LocalCastStatus::Failed ? "UNIT_SPELLCAST_FAILED" : "UNIT_SPELLCAST_INTERRUPTED", event.previousSpell);
        castEvent("UNIT_SPELLCAST_STOP", event.previousSpell);
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
        castEvent("UNIT_SPELLCAST_SUCCEEDED", event.completedSpell);
        castEvent("UNIT_SPELLCAST_STOP", event.completedSpell);
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
        castEvent("UNIT_SPELLCAST_START", event.startedSpell);
        LOG_INFO("[LOCAL_PRESENTATION] cast start spell=", event.startedSpell, " remainingMs=", snapshot.castRemainingMs);
    }
    if (event.delayed) fireAddonEvent("UNIT_SPELLCAST_DELAYED", {"player"});
    if (visuals && snapshot.castingSpellId)
        visuals->synchronizePrecastRemaining(resolveUnitRenderInstance(snapshot.guid), snapshot.castRemainingMs);
}

void GameHandler::resetLocalPresentation() {
    playerDead_ = false;
    releasedSpirit_ = false;
    corpsePositionValid_ = false;
    corpseGuid_ = 0;
    corpseReclaimAvailableMs_ = 0;
    corpseInRangeAnnounced_ = false;
    deathReleaseValid_ = false;
    localMeleePresentationSerial_=0;localMeleePresentationSpeeds_={};
    comboPoints_=0;comboTarget_=0;fireAddonEvent("PLAYER_COMBO_POINTS",{});
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
    localGhostUnits_.clear();
    localCorpseVisualGuid_ = 0;
    localCastCallbackSpellId_ = 0;
    localCastCommittedThisFrame_ = false;
}

} // namespace wowee::game
