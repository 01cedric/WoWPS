#pragma once
#include "game/local_collision.hpp"
#include "game/local_combat_events.hpp"
#include "game/local_runes.hpp"
#include "game/local_equipment.hpp"
#include "game/local_travel.hpp"
#include "game/reputation_standing.hpp"
#include "game/local_quest_chain.hpp"
#include "game/local_script_actions.hpp"
#include "game/local_world_event.hpp"
#include "game/local_vehicle_effects.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee::game {
/// P05 line of sight . Defined in game/local_line_of_sight.hpp, which the
/// authority's own translation unit includes; every other consumer of this
/// header only ever sees the pointer.
class LocalCollisionData;
class LocalWorldCatalog;
class LocalVendorInventory;
enum class LocalResourceType : uint8_t { Mana = 0, Rage = 1, Energy = 3, RunicPower = 6 };
struct LocalFactionTemplate {
    uint32_t id = 0, faction = 0, flags = 0, factionGroup = 0, friendGroup = 0, enemyGroup = 0;
    std::array<uint32_t, 4> enemies{}, friends{};
};
struct LocalFactionReputationBase {
    uint32_t factionId = 0;
    std::array<uint32_t,4> raceMasks{}, classMasks{};
    std::array<int32_t,4> base{};
};
inline int32_t localFactionBaseReputation(const LocalFactionReputationBase& row, uint8_t race, uint8_t classId, bool* matched = nullptr) {
    const uint32_t raceMask = race && race <= 32 ? (1u << (race - 1)) : 0;
    const uint32_t classMask = classId && classId <= 32 ? (1u << (classId - 1)) : 0;
    for (size_t i=0;i<4;++i) {
        if ((!row.raceMasks[i] || (row.raceMasks[i] & raceMask)) &&
            (!row.classMasks[i] || (row.classMasks[i] & classMask))) {
            if (matched) *matched = true;
            return row.base[i];
        }
    }
    if (matched) *matched = false;
    return 0;
}
struct LocalAreaTriggerVolume {
    uint32_t id = 0, mapId = 0;
    // Raw/server coordinates from AreaTrigger.dbc, not render-space coordinates.
    float x = 0, y = 0, z = 0, radius = 0;
    float boxLength = 0, boxWidth = 0, boxHeight = 0, boxYaw = 0;
};
/// One SkillLine.dbc row, as the local realm needs it. Category 9 is a
/// secondary skill and 11 a primary profession; nothing else is trainable here.
struct LocalSkillLine {
    uint32_t id = 0, category = 0;
    std::string name;
};
inline constexpr uint32_t kLocalSkillCategorySecondary = 9;
inline constexpr uint32_t kLocalSkillCategoryProfession = 11;
struct LocalRealmPortal {
    uint32_t id = 0, sourceMapId = 0, targetMapId = 0;
    std::string name;
    bool instanceMap = false;
    // Map.dbc InstanceType: 0 world, 1 party dungeon, 2 raid, 3 battleground,
    // 4 arena. Zero when the client has no row for the target map, in which
    // case instanceMap rests on the catalog alone as it always did.
    uint32_t instanceType = 0, maxPlayers = 0;
};
/// One row of the client's Map.dbc, as the local realm needs it. This is what
/// makes a dungeon or raid entrance usable on the client's own authority rather
/// than only when the world catalog happens to carry the map.
struct LocalMapDefinition {
    uint32_t id = 0, instanceType = 0, maxPlayers = 0, expansion = 0;
    std::string name;
    bool instance() const { return instanceType == 1 || instanceType == 2; }
};
struct LocalInstanceState {
    uint32_t id = 0, mapId = 0;
    // 0: legacy shared; human GUID: solo private; high-bit key: a session
    // party's durable instance owner. This is not the transient party ID.
    uint64_t groupId = 0;
};

struct LocalItemInstanceState {
    // Concrete item-object state. These values intentionally mirror the
    // per-instance portion of ItemDef so a local-realm item can travel through
    // bank, mail, trade and auction escrow without being reconstructed from its
    // template and losing its roll/enchant/durability state.
    uint32_t instanceFlags = 0;
    uint32_t permanentEnchantId = 0;
    uint32_t temporaryEnchantId = 0;
    std::array<uint32_t,3> socketEnchantIds{};
    uint32_t curDurability = 0, maxDurability = 0;
    int32_t randomPropertyId = 0;
    uint32_t suffixFactor = 0;
    bool soulbound = false;
    bool operator==(const LocalItemInstanceState&) const = default;
};
struct LocalItemStack {
    uint32_t itemId = 0;
    uint16_t count = 0;
    uint8_t bagSlot = 255;
    LocalItemInstanceState instance{};
    bool operator==(const LocalItemStack&) const = default;
};
inline constexpr size_t kLocalBankSlots = 28;
struct LocalMerchantBuyback {
    uint32_t id = 0, itemId = 0, price = 0;
    uint16_t count = 0;
    bool operator==(const LocalMerchantBuyback&) const = default;
};
inline constexpr size_t kLocalMaxBuyback = 12;
/// Depleted physical merchant offer. Restocking uses active simulation time;
/// offline time does not replenish stock. Only the partial interval is saved.
struct LocalVendorStockRecord {
    uint64_t npcGuid = 0;
    uint32_t entry = 0, itemId = 0, remaining = 0;
    uint64_t elapsedMs = 0;
    bool operator==(const LocalVendorStockRecord&) const = default;
};
/// A profession this character has learned. `skillId` is a SkillLine.dbc id,
/// `max` the rank cap a trainer has sold them, `current` the points earned
/// towards it. Points come from crafting; `progress` is the fraction of the
/// next point already earned, in thousandths - see localCraftSkillChance()
/// about why this realm accumulates the client's own skill-up chance rather
/// than rolling it.
inline constexpr size_t kLocalMaxStatAuras = 16;
inline constexpr size_t kLocalMaxHealingAuraViews=8;
struct LocalHealingAuraView {
    uint32_t spellId=0,remainingMs=0,durationMs=0;
    uint64_t casterGuid=0;
    uint8_t stacks=1;
    // Harmful (creature-cast) views only: the effect amounts the authority
    // resolved at application. Healing views keep both zero.
    int32_t armorModifier=0;
    uint8_t slowPercent=0;
    int8_t armorPercent=0; // LAN105: MOD_RESISTANCE_PCT on armor (TOTAL_PCT)
    uint8_t controlKind=0; // LAN106: 1 = MOD_STUN, 2 = MOD_ROOT, LAN107: 3 = MOD_FEAR, 4 = MOD_CONFUSE, 5 = MOD_SILENCE
    // LAN107: the remaining creature-cast amounts resolved at application.
    // schoolMask scopes the damage-done and damage-taken modifiers.
    int32_t attackPower=0,damageDoneFlat=0,damageTakenFlat=0;
    int16_t damageDonePct=0,damageTakenPct=0,healingPct=0,hastePct=0;
    uint8_t schoolMask=0;
    bool breakOnDamage=false; // AURA_INTERRUPT_FLAG_TAKE_DAMAGE on the control
    // LAN108: MOD_CASTING_SPEED_NOT_STACK, MOD_HIT_CHANCE, MOD_DODGE/PARRY/BLOCK_PERCENT,
    // MOD_RESISTANCE on a magic school and MOD_DISARM from creature casts.
    int16_t castSpeedPct=0;
    int8_t hitChancePct=0,dodgePct=0,parryPct=0,blockPct=0;
    int32_t resistance=0;
    uint8_t resistanceSchool=0;
    bool disarmed=false;
    bool operator==(const LocalHealingAuraView&) const = default;
};
/// Spell::EffectInterruptCast: the interrupted school(s) stay locked for the
/// interrupting spell's duration (Unit::ProhibitSpellSchool).
struct LocalSchoolLockout { uint8_t schoolMask=0; uint32_t remainingMs=0; bool operator==(const LocalSchoolLockout&) const = default; };
/// Distinct school masks a character can be locked out of at once (wire bound).
inline constexpr size_t kLocalMaxSchoolLockouts=8;
/// What an owner projects: one indefinite raid area aura. There is no timer
/// here by construction - every consumer of LocalStatAura::remainingMs treats it
/// as a decrementing lease, and an indefinite emitter has no lease. Rules,
/// bounds and source anchors live in include/game/local_area_aura.hpp.
struct LocalAreaAuraEmitter {
    uint32_t spellId=0, mapId=0, instanceId=0;
    /// Effect-zero amount, computed once through the original caster at
    /// activation, as AuraEffect::CalculateAmount does.
    uint32_t amount=0;
    /// Which of the three source effects this emitter carries. Zero-amount
    /// marker effects are carried and contribute nothing, which is what the
    /// reference does; it is not a claim that their aura types are implemented.
    uint8_t effectMask=0;
    /// Authority-only identity of one activation; never serialized.
    uint64_t generation=0;
    bool operator==(const LocalAreaAuraEmitter&) const = default;
};
/// What a recipient holds. Derived, rebuilt by the authority, never saved and
/// never accepted from a client. `effective` false is the reference's
/// "application kept, effects stripped" state (SpellAuras.cpp:621-625), which
/// exists so a dominant neighbour can still be found.
struct LocalAreaAuraApplication {
    uint32_t spellId=0, mapId=0, instanceId=0;
    uint64_t emitterGuid=0, emitterGeneration=0;
    uint32_t amount=0;
    uint8_t effectMask=0;
    bool effective=false;
    bool operator==(const LocalAreaAuraApplication&) const = default;
};
struct LocalStatAura {
    uint32_t spellId=0, remainingMs=0, mapId=0, instanceId=0;
    uint64_t casterGuid=0; // zero denotes legacy/self caster
    uint32_t absorbRemaining=0;
    uint8_t stacks=1;
    uint8_t procCharges=0;
    uint32_t procCooldownMs=0, manaRegenRemainder=0;
    uint32_t procAmountSnapshot=0;
    bool hasProcAmountSnapshot=false;
    uint32_t buffArmorSnapshot=0; // Caster-modified armor; zero reads legacy unmodified amount.
    uint64_t costModGeneration=0; // Session-only identity; a refresh cannot spend an older cast reservation.
    uint16_t reflectChanceBasisPointsSnapshot=0; // Ward effect1 at application; persisted.
    uint64_t applicationGeneration=0; // Authority-only identity of one aura application; never serialized.
    bool operator==(const LocalStatAura&) const = default;
};
struct LocalProfessionSkill { uint16_t skillId = 0, current = 0, max = 0, progress = 0; };
struct LocalRecipeAccess {
    uint32_t races = 0, classes = 0, excludedRaces = 0, excludedClasses = 0;
};
/// One trade-skill recipe, joined from the client's own SkillLineAbility.dbc
/// and Spell.dbc. Nothing here is authored: the reagents, the item produced and
/// the skill thresholds are all columns of the player's own data files.
struct LocalRecipe {
    uint32_t spellId = 0;
    uint16_t skillId = 0;
    /// SkillLineAbility.MinSkillLineRank: the skill needed to learn it.
    uint16_t requiredSkill = 0;
    /// TrivialSkillLineRankHigh/Low: the skill at which this recipe stops
    /// granting points (grey) and where it starts to taper (green).
    uint16_t trivialHigh = 0, trivialLow = 0;
    uint32_t createdItemId = 0;
    uint16_t createdCount = 1;
    std::string name;
    /// Spell.dbc Reagent[8]/ReagentCount[8], as many as the recipe declares.
    std::vector<LocalItemStack> reagents;
    // Non-consumed Spell.dbc Totem item requirements. Tool-category and world
    // focus rules are separate and are not represented by these exact IDs.
    std::array<uint32_t, 2> tools{};
    // Alternative SkillLineAbility race/class restrictions for this skill.
    std::vector<LocalRecipeAccess> access;
    // Keep previously learned recipe IDs loadable even when a source rule
    // cannot be executed. Such a recipe is visible but cannot be sold/crafted.
    std::string unsupportedReason;
};
struct LocalCooldown { uint32_t spellId = 0; uint32_t remainingMs = 0; };
inline constexpr size_t kLocalMaxCategoryCooldowns=16;
struct LocalCategoryCooldown { uint32_t category=0,family=0,remainingMs=0; };
enum class LocalCastStatus : uint8_t { None = 0, Casting, Finished, Interrupted, Failed };
enum class LocalQuestStatus : uint8_t { Active = 0, Complete = 1, Rewarded = 2 };
struct LocalQuestProgress {
    uint32_t id = 0;
    LocalQuestStatus status = LocalQuestStatus::Active;
    std::vector<uint16_t> progress;
};
struct LocalReputationEntry {
    uint32_t factionId = 0;
    int32_t standing = 0;
};
inline constexpr size_t kLocalMaxReputations = 128;

// Persistent, authority-owned state for data-driven world scripts.  The value
// is deliberately a signed 32-bit scalar: quest/event scripts can use it as a
// counter, enum or boolean without inventing a new save field for every event.
// Rows are kept sorted by scriptId so save/LAN validation is deterministic.
struct LocalScriptState {
    uint32_t scriptId = 0;
    int32_t value = 0;
    bool operator==(const LocalScriptState&) const = default;
};
inline constexpr size_t kLocalMaxScriptStates = 64;

// Save37/LAN92: bounded one-shot script timers.  Only the timer identity and
// remaining authority time are persisted; the action remains immutable world
// content.  Rows are kept sorted by timerId for deterministic save/wire data.
struct LocalScriptTimer {
    uint32_t timerId = 0;
    uint32_t remainingMs = 0;
    bool operator==(const LocalScriptTimer&) const = default;
};
inline constexpr size_t kLocalMaxScriptTimers = 16;

// Small data-driven bridge from authoritative gameplay events into the
// persistent state introduced by Save36/LAN91.  5.3 extends the same atomic
// transition with start/restart/cancel operations for bounded script timers.
enum class LocalScriptTriggerKind : uint8_t {
    QuestAccept = 0, QuestComplete = 1, QuestReward = 2, NpcTalk = 3, NpcKill = 4,
    VehicleEnter = 5, VehicleExit = 6, QuestAbandon = 7, AreaEnter = 8, AreaLeave = 9, ObjectUse = 10, EscortStart = 11, EscortWaypoint = 12, EscortComplete = 13, EscortFail = 14
};
enum class LocalScriptValueOp : uint8_t { None = 0, Set = 1, Add = 2 };
struct LocalScriptTrigger {
    LocalScriptTriggerKind kind = LocalScriptTriggerKind::QuestAccept;
    uint32_t sourceId = 0;
    uint32_t requiredScriptId = 0;
    int32_t requiredValue = 0;
    uint32_t scriptId = 0;
    LocalScriptValueOp valueOp = LocalScriptValueOp::None;
    int32_t value = 0;
    uint32_t addPhaseMask = 0, removePhaseMask = 0;
    uint32_t scheduleTimerId = 0, scheduleDelayMs = 0, cancelTimerId = 0;
    std::vector<uint32_t> actionIds;
};
struct LocalScriptTimerAction {
    uint32_t timerId = 0;
    uint32_t requiredScriptId = 0;
    int32_t requiredValue = 0;
    uint32_t scriptId = 0;
    LocalScriptValueOp valueOp = LocalScriptValueOp::None;
    int32_t value = 0;
    uint32_t addPhaseMask = 0, removePhaseMask = 0;
    uint32_t scheduleTimerId = 0, scheduleDelayMs = 0, cancelTimerId = 0;
    std::vector<uint32_t> actionIds;
};
struct LocalEscortPoint { float x=0,y=0,z=0; uint32_t waitMs=0; };
struct LocalEscortRoute {
    uint32_t id=0,questId=0,spawnId=0;
    float speed=2.5f,followRadius=25,failRadius=100;
    uint32_t timeoutMs=600000;
    std::vector<LocalEscortPoint> points;
    bool combat=false;
    float combatChaseRadius=10;
};
struct LocalEscortProgress {
    uint32_t routeId=0,nextPoint=0,waitMs=0,remainingMs=0;
    float x=0,y=0,z=0;
    uint32_t guideHealth=0; // Save41. Zero hydrates a legacy non-combat route once.
    bool operator==(const LocalEscortProgress&)const=default;
};
inline bool validLocalEscortProgress(const LocalEscortProgress& e) {
    if(!e.routeId)return e==LocalEscortProgress{};
    return e.nextPoint<=64 && e.waitMs<=60000 && e.remainingMs && e.remainingMs<=1800000 && e.guideHealth<=1000000000 &&
        std::isfinite(e.x)&&std::isfinite(e.y)&&std::isfinite(e.z)&&
        std::abs(e.x)<=100000 && std::abs(e.y)<=100000 && std::abs(e.z)<=20000;
}
struct LocalScriptArea {
    uint32_t id = 0, mapId = 0;
    float x = 0, y = 0, z = 0, radius = 1, hysteresis = .5f;
    uint32_t requiredPhaseMask = 0, excludedPhaseMask = 0;
};
inline constexpr size_t kLocalMaxScriptAreas = 32;
inline bool validLocalScriptAreaIds(const std::vector<uint32_t>& ids) {
    if(ids.size()>kLocalMaxScriptAreas)return false;
    uint32_t previous=0;
    for(auto id:ids) {if(!id || id<=previous)return false;previous=id;}
    return true;
}
inline bool validLocalScriptMutation(uint32_t scriptId, LocalScriptValueOp valueOp, int32_t value,
                                     uint32_t addPhaseMask, uint32_t removePhaseMask) {
    if (addPhaseMask & removePhaseMask) return false;
    if (unsigned(valueOp) > unsigned(LocalScriptValueOp::Add)) return false;
    if (valueOp == LocalScriptValueOp::None) {
        if (scriptId || value) return false;
    } else if (!scriptId) return false;
    return valueOp != LocalScriptValueOp::None || addPhaseMask || removePhaseMask;
}
inline bool validLocalScriptTrigger(const LocalScriptTrigger& trigger) {
    if (!trigger.sourceId || (trigger.addPhaseMask & trigger.removePhaseMask)) return false;
    if (unsigned(trigger.kind) > unsigned(LocalScriptTriggerKind::EscortFail) ||
        unsigned(trigger.valueOp) > unsigned(LocalScriptValueOp::Add)) return false;
    if ((trigger.scheduleTimerId == 0) != (trigger.scheduleDelayMs == 0)) return false;
    if (trigger.scheduleTimerId && trigger.scheduleTimerId == trigger.cancelTimerId) return false;
    const bool hasMutation = trigger.valueOp != LocalScriptValueOp::None || trigger.scriptId || trigger.value ||
                             trigger.addPhaseMask || trigger.removePhaseMask;
    if (hasMutation && !validLocalScriptMutation(trigger.scriptId, trigger.valueOp, trigger.value,
                                                  trigger.addPhaseMask, trigger.removePhaseMask)) return false;
    return validLocalScriptActionRefs(trigger.actionIds) &&
           (hasMutation || trigger.scheduleTimerId || trigger.cancelTimerId || !trigger.actionIds.empty());
}
inline LocalScriptTrigger localScriptTimerTransition(const LocalScriptTimerAction& action) {
    LocalScriptTrigger transition;
    transition.sourceId=action.timerId;
    transition.requiredScriptId=action.requiredScriptId;transition.requiredValue=action.requiredValue;
    transition.scriptId=action.scriptId;transition.valueOp=action.valueOp;transition.value=action.value;
    transition.addPhaseMask=action.addPhaseMask;transition.removePhaseMask=action.removePhaseMask;
    transition.scheduleTimerId=action.scheduleTimerId;transition.scheduleDelayMs=action.scheduleDelayMs;
    transition.cancelTimerId=action.cancelTimerId;
    transition.actionIds=action.actionIds;
    return transition;
}
inline bool validLocalScriptTimerAction(const LocalScriptTimerAction& action) {
    return validLocalScriptTrigger(localScriptTimerTransition(action));
}
inline bool validLocalScriptStates(const std::vector<LocalScriptState>& states) {
    if (states.size() > kLocalMaxScriptStates) return false;
    uint32_t previous = 0;
    for (const auto& state : states) {
        if (!state.scriptId || state.scriptId <= previous) return false;
        previous = state.scriptId;
    }
    return true;
}
inline bool validLocalScriptTimers(const std::vector<LocalScriptTimer>& timers) {
    if (timers.size() > kLocalMaxScriptTimers) return false;
    uint32_t previous = 0;
    for (const auto& timer : timers) {
        if (!timer.timerId || !timer.remainingMs || timer.timerId <= previous) return false;
        previous = timer.timerId;
    }
    return true;
}
// P04 diminishing returns, on creatures and (2.35) on characters. Authority-only:
// a guest renders what the host owns, and the diminished result already travels
// as the control's remaining time, so this record is neither persisted nor replicated.
struct LocalNpcDiminishing {
    uint8_t group=0, hitCount=0, stack=0;
    // Stamped when the last aura of the group is removed. Held at the width of
    // the clock it is compared against: truncating it to 32 bits turns the
    // window comparison into nonsense once a realm has been up 49.7 days, which
    // would silently switch diminishing returns off rather than wrap.
    uint64_t hitTimeMs=0;
};
/// 2.40: the gossip tables (gossip_menu, gossip_menu_option, npc_text and
/// their `conditions` rows) as the catalog's gossip packs carry them.
struct LocalGossipCondition {
    uint8_t type = 0, elseGroup = 0;   // ConditionMgr.h ConditionTypes; the OR group
    bool negative = false;
    uint32_t value1 = 0, value2 = 0, value3 = 0;
};
struct LocalGossipMenuText {
    uint32_t textId = 0;
    std::vector<LocalGossipCondition> conditions;
};
struct LocalGossipOption {
    uint16_t id = 0;
    uint8_t icon = 0, type = 0;        // GossipOptionIcon, GossipOptionType
    uint32_t npcFlag = 0, actionMenuId = 0, boxMoney = 0;
    bool boxCoded = false;
    std::string text, boxText;
    std::vector<LocalGossipCondition> conditions;
};
struct LocalGossipMenu {
    uint32_t id = 0;
    std::vector<LocalGossipMenuText> texts;
    std::vector<LocalGossipOption> options;
};
struct LocalGossipTextVariant {
    float probability = 0;
    uint8_t language = 0;
    std::string maleText, femaleText;
    std::array<uint16_t,6> emotes{};   // (delay, emote) x 3
};
struct LocalGossipText {
    uint32_t id = 0;
    std::vector<LocalGossipTextVariant> variants;
};
struct LocalGossipOwner { uint32_t entry = 0, menuId = 0, npcFlags = 0; };
/// What a player sees of a creature's gossip (Player::PlayerTalkClass): the
/// creature, the menu shown, its npc_text, the options that passed their
/// conditions and the creature's flags, and whether the quest list is the
/// page instead. `revision` grows with every change so a guest redraws.
struct LocalGossipShownOption {
    uint16_t id = 0;
    uint8_t icon = 0, type = 0;
    uint32_t actionMenuId = 0, boxMoney = 0;
    std::string text, boxText;
};
struct LocalGossipState {
    uint64_t npcGuid = 0;
    uint32_t menuId = 0, textId = 0, revision = 0;
    // OFFER_QUEST without directAdd: the quest whose details the page shows
    // (SendQuestGiverQuestDetails), acceptable from this creature.
    uint32_t offeredQuestId = 0;
    bool questMenu = false;
    std::vector<LocalGossipShownOption> options;
    bool open() const { return npcGuid != 0; }
};
inline constexpr size_t kLocalMaxGossipOptions = 32;
inline constexpr uint32_t kLocalGossipDefaultText = 0xffffff; // DEFAULT_GOSSIP_MESSAGE
struct LocalRealmPlayer {
    uint64_t guid = 0;
    std::string name;
    uint32_t mapId = 0, instanceId = 0;
    LocalResourceType resourceType = LocalResourceType::Mana;
    float x = -8949.95f, y = -132.493f, z = 83.5312f, orientation = 0;
    uint8_t race = 1, classId = 1, gender = 0, level = 1;
    // Appearance chosen at character creation (CharSections indices). Saved
    // with the player from save version 4 on; older saves read as zeros.
    uint8_t skin = 0, face = 0, hairStyle = 0, hairColor = 0, facialHair = 0;
    bool useFemaleModel = false;
    uint32_t health = 100, maxHealth = 100, mana = 100, maxMana = 100;
    uint32_t xp = 0, xpToLevel = 400, money = 0, positionRevision = 0;
    uint32_t formSpellId=0,druidMana=0,druidManaRemainder=0; // Save25: active form and hidden mana.
    bool dead = false;
    // Save32/wire87: the corpse stays at the death location while its owner
    // moves as a ghost. Health remains zero until authoritative reclaim.
    bool ghost = false, corpseValid = false;
    uint32_t corpseMapId = 0, corpseInstanceId = 0, corpseZoneId = 0;
    uint32_t zoneId = 0; // Terrain-derived local zone; unknown for remote actors.
    float corpseX = 0, corpseY = 0, corpseZ = 0, corpseOrientation = 0;
    // Legacy saves skip onboarding; createPlayer opts new identities into it.
    bool introSeen = true;
    std::vector<LocalItemStack> inventory;
    std::array<LocalItemStack, kLocalBankSlots> bank{}; // Stable personal bank slots; no purchased bags yet.
    // The most recent twelve sales, newest first. IDs never shift when an
    // older row is bought back, so a delayed LAN command cannot buy another row.
    uint32_t buybackSerial = 0;
    std::vector<LocalMerchantBuyback> buyback;
    // Canonical nineteen worn slots. Each reference reserves one inventory
    // copy; equipped items remain in inventory, including paired duplicates.
    std::array<uint32_t, kLocalEquipmentSlotCount> equipment{};
    // Only accepted, unrewarded quests occupy the bounded active log.
    std::vector<LocalQuestProgress> quests;
    // Strictly increasing unique IDs; objective data is no longer needed after
    // rewarding. Kept separate so completed quests never occupy active slots.
    std::vector<uint32_t> completedQuestIds;
    // Standalone reputation is authoritative character progression. Only
    // factions this character has actually met/changed are retained; an absent
    // faction reads as neutral (0). Save34/LAN89.
    std::vector<LocalReputationEntry> reputations;
    // Save34 migration marker. Runtime-only: old saves/new characters seed the
    // matching Faction.dbc base standings once; Save34+ restores exact state.
    bool migrateLegacyReputation = true;
    // Save36/LAN91. Bit 0 is the normal world; additional bits are authored
    // script phases. Script variables are generic persistent event state.
    uint32_t phaseMask = 1;
    std::vector<LocalScriptState> scriptStates;
    std::vector<LocalScriptTimer> scriptTimers;
    // 5.4 vehicle occupancy is session-only. Saving an active seat would leave
    // a character attached to an actor that may not exist after reconnect.
    uint64_t vehicleGuid = 0;
    uint32_t vehicleId = 0;
    uint8_t vehicleSeat = 0;
    bool vehicleControl = false;
    // Authority-only distance budget, replenished by simulation time rather
    // than packets. A guest cannot gain movement by flooding position reports.
    float vehicleMoveAllowance = 0;
    // Save38 remembers only the exit-script identity, never a live actor/seat.
    uint32_t vehicleRecoveryId = 0;
    // Save39: edge membership survives reload, so standing inside an area does
    // not repeatedly award entry credit. Instance transfers create new edges.
    std::vector<uint32_t> scriptAreaIds;
    uint32_t scriptAreaInstanceId = 0;
    LocalEscortProgress escort; // Save40: exact route cursor, position, wait and online timeout.
    std::vector<uint32_t> knownSpells;
    std::vector<LocalHealingAuraView> healingAuras; // transient presentation only; never saved
    // Creature-cast periodic damage on this character: transient presentation
    // derived by the authority (never saved), drawn as harmful auras.
    std::vector<LocalHealingAuraView> harmfulAuras;
    // 2.40: the gossip page open with a creature (Player::PlayerTalkClass);
    // authority state mirrored to the owner's client, never saved.
    LocalGossipState gossip;
    // LAN107: creature knockbacks (SMSG_MOVE_KNOCK_BACK) - the sequence rises
    // per knockback and the owning client applies the newest one once - and
    // the school lockouts of creature interrupts.
    uint32_t knockbackSequence=0;
    float knockbackCos=0,knockbackSin=0,knockbackSpeedXY=0,knockbackSpeedZ=0;
    std::vector<LocalSchoolLockout> schoolLockouts;
    // Diminishing returns for creature controls on this character
    // (Unit::GetDiminishing/IncrDiminishing). Authority-only; never saved or
    // replicated - the diminished duration travels in harmfulAuras.
    std::vector<LocalNpcDiminishing> diminishing;
    std::vector<LocalStatAura> statAuras; // timed recipient effects; shield capacity is bounded against content
    // What this character projects, and what it currently receives. The emitter
    // is saved; the derived applications are rebuilt by the authority and never
    // saved or accepted from a client. See include/game/local_area_aura.hpp.
    std::vector<LocalAreaAuraEmitter> areaEmitters;
    std::vector<LocalAreaAuraApplication> areaAuras;
    std::vector<std::pair<uint32_t,uint8_t>> talents; // talent ID and learned rank (1..5), at most 71 points

    // Trade-skill recipes this character has been taught. Separate from
    // knownSpells because a recipe is not cast at anything: it is crafted, and
    // the rules that gate it are skill points rather than mana and range.
    std::vector<uint32_t> knownRecipes;
    std::vector<LocalCooldown> cooldowns;
    std::vector<LocalCategoryCooldown> categoryCooldowns;
    bool migrateLegacyCooldowns=false; // Preserved until imported category metadata is available.
    // Six independent base-rune timers, authority-owned; save format 9.
    LocalRuneCooldowns runeCooldownMs{};
    // Owner-only cast progress; active casts are cancelled on load/reconnect.
    uint32_t castingSpellId = 0, castRemainingMs = 0, castTotalMs = 0, globalCooldownMs = 0;
    uint32_t castSequence = 0, castPushbackMs = 0; // Session-only owner presentation; never saved.
    uint64_t nextCostModGeneration=0, castCostModGeneration=0;
    uint32_t castPreparedCost=0, castCostModSpellId=0;
    bool castCostPrepared=false; // Prepare-time power cost is retained through expiry/refresh. Never saved.
    uint8_t castPushbackCount = 0; // Authority-only, at most two damaging hits per cast.
    uint64_t castTarget = 0;
    LocalCastStatus castStatus = LocalCastStatus::None;
    // Owner-only, session-local successful cast events, including instant spells
    // with no cooldown. Failed/interrupted casts never advance this serial.
    uint32_t castRevision = 0, lastCastSpellId = 0;
    uint64_t lastCastTarget = 0;
    // Authority-only origin used to interrupt movement and map/instance travel.
    float castOriginX = 0, castOriginY = 0, castOriginZ = 0;
    uint32_t castOriginMap = 0, castOriginInstance = 0;
    // Runtime values are kept on the authority; cooldowns are persisted.
    uint64_t attackTarget = 0;
    // Encounter-local combo state. Only target/points are sent to the owner;
    // the authority epoch prevents reuse after NPC reset or respawn.
    uint64_t comboTarget=0;
    uint64_t comboTargetEpoch=0;uint32_t comboPositionRevision=0;
    uint8_t comboPoints=0;
    float attackTimer = 0, deadTimer = 0;
    float offHandTimer=0;
    float meleePeriodMain=0,meleePeriodOff=0; // Authority-only period used to preserve swing progress across aura changes.
    uint32_t meleeWeaponMain=0,meleeWeaponOff=0,meleeForm=0;
    std::array<LocalMeleeView,4> meleeViews{};uint32_t meleeSerial=0,meleeViewPositionRevision=0; // Owner-only transient presentation window.
    uint32_t druidManaCapacity=0; // Derived maximum caster mana, owner wire only; rebuilt on load.
    uint32_t regenerationTickMs=0; // Authority-only two-second health/decay cadence.
    uint32_t manaRegenSubMilli=0; // Authority-only millionths; saved thousandths stay compatible.
    uint32_t manaRegenDelayMs=0,resourceRegenRemainder=0; // Save24: delay and thousandths of a resource unit.
    bool gameplayInitialized = false;
    uint32_t rangedAutoSpellId=0,rangedWeapon=0,rangedRemainingMs=0;
    uint64_t rangedTarget=0;
    float rangedOriginX=0,rangedOriginY=0,rangedOriginZ=0;
    uint32_t rangedOriginRevision=0;
    uint32_t returnMapId = 0, returnInstanceId = 0;
    float returnX = 0, returnY = 0, returnZ = 0, returnOrientation = 0;
    bool hasInstanceReturn = false;
    float portalCooldown = 0;
    // Flight points this character has visited. Retail sells a flight only to
    // a node the player has already discovered, and that rule is what keeps a
    // level-one character from crossing the continent in a minute.
    std::vector<uint32_t> knownTaxiNodes;
    uint16_t ridingSkill = 0; // Ground riding: untrained, apprentice 75, journeyman 150.
    bool migrateLegacyRiding = false; // Save16 encodes a pending migration as reserved riding value 65535.
    // A taxi flight in progress. Persisted with the character, so a save made
    // mid-flight resumes rather than dropping the player out of the sky.
    LocalFlightState flight;
    // Transport the character is standing on, and where on its hull. Position
    // is stored as an offset so the passenger rides with the hull instead of
    // being teleported to it every tick.
    uint32_t transportEntry = 0;
    float transportOffsetX = 0, transportOffsetY = 0, transportOffsetZ = 0;
    float transportLastYaw = 0;
    // Professions learned from a trainer. Bounded like every other per-character
    // list here; retail allows two primaries and the secondaries, so the cap is
    // a corruption guard rather than a limit a player can reach.
    std::vector<LocalProfessionSkill> professions;
    // Where the innkeeper this character last spoke to stands, and how long
    // until they can return to it. The position is the innkeeper's own, so
    // nothing about home is invented; an unbound character has hasHome false
    // and simply cannot use it.
    bool hasHome = false;
    uint32_t homeMapId = 0;
    float homeX = 0, homeY = 0, homeZ = 0, homeOrientation = 0;
    float hearthCooldown = 0;
    // What the owner's own client last said about how this character is moving;
    // see kLocalMovementFalling. Reported alongside the position and never
    // saved: it describes this instant, not the character.
    uint8_t movementState = 0;
    uint32_t mountSpellId = 0; // session-only; learned spell remains in persistent knownSpells

    // The fall in progress, measured by the authority alone. `fallStartZ` is the
    // height the descent began at and `fallRevision` the positionRevision it
    // began under - a relocation bumps that and abandons the measurement, which
    // is what stops a portal, a flight or the below-the-world rescue from
    // arriving as a fall. Runtime only, like attackTimer.
    bool falling = false;
    float fallStartZ = 0;
    uint32_t fallRevision = 0;
};

inline bool validLocalVehicleState(const LocalRealmPlayer& p) {
    if (!p.vehicleGuid) return !p.vehicleId && !p.vehicleSeat && !p.vehicleControl;
    return p.vehicleId && p.vehicleSeat < 8 && !p.dead && !p.ghost && p.health &&
        !p.flight.active && !p.transportEntry &&
        (p.vehicleGuid & 0xffff000000000000ULL) == 0xf130000000000000ULL &&
        uint32_t((p.vehicleGuid >> 32) & 0xffff) == p.instanceId;
}
inline void copyLocalVehicleState(LocalRealmPlayer& to, const LocalRealmPlayer& from) {
    to.vehicleGuid=from.vehicleGuid;to.vehicleId=from.vehicleId;
    to.vehicleSeat=from.vehicleSeat;to.vehicleControl=from.vehicleControl;
}
inline int32_t localScriptState(const LocalRealmPlayer& player, uint32_t scriptId) {
    const auto found = std::lower_bound(player.scriptStates.begin(), player.scriptStates.end(), scriptId,
        [](const LocalScriptState& state, uint32_t id) { return state.scriptId < id; });
    return found != player.scriptStates.end() && found->scriptId == scriptId ? found->value : 0;
}
inline bool localSetScriptState(LocalRealmPlayer& player, uint32_t scriptId, int32_t value) {
    if (!scriptId) return false;
    auto found = std::lower_bound(player.scriptStates.begin(), player.scriptStates.end(), scriptId,
        [](const LocalScriptState& state, uint32_t id) { return state.scriptId < id; });
    if (found != player.scriptStates.end() && found->scriptId == scriptId) {
        if (!value) player.scriptStates.erase(found);
        else found->value = value;
        return true;
    }
    if (!value) return true;
    if (player.scriptStates.size() >= kLocalMaxScriptStates) return false;
    player.scriptStates.insert(found, LocalScriptState{scriptId, value});
    return true;
}
inline bool localScriptConditionMatches(const LocalRealmPlayer& player, uint32_t scriptId, int32_t value) {
    return !scriptId || localScriptState(player, scriptId) == value;
}
inline bool localSetScriptTimer(LocalRealmPlayer& player, uint32_t timerId, uint32_t remainingMs) {
    if (!timerId || !remainingMs) return false;
    auto found = std::lower_bound(player.scriptTimers.begin(), player.scriptTimers.end(), timerId,
        [](const LocalScriptTimer& timer, uint32_t id) { return timer.timerId < id; });
    if (found != player.scriptTimers.end() && found->timerId == timerId) { found->remainingMs = remainingMs; return true; }
    if (player.scriptTimers.size() >= kLocalMaxScriptTimers) return false;
    player.scriptTimers.insert(found, LocalScriptTimer{timerId, remainingMs});
    return true;
}
inline bool localCancelScriptTimer(LocalRealmPlayer& player, uint32_t timerId) {
    if (!timerId) return false;
    const auto found = std::lower_bound(player.scriptTimers.begin(), player.scriptTimers.end(), timerId,
        [](const LocalScriptTimer& timer, uint32_t id) { return timer.timerId < id; });
    if (found == player.scriptTimers.end() || found->timerId != timerId) return true;
    player.scriptTimers.erase(found);
    return true;
}
inline bool localApplyScriptMutation(LocalRealmPlayer& player, uint32_t requiredScriptId, int32_t requiredValue,
                                     uint32_t scriptId, LocalScriptValueOp valueOp, int32_t value,
                                     uint32_t addPhaseMask, uint32_t removePhaseMask) {
    if (!validLocalScriptMutation(scriptId, valueOp, value, addPhaseMask, removePhaseMask) ||
        !localScriptConditionMatches(player, requiredScriptId, requiredValue)) return false;
    const uint32_t nextPhaseMask = (player.phaseMask | addPhaseMask) & ~removePhaseMask;
    if (!nextPhaseMask) return false;
    LocalRealmPlayer staged; staged.phaseMask = player.phaseMask; staged.scriptStates = player.scriptStates;
    if (valueOp != LocalScriptValueOp::None) {
        const int64_t current = localScriptState(staged, scriptId);
        const int64_t next = valueOp == LocalScriptValueOp::Set ? int64_t(value) : current + int64_t(value);
        if (next < INT32_MIN || next > INT32_MAX) return false;
        if (next && !localScriptState(staged, scriptId) && staged.scriptStates.size() >= kLocalMaxScriptStates) return false;
        if (!localSetScriptState(staged, scriptId, int32_t(next))) return false;
    }
    player.phaseMask = nextPhaseMask;
    player.scriptStates = std::move(staged.scriptStates);
    return true;
}
inline bool localScriptTriggerMatches(const LocalRealmPlayer& player, const LocalScriptTrigger& trigger) {
    return localScriptConditionMatches(player, trigger.requiredScriptId, trigger.requiredValue);
}
inline bool localApplyScriptTrigger(LocalRealmPlayer& player, const LocalScriptTrigger& trigger) {
    if (!validLocalScriptTrigger(trigger) || !localScriptTriggerMatches(player, trigger)) return false;
    LocalRealmPlayer staged; staged.phaseMask = player.phaseMask; staged.scriptStates = player.scriptStates; staged.scriptTimers = player.scriptTimers;
    const bool hasMutation = trigger.valueOp != LocalScriptValueOp::None || trigger.addPhaseMask || trigger.removePhaseMask;
    if (hasMutation && !localApplyScriptMutation(staged, trigger.requiredScriptId, trigger.requiredValue, trigger.scriptId,
                                                trigger.valueOp, trigger.value, trigger.addPhaseMask, trigger.removePhaseMask)) return false;
    if (trigger.cancelTimerId && !localCancelScriptTimer(staged, trigger.cancelTimerId)) return false;
    if (trigger.scheduleTimerId && !localSetScriptTimer(staged, trigger.scheduleTimerId, trigger.scheduleDelayMs)) return false;
    player.phaseMask = staged.phaseMask;
    player.scriptStates = std::move(staged.scriptStates);
    player.scriptTimers = std::move(staged.scriptTimers);
    return true;
}
inline bool localApplyScriptTimerAction(LocalRealmPlayer& player, const LocalScriptTimerAction& action) {
    if (!validLocalScriptTimerAction(action) || !localScriptConditionMatches(player, action.requiredScriptId, action.requiredValue)) return false;
    return localApplyScriptTrigger(player,localScriptTimerTransition(action));
}
inline bool localPhaseVisible(uint32_t playerPhaseMask, uint32_t requiredMask, uint32_t excludedMask) {
    if (!playerPhaseMask) return false;
    if (excludedMask && (playerPhaseMask & excludedMask)) return false;
    return !requiredMask || (playerPhaseMask & requiredMask) != 0;
}

/// Standalone reputation helpers. The rank is the item_template convention
/// (0=Hated .. 7=Exalted); standing is clamped to the WotLK range.
inline int32_t localReputationStanding(const LocalRealmPlayer& player, uint32_t factionId) {
    if (!factionId) return 0;
    const auto found = std::find_if(player.reputations.begin(), player.reputations.end(),
        [&](const LocalReputationEntry& row) { return row.factionId == factionId; });
    return found == player.reputations.end() ? 0 : found->standing;
}
inline uint8_t localReputationRank(const LocalRealmPlayer& player, uint32_t factionId) {
    const auto& standing = reputationStandingFor(localReputationStanding(player, factionId));
    return uint8_t(std::clamp(standing.id - 1, 0, 7));
}
inline bool localMeetsReputation(const LocalRealmPlayer& player, uint32_t factionId, uint8_t requiredRank) {
    return factionId == 0 || (requiredRank < 8 && localReputationRank(player, factionId) >= requiredRank);
}
inline bool localChangeReputation(LocalRealmPlayer& player, uint32_t factionId, int32_t delta) {
    if (!factionId || !delta) return false;
    auto found = std::lower_bound(player.reputations.begin(), player.reputations.end(), factionId,
        [](const LocalReputationEntry& row, uint32_t id) { return row.factionId < id; });
    const int32_t old = found != player.reputations.end() && found->factionId == factionId ? found->standing : 0;
    const int64_t changed = std::clamp<int64_t>(int64_t(old) + delta, -42000, 42999);
    if (changed == old) return false;
    if (found == player.reputations.end() || found->factionId != factionId) {
        if (player.reputations.size() >= kLocalMaxReputations) return false;
        player.reputations.insert(found, LocalReputationEntry{factionId, int32_t(changed)});
    } else found->standing = int32_t(changed);
    return true;
}
inline bool validLocalReputations(const LocalRealmPlayer& player) {
    if (player.reputations.size() > kLocalMaxReputations) return false;
    uint32_t previous = 0;
    for (const auto& row : player.reputations) {
        if (!row.factionId || row.factionId <= previous || row.standing < -42000 || row.standing > 42999) return false;
        previous = row.factionId;
    }
    return true;
}

struct LocalItemDefinition {
    uint32_t id = 0, displayId = 0;
    std::string name;
    uint32_t requiredReputationFaction = 0;
    uint8_t requiredReputationRank = 0; // 0=Hated .. 7=Exalted, item_template convention.

    uint16_t stack = 1;
    // slot retains legacy schema-1/catalog meaning. Use inventoryType through
    // localEquipmentSlotMask for runtime equipment compatibility.
    uint8_t slot = 0, inventoryType = 0;
    uint32_t maxHealth = 0, attack = 0, armor = 0, heal = 0, mana = 0, value = 0;
};
enum class LocalProcEffect : uint8_t { None, DamageAttacker, RestoreMana, MeleeDamageShield, HealOwner, RestorePower, AddComboPoints, HealOwnerPctMaxHealth, ApplyOwnerAura, ConsumeOwnerAuraCharge, ConsumeSpellCostCharge, Ignite, RestorePetPower };
/// Who an effect is applied to, as distinct from whose aura selected it.
/// Unit::GetSpellModOwner and the owned-creature recipient are two different
/// relationships: an owner supplies modifiers, a recipient receives the effect.
enum class LocalProcRecipient : uint8_t { AuraOwner = 0, OwnedPet = 1 };
struct LocalProcDefinition {
    LocalProcEffect effect=LocalProcEffect::None;
    uint32_t spellId=0, flags=0, cooldownMs=0, amount=0, baseLevel=0, maxLevel=0;
    uint32_t schoolMask=0,spellFamily=0;
    std::array<uint32_t,3> spellFamilyFlags{};
    float amountPerLevel=0, range=0;
    uint8_t chance=0, charges=0;
    // Filters describe the event that triggers this aura. Child spell school
    // and family above remain independent (not reused as event filters).
    uint32_t triggerSchoolMask=0,triggerSpellFamily=0;
    std::array<uint32_t,3> triggerSpellFamilyFlags{};
    uint32_t hitMask=LocalProcHitNormal|LocalProcHitCritical;
    float ppm=0; // Zero uses chance; positive values need a weapon event period.
    uint8_t phaseMask=LocalProcPhaseHit;
    uint8_t spellTypeMask=7; // Source spell_proc damage1, heal2, other4.
    bool allowTriggered=false;
    uint8_t pushbackPercent=0; // Reviewed Earth Shield secondary effect.
    uint8_t resourceType=0; // RestorePower: rage1 or energy3 in displayed units; RestorePetPower: source focus2.
    uint8_t recipient=uint8_t(LocalProcRecipient::AuraOwner); // LocalProcRecipient; never reinterpreted as an owner.
    uint64_t requiredForms=0; // The passive aura's own form mask, independent of its sibling.
    uint32_t attributesMask=0;
    uint8_t disableEffectsMask=0,sourceEffectMask=1;
    bool hasUnsupportedConditions=false,hasUnsupportedScript=false;
};
struct LocalPassiveCastModifier {
    uint8_t operation=0; // 0: direct; 3/12/23: source effect slots 0/1/2; 5: range; 8: all effects; 10: cast; 11: recovery; 14: cost; 22: periodic.
    bool percentage=false; // Aura 108; aura 107 retains a flat amount.
    bool active=false; // Operation zero is a real direct-amount modifier.
    int32_t amount=0;
    std::array<uint32_t,3> mask{};
};
static_assert(sizeof(LocalPassiveCastModifier)==20, "Bounded modifier record size");
struct LocalSpellDefinition {
    uint16_t passiveArmorAttackPowerDivisor=0;
    uint8_t passiveOffhandDamagePct=0,passiveWeaponHitPct=0;
    uint8_t passivePhysicalDamagePct=0;
    uint8_t passiveEquipmentArmorPct=0,passiveFeralCritPct=0,passiveFeralDodgePct=0,passiveCatRunPct=0;
    uint8_t directEffectSlot=255,periodicEffectSlot=255; // Exact DBC slot; 255 denotes unknown/aggregate.
    uint8_t stormstrikeProfile=0,stormstrikeManaChancePct=0;
    uint8_t passiveIntellectAttackPowerPct=0,passiveDualWieldHitPct=0;
    bool passiveCanParry=false,passiveCanDualWield=false;
    uint8_t physicalDamageDonePct=0,damageTakenPct=0;
    uint8_t periodicHealMaxHealthPct=0;
    uint8_t arcaneBlastProfile=0; // Ordinary ranks 1; internal four-stack aura 2.
    // Source crit metadata distinguishes magic, melee, ranged and non-damaging
    // spells. Only explicitly reviewed retaliation leaves may roll proc crits.
    std::array<uint8_t,5> passiveTotalStatPct{};
    uint8_t passiveSpellCritPct=0;
    uint8_t clearcastingProfile=0; // Mage parent1, Omen parent2, Mage child3, Druid child4.
    int16_t chargedCostPct=0;
    std::array<uint32_t,3> chargedCostMask{};
    bool omenProcEligible=false; // Exact source script admission for an ordinary spell hit.
    uint32_t sourceRawCastTimeMs=0; // Unmodified DBC cast time used by spell PPM.
    uint8_t sourceDamageClass=0;
    bool sourceNotAProc=false; // Source attribute permits triggered spell proc selection.
    bool sourceDoNotConsumeResources=false;
    bool sourceIgnoreCasterModifiers=false;
    uint8_t rangedAutoProfile=0;
    // Reviewed SPELL_EFFECT_APPLY_AREA_AURA_RAID profile: caster-anchored, one
    // fixed radius, indefinite duration. Zero on every other spell. The marker
    // effects are retained with their real source values rather than normalised
    // away; carrying them is not a claim that their aura types are implemented.
    // SPELL_AURA_ABILITY_PERIODIC_CRIT (286) grant: the family and class mask it
    // affects. Zero on every spell the importer has admitted at this baseline -
    // no granting row is importable yet, and none is fabricated.
    uint32_t periodicCritFamily=0;
    std::array<uint32_t,3> periodicCritMask{};
    uint8_t areaAuraProfile=0,areaAuraEffectMask=0;
    std::array<uint8_t,3> areaAuraTypes{};
    std::array<int32_t,3> areaAuraAmounts{};
    std::array<uint32_t,3> areaAuraMiscValues{};
    float areaAuraRadius=0;
    // SpellDuration.dbc -1. durationMs stays zero: this is not a lease.
    bool indefiniteDuration=false;
    // Reviewed SPELL_EFFECT_SUMMON_PET profile: the creature entry the caster's
    // controlled summon is built from, and which effect slot named it. Zero on
    // every spell that is not such a summon.
    uint32_t summonPetEntry=0,summonPetDurationMs=0;
    uint8_t summonPetKind=0,summonPetEffectSlot=255;
    uint8_t wardProfile=0,moltenShieldsChancePct=0;
    bool npcOnly=false,sourceCantReflect=false,sourceAlwaysHit=false;
    float sourceProjectileSpeed=0;
    bool sourceCantCrit=false,procCanCrit=false;
    uint8_t spiritCritRatingPct=0,incomingCritReductionPct=0,mageArmorGroup=0;
    uint8_t meleeSpecialProfile=0; // 1: reviewed Bloodthirst AP-based melee special.
    uint32_t triggeredAuraSpellId=0; // Scripted self aura, resolved and admitted before cast commit.
    bool triggeredOnly=false; // Internal chain child: never learned, trained or directly cast.
    uint8_t meleeHastePct=0; // Reviewed temporary aura 138: both melee hands, never ranged.
    uint32_t procParentTalentId=0; // Internal child is valid only for its current allocated parent rank.
    uint8_t comboProfile=0,comboGain=0;
    bool comboFinisher=false,weaponDamage=false,normalizedWeapon=false,requiresBehind=false;
    uint16_t weaponPercent=100;
    float directPerCombo=0,periodicPerCombo=0,extraEnergyMultiplier=0;
    uint64_t requiredForms=0,excludedForms=0;
    uint8_t formId=0;bool notShapeshifted=false,allowWithoutForm=false;
    uint32_t id = 0, mana = 0, cooldownMs = 0, damage = 0, heal = 0;
    bool directIgnoresArmor=false,periodicIgnoresArmor=false;
    int16_t passiveSchoolThreatPercent=0;uint8_t passiveSchoolThreatMask=0;
    int8_t requiredItemClass=-1;
    uint32_t requiredItemSubclasses=0,requiredInventoryTypes=0;
    bool requiresMainHand=false,requiresOffHand=false;
    uint8_t chainTargets=1;
    uint16_t chainMultiplierPermille=1000;
    float chainRadius=0,areaRadius=0; // Reviewed caster-centered direct damage only.
    uint32_t cooldownCategory=0,categoryCooldownMs=0;
    bool noCategoryCooldownMods=false;
    uint32_t allowableClasses = 0;
    uint32_t talentId=0,talentTab=0;
    uint8_t talentRank=0,talentRow=0;
    std::array<uint32_t,3> talentPrerequisites{};
    std::array<uint8_t,3> talentPrerequisiteRanks{};
    uint32_t buffAbsorb=0,absorbSchoolMask=0;
    uint32_t manaPerAbsorbMilli=0; // Zero for ordinary shields; Mana Shield uses DBC multiplier * 1000.
    uint8_t maxAuraStacks=1;
    LocalProcDefinition proc;
    LocalProcDefinition secondaryProc; // Reviewed talent learning two complete passive proc auras.
    uint32_t manaPer5=0; // Fixed mana regeneration while the recipient aura lives.
    bool buffSelfOnly=true;
    uint32_t buffHealth=0,buffArmor=0; // fixed positive, timed stat buffs
    uint32_t passiveHealth=0,passiveArmor=0;
    uint8_t passiveMeleeCritPct=0; // Reviewed weapon-qualified aura 52 melee critical chance.
    // P06  form-boost amounts. AuraEffect::HandleShapeshiftBoosts
    // (SpellAuraEffects.cpp:1350-1445) is the reference's per-form grant table;
    // SpellShapeshiftForm.dbc's stanceSpell[] is not (0 of the nine modelled
    // forms carries one). These carry the boost spells' own DBC amounts rather
    // than a transcribed constant.
    uint16_t passiveAttackPower=0;      // Aura 99, CalcValue's BasePoints + DieSides term.
    float passiveAttackPowerPerLevel=0; // Aura 99, EffectRealPointsPerLevel; clamped by baseLevel/maxLevel.
    uint8_t passiveArmorPenetrationPct=0; // Aura 280, Unit::CalcArmorReducedDamage's bonusPct (Unit.cpp:2239-2267).
    uint8_t furorChancePct=0;   // Furor's SPELL_AURA_DUMMY amount (SpellAuraEffects.cpp:2100-2132).
    uint8_t retainedRage=0;     // Stance/Tactical Mastery's SPELL_AURA_DUMMY amount (:2193-2221), whole rage.
    uint8_t passiveManaRegenInterruptPct=0; // Aura 134: spirit regen retained after spending mana.
    std::array<uint8_t,5> passiveManaRegenStatPct{}; // Aura 219: percent of each stat as mana per five seconds.
    std::array<LocalPassiveCastModifier,3> passiveCastModifiers{};
    uint32_t spellFamily=0;
    std::array<uint32_t,3> spellFamilyFlags{};
    uint8_t passivePushbackPct=0; // Aura 108, spell modifier operation 9 only.
    std::array<uint32_t,3> pushbackSpellMask{};
    bool passive=false;
    // 255 consumes the active class resource; otherwise an explicit power type.
    uint8_t resourceType = 255;
    LocalRuneCost runeCost{};
    uint16_t runicPowerGain = 0; // Displayed units; SpellRuneCost stores tenths.
    float range = 30;
    std::string name;
    // Imported 3.3.5a metadata. Nonempty unsupportedReason rejects the whole
    // spell, including otherwise supported effects, rather than silently losing them.
    bool clientSpell = false;
    uint32_t iconId = 0, castTimeMs = 0, globalCooldownMs = 0, durationMs = 0;
    uint32_t interruptFlags = 1; // DBC casting flags; legacy catalog casts stop on movement.
    bool noPushback = false; // Spell AttributesEx6 NO_PUSHBACK.
    uint32_t visualId = 0, schoolMask = 0;
    uint32_t manaPercent = 0, baseLevel = 1, maxLevel = 0, damageMax = 0, healMax = 0;
    uint8_t snarePercent = 0; // Reviewed NPC movement reduction, strongest effect wins.
    // Creature-cast auras on players (generated SmartAI family only):
    // MOD_DECREASE_SPEED as a positive percentage, and MOD_RESISTANCE on
    // armor as CalcValue's base (with the +1 die) and RealPointsPerLevel.
    uint8_t npcSlowPercent = 0;
    int32_t npcArmorAmount = 0, npcArmorAmountMax = 0; // 2.37: any sign, a die range
    float npcArmorPerLevel = 0;
    // MOD_RESISTANCE_PCT on armor (TOTAL_PCT), negative percentage; 2.37 also
    // a bonus (npcArmorPercentWide carries the full range).
    int8_t npcArmorPercent = 0;
    int16_t npcArmorPercentWide = 0;
    // A creature's MOD_STUN (1), MOD_ROOT (2), MOD_FEAR (3), MOD_CONFUSE (4) or
    // MOD_SILENCE (5) on a player; npcBreakOnDamage carries
    // AURA_INTERRUPT_FLAG_TAKE_DAMAGE (any damage removes the control).
    uint8_t npcPlayerControl = 0;
    bool npcBreakOnDamage = false;
    // 2.36 creature caster profile (generated SmartAI family only). The target
    // shape decides who the cast lands on: 0 one enemy, 1 the caster, 2 the
    // enemies around the caster, 3 the enemies around the target, 4 a cone in
    // front of the caster, 5 the caster and its allies around it, 6 (2.38)
    // the enemies around the spell's destination - the explicit target's
    // position, or the caster's without one (Spell::InitExplicitTargets).
    uint8_t npcTargetShape = 0;
    // 2.38 destination-only effects. SPELL_EFFECT_SUMMON: the entry, the
    // count (BasePoints for the listed SummonProperties), the properties'
    // category (0 wild, 1 ally, 2 pet) and type, whether the summon takes the
    // summoner's faction (ally / pet / USE_SUMMONER_FACTION), the placement
    // code (local_spell_import.hpp: LocalNpcDest) and the spread radius.
    // SPELL_EFFECT_PERSISTENT_AREA_AURA: a dynamic object of npcGroundRadius at
    // the placement whose aura kinds (the ordinary npc* aura fields of this
    // definition) land on the enemies inside it.
    uint32_t npcSummonEntry = 0;
    uint8_t npcSummonCount = 0, npcSummonCategory = 0, npcSummonType = 0, npcSummonDest = 0, npcGroundDest = 0;
    bool npcSummonOwnerFaction = false, npcGroundAura = false;
    float npcSummonRadius = 0, npcGroundRadius = 0;
    // SPELL_ATTR0_ALLOW_CAST_WHILE_DEAD: a dead creature's DEATH rows may cast it.
    bool npcCastableWhileDead = false;
    float npcAreaRadius = 0, npcConeDegrees = 0, npcJumpDistance = 0, npcChainMultiplier = 1;
    uint8_t npcChainTargets = 0, npcMaxTargets = 0;
    bool npcPositive = false, npcChannel = false, npcCosmetic = false, npcScales = false, npcCostScales = false;
    // RealPointsPerLevel of the fixed-percentage auras (CalcValue's level term).
    float npcSlowPerLevel = 0, npcArmorPercentPerLevel = 0;
    // Amounts resolved through CalcValue at application (base with the +1 die,
    // the die range, RealPointsPerLevel, creature scaling where the effect allows it).
    struct NpcAmount { int32_t low = 0, high = 0; float perLevel = 0; bool scales = false; bool set = false; };
    NpcAmount npcDamageTakenFlat, npcDamageTakenPct, npcHealingPct, npcHaste, npcDamagePct, npcDamageFlat, npcAttackPower,
        npcKnockbackZ, npcPeriodicHealAmount, npcHealAmount;
    uint8_t npcDamageTakenSchool = 0, npcDamagePctSchool = 0, npcDamageFlatSchool = 0;
    // SPELL_EFFECT_KNOCK_BACK(_DEST): MiscValue / 10 is the horizontal speed.
    float npcKnockbackSpeedXY = 0;
    // SPELL_EFFECT_INTERRUPT_CAST: the player's cast stops and its school is
    // locked for this spell's duration.
    bool npcInterrupt = false;
    // SPELL_EFFECT_HEALTH_LEECH / SPELL_AURA_PERIODIC_LEECH: the damage dealt
    // times EffectValueMultiplier heals the caster.
    bool npcLeech = false, npcPeriodicLeech = false;
    float npcLeechMultiplier = 0;
    // SPELL_EFFECT_TRIGGER_SPELL / SPELL_AURA_PERIODIC_TRIGGER_SPELL.
    uint32_t npcTriggerSpellId = 0, npcPeriodicTriggerSpellId = 0, npcPeriodicTriggerIntervalMs = 0;
    // 2.37 creature families. Auras resolved through CalcValue (NpcAmount):
    // MOD_INCREASE/DECREASE_SPEED on the creature (npcSpeed), MOD_RESISTANCE on
    // a magic school (npcResistance / npcResistanceSchool), MOD_CASTING_SPEED_NOT_STACK,
    // MOD_HIT_CHANCE, MOD_DODGE/PARRY/BLOCK_PERCENT, SCHOOL_ABSORB, MOD_INCREASE_HEALTH(_PERCENT),
    // DAMAGE_SHIELD, PROC_TRIGGER_DAMAGE; the immunities as masks; MOD_DISARM.
    NpcAmount npcSpeed, npcResistance, npcCastSpeed, npcHitChance, npcDodge, npcParry, npcBlock, npcAbsorb, npcMaxHealth,
        npcMaxHealthPct, npcDamageShield, npcProcDamage, npcPowerBurn, npcPowerDrain, npcHealPct, npcEnergize, npcEnergizePct;
    uint8_t npcResistanceSchool = 0, npcAbsorbSchool = 0, npcSchoolImmunity = 0, npcDamageImmunity = 0;
    uint32_t npcMechanicImmunity = 0; // bit per mechanic (1 << MiscValue)
    bool npcDisarm = false, npcHealMax = false, npcCharge = false, npcUtility = false;
    // SPELL_AURA_PROC_TRIGGER_SPELL / PROC_TRIGGER_DAMAGE: the proc flags
    // (SpellMgr::LoadSpellProcs' entry), chance, charges and triggered spell.
    uint32_t npcProcSpellId = 0, npcProcFlags = 0;
    uint8_t npcProcChance = 0, npcProcCharges = 0;
    // A control aura spending Spell.dbc proc charges on damage taken
    // (AuraEffect::CheckEffectProc): the chance per damaging hit and charges.
    uint8_t npcProcBreakChance = 0, npcProcBreakCharges = 0;
    // SPELL_EFFECT_POWER_BURN / POWER_DRAIN multipliers (EffectValueMultiplier),
    // SPELL_EFFECT_DISPEL (type, count), DISPEL_MECHANIC (mechanic, count),
    // MODIFY_THREAT_PERCENT, KILL_CREDIT2 (entry), CREATE_ITEM (item, count),
    // ADD_EXTRA_ATTACKS.
    float npcPowerBurnMultiplier = 0, npcPowerDrainMultiplier = 0;
    uint8_t npcDispelType = 0, npcDispelCount = 0, npcDispelMechanic = 0, npcDispelMechanicCount = 0, npcExtraAttacks = 0;
    int16_t npcThreatPct = 0;
    uint32_t npcKillCredit = 0, npcCreateItem = 0;
    uint8_t npcCreateItemCount = 0;
    // 2.39: an instakill of the caster, a stun (1) / root (2) the creature
    // applies to itself, invisibility on itself.
    bool npcInstakillSelf = false, npcInvisible = false;
    uint8_t npcSelfControl = 0;
    // Creature melee specials (DmgClass MELEE): Spell::EffectWeaponDmg's fixed
    // bonus (WEAPON_DAMAGE / _NOSCHOOL / NORMALIZED) as CalcValue inputs, the
    // WEAPON_PERCENT_DAMAGE percentage (0 = none) and whether the percentage
    // effect precedes the bonus effect; SPELL_ATTR0_ON_NEXT_SWING(_NO_DAMAGE)
    // replaces the creature's next main-hand swing. CalcValue's creature
    // scaling covers WEAPON_DAMAGE and NORMALIZED_WEAPON_DMG, not _NOSCHOOL.
    bool npcWeaponEffect = false, npcWeaponScales = false, npcWeaponPercentFirst = false, npcNextSwing = false;
    uint32_t npcWeaponBonus = 0, npcWeaponBonusMax = 0;
    float npcWeaponBonusPerLevel = 0;
    uint16_t npcWeaponPercent = 0;
    // SPELL_ATTR7_NO_ATTACK_DODGE / _PARRY / _MISS (Unit::MeleeSpellHitResult).
    bool sourceNoAttackDodge = false, sourceNoAttackParry = false, sourceNoAttackMiss = false;
    // P04 control metadata, carried raw from Spell.dbc. `mechanic` is column 3
    // and `effectMechanic` columns 83-85; both were already read as a bare
    // `== 15` for the bleed rule and are now named. `auraInterruptFlags` is
    // column 32 and is carried verbatim, including the four bits the reference's
    // own enumeration does not name, because rejecting a row over a bit the
    // reference never reads would lose real content.
    uint8_t mechanic = 0;
    std::array<uint8_t,3> effectMechanic{};
    uint32_t auraInterruptFlags = 0;
    uint8_t preventionType = 0; // Column 214: what a silence actually prevents.
    // SPELL_ATTR4_DAMAGE_DOESNT_BREAK_AURAS. Carried on the DAMAGING spell: it
    // exempts that spell from breaking a control, never the control itself.
    bool sourceDamageDoesNotBreakAuras = false;
    // 0 none, 1 stun, 2 silence. Only the narrow single-effect, single-target,
    // fixed-duration, non-proc, non-channelled, non-area shape is admitted.
    uint8_t controlProfile = 0;
    uint8_t controlEffectSlot = 0;
    // P04 immunity and dispel metadata, carried raw from Spell.dbc.
    // `effectMask` has bit k set when column 71+k names any effect: it is what
    // the reference's IsEffect() loop walks (Creature.cpp:2305-2313), so the
    // "immune to all effects" rule can count the real slots rather than the
    // aggregated amounts. `dispelType` is column 2 (DispelType 0-11), the
    // value Unit::IsImmunedToSpell tests against IMMUNITY_DISPEL and the client
    // already resolves for its debuff frame. The three attribute bits are the
    // ones the immunity and resistance predicates read: SPELL_ATTR0_NO_IMMUNITIES
    // (0x20000000, Unit.cpp:9751), SPELL_ATTR2_NO_SCHOOL_IMMUNITIES (0x04000000,
    // Unit.cpp:9634/9792) and SPELL_ATTR4_NO_CAST_LOG (0x1, Unit.cpp:2341).
    uint8_t effectMask = 0;
    uint8_t dispelType = 0;
    bool sourceNoImmunities = false, sourceNoSchoolImmunities = false, sourceNoCastLog = false;
    bool sourceBinary = false; // reviewed whole-profile CU_BINARY attribution 
    // SPELL_EFFECT_DISPEL (38) admitted in exactly one shape: a magic dispel of
    // a hostile unit target beside a non-dispel effect, where Spell::CheckCast
    // skips the nothing-to-dispel gate (Spell.cpp:6278-6282). `dispelAttempts`
    // is BasePoints + 1 - how many auras EffectDispel would draw. Zero on every
    // other spell; a pure dispel is rejected by name, not carried.
    uint8_t dispelProfile = 0, dispelAttempts = 0;
    bool snareZeroHealingMarker = false; // Frostbolt aura 118 has exactly zero modifier.
    uint32_t periodicDamage = 0, periodicDamageMax = 0, periodicIntervalMs = 0;
    float periodicDamagePerLevel = 0;
    uint32_t periodicHeal = 0, periodicHealMax = 0;
    float periodicHealPerLevel = 0;
    bool healingSelfOnly = false;
    float minRange = 0, damagePerLevel = 0, healPerLevel = 0;
    // The next rank of this ability. the implementation (P04): filled from the reference's
    // `spell_ranks` table (SpellMgr::LoadSpellRanks, the authority the
    // reference actually reads) and, for a spell absent from it, from the
    // client's SkillLineAbility.SupercededBySpell column or a reviewed profile.
    // Retail replaces a rank rather than stacking it, and so does this - which
    // is what keeps a level-eighty spellbook the size of a spellbook.
    uint32_t supercededBySpell = 0;
    // SpellInfo::GetFirstRankSpell()->Id: the first rank of this spell's chain
    // (spell_ranks, Talent.dbc rank 1, or the root of the linked chain), or the
    // spell's own id when it is unranked. SpellInfo::IsRankOf compares these;
    // the group tables are keyed by them. Zero only on a hand-built definition.
    uint32_t firstRankSpell = 0;
    // P04 stacking inputs, carried raw from Spell.dbc: columns 71-73 `Effect`
    // and 95-97 `EffectApplyAuraName` (what LoadSpellSpecific's food / drink,
    // polymorph, charm and tracker arms and CanStackWith's periodic arm read),
    // SPELL_ATTR1_NO_THREAT (AttributesEx 0x400, SharedDefines.h:417, the elemental-shield arm),
    // SPELL_ATTR1_IS_CHANNELED | IS_SELF_CHANNELED (AttributesEx 0x44, SharedDefines.h:409/:413) and
    // SPELL_ATTR3_DOT_STACKING_RULE (AttributesEx3 0x80, SharedDefines.h:488, "stack separately for
    // each caster").
    std::array<uint8_t,3> sourceEffect{};
    std::array<uint16_t,3> effectAura{};
    bool sourceNoThreat = false, sourceChanneled = false, sourceDotStackingRule = false;
    // SPELL_ATTR0_ABILITY / TRADESPELL: Unit::ModSpellCastTime leaves such a
    // cast out of UNIT_MOD_CAST_SPEED (a creature's cast-speed view, 2.37).
    bool sourceAbilityOrTrade = false;
    // P05 shared combat inputs , carried raw from Spell.dbc and from the
    // reference's computed custom attributes (the source audit):
    // column 39 `SpellLevel`, the flat initial threat of a cast
    // (Spell::HandleThreatSpells, Spell.cpp:5779-5780);
    // SPELL_ATTR0_NO_ACTIVE_DEFENSE (Attributes 0x200000, SharedDefines.h:391 -
    // "cannot be dodged, parried or blocked", Unit::MeleeSpellHitResult :3354,
    // Unit::isSpellBlocked :3266); SPELL_ATTR3_COMPLETELY_BLOCKED (AttributesEx3
    // 0x8, :484, the full block of MeleeSpellHitResult :3351);
    // SPELL_ATTR3_SUPPRESS_TARGET_PROCS (AttributesEx3 0x20000, :498, "No initial
    // aggro", HandleThreatSpells :5764); SPELL_ATTR0_CU_DIRECT_DAMAGE, which
    // SpellMgr::LoadSpellInfoCustomAttributes derives from a school, weapon or
    // heal effect (SpellMgr.cpp:3341-3347) and which cancels the full block
    // (:3351); SPELL_ATTR0_CU_NO_INITIAL_THREAT, derived from the tracking /
    // ranged-haste / possess-pet / invisibility-detect / water-breathing auras
    // (:3293-3299), the drain / burn / leech / heal-pct / energize / create-item
    // effects (:3350-3358) and the hunter aspects (:3499-3502).
    uint16_t spellLevel = 0;
    bool sourceNoActiveDefense = false, sourceCompletelyBlocked = false, sourceSuppressTargetProcs = false;
    bool sourceDirectDamage = false, sourceNoInitialThreat = false;
    // P05  line of sight: SPELL_ATTR2_IGNORE_LINE_OF_SIGHT (AttributesEx2
    // 0x4, SharedDefines.h:446) or SPELL_ATTR5_ALWAYS_AOE_LINE_OF_SIGHT
    // (AttributesEx5 0x04000000, SharedDefines.h:581) - either lets a cast past
    // the line-of-sight test at Spell.cpp:6092-6093.
    bool sourceIgnoreLineOfSight = false;
    // P06  SPELL_ATTR0_ONLY_OUTDOORS (Attributes 0x8000,
    // SharedDefines.h:385), the gate Spell::CheckCast applies to a player caster
    // at Spell.cpp:5902-5904. Set on exactly two of the nine modelled form
    // spells: Travel Form 783 and Ghost Wolf 2645.
    bool sourceOnlyOutdoors = false;
    // P05  range, facing and target-attribute inputs.
    // `sourceRangeFlags` is SpellRange.dbc's Flags column (Spell.h:100-102:
    // 0 default, 1 melee, 2 ranged), which Spell::CheckRange branches on for
    // the melee reach and the completion leniency (Spell.cpp:7332-7396).
    // `sourceFacingFlags` is Spell.dbc column 19, FacingCasterFlags; bit 0 is
    // SPELL_FACING_FLAG_INFRONT (SpellDefines.h:136) and makes the cast fail
    // with SPELL_FAILED_UNIT_NOT_INFRONT unless the caster has the target in a
    // pi arc or is inside its boundary radius (Spell.cpp:7360).
    // `targetCreatureType` is Spell.dbc column 17, the mask
    // SpellInfo::CheckTargetCreatureType compares against
    // Unit::GetCreatureTypeMask (SpellInfo.cpp:1906-1919, refused at :1819-1825).
    // `sourceOnlyPeacefulTargets` is SPELL_ATTR1_ONLY_PEACEFUL_TARGETS
    // (AttributesEx 0x100, SharedDefines.h:415), refused as
    // SPELL_FAILED_TARGET_AFFECTING_COMBAT (SpellInfo.cpp:1694-1695).
    uint8_t sourceRangeFlags = 0, sourceFacingFlags = 0;
    uint32_t targetCreatureType = 0;
    bool sourceOnlyPeacefulTargets = false;
    uint32_t mountCreatureId = 0, mountDisplayId = 0, mountSpeedPercent = 0;

    std::string iconPath, unsupportedReason;
};
struct LocalQuestObjective {
    enum class Type : uint8_t { Kill = 0, Collect = 1, Talk = 2, Script = 3 };
    Type type = Type::Kill;
    uint32_t entry = 0;
    uint16_t count = 1;
    std::string text; // Authored label for script objectives; no invented NPC ID.
};
struct LocalQuestReputationReward {
    uint32_t factionId = 0;
    int32_t valueId = 0;       // QuestFactionReward.dbc column selector.
    int32_t overrideValue = 0; // hundredths, as stored by quest_template.
};
struct LocalQuestDefinition {
    uint32_t id = 0, giverEntry = 0, turnInEntry = 0, prerequisite = 0;
    uint32_t allowableRaces = 0, allowableClasses = 0, requiredSkill = 0;
    uint32_t requiredMinRepFaction = 0, requiredMaxRepFaction = 0;
    int32_t requiredMinRepValue = 0, requiredMaxRepValue = 0;
    // quest_template.RequiredFactionId/Value 1..2: both are minimum
    // standing requirements and coexist with the addon min/max gates.
    std::array<uint32_t, 2> requiredReputationFactions{};
    std::array<int32_t, 2> requiredReputationValues{};
    uint8_t minLevel = 1;
    std::string title, description;
    uint32_t xp = 0, money = 0, rewardItem = 0;
    uint16_t rewardCount = 0;
    std::vector<LocalQuestObjective> objectives;
    // Legacy rewardItem/rewardCount is the first guaranteed reward. These
    // optional lists extend old content without changing character saves.
    std::vector<LocalItemStack> additionalRewards; // up to three more guaranteed items
    std::vector<LocalItemStack> rewardChoices;     // choose exactly one of up to six
    std::vector<LocalQuestReputationReward> reputationRewards; // up to five source rewards
    LocalQuestChainGate chainGate; // Immutable companion; no save/pack wire change.
};
inline size_t localQuestRewardCount(const LocalQuestDefinition& q) {
    return (q.rewardItem ? 1u : 0u) + q.additionalRewards.size();
}
inline LocalItemStack localQuestRewardAt(const LocalQuestDefinition& q,size_t index) {
    if(q.rewardItem) {if(!index)return {q.rewardItem,q.rewardCount};--index;}
    return index<q.additionalRewards.size()?q.additionalRewards[index]:LocalItemStack{};
}
inline bool validLocalQuestRewards(const LocalQuestDefinition& q) {
    if(bool(q.rewardItem)!=bool(q.rewardCount) || q.additionalRewards.size()>3 ||
       (!q.rewardItem && !q.additionalRewards.empty()) || q.rewardChoices.size()>6)return false;
    if(q.reputationRewards.size()>5)return false;
    for(const auto& r:q.additionalRewards)if(!r.itemId || !r.count)return false;
    for(const auto& r:q.rewardChoices)if(!r.itemId || !r.count)return false;
    for(const auto& r:q.reputationRewards)if(!r.factionId || r.valueId < -9 || r.valueId > 9)return false;
    return true;
}
struct LocalNpcDefinition {
    uint32_t id = 0, displayId = 0, health = 40, damage = 4, armor = 0, xp = 50, money = 0;
    uint32_t requiredReputationFaction = 0;
    uint8_t requiredReputationRank = 0;
    uint8_t level = 1;
    uint32_t faction = 0, unitFlags = 0;
    // creature_template.npcflag: which services this NPC offers. Zero in a
    // catalog built before the field was imported, which is why flight-master
    // identification also has a proximity fallback - see LocalTravelNetwork.
    uint32_t npcFlags = 0;
    std::string name;
    // Optional server-authored gossip. These strings do not live in client MPQs.
    std::string gossipText, subname;
    bool hostile = false, questGiver = false;
    float respawnSeconds = 30, aggroRadius = 0;
    std::vector<LocalItemStack> loot;
    // Explicit catalog stock overrides the compiled per-NPC npc_vendor table.
    // Empty uses the matching upstream merchant's exact stock, never generic
    // item categories. See local_services.hpp for unsupported purchase gates.
    std::vector<uint32_t> vendorItems;
    // What a trainer teaches, when a catalog states it outright rather than
    // leaving it to be read out of the subname. A SkillLine id and a class id
    // 1-11; zero means fall back to the transcription beside the code.
    uint16_t trainerSkill = 0;
    uint8_t trainerClass = 0;
    // P04 creature template immunity: the creature_immunities set this template
    // points at through creature_template.CreatureImmunitiesId, reduced to the
    // two columns that intersect any admitted spell (SchoolMask, 7 bits, and
    // MechanicsMask, 64 bits; the source audit section
    // 6.5 measured DispelTypeMask, Effects, Auras, ImmuneAoE and ImmuneChain to
    // intersect nothing). Creature::LoadTemplateImmunities applies them with the
    // placeholder spell id UINT32_MAX (Creature.cpp:2251), which is how the
    // reference tells template immunity from aura immunity. Zero on a creature
    // without a set, and on any catalog built before the field existed.
    uint8_t immuneSchoolMask = 0;
    uint64_t immuneMechanicsMask = 0;
    // P04 creature_template_resistance, one value per school in the
    // UNIT_FIELD_RESISTANCES order 1..6: holy, fire, nature, frost, shadow,
    // arcane (ObjectMgr.cpp:764-808 into CreatureTemplate::resistance). Negative
    // rows are clamped to zero at compile time, so the uint32 wrap of
    // Unit::GetResistance cannot arise here. Zero without a row.
    std::array<uint16_t,6> resistances{};
    // P05  UNIT_FIELD_COMBATREACH and UNIT_FIELD_BOUNDINGRADIUS, as
    // Creature::SetObjectScale derives them (Creature.cpp:3536-3550):
    // creature_model_info for the chosen display id, scaled by that model row's
    // DisplayScale. `combatReach` is what Unit::IsWithinCombatRange,
    // Unit::GetMeleeRange and Spell::CheckRange add to every range test
    // (Unit.cpp:766-803, Spell.cpp:7301-7396); `boundingRadius` is what
    // Unit::IsWithinBoundaryRadius compares against, floored at MIN_MELEE_REACH
    // (Unit.cpp:820-828), which is the facing check's own escape. Zero on a
    // catalog built previously; localCreatureCombatReach() then substitutes
    // the reference's own DEFAULT_WORLD_OBJECT_SIZE.
    float combatReach = 0, boundingRadius = 0;
};
/// creature_template.npcflag bits, as the server writes them. The catalog is
/// the authority whenever it carries the field; when it does not, the
/// transcription in local_service_npcs_generated.inc stands in - see
/// localEffectiveNpcFlags() in local_services.hpp, which is the only place
/// these should be read from a definition.
inline constexpr uint32_t kLocalNpcFlagTrainer = 0x00000010u;
inline constexpr uint32_t kLocalNpcFlagTrainerClass = 0x00000020u;
inline constexpr uint32_t kLocalNpcFlagTrainerProfession = 0x00000040u;
inline constexpr uint32_t kLocalNpcFlagVendor = 0x00000080u;
inline constexpr uint32_t kLocalNpcFlagVendorAmmo = 0x00000100u;
inline constexpr uint32_t kLocalNpcFlagVendorFood = 0x00000200u;
inline constexpr uint32_t kLocalNpcFlagVendorPoison = 0x00000400u;
inline constexpr uint32_t kLocalNpcFlagVendorReagent = 0x00000800u;
inline constexpr uint32_t kLocalNpcFlagRepair = 0x00001000u;
inline constexpr uint32_t kLocalNpcFlagInnkeeper = 0x00010000u;
inline constexpr uint32_t kLocalNpcFlagBanker = 0x00020000u;
inline constexpr uint32_t kLocalNpcFlagAuctioneer = 0x00200000u;

/// Below this height a character has left the world rather than gone somewhere
/// low, and the authority puts them back. The lowest real ground in 3.3.5a is
/// a few hundred units above sea level's zero at worst, so this leaves the
/// whole of the playable world - including the deepest instance floors - well
/// clear of it, and only catches a fall with nothing under it.
inline constexpr float kLocalWorldFloorZ = -2000.0f;

/// What the owner's own client reports about how its character is moving.
///
/// The authority owns positions and rules; it does not own terrain. There is no
/// height map, no collision and no liquid in this realm - the renderer holds all
/// three, because it has to draw them - so these two bits are the only way the
/// host can tell a character dropping through the air from one walking down a
/// hill, or one that hit water from one that hit stone. Retail draws the line in
/// exactly the same place: the client sets MOVEMENTFLAG_FALLING and the server
/// keeps the fall height and deals the damage.
///
/// Falling means airborne *and* descending, so the apex of a jump is where a
/// fall starts and a jump on the spot measures no drop at all.
///
/// These arrive with a reported position and are trusted exactly as far: the
/// authority still owns the measurement, the curve and the health, so a client
/// that lies about them can spare itself a fall it had coming and can do
/// nothing whatever to anybody else.
inline constexpr uint8_t kLocalMovementFalling = 0x01;
inline constexpr uint8_t kLocalMovementInLiquid = 0x02;
inline constexpr uint8_t kLocalMovementIndoors = 0x04;
inline constexpr uint8_t kLocalMovementMask = kLocalMovementFalling | kLocalMovementInLiquid | kLocalMovementIndoors;

/// Fall damage, as 3.3.5a deals it.
///
/// Transcribed from Player::HandleFall of the same server line this realm's
/// world data is imported from: nothing at all below 14.57 units of drop, then
/// 1.8% of the character's maximum health per further unit, less a 24.26%
/// offset, capped at the whole bar. Nothing here is fitted - the three numbers
/// are the upstream ones - and the shape they give is the one a player
/// remembers: the first damaging fall costs about two percent, and anything
/// past roughly sixty-nine units is fatal from full health.
///
/// The offset is what makes the curve start gently rather than at a step, and
/// truncating rather than rounding is what keeps a fall of exactly the safe
/// distance free for a low-level character whose two percent is under a point.
inline constexpr float kLocalFallSafeDistance = 14.57f;
inline constexpr float kLocalFallDamagePerUnit = 0.018f;
inline constexpr float kLocalFallDamageOffset = 0.2426f;
/// The drop at which the curve reaches the whole health bar (~69.03 units).
inline constexpr float kLocalFallLethalDistance =
    (1.0f + kLocalFallDamageOffset) / kLocalFallDamagePerUnit;
inline uint32_t localFallDamage(float droppedUnits, uint32_t maxHealth) {
    if (!std::isfinite(droppedUnits) || droppedUnits < kLocalFallSafeDistance || !maxHealth) return 0;
    const float fraction = kLocalFallDamagePerUnit * droppedUnits - kLocalFallDamageOffset;
    if (fraction <= 0.0f) return 0;
    if (fraction >= 1.0f) return maxHealth;
    return std::min(maxHealth, uint32_t(fraction * float(maxHealth)));
}

/// Any of the five merchant bits. A creature with one of them sells something.
inline constexpr uint32_t kLocalNpcFlagAnyVendor =
    kLocalNpcFlagVendor | kLocalNpcFlagVendorAmmo | kLocalNpcFlagVendorFood |
    kLocalNpcFlagVendorPoison | kLocalNpcFlagVendorReagent;

// Authored vehicle weapon profiles. Effects are independent of rider stats.
inline constexpr size_t kLocalVehicleAbilities=6;
struct LocalVehicleAbility {
    uint32_t spellId=0,powerCost=0,cooldownMs=0,damage=0,repair=0;
    uint8_t seatMask=0;
    float range=0;
    float projectileSpeed=0,projectileGravity=0,projectileRadius=.5f;
    uint32_t projectileLifetimeMs=0;
    uint32_t castTimeMs=0;
    float areaRadius=0;
    uint8_t schoolMask=kLocalVehiclePhysicalSchool;
    LocalVehiclePowerType powerType=LocalVehiclePowerType::Energy;
    bool interruptOnMove=true;
};
struct LocalVehicleKit {
    uint32_t id=0,maxPower=0,regenPerSecond=0;
    float minPitch=-1.4f,maxPitch=1.4f,muzzleHeight=1.5f;
    std::array<LocalVehicleAbility,kLocalVehicleAbilities> abilities{};
};
inline constexpr size_t kLocalMaxVehicleProjectiles=16;
struct LocalVehicleProjectile {
    uint32_t id=0,spellId=0,mapId=0,instanceId=0,phaseMask=0,remainingMs=0;
    uint64_t sourceGuid=0,ownerGuid=0;
    float x=0,y=0,z=0,vx=0,vy=0,vz=0,gravity=0;
    // Authority only; never decoded from a guest command or a save.
    uint64_t sourceEpoch=0;
    uint32_t damage=0;
    float radius=0,traveled=0,maxRange=0,areaRadius=0;
    uint8_t schoolMask=kLocalVehiclePhysicalSchool;
};
inline constexpr size_t kLocalMaxVehicleCasts=16;
struct LocalVehicleCast {
    uint64_t sourceGuid=0,ownerGuid=0,targetGuid=0;
    uint32_t spellId=0,remainingMs=0,totalMs=0,mapId=0,instanceId=0,phaseMask=0;
    uint8_t slot=0,seat=0;
    // Authority-only lifecycle and launch snapshots. The LAN deck omits these.
    uint64_t sourceEpoch=0,targetEpoch=0;
    uint32_t ownerPositionRevision=0;
    float sourceX=0,sourceY=0,sourceZ=0,sourceOrientation=0,aimYaw=0,aimPitch=0;
};
// 2.39: a spawn's default movement (creature.MovementType / wander_distance,
// creature_addon.path_id) and one waypoint_data node of a patrol path.
struct LocalSpawnMotion {
    uint8_t movementType = 0;   // 1 random within wanderDistance, 2 the waypoint path
    uint16_t currentWaypoint = 0;
    float wanderDistance = 0;
    uint32_t pathId = 0;
};
struct LocalWaypointNode {
    float x = 0, y = 0, z = 0, orientation = 0;
    bool hasOrientation = false, smooth = false;
    uint32_t delayMs = 0;
    uint8_t moveType = 0;       // 0 walk, 1 run, 2 land, 3 takeoff
    uint16_t id = 0;            // waypoint_data.point (MovementInform's data)
};
struct LocalNpcSpawn {
    uint32_t id = 0, entry = 0, mapId = 0;
    float x = 0, y = 0, z = 0, orientation = 0;
    // Zero required/excluded masks mean unphased.  These are evaluated per
    // player, so LAN peers in different phases can receive different NPC decks.
    uint32_t requiredPhaseMask = 0, excludedPhaseMask = 0;
    // 5.4: optional authority-owned scripted vehicle attached to this spawn.
    // vehicleId is the script-visible identity; seats are 0..seatCount-1.
    uint32_t vehicleId = 0;
    uint8_t vehicleSeatCount = 0, vehicleControllerSeat = 0;
    std::array<std::array<float,3>,8> vehicleSeatOffsets{};
    // 2.39: the default movement (creature.MovementType: 1 random within
    // wanderDistance of the spawn, 2 the creature_addon path starting at
    // currentWaypoint) the catalog's motion table carries.
    uint8_t movementType = 0;
    uint16_t currentWaypoint = 0;
    float wanderDistance = 0;
    uint32_t pathId = 0;
};
inline constexpr size_t kLocalMaxNpcSnares = 8;
inline constexpr size_t kLocalMaxNpcControls = 4;
inline constexpr size_t kLocalMaxNpcBuffs = 16;
// Four diminishing groups are reachable from the admitted spells; eight leaves
// headroom without pretending to the reference's twenty-one.
inline constexpr size_t kLocalMaxNpcDiminishing = 8;
// Named for the two places the importer already compared against a bare number.
inline constexpr uint8_t kLocalMechanicSilenced = 9;
// SPELL_PREVENTION_TYPE_SILENCE. Read from the spell being prevented.
inline constexpr uint8_t kLocalPreventionSilence = 1;
inline constexpr uint8_t kLocalMechanicStunned = 12;
inline constexpr uint8_t kLocalMechanicBleed = 15;
// AURA_INTERRUPT_FLAG_TAKE_DAMAGE. The reference's rule 1 is binary: no
// threshold and no chance, and it fires before the zero-damage early-out.
inline constexpr uint32_t kLocalAuraInterruptTakeDamage = 0x2u;
inline constexpr size_t kLocalMaxNpcDamageAuras = 8;
inline constexpr size_t kLocalMaxNpcStormstrikeAuras = 8;
struct LocalNpcStormstrikeAura {
    uint32_t spellId=17364,remainingMs=0;
    uint64_t casterGuid=0;
    uint8_t charges=4;
    uint32_t casterRevision=0; // Authority-only encounter lifecycle.
};
struct LocalNpcSnare {
    uint32_t spellId=0, remainingMs=0;
    uint64_t casterGuid=0;
    uint8_t percent=0;
    uint32_t casterRevision=0; // Authority-only: travel cancels the old encounter effect.
};
// P04: a control applied to an owned creature. Transient authority and LAN
// state, never character-save data, exactly like LocalNpcSnare. UNIT_STATE_ROOT
// is deliberately absent: the reference keeps root outside UNIT_STATE_CONTROLLED
// and a rooted unit still swings, casts and turns, so root is not a control
// this list models.
enum class LocalNpcControlKind : uint8_t { Stun=0, Silence=1 };
struct LocalNpcControl {
    uint32_t spellId=0, remainingMs=0;
    uint64_t casterGuid=0;
    uint32_t casterRevision=0; // Authority-only: travel cancels the old encounter effect.
    uint8_t kind=0;            // LocalNpcControlKind
};
struct LocalNpcThreatView {
    uint64_t viewerGuid=0,amount=0;
    uint32_t rawBasisPoints=0;uint16_t scaledBasisPoints=0;uint8_t status=0;bool present=false;
};
struct LocalNpcThreat { uint64_t guid=0,amount=0; };
inline constexpr size_t kLocalMaxNpcThreat=100;
/// A creature aura from a creature cast (a self-buff, an ally's heal-over-time):
/// AuraEffect::CalculateAmount resolved once at application, x stacks.
struct LocalNpcBuff {
    uint32_t spellId=0,remainingMs=0,durationMs=0;
    uint64_t casterGuid=0;
    uint8_t stacks=1;
    int32_t hastePct=0,damagePct=0,damageFlat=0,attackPower=0,damageTakenFlat=0,damageTakenPct=0,healingPct=0;
    uint8_t damagePctSchool=0,damageFlatSchool=0,damageTakenSchool=0;
    uint32_t periodicHeal=0,periodicIntervalMs=0,periodicNextMs=0,periodicTriggerSpellId=0;
    bool indefinite=false;
    // 2.37 (authority-only; the wire carries the presentation row above):
    // movement speed, magic resistance, cast speed, hit chance, dodge/parry/
    // block, the remaining absorb, immunities, max health, the damage shield
    // and the proc definition of the aura.
    int32_t speedPct=0,resistance=0,castSpeedPct=0,hitChancePct=0,dodgePct=0,parryPct=0,blockPct=0,maxHealth=0,maxHealthPct=0;
    uint32_t absorbRemaining=0,damageShield=0,procSpellId=0,procFlags=0,procDamage=0,mechanicImmunity=0;
    uint8_t resistanceSchool=0,absorbSchool=0,schoolImmunity=0,damageImmunity=0,damageShieldSchool=0,procSchool=0,procChance=0,procCharges=0;
    // 2.39 (authority-only): a self stun (1) / root (2), invisibility.
    uint8_t selfControl=0;
    bool invisible=false;
    bool operator==(const LocalNpcBuff&) const = default;
};
/// The sum of one creature-buff field over the creature's live buffs
/// (Unit::GetTotalAuraModifier for MOD_DODGE/PARRY/BLOCK_PERCENT, MOD_HIT_CHANCE).
template<class Npc> int32_t localNpcBuffTotal(const Npc& n,int32_t LocalNpcBuff::*field) {
    int32_t sum=0;for(const auto& b:n.npcBuffs)if(b.remainingMs||b.indefinite)sum+=b.*field;return sum;
}
struct LocalRealmNpc {
    uint64_t combatEpoch=0; // Authority-only identity of this NPC encounter.
    LocalNpcThreatView playerThreat;
    bool viewerVehicleCombat=false; // Per-viewer LAN fact; never attributed as player threat.
    std::array<LocalNpcThreat,kLocalMaxNpcThreat> threat{}; // Authority-only, thousandths of threat.
    std::vector<LocalHealingAuraView> damageAuras; // Authority-derived periodic damage views.
    std::vector<LocalNpcSnare> snares; // Transient; replicated, never character-save data.
    std::vector<LocalNpcControl> controls; // Transient; replicated, never character-save data.
    std::vector<LocalNpcDiminishing> diminishing; // Authority-only; neither saved nor replicated.
    std::vector<LocalNpcStormstrikeAura> stormstrikeAuras;
    uint64_t guid = 0, targetGuid = 0, lootOwner = 0;
    // Authority-only cohort captured at death. Only lootOwner is sent over LAN;
    // new group members cannot acquire an earlier corpse's reservation.
    std::array<uint64_t, 5> lootCandidates{};
    uint32_t entry = 0, displayId = 0, mapId = 0, instanceId = 0, health = 0, maxHealth = 0;
    float x = 0, y = 0, z = 0, orientation = 0;
    uint8_t level = 1;
    std::string name;
    bool hostile = false, aggressive = false, questGiver = false, dead = false, lootable = false;
    // Set when this NPC serves a taxi node, and which one. Resolved on the
    // authority when the NPC is spawned; replicated so a LAN guest can open
    // the flight list without its own copy of the network.
    bool flightMaster = false;
    uint32_t taxiNodeId = 0;
    /// Set when this NPC runs an auction house. Resolved on the authority from
    /// the catalog's npcflag, replicated so a guest sees the same auctioneers.
    bool auctioneer = false, banker = false;
    /// The rest of the services this NPC offers, resolved on the authority the
    /// same way and replicated for the same reason: a guest that decided for
    /// itself could offer training the host would refuse.
    bool vendor = false, repairer = false, classTrainer = false,
         professionTrainer = false, innkeeper = false;
    /// Which categories of goods a merchant carries, packed by
    /// localVendorCategories(). Zero on anything that is not a merchant.
    uint8_t vendorCategories = 0;
    /// What a trainer teaches: a SkillLine id for a profession trainer, a class
    /// id 1-11 for a class trainer. Zero when the NPC's own subname named
    /// something this realm does not model, in which case it teaches nothing.
    uint16_t trainerSkill = 0;
    uint8_t trainerClass = 0;
    // Static deck passengers retain model-local offsets on both host and guest.
    // These are transient world actors, not additional saved characters.
    uint32_t transportEntry = 0;
    float transportX = 0, transportY = 0, transportZ = 0, transportOrientation = 0;
    // Internal authority simulation values; never accepted from a client.
    uint32_t spawnId = 0;
    uint32_t scriptActorId = 0; // Transient authored actor; never saved or client-created.
    uint32_t scriptLifetimeMs = 0;
    bool scriptActorRetired = false; // Hidden tombstone, pruned only at a stable tick boundary.
    uint32_t requiredPhaseMask = 0, excludedPhaseMask = 0;
    uint32_t vehicleId = 0;
    uint8_t vehicleSeatCount = 0, vehicleControllerSeat = 0;
    std::array<std::array<float,3>,8> vehicleSeatOffsets{}; // Immutable authority cache; clients follow owner positions.
    uint32_t vehiclePower=0,vehicleGlobalCooldownMs=0;
    std::array<uint32_t,kLocalVehicleAbilities> vehicleCooldownMs{};
    std::array<std::array<float,2>,8> vehicleAim{}; // Per-seat hull-relative yaw / pitch, radians.
    uint32_t vehicleRegenRemainder=0; // Authority-only fractional power, thousandths.
    uint64_t escortOwner = 0; // Authority-only route reservation.
    float homeX = 0, homeY = 0, homeZ = 0, attackTimer = 0, respawnTimer = 0;
    // 2.38: the spawn position (Creature::GetRespawnPosition); the home moves
    // along an escort path, the spawn does not. Authority-only.
    float spawnX = 0, spawnY = 0, spawnZ = 0, spawnOrientation = 0;
    uint32_t npcCastingSpellId=0,npcCastRemainingMs=0,npcSpellTimerMs=0,npcSpellReturnMs=0;
    uint64_t npcCastTargetGuid=0;
    bool npcSpellTimerInitialized=false,npcSpellLaunched=false,npcSpellReflected=false,npcSpellReflectReturn=false,npcSpellMissed=false;
    // Further SmartAI rows of the script owner (row 0 uses npcSpellTimerMs)
    // and the rows already run (SmartScriptHolder::runOnce).
    std::vector<uint32_t> npcSpellExtraTimers;
    uint32_t npcSpellDoneMask=0;
    // SmartScript state: the event phase, the last invoker (mLastInvoker), the
    // running timed action list and the two SmartAI switches. Authority-only.
    uint8_t smartPhase=0;
    uint64_t smartInvoker=0;
    uint32_t smartListId=0,smartListTimer=0;
    uint8_t smartListIndex=0,smartListTimerType=0;
    bool npcCombatMove=true,npcAutoAttack=true;
    // SMART_ACTION_EVADE / CALL_FOR_HELP requested by the script this tick.
    bool smartEvadeRequested=false,smartCallForHelpEmote=false;
    float smartCallForHelpRange=0;
    // A channel in progress: the spell stays in npcCastingSpellId with
    // npcChanneling set; the creature neither moves nor swings meanwhile.
    bool npcChanneling=false;
    uint32_t npcChannelRemainingMs=0;
    // Buffs the creature carries from its own or an ally's SmartAI casts.
    std::vector<LocalNpcBuff> npcBuffs;
    // 2.37 SmartAI state (authority-only): SET_REACT_STATE (255 = template
    // default), UNIT_FIELD_FLAGS bits set by the script, SET_FACTION (0 =
    // template), SET_INVINCIBILITY_HP_LEVEL, SET_HEALTH_REGEN, DISABLE_EVADE,
    // SET_SIGHT_DIST / SET_COMBAT_DISTANCE, ADD/REMOVE_IMMUNITY, extra
    // attacks queued by SPELL_EFFECT_ADD_EXTRA_ATTACKS, the pending DIE and
    // FORCE_DESPAWN delays and the respawn override of a forced despawn.
    uint8_t npcReactState=255;
    uint32_t npcUnitFlags=0,npcFactionOverride=0,npcInvincibleHp=0,npcSightDistance=0,npcCombatDistance=0;
    bool npcRegenDisabled=false,npcEvadeDisabled=false,npcHomeReached=true;
    uint8_t npcSchoolImmunity=0,npcDamageImmunity=0,npcExtraAttacks=0;
    uint32_t npcMechanicImmunity=0,npcDieDelayMs=0,npcDespawnDelayMs=0,npcDespawnRespawnSeconds=0,npcCorpseDelayOverride=0;
    std::vector<uint32_t> npcSpellImmunity;
    bool npcDiePending=false,npcDespawnPending=false;
    // The script's copy of UNIT_FIELD_FLAGS replaces the template's once it
    // touched them; a forced despawn hides the corpse until the respawn;
    // SET_EVENT_FLAG_RESET(0) keeps the phase across OnReset.
    bool npcUnitFlagsOverride=false,npcDespawned=false,smartPhaseResetDisabled=false;
    // COMBAT_STOP this tick: the creature leaves combat without an evade;
    // FLEE_FOR_ASSIST requested this tick (Creature::DoFleeToGetAssistance).
    bool smartCombatStopped=false,smartFleeRequested=false,smartFleeEmote=false;
    // STORE_TARGET_LIST / SET_COUNTER / CREATE_TIMED_EVENT state.
    std::vector<std::pair<uint32_t,std::vector<uint64_t>>> smartStoredTargets;
    std::vector<std::pair<uint32_t,uint32_t>> smartCounters;
    struct SmartTimedEvent { uint32_t id=0,timerMs=0,repeatMinMs=0,repeatMaxMs=0; uint8_t chance=100; bool once=false; };
    std::vector<SmartTimedEvent> smartTimedEvents;
    // 2.38 summons (TempSummon, authority-only): the summoner's guid, the
    // TempSummonType (0 = not a summon), the timer and lifetime, the spell
    // that made it; a dead summon keeps its corpse for npcCorpseRemainingMs;
    // npcUnsummoned marks a removed summon until the stable tick boundary
    // prunes it. smartSummons is SmartScript's own summon list.
    uint64_t npcSummoner=0;
    uint8_t npcSummonType=0;
    uint32_t npcSummonTimerMs=0,npcSummonLifetimeMs=0,npcSummonSpellId=0,npcCorpseRemainingMs=0;
    bool npcUnsummoned=false,npcSummonOwned=false; // owned: an ally / pet summon, gone with its summoner's death
    std::vector<uint64_t> smartSummons;
    // 2.38 movement (MotionMaster's active slot, authority-only): 0 idle, 1 a
    // point (MOVE_TO_POS / MOVE_FORWARD, the id MOVEMENTINFORM carries), 2 an
    // escort path, 3 random movement around the home, 4 following a unit,
    // 5 a jump (EFFECT_MOTION_TYPE); the goal, the walk flag (SET_RUN 0),
    // SET_ROOT, SET_VISIBILITY(0).
    uint8_t npcMotion=0;
    float npcMotionX=0,npcMotionY=0,npcMotionZ=0,npcMotionSpeed=0;
    uint32_t npcMotionId=0,npcRandomDistance=0;
    bool npcWalking=false,npcRooted=false,npcHidden=false;
    // SmartAI escort state (SMART_ESCORT_*): the path, the point being moved
    // to (1-based), the escort flags, the forced movement (0 own, 1 walk,
    // 2 run), the pause timer, the quest, the despawn time, the invoker check.
    uint32_t escortPathId=0,escortPauseMs=0,escortQuestId=0,escortInvokerCheckMs=0;
    uint8_t escortIndex=0,escortMovement=0;
    bool escortActive=false,escortPaused=false,escortForcedPause=false,escortReached=false,escortRepeat=false,escortReturning=false;
    // SmartAI::SetDespawnTime / StartDespawn: 0 none, 1 armed, 2 counting, 3 hidden.
    uint8_t smartDespawnState=0;
    uint32_t smartDespawnMs=0;
    // SmartAI::SetFollow: the unit, distance, angle, the arrival creature
    // entry (INTERACTION_DISTANCE, alive state), the credit and its type.
    uint64_t followGuid=0;
    float followDistance=0,followAngle=0;
    uint32_t followEndEntry=0,followCredit=0,followArrivedTimerMs=1000,followCheckMs=0;
    uint8_t followCreditType=0;
    bool followArrivedAlive=true;
    // 2.39 default movement generators. RandomMovementGenerator: the wander
    // distance, the current one of the 12 destination points (12 = the
    // initial position), the move count and the next-move timer.
    // WaypointMovementGenerator (waypoint_data): the path, the node moved
    // to, repeat, the node delay, an explicit pause (MOVEMENT_PAUSE), the
    // stall flags and the loaded path id (Creature::m_path_id).
    // 2.39: SET/ADD/REMOVE_NPC_FLAG (cleared at respawn: JUST_RESPAWNED
    // restores the template's flags), Unit::SetSpeed rates (SET_MOVEMENT_SPEED).
    uint32_t npcFlagsOverride=0;
    bool npcFlagsOverridden=false;
    float npcRunSpeedRate=1.f,npcWalkSpeedRate=1.f;
    // 2.39 Creature::_playerDamageReq / _damagedByPlayer: the damage players,
    // their pets and vehicles dealt (capped at the remaining health) must
    // reach half the max health, with a player among the attackers, for the
    // death to reward (loot, experience, quest credit); reset at the respawn.
    uint32_t npcPlayerDamage=0;
    // 2.40 SET_GOSSIP_MENU (Creature::SetGossipMenuId; the template's menu
    // returns at the respawn).
    uint32_t npcGossipMenuOverride=0;
    bool npcGossipMenuOverridden=false;
    bool npcDamagedByPlayer=false;
    uint8_t npcDefaultMotion=0; // 1 random, 2 waypoint path
    float npcWanderDistance=0,npcRandomCenterX=0,npcRandomCenterY=0,npcRandomCenterZ=0;
    uint8_t npcRandomPoint=12,npcRandomMoveCount=0;
    bool npcRandomInit=false,npcRandomMoving=false;
    std::array<uint8_t,12> npcRandomFactors{};
    uint32_t npcRandomNextMoveMs=0;
    uint32_t patrolPathId=0,patrolLoadedPath=0,patrolDelayMs=0,patrolPauseMs=0;
    uint16_t patrolNode=0,patrolStartNode=0;
    bool patrolRepeat=true,patrolReached=true,patrolStalled=false,patrolHasBeenStalled=false,patrolDone=false,patrolPaused=false,patrolMoving=false;
    // SmartScript's text timer (TALK duration -> TEXT_OVER) and delayed talks.
    uint32_t smartTextTimerMs=0,smartTalkerEntry=0;
    uint8_t smartTextGroup=0;
    bool smartTextTimerActive=false;
    struct SmartPendingTalk { uint32_t delayMs=0; uint8_t group=0; uint64_t talker=0,target=0; };
    std::vector<SmartPendingTalk> smartPendingTalks;
    // SmartAI range mode (SetMainSpell / SetCurrentRangeMode): a caster in
    // range mode chases only to npcAttackDistance and swings only inside melee
    // range. Chosen once per spawn from the first COMBAT_MOVE row; SMART_ACTION_CAST
    // results change it and it survives evades, as in the pinned SmartAI.
    bool npcRangeMode=false,npcRangeInit=false;
    float npcAttackDistance=0;
    // Creature mana for SmartAI casts (Creature::InitStatsForLevel/Regenerate):
    // authority-only, rebuilt on spawn, never saved or replicated.
    bool npcManaReady=false;
    uint32_t npcMana=0,npcMaxMana=0,npcManaRegenMs=2000,npcSinceManaUseMs=5000,npcCastManaCost=0;
    // CURRENT_MELEE_SPELL: a queued next-swing special and the target it was cast at.
    uint32_t npcNextSwingSpellId=0;uint64_t npcNextSwingTargetGuid=0;
    // Spell::AddUnitTarget decides a melee special's hit result at launch; a
    // missile carries it to impact (0 = not rolled, else outcome + 1).
    uint8_t npcSpellMeleeOutcome=0;
    // Original SmartAI speech: authority-only, never saved or replicated.
    bool talkEngaged=false, smartTimersReady=false;
    uint64_t talkOnceMask=0;
    uint32_t talkKillCooldownMs=0;
    std::vector<std::pair<uint8_t,uint32_t>> smartTimers; // ownerIndex -> remaining ms
    // FLEE_FOR_ASSIST: 1 seek assistance, 2 timed flee, 3 distracted after the call.
    uint8_t fleeMode=0;
    uint32_t fleeMs=0;
    float fleeX=0, fleeY=0, fleeZ=0;
    uint64_t assistTargetGuid=0; // CallAssistance: attack this victim when the delay ends
    uint32_t assistDelayMs=0;
};

// A realm command may commit gameplay and still fail its atomic character
// save. The caller checkpoints this authority-only world slice before execute
// and restores it when that wider transaction rolls back.
struct LocalPendingScriptKill {
    uint64_t playerGuid=0;
    uint32_t npcEntry=0;
    uint64_t xp=0;
    uint32_t count=1;
};
struct LocalScriptActionCheckpoint {
    std::vector<LocalRealmNpc> npcs;
    std::vector<LocalScriptDialogue> dialogues;
    std::vector<LocalPendingScriptKill> pendingKills;
    uint64_t dialogueRevision = 0, overwrittenDialogues = 0, nextNpcEpoch = 0;
};
struct LocalMailboxSite {uint64_t guid=0;uint32_t mapId=0;float x=0,y=0,z=0,orientation=0;};
enum class LocalGameObjectKind : uint8_t { Script, Door, Chest, Resource, Decorative, Chair };
/// Shared lifecycle. Status 3 is a pooled spawn that the pool currently keeps
/// absent (AzerothCore PoolMgr keeps only max_limit members spawned).
inline constexpr uint8_t kLocalGameObjectReady=0, kLocalGameObjectOpen=1, kLocalGameObjectDepleted=2, kLocalGameObjectDormant=3;
struct LocalGameObjectState {
    uint32_t id=0, revision=1;
    uint8_t status=0; // Ready/closed=0, open=1, depleted=2, pooled-dormant=3.
    uint32_t remainingMs=0;
    bool operator==(const LocalGameObjectState&) const = default;
};
/// One gameobject_loot_template row. `chance` is a percentage; a grouped row
/// with chance 0 is an equal-chance member of its group (LootMgr.cpp).
struct LocalGameObjectLootRow {
    uint32_t itemId=0;
    float chance=100;
    uint8_t group=0;
    uint16_t minCount=1,maxCount=1;
    bool questRequired=false;
    bool operator==(const LocalGameObjectLootRow&) const = default;
};
inline constexpr size_t kLocalMaxGameObjectLootRows=192;
inline constexpr size_t kLocalMaxGameObjectToolItems=8;
inline constexpr uint8_t kLocalMaxChairSlots=32;
// Authored open-world objects. A shared spawn has one state across visible phases.
struct LocalGameObject {
    uint32_t id=0, entry=0, displayId=0, mapId=0;
    std::string name;
    float x=0,y=0,z=0,orientation=0,scale=1,useRadius=5;
    uint32_t requiredPhaseMask=0,excludedPhaseMask=0,requiredScriptId=0,requiredQuestId=0;
    int32_t requiredValue=0;
    LocalGameObjectKind kind=LocalGameObjectKind::Script;
    std::vector<LocalItemStack> loot;
    uint32_t money=0,respawnMs=0,requiredSkillId=0,requiredSkill=0,toolItemId=0;
    // Reviewed original chests/resources: rolled loot, alternative tools
    // (TotemCategory members), GO_FLAG_INTERACT_COND quest-loot gating, a
    // non-consumable chest and the pool that owns this spawn.
    std::vector<LocalGameObjectLootRow> lootTable;
    std::vector<uint32_t> toolItemIds;
    bool questLootOnly=false, persistent=false;
    uint32_t poolId=0;
    // GAMEOBJECT_TYPE_CHAIR: data0 slots and data1 height (low/medium/high).
    uint8_t chairSlots=0, chairHeight=0;
};
/// Original SMART_ACTION_TALK rows (tools/local_realm/compile_creature_talk.py).
enum class LocalCreatureTalkEvent : uint8_t { Aggro=1, Kill=2, Death=3, QuestAccept=4, QuestReward=5,
    UpdateIc=6, UpdateOoc=7, HealthPct=8 };
enum class LocalCreatureSmartAction : uint8_t { Talk=1, FleeForAssist=2 };
/// creature_text.Type values, which are the ChatMsg ids the client prints.
inline constexpr uint8_t kLocalChatMonsterSay=12, kLocalChatMonsterYell=14, kLocalChatMonsterWhisper=15,
    kLocalChatMonsterEmote=16, kLocalChatRaidBossEmote=41;
struct LocalCreatureTalkLine { std::string text; uint8_t chatType=kLocalChatMonsterSay; float weight=100; };
struct LocalCreatureTalkRule {
    int64_t owner=0;          // >0 creature entry, <0 spawn guid script
    uint32_t entry=0, questId=0, cooldownMinMs=0, cooldownMaxMs=0;
    LocalCreatureTalkEvent event=LocalCreatureTalkEvent::Aggro;
    uint8_t chance=100, ownerIndex=0; // ownerIndex: bit in LocalRealmNpc::talkOnceMask
    bool once=false, keepOnEvade=false, invokerTarget=false, withEmote=false;
    LocalCreatureSmartAction action=LocalCreatureSmartAction::Talk;
    // Timed events: UPDATE_IC/OOC initial+repeat, HEALTH_PCT range+repeat.
    uint32_t initialMinMs=0, initialMaxMs=0, repeatMinMs=0, repeatMaxMs=0;
    uint8_t minPct=0, maxPct=100;
    std::vector<LocalCreatureTalkLine> lines;
};
inline constexpr size_t kLocalMaxCreatureTalkRules=4096;
/// A creature_text group the generated SmartAI family's TALK action names
/// (2.37; compile_creature_talk.py's textGroups): the talker's entry and the
/// group id with its weighted lines.
struct LocalCreatureTextGroup {
    uint32_t entry=0;
    uint8_t group=0;
    std::vector<LocalCreatureTalkLine> lines;
};
inline constexpr size_t kLocalMaxCreatureTextGroups=16384;
/// World.conf defaults used by Creature::DoFleeToGetAssistance / CallAssistance.
inline constexpr float kLocalFleeAssistanceRadius=30.f, kLocalAssistanceRadius=10.f;
inline constexpr uint32_t kLocalAssistanceDelayMs=2000, kLocalFleeDelayMs=7000, kLocalSeekAssistanceTimeoutMs=10000;
/// CreatureTextMgr listen ranges (ListenRange.Say / .TextEmote / .Yell).
inline float localCreatureTalkRange(uint8_t chatType) {
    return chatType==kLocalChatMonsterYell?300.f:chatType==kLocalChatRaidBossEmote?1000.f:25.f;
}
/// Race/class/gender placeholders the client expands in creature text.
std::string localExpandCreatureText(const std::string& text,const LocalRealmPlayer* target);
struct LocalChairSeat {
    uint32_t objectId=0,mapId=0;
    uint8_t slot=0,standState=0;
    float x=0,y=0,z=0,orientation=0;
};
/// AzerothCore pool_template/pool_gameobject with equal member chances.
struct LocalGameObjectPool {
    uint32_t id=0, maxActive=1;
    std::vector<uint32_t> members; // sorted object IDs
};
inline constexpr size_t kLocalMaxGameObjectPools=256;
inline constexpr size_t kLocalMaxGameObjects=1024;
inline bool localGameObjectStateful(LocalGameObjectKind kind) {
    return kind==LocalGameObjectKind::Door || kind==LocalGameObjectKind::Chest || kind==LocalGameObjectKind::Resource;
}
/// Player::SkillGainChance for gathering (Player.cpp UpdateGatherSkill):
/// grey at required+100, green +50, yellow +25, otherwise orange; returned in
/// thousandths with the default rates 0/25/75/100 %.
inline uint32_t localGatherSkillChance(uint32_t current,uint32_t required) {
    if(current>=required+100)return 0;
    if(current>=required+50)return 250;
    if(current>=required+25)return 750;
    return 1000;
}
/// GameObject::Use GAMEOBJECT_TYPE_CHAIR slot geometry: slots lie on the line
/// through the object orthogonal to its facing, `size` apart and centred.
inline std::array<float,2> localChairSlotPosition(const LocalGameObject& object,uint8_t slot) {
    const float relative=object.scale*float(slot)-object.scale*float(object.chairSlots-1)/2.f;
    const float orthogonal=object.orientation+1.57079632679f;
    return {object.x+relative*std::cos(orthogonal),object.y+relative*std::sin(orthogonal)};
}
/// Stand state UNIT_STAND_STATE_SIT_LOW_CHAIR(4) + chair height.
inline uint8_t localChairStandState(const LocalGameObject& object) { return uint8_t(4+std::min<uint8_t>(object.chairHeight,2)); }
/// Nearest free slot, skipping slots another character occupies (within 0.1
/// yards, as GameObject::Use does). Returns -1 when every slot is taken.
template<class Occupied>
inline int localChairNearestFreeSlot(const LocalGameObject& object,float px,float py,Occupied&& occupied) {
    if(object.kind!=LocalGameObjectKind::Chair || !object.chairSlots)return -1;
    int best=-1;float lowest=std::numeric_limits<float>::max();
    for(uint8_t slot=0;slot<object.chairSlots;++slot) {
        const auto pos=localChairSlotPosition(object,slot);
        if(occupied(pos[0],pos[1]))continue;
        const float d=std::hypot(px-pos[0],py-pos[1]);
        if(d<=lowest){lowest=d;best=slot;}
    }
    return best;
}
/// LootTemplate::Process for one gameobject loot id and one looter. Ungrouped
/// rows roll independently; each group yields at most one row: explicitly
/// chanced rows first against one 0-100 roll, then a uniform equal-chance row.
/// Quest rows reach only a player who still needs that item.
template<class Rng,class NeedsItem>
inline std::vector<LocalItemStack> localRollGameObjectLoot(const std::vector<LocalGameObjectLootRow>& rows,Rng& rng,NeedsItem&& needs) {
    std::vector<LocalItemStack> out;
    const auto chance=[&]{return std::uniform_real_distribution<float>(0.f,100.f)(rng);};
    const auto emit=[&](const LocalGameObjectLootRow& row) {
        if(row.questRequired && !needs(row.itemId))return;
        const auto count=uint16_t(std::uniform_int_distribution<uint32_t>(row.minCount,std::max(row.minCount,row.maxCount))(rng));
        for(auto& stack:out)if(stack.itemId==row.itemId){stack.count=uint16_t(std::min<uint32_t>(65535,stack.count+count));return;}
        LocalItemStack stack;stack.itemId=row.itemId;stack.count=count;out.push_back(stack);
    };
    uint8_t maxGroup=0;
    for(const auto& row:rows) {
        if(row.group){maxGroup=std::max(maxGroup,row.group);continue;}
        if(row.chance>=100.f || chance()<row.chance)emit(row);
    }
    for(uint32_t group=1;group<=maxGroup;++group) {
        std::vector<const LocalGameObjectLootRow*> explicitRows,equalRows;
        for(const auto& row:rows)if(row.group==group)(row.chance>0?explicitRows:equalRows).push_back(&row);
        if(explicitRows.empty()&&equalRows.empty())continue;
        const LocalGameObjectLootRow* picked=nullptr;
        if(!explicitRows.empty()) {
            float roll=chance();
            for(const auto* row:explicitRows) {
                if(row->chance>=100.f){picked=row;break;}
                roll-=row->chance;
                if(roll<0){picked=row;break;}
            }
        }
        if(!picked && !equalRows.empty())
            picked=equalRows[std::uniform_int_distribution<size_t>(0,equalRows.size()-1)(rng)];
        if(picked)emit(*picked);
    }
    return out;
}
inline bool validLocalGameObjectLootTable(const std::vector<LocalGameObjectLootRow>& rows) {
    if(rows.size()>kLocalMaxGameObjectLootRows)return false;
    std::set<std::pair<uint8_t,uint32_t>> seen;
    for(const auto& row:rows) {
        if(!row.itemId || !std::isfinite(row.chance) || row.chance<0 || row.chance>100 || (!row.group && row.chance<=0) ||
           !row.minCount || row.maxCount<row.minCount || !seen.insert({row.group,row.itemId}).second)return false;
    }
    return true;
}
inline uint64_t localGameObjectGuid(uint32_t id) { return 0xf110000100000000ULL | uint64_t(id); }
inline bool localGameObjectVisible(const LocalGameObject& object,const LocalRealmPlayer& player) {
    return !player.instanceId && object.mapId==player.mapId &&
        localPhaseVisible(player.phaseMask,object.requiredPhaseMask,object.excludedPhaseMask);
}
inline bool localGameObjectUsable(const LocalGameObject& object,const LocalRealmPlayer& player) {
    if(object.kind==LocalGameObjectKind::Decorative || !localGameObjectVisible(object,player) || player.dead || player.ghost || !player.health ||
       player.vehicleGuid || player.flight.active || player.transportEntry || player.castingSpellId || player.attackTarget ||
       !localScriptConditionMatches(player,object.requiredScriptId,object.requiredValue))return false;
    // Range first: most of the ~900 placed objects are far away every frame.
    const float dx=object.x-player.x,dy=object.y-player.y,dz=object.z-player.z;
    if(!std::isfinite(dx+dy+dz) || dx*dx+dy*dy+dz*dz>object.useRadius*object.useRadius)return false;
    if(object.requiredSkillId && std::none_of(player.professions.begin(),player.professions.end(),[&](const auto& skill){
        return skill.skillId==object.requiredSkillId && skill.current>=object.requiredSkill;
    }))return false;
    if((object.toolItemId || !object.toolItemIds.empty()) && std::none_of(player.inventory.begin(),player.inventory.end(),[&](const auto& item){
        return item.count && (item.itemId==object.toolItemId ||
            std::find(object.toolItemIds.begin(),object.toolItemIds.end(),item.itemId)!=object.toolItemIds.end());
    }))return false;
    if(object.requiredQuestId && std::none_of(player.quests.begin(),player.quests.end(),[&](const auto& quest){
        return quest.id==object.requiredQuestId && quest.status==LocalQuestStatus::Active;
    }))return false;
    return true;
}
struct LocalWorldContent {
    mutable bool mailboxSitesReady=false;
    mutable uint32_t mailboxMap=0;
    mutable float mailboxX=0,mailboxY=0,mailboxZ=0;
    mutable std::vector<LocalMailboxSite> mailboxSites;
    uint32_t fingerprint = 0;
    std::string sourcePath;
    std::vector<LocalItemDefinition> items;
    std::vector<LocalSpellDefinition> spells;
    // Immutable installed-content index: talent ID, rank, spell ID. No player cache.
    bool talentIndexReady=false;
    std::vector<std::array<uint32_t,3>> talentSpellIndex;
    // Sorted by spellId, like every other definition list here.
    std::vector<LocalRecipe> recipes;
    std::vector<LocalQuestDefinition> quests;
    bool questChainCatalogRequired = false;
    std::map<uint32_t, LocalQuestChainGate> questChainGates;
    // Authored state/phase transitions keyed by quest ID or NPC entry. They are
    // immutable content; only their results live in the character save.
    std::vector<LocalScriptTrigger> scriptTriggers;
    std::vector<LocalScriptTimerAction> scriptTimerActions;
    std::vector<LocalScriptAction> scriptActions;
    std::vector<LocalScriptArea> scriptAreas;
    std::vector<LocalGameObject> gameObjects;
    std::vector<LocalGameObjectPool> gameObjectPools;
    // Sorted by (owner, event); guid-scripted spawns ignore their entry rules.
    std::vector<LocalCreatureTalkRule> creatureTalk;
    std::vector<LocalCreatureTextGroup> creatureTextGroups; // sorted by (entry, group)
    const LocalCreatureTextGroup* creatureText(uint32_t entry,uint8_t group) const {
        auto it=std::lower_bound(creatureTextGroups.begin(),creatureTextGroups.end(),std::pair<uint32_t,uint8_t>{entry,group},
            [](const auto& a,const auto& key){return a.entry!=key.first?a.entry<key.first:a.group<key.second;});
        return it!=creatureTextGroups.end()&&it->entry==entry&&it->group==group?&*it:nullptr;
    }
    std::vector<uint32_t> creatureGuidScripts;
    std::vector<LocalEscortRoute> escortRoutes;
    // Immutable shared schedules; mutable lifecycle belongs to LocalRealm and
    // is persisted once for the authority rather than once per character.
    std::vector<LocalWorldEventSchedule> worldEvents;
    std::vector<LocalVehicleKit> vehicleKits;
    std::vector<LocalNpcDefinition> npcs;
    std::vector<LocalNpcSpawn> spawns;
    // 2.39: waypoint_data paths of a content file without a catalog (tests);
    // the catalog's paths.pack is the source otherwise.
    std::map<uint32_t,std::vector<LocalWaypointNode>> waypointPaths;
    // 2.40: the gossip tables of a test content (the catalog carries them otherwise).
    std::map<uint32_t,LocalGossipMenu> gossipMenus;
    std::map<uint32_t,LocalGossipText> gossipTexts;
    std::map<uint32_t,LocalGossipOwner> gossipOwners;
    LocalRealmPlayer start;
    bool classResources = false, clientStarterSpells = false;
    std::string spellDiagnostic;
    std::shared_ptr<LocalWorldCatalog> catalog;
    // Lazy definitions have stable addresses and fixed cache caps. Spawn rows
    // remain on disk and are fetched only for active regions.
    mutable std::map<uint32_t, LocalItemDefinition> itemCache;
    mutable std::map<uint32_t, LocalSpellDefinition> spellCache;
    mutable std::map<uint32_t, LocalQuestDefinition> questCache;
    mutable std::map<uint32_t, LocalNpcDefinition> npcCache;
    mutable std::map<uint32_t, std::vector<uint32_t>> npcQuestCache;
    mutable std::string catalogError;
    const LocalVehicleKit* vehicleKit(uint32_t id) const;
    const LocalEscortRoute* escortRoute(uint32_t id) const;
    const LocalWorldEventSchedule* worldEvent(uint32_t id) const;
    const LocalGameObject* gameObject(uint32_t id) const;
    const LocalGameObjectPool* gameObjectPool(uint32_t id) const;
    const LocalScriptAction* scriptAction(uint32_t id) const;
    const LocalGameObject* nearbyGameObject(const LocalRealmPlayer& player) const;
    const LocalItemDefinition* item(uint32_t id) const;
    const LocalSpellDefinition* spell(uint32_t id) const;
    const LocalRecipe* recipe(uint32_t spellId) const;
    const LocalQuestDefinition* quest(uint32_t id) const;
    const LocalNpcDefinition* npc(uint32_t id) const;
    std::vector<LocalQuestDefinition> questsForNpc(uint32_t entry) const;
};
/// Whether an active quest still needs this item (collect objective below its
/// count). Mirrors Player::HasQuestForItem for quest loot and INTERACT_COND.
bool localPlayerNeedsQuestItem(const LocalRealmPlayer& player,const LocalWorldContent& content,uint32_t itemId);
/// Base usability plus GO_FLAG_INTERACT_COND quest-loot gating.
bool localGameObjectUsable(const LocalGameObject& object,const LocalRealmPlayer& player,const LocalWorldContent& content);
const LocalVehicleAbility* localVehicleCastAbility(const LocalVehicleCast& cast,const LocalWorldContent& content);
bool validLocalVehicleCastView(const LocalVehicleCast& cast,const LocalWorldContent& content);
inline std::array<float,3> localVehicleSeatPosition(const LocalRealmNpc& vehicle,uint8_t seat) {
    std::array<float,3> result{vehicle.x,vehicle.y,vehicle.z};
    if(seat>=vehicle.vehicleSeatCount || seat>=8)return result;
    const auto& offset=vehicle.vehicleSeatOffsets[seat];const float c=std::cos(vehicle.orientation),s=std::sin(vehicle.orientation);
    result[0]+=offset[0]*c-offset[1]*s;result[1]+=offset[0]*s+offset[1]*c;result[2]+=offset[2];
    return result;
}
struct LocalTradeItem {uint32_t item=0;uint16_t count=0,sourceCount=0;uint8_t bag=0;bool operator==(const LocalTradeItem&)const=default;};
struct LocalGraveyardSite {
    uint32_t id = 0, mapId = 0, raceMask = 0, zoneId = 0; // zero mask: neutral sanctuary
    float x = 0, y = 0, z = 0, orientation = 0;
};
inline constexpr float kLocalCorpseReclaimRadius = 10.0f;
inline bool localCanReclaimCorpse(const LocalRealmPlayer& p) {
    if (!p.dead || !p.ghost || !p.corpseValid || p.mapId != p.corpseMapId ||
        p.instanceId != p.corpseInstanceId) return false;
    const float dx=p.x-p.corpseX,dy=p.y-p.corpseY,dz=p.z-p.corpseZ;
    return std::isfinite(dx+dy+dz) && dx*dx+dy*dy+dz*dz <= kLocalCorpseReclaimRadius*kLocalCorpseReclaimRadius;
}
inline void localCaptureCorpse(LocalRealmPlayer& p) {
    if (!p.dead || p.corpseValid) return;
    p.corpseValid=true;p.ghost=false;p.corpseMapId=p.mapId;p.corpseInstanceId=p.instanceId;
    p.corpseZoneId=p.zoneId;p.corpseX=p.x;p.corpseY=p.y;p.corpseZ=p.z;p.corpseOrientation=p.orientation;
}
enum class LocalAction : uint8_t {
    Attack = 1, StopAttack, CastSpell, AcceptQuest, TurnInQuest, Loot, EquipItem,
    UseItem, Respawn, Interact, EnterPortal, LeaveInstance, AbandonQuest = 13,
    UnequipItem = 14, CancelCast = 15, CompleteIntro = 16,
    // Travel. TakeFlight carries the destination node in `id`; BoardTransport
    // carries the transport's gameobject entry there.
    TakeFlight = 17, BoardTransport = 18, LeaveTransport = 19,
    // Auction house. The board lives beside the gameplay rules rather than in
    // them, so LocalRealm handles these three rather than LocalGameplay::execute.
    // BuyoutAuction and BidAuction carry the listing in `id`, and BidAuction the
    // bid in `target`; ListAuction carries the item in `id` and the count in
    // `target`.
    BuyoutAuction = 20, BidAuction = 21, ListAuction = 22,
    // Merchants, repair and training. Merchant commands name the selected NPC
    // and the authority validates that exact nearby service; legacy zero-GUID
    // callers resolve the nearby service. A client cannot trade on the
    // far side of the world or buy training from something that is not a
    // trainer. `id` names the item, spell or skill; `target` carries the stack
    // size for the two merchant actions and nothing for the rest.
    SellToVendor = 23, BuyFromVendor = 24, RepairEquipment = 25,
    LearnSpell = 26, LearnProfession = 27, TrainProfessionRank = 28,
    // Innkeepers. SetHome binds to the innkeeper the player is standing at;
    // ReturnHome works anywhere, on a cooldown.
    SetHome = 29, ReturnHome = 30,
    // Trade skills. LearnRecipe is bought from the profession trainer the
    // player is standing at and carries the recipe in `id`; CraftItem needs no
    // trainer and carries the recipe the character already knows.
    LearnRecipe = 31, CraftItem = 32, CancelAuction = 33, Dismount = 34, BuybackItem = 35,
    BankDeposit = 36, BankWithdraw = 37, UnlearnProfession = 38, BankMove = 39,
    PartyInvite = 40, PartyAccept = 41, PartyDecline = 42, PartyLeave = 43, PartyRemove = 44, PartyPromote = 45,
    BankDepositSlot = 46, // bag slot -> bank slot; same snapshot fields as BankMove.
    ReadyStart=47, ReadyAnswer=48, TradeRequest=49, TradeOpen=50, TradeOffer=51,
    TradeMoney=52, TradeAccept=53, TradeUnaccept=54, TradeCancel=55,
    MailSend=56, MailTakeMoney=57, MailTakeItem=58, MailReturn=59, MailDelete=60, MailRead=61,
    BackpackMove=62, BankWithdrawSlot=63, BankDepositFromSlot=64,
    TrainRiding=65, DiscoverTaxi=66, LearnTalent=67, ResetTalents=68, CancelStatAura=69, CancelForm=70,
    // P03/D1: the owner retires their own summon. Acquisition remains a real
    // cast; P07 still owns stables and the rest of the lifecycle.
    DismissPet=71,
    // P07 : CMSG_PET_ACTION. `target` names the pet and `id` carries the
    // packed action word the client already builds - `(type << 24) | action`,
    // include/game/pet_action.hpp - so the authority and the connected-server
    // path speak one encoding. Commands 0-3 and reactions 0-2 are accepted and
    // everything else is refused, which is HandlePetActionHelper's `default:`.
    PetAction=72,
    ReclaimCorpse=74,
    PetSpellAutocast=73, // Pet GUID, spell id and explicit bid 0/1.
    EnterVehicle=75, ExitVehicle=76, UseGameObject=77, SwitchVehicleSeat=78, VehicleAbility=79, VehicleAim=80,
    // 2.40 (LAN109): a gossip option chosen at the creature in `target` (`bid`
    // the menu id, `id` the option id); a text emote (`id` the TextEmotes.dbc
    // id) performed at the creature in `target` (SmartAI's RECEIVE_EMOTE).
    GossipSelect=81, TextEmote=82,
};
/// The highest action a client may send. Anything above it is rejected at the
/// wire rather than reaching the rules, so adding an action here is a
/// deliberate act and a forgotten one is inert instead of dangerous.
inline constexpr LocalAction kLocalActionMax = LocalAction::TextEmote;
struct LocalRealmCommand { LocalAction action = LocalAction::StopAttack; uint64_t target = 0; uint32_t id = 0; uint32_t bid = 0, buyout = 0, durationMinutes = 0; uint64_t serviceNpcGuid = 0; uint16_t auctionCount = 1; uint16_t bankSourceCount = 0, bankDestinationCount = 0;
    float vehicleAimYaw=0,vehicleAimPitch=0;
    std::string mailRecipient,mailSubject,mailBody;
    std::vector<LocalTradeItem> mailAttachments;
};

// Deliberately bounded local simulation. This is a standalone ruleset, not an
// AzerothCore replacement claiming complete retail scripts or WoW formulas.
struct LocalParty;
struct LocalRealmPet;
class LocalGameplay {
public:
    static constexpr size_t MaxInventory = 24, MaxQuests = 32, MaxNpcs = 128, MaxInstances = 128;
    // Deferred kill facts stay bounded. A full queue rejects a lethal hit
    // before the NPC death is published, so credit can never be discarded.
    static constexpr size_t MaxPendingScriptKills = 256;
    // Authority-owned, transient single-target healing effects. Recasts replace
    // an existing rank rather than consuming another slot.
    static constexpr size_t MaxPeriodicHeals = 256, MaxPeriodicHealsPerTarget = 8;
    // A spellbook and a recipe book, per character.
    //
    // Owner progress uses bounded multipart snapshots. These caps also bound
    // save parsing, memory use and reassembly; see local_realm.cpp's packet budget.
    static constexpr size_t MaxSpells = 192, MaxRecipes = 1024;
    // Cooldowns are bounded separately because only the handful of abilities
    // actually cooling down are ever present; sizing this with the spellbook
    // would have spent 384 bytes a packet on entries that are never sent.
    static constexpr size_t MaxCooldowns = 16;
    // Two primaries and the secondaries, plus headroom for a client whose
    // SkillLine.dbc lists more secondary skills than 3.3.5a's three.
    static constexpr size_t MaxProfessions = 8;
    /// How far a merchant, trainer or innkeeper reaches. The same eight yards
    /// every other conversation in this realm uses.
    static constexpr float ServiceRange = 8.0f;
    /// Seconds between uses of the innkeeper's return. Retail's hearthstone is
    /// thirty minutes in 3.3.5a; this is the same rule with the same purpose -
    /// a way home that cannot replace travelling.
    static constexpr float HearthCooldownSeconds = 1800.0f;
    // Independent corruption/transport bound, not an active-log restriction.
    static constexpr size_t MaxCompletedQuests = 65536;
    LocalGameplay();
    ~LocalGameplay();
    LocalGameplay(LocalGameplay&&) noexcept;
    LocalGameplay& operator=(LocalGameplay&&) noexcept;
    // Authority persistence integration for single-target aura replacement.
    // Queried only on transfer/reset, never used as a combat actor roster.
    void setAuraOwnerProvider(std::function<std::vector<LocalRealmPlayer*>()> provider);
    bool loadContent(const std::string& path, std::string& error);
    const std::vector<LocalGameObjectState>& gameObjectStates() const;
    const LocalGameObjectState* gameObjectState(uint32_t id) const;
    bool validateGameObjectStates(const std::vector<LocalGameObjectState>& states) const;
    bool restoreGameObjectStates(const std::vector<LocalGameObjectState>& states);
    const LocalGameObject* nearbyGameObject(const LocalRealmPlayer& player) const;
    /// Deterministic loot/pool rolls for tests; the authority seeds from time.
    void seedGameObjectRandom(uint32_t seed);
    bool loadCatalog(const std::string& directory, std::string& error);
    /// P05 line of sight . Optional: a realm with no collision pack
    /// installed answers every line-of-sight test with "visible", which is how
    /// every build up to the implementation behaved and how this one ships. A pack that does
    /// load folds its fingerprint into the content fingerprint, because two
    /// peers whose packs differ would disagree about which casts land.
    bool loadCollision(const std::string& directory, std::string& error);
    const LocalCollisionData* collision() const;
    /// The suite and the extractor author tiles rather than reading them.
    void adoptCollisionTile(LocalCollisionTile tile);
    bool setStarterSpells(const std::vector<LocalSpellDefinition>& spells, const std::string& diagnostic, std::string& error);
    bool setAreaTriggers(const std::vector<LocalAreaTriggerVolume>& volumes, std::string& error);
    const std::vector<LocalAreaTriggerVolume>& areaTriggers() const;
    bool setGraveyards(const std::vector<LocalGraveyardSite>& sites, std::string& error);
    const std::vector<LocalGraveyardSite>& graveyards() const;
    bool setFactionTemplates(const std::vector<LocalFactionTemplate>& rows,
                             const std::array<uint32_t, 12>& raceTemplates, std::string& error);
    bool setFactionReputationBases(const std::vector<LocalFactionReputationBase>& rows, std::string& error);
    const std::vector<LocalFactionReputationBase>& factionReputationBases() const;
    const std::vector<LocalFactionTemplate>& factionTemplates() const;
    const std::array<uint32_t, 12>& raceFactionTemplates() const;
    // Shared attackability/aggression query for presentation; resolves the
    // same NPC and faction templates once rather than three times.
    struct NpcDisposition { bool attackable = false; bool aggressive = false; };
    NpcDisposition npcDisposition(const LocalRealmPlayer& player, const LocalRealmNpc& npc) const;
    bool npcVisibleTo(const LocalRealmPlayer& player, const LocalRealmNpc& npc) const;
    bool canAttack(const LocalRealmPlayer& player, const LocalRealmNpc& npc) const;
    bool isAggressive(const LocalRealmPlayer& player, const LocalRealmNpc& npc) const;
    bool insidePortal(uint32_t portalId, const LocalRealmPlayer& player) const;
    std::vector<LocalRealmPortal> portals() const;

    // --- Instances ----------------------------------------------------------
    /// Install the client's Map.dbc rows. Without them the realm falls back to
    /// the catalog's own instance flag exactly as it did before, so this is
    /// additive: it can only make more of the client's dungeons reachable.
    bool setClientMaps(std::vector<LocalMapDefinition> maps, std::string& error);
    const std::vector<LocalMapDefinition>& clientMaps() const;
    const LocalMapDefinition* clientMap(uint32_t mapId) const;
    /// Whether this map holds private/shared instances, from either source.
    bool instanceMap(uint32_t mapId) const;

    // --- Services -----------------------------------------------------------
    /// Install the client's SkillLine.dbc rows. Only professions and secondary
    /// skills are kept; everything else a trainer could theoretically teach is
    /// a class ability, a weapon skill or a language, none of which this realm
    /// models. Empty rows leave the built-in fourteen in place.
    bool setSkillLines(const std::vector<LocalSkillLine>& lines, std::string& error);
    const std::vector<LocalSkillLine>& skillLines() const;
    /// QuestFactionReward.dbc rows 1 (gains) and 2 (losses), columns 1..10.
    /// Values are whole reputation points; quest_template overrides remain in
    /// hundredths and win when present.
    bool setQuestFactionRewards(const std::array<int32_t,10>& gains,
                                const std::array<int32_t,10>& losses);
    /// The qualifying NPC of a kind the player is standing at, or nullptr.
    const LocalRealmNpc* serviceNpc(const LocalRealmPlayer& player, uint32_t npcFlag, uint64_t npcGuid = 0) const;
    /// What the merchant the player is standing at sells. Empty away from one.
    std::vector<uint32_t> vendorStock(const LocalRealmPlayer& player, uint64_t npcGuid = 0) const;
    int32_t vendorRemaining(const LocalRealmPlayer& player, uint32_t itemId, uint64_t npcGuid = 0) const;
    // Financial commands can roll back stock along with bags/gold if the
    // atomic character save fails.
    LocalVendorInventory vendorInventorySnapshot() const;
    void restoreVendorInventory(LocalVendorInventory snapshot);
    std::vector<LocalVendorStockRecord> savedVendorStock() const;
    bool restoreVendorStock(const std::vector<LocalVendorStockRecord>& records);
    /// Abilities the class trainer the player is standing at can teach them.
    std::vector<uint32_t> trainableSpells(const LocalRealmPlayer& player, uint64_t npcGuid = 0) const;
    /// Install the trade-skill recipes read from the client's own
    /// SkillLineAbility.dbc and Spell.dbc. Without them no profession can be
    /// practised and the trainer says so, rather than inventing recipes.
    bool setRecipes(std::vector<LocalRecipe> recipes, std::string& error);
    /// Recipes the profession trainer the player is standing at can teach them
    /// now: their skill line, within their current skill, not already known.
    std::vector<uint32_t> trainableRecipes(const LocalRealmPlayer& player, uint64_t npcGuid = 0) const;
    /// Recipes the character knows and currently has the reagents for.
    std::vector<uint32_t> craftableRecipes(const LocalRealmPlayer& player) const;

    // --- Travel -------------------------------------------------------------
    // Client taxi data, supplied by the application after the MPQs are open.
    // Without it flights and transports simply do not exist; nothing else in
    // the simulation depends on them.
    bool setTravelNetwork(std::vector<LocalTaxiNode> nodes,
                          std::vector<LocalTaxiPath> paths,
                          std::vector<LocalTaxiWaypoint> waypoints,
                          std::string& error);
    /// Adopt an already-parsed network. Starting a realm builds a fresh
    /// gameplay object, and the client's taxi rows are read once before that
    /// happens; this is how they survive it without being re-parsed.
    void useTravelNetwork(const LocalTravelNetwork& network);
    const LocalTravelNetwork& travel() const;
    double transportTime() const;
    void setTransportTime(double seconds);
    void advanceTransportTime(double seconds);
    // Nodes this player can currently fly to from the flight master they are
    // talking to, already filtered by what they have discovered.
    std::vector<uint32_t> flightDestinations(const LocalRealmPlayer& player,
                                             uint32_t fromNode) const;
    // Where every transport is, as of the last tick.
    const std::vector<LocalTransportState>& transports() const;
    // Mark a node discovered. Returns true when it was new.
    static bool discoverTaxiNode(LocalRealmPlayer& player, uint32_t nodeId);
    const std::vector<LocalInstanceState>& instances() const;
    bool restoreInstances(const std::vector<LocalInstanceState>& instances, std::string& error);
    static bool validCharacterOptions(uint8_t race, uint8_t classId, uint8_t gender);
    const LocalWorldContent& content() const;
    void refreshInventoryObjectives(LocalRealmPlayer& player);
    bool refreshInventoryObjectives(LocalRealmPlayer& player,
                                    const std::vector<LocalRealmPlayer*>& authorityPlayers,
                                    std::string& error);
    void useContent(std::shared_ptr<LocalWorldContent> content);
    std::shared_ptr<LocalWorldContent> sharedContent() const;
    bool validatePlayer(const LocalRealmPlayer& player, std::string& error) const;
    /// Bring a player to the state the shipped rules derive for it.
    ///
    /// `forcedLevel` (0 = the catalog's own starting level) applies only to a
    /// fresh character, and applies BEFORE the spellbook, the resource type and
    /// the health/mana pools are derived, because all three are read off
    /// `player.level`. Writing a level over a finished level-1 character
    /// instead would leave every one of them stale.
    void initializePlayer(LocalRealmPlayer& player, bool fresh, uint8_t forcedLevel = 0);
    bool execute(LocalRealmPlayer& player, const LocalRealmCommand& command,
                 const std::vector<LocalRealmPlayer*>& players, std::string& result);
    bool moveVehicle(LocalRealmPlayer& player, uint32_t mapId, float x, float y, float z,
                     float orientation, uint8_t movement);
    /// Forced detach always releases the seat, even if an exit script fails.
    bool detachVehicle(LocalRealmPlayer& player);
    bool tick(float seconds, const std::vector<LocalRealmPlayer*>& players);
    // Atomically install the authenticated session roster (never saved/client supplied).
    bool setPartyMembership(const std::vector<LocalParty>& parties);
    // Restore the consumed corpse reservation after a failed atomic realm save.
    void restoreLootable(uint64_t guid, bool lootable);
    const std::vector<LocalVehicleProjectile>& vehicleProjectiles() const;
    void setRemoteVehicleProjectiles(std::vector<LocalVehicleProjectile> shots);
    const std::vector<LocalVehicleCast>& vehicleCasts() const;
    void setRemoteVehicleCasts(std::vector<LocalVehicleCast> casts);
    const std::vector<LocalRealmNpc>& npcs() const;
    void setRemoteNpcs(std::vector<LocalRealmNpc> npcs);
    // Owned creatures. Authority state; a guest receives them like NPCs and
    // never creates one, and a save restores only what validLocalPets accepts.
    const std::vector<LocalRealmPet>& pets() const;
    void setRemotePets(std::vector<LocalRealmPet> pets);
    bool restorePets(std::vector<LocalRealmPet> pets, std::string& error);
    const LocalRealmPet* controlledPet(uint64_t ownerGuid) const;
    // Recent authority health changes. Observation only; not saved or replicated.
    std::vector<LocalCombatEvent> combatEvents() const;
    uint64_t overwrittenCombatEvents() const;
    // Applies the complete immutable action list to one staged copy of the
    // world. No actor or dialogue is visible unless every action preflights.
    bool executeScriptActions(const std::vector<uint32_t>& actionIds,
                              const std::vector<LocalRealmPlayer*>& scope,
                              std::string& error);
    bool executeScriptActions(const std::vector<uint32_t>& actionIds,
                              const std::vector<LocalRealmPlayer*>& viewers,
                              const std::vector<LocalRealmPlayer*>& authorityPlayers,
                              std::string& error);
    LocalScriptActionCheckpoint scriptActionCheckpoint() const;
    void restoreScriptActionCheckpoint(LocalScriptActionCheckpoint checkpoint);
    static bool validPendingScriptKills(const std::vector<LocalPendingScriptKill>& kills);
    const std::vector<LocalPendingScriptKill>& pendingScriptKills() const;
    bool restorePendingScriptKills(std::vector<LocalPendingScriptKill> kills);
    const std::vector<LocalScriptDialogue>& scriptDialogues() const;
    // 2.40 gossip: the texts and menus of the loaded content / catalog for the
    // dialogue page; a guest fills the option texts of a received page from
    // its own catalog; the realm reports the active game events for the
    // ACTIVE_EVENT gossip conditions.
    bool gossipTextFor(uint32_t textId, LocalGossipText& out) const;
    bool gossipMenuFor(uint32_t menuId, LocalGossipMenu& out) const;
    void resolveGossipOptions(LocalGossipState& state) const;
    void setActiveWorldEvents(std::vector<uint32_t> ids);
    bool setRemoteScriptDialogues(std::vector<LocalScriptDialogue> dialogues);
    uint64_t overwrittenScriptDialogues() const;
private:
    // Run one player command while the caller still owns any combat-stack
    // references. The public execute() wrapper drains resulting immutable kill
    // facts only after this implementation has completely unwound.
    bool executeUnsettled(LocalRealmPlayer& player, const LocalRealmCommand& command,
        const std::vector<LocalRealmPlayer*>& players, std::string& result);
    // Apply queued XP, objective credit and authored kill actions at a point
    // where no combat iterator retains an NPC reference. Returns whether any
    // queued fact committed; refused facts remain queued for a later boundary.
    bool settlePendingScriptKills(const std::vector<LocalRealmPlayer*>& players,
        const std::set<uint64_t>* playerFilter=nullptr);
    bool detachVehicleScoped(LocalRealmPlayer& player,const std::vector<LocalRealmPlayer*>& players);
    bool executeScriptActionsScoped(const std::vector<uint32_t>& actionIds,
        const std::vector<LocalRealmPlayer*>& viewers,
        const std::vector<LocalRealmPlayer*>& authorityPlayers,std::string& error);
    bool executeVehicleAbility(LocalRealmPlayer& player,const LocalRealmCommand& command,
        const std::vector<LocalRealmPlayer*>& players,std::string& result,
        bool finishing=false,const LocalVehicleCast* pending=nullptr);
    bool canDamageVehicleArea(const LocalRealmNpc& primary,const LocalRealmNpc& hull,LocalRealmPlayer& owner,
        uint32_t raw,uint8_t schoolMask,float centerX,float centerY,float centerZ,
        float radius,const std::vector<LocalRealmPlayer*>& players);
    bool damageVehicleArea(LocalRealmNpc& primary,LocalRealmNpc& hull,LocalRealmPlayer& owner,
        uint32_t raw,uint32_t spell,uint8_t schoolMask,float centerX,float centerY,float centerZ,
        float radius,const std::vector<LocalRealmPlayer*>& players);
    bool executeCastSpell(LocalRealmPlayer& player, const LocalRealmCommand& command,
        const std::vector<LocalRealmPlayer*>& players, std::string& result, bool finishing);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wowee::game
