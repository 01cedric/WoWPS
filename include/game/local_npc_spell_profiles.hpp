#pragma once
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <cmath>
#include <utility>
#include <climits>

namespace wowee::game {
// Whole-owner SmartAI scripts generated from the pinned source by
// tools/local_realm/generate_npc_spell_profiles.py: one row per smart_scripts
// row the runtime reproduces (the creature-talk companion keeps its own rows).
// A script owner is a creature entry or, for a spawn whose guid script replaces
// the entry script, the negated spawn guid. Timed action lists (source_type 9)
// are a second table keyed by list id.
inline constexpr uint32_t kLocalSmartCastInterruptPrevious=1, kLocalSmartCastTriggered=2, kLocalSmartCastAuraNotPresent=32,
    kLocalSmartCastCombatMove=64, kLocalSmartCastMainSpell=0x400;
inline constexpr uint32_t kLocalSmartFlagNotRepeatable=1, kLocalSmartFlagDontReset=0x100;
// SMART_EVENT ids.
inline constexpr uint32_t kLocalSmartEventUpdateIc=0, kLocalSmartEventUpdateOoc=1, kLocalSmartEventHealthPct=2, kLocalSmartEventManaPct=3,
    kLocalSmartEventAggro=4, kLocalSmartEventKill=5, kLocalSmartEventDeath=6, kLocalSmartEventEvade=7, kLocalSmartEventSpellHit=8,
    kLocalSmartEventRange=9, kLocalSmartEventOocLos=10, kLocalSmartEventRespawn=11, kLocalSmartEventTargetHealthPct=12,
    kLocalSmartEventVictimCasting=13, kLocalSmartEventFriendlyHealth=14, kLocalSmartEventFriendlyMissingBuff=16,
    kLocalSmartEventTargetManaPct=18, kLocalSmartEventAcceptedQuest=19, kLocalSmartEventRewardQuest=20, kLocalSmartEventReachedHome=21,
    kLocalSmartEventHasAura=23, kLocalSmartEventTargetBuffed=24, kLocalSmartEventReset=25, kLocalSmartEventIcLos=26,
    kLocalSmartEventSpellHitTarget=31, kLocalSmartEventDamaged=32, kLocalSmartEventDamagedTarget=33, kLocalSmartEventCorpseRemoved=36,
    kLocalSmartEventAiInit=37, kLocalSmartEventDataSet=38, kLocalSmartEventReceiveHeal=53, kLocalSmartEventTimedEventTriggered=59,
    kLocalSmartEventUpdate=60, kLocalSmartEventLink=61, kLocalSmartEventJustCreated=63, kLocalSmartEventEventPhaseChange=66,
    kLocalSmartEventIsBehindTarget=67, kLocalSmartEventFriendlyHealthPct=74, kLocalSmartEventCounterSet=77,
    kLocalSmartEventNearPlayers=101, kLocalSmartEventNearPlayersNegation=102, kLocalSmartEventAreaCasting=105, kLocalSmartEventAreaRange=106,
    // 2.38: summons, escort paths, point movement, the text timer, follow.
    kLocalSmartEventSummonedUnit=17, kLocalSmartEventMovementInform=34, kLocalSmartEventSummonDespawned=35, kLocalSmartEventEscortStart=39,
    kLocalSmartEventEscortReached=40, kLocalSmartEventTextOver=52, kLocalSmartEventJustSummoned=54, kLocalSmartEventEscortPaused=55,
    kLocalSmartEventEscortResumed=56, kLocalSmartEventEscortStopped=57, kLocalSmartEventEscortEnded=58, kLocalSmartEventFollowCompleted=65,
    kLocalSmartEventSummonedUnitDies=82, kLocalSmartEventSummonedUnitEvade=107,
    // 2.39: waypoint_data patrols, DO_ACTION, emotes, vehicle passengers, game events, distances.
    kLocalSmartEventWaypointReached=108, kLocalSmartEventWaypointEnded=109, kLocalSmartEventActionDone=72, kLocalSmartEventReceiveEmote=22,
    kLocalSmartEventPassengerBoarded=27, kLocalSmartEventPassengerRemoved=28, kLocalSmartEventGameEventStart=68, kLocalSmartEventGameEventEnd=69,
    kLocalSmartEventDistanceCreature=75, kLocalSmartEventFriendlyIsCc=15;
// SMARTAI_TARGETS ids.
inline constexpr uint32_t kLocalSmartTargetNone=0, kLocalSmartTargetSelf=1, kLocalSmartTargetVictim=2, kLocalSmartTargetHostileSecond=3,
    kLocalSmartTargetHostileLast=4, kLocalSmartTargetHostileRandom=5, kLocalSmartTargetHostileRandomNotTop=6, kLocalSmartTargetInvoker=7,
    kLocalSmartTargetCreatureRange=9, kLocalSmartTargetCreatureGuid=10, kLocalSmartTargetCreatureDistance=11, kLocalSmartTargetStored=12,
    kLocalSmartTargetInvokerParty=16, kLocalSmartTargetPlayerRange=17, kLocalSmartTargetPlayerDistance=18, kLocalSmartTargetClosestCreature=19,
    kLocalSmartTargetClosestPlayer=21, kLocalSmartTargetThreatList=24, kLocalSmartTargetClosestEnemy=25, kLocalSmartTargetClosestFriendly=26,
    kLocalSmartTargetLootRecipients=27, kLocalSmartTargetFarthest=28, kLocalSmartTargetPlayerWithAura=201,
    kLocalSmartTargetPosition=8, kLocalSmartTargetOwnerOrSummoner=23, kLocalSmartTargetRandomPoint=202, kLocalSmartTargetSummonedCreatures=204;
// SMART_ACTION ids.
inline constexpr uint32_t kLocalSmartActionTalk=1, kLocalSmartActionSetFaction=2, kLocalSmartActionSound=4, kLocalSmartActionPlayEmote=5,
    kLocalSmartActionSetReactState=8, kLocalSmartActionRandomEmote=10, kLocalSmartActionCast=11, kLocalSmartActionThreatSinglePct=13,
    kLocalSmartActionThreatAllPct=14, kLocalSmartActionSetEmoteState=17, kLocalSmartActionSetUnitFlag=18, kLocalSmartActionRemoveUnitFlag=19,
    kLocalSmartActionAutoAttack=20, kLocalSmartActionCombatMove=21, kLocalSmartActionSetPhase=22, kLocalSmartActionIncPhase=23,
    kLocalSmartActionEvade=24, kLocalSmartActionFlee=25, kLocalSmartActionCombatStop=27, kLocalSmartActionRemoveAuras=28,
    kLocalSmartActionRandomPhase=30, kLocalSmartActionRandomPhaseRange=31, kLocalSmartActionCallKilledMonster=33, kLocalSmartActionDie=37,
    kLocalSmartActionCallForHelp=39, kLocalSmartActionSetSheath=40, kLocalSmartActionForceDespawn=41, kLocalSmartActionSetInvincibilityHp=42,
    kLocalSmartActionSetData=45, kLocalSmartActionSetActive=48, kLocalSmartActionAttackStart=49, kLocalSmartActionKillUnit=51,
    kLocalSmartActionSetRun=59, kLocalSmartActionSetCounter=63, kLocalSmartActionStoreTargetList=64, kLocalSmartActionSetOrientation=66,
    kLocalSmartActionCreateTimedEvent=67, kLocalSmartActionTriggerTimedEvent=73, kLocalSmartActionRemoveTimedEvent=74, kLocalSmartActionAddAura=75,
    kLocalSmartActionCallScriptReset=78, kLocalSmartActionSetRangedMovement=79, kLocalSmartActionTimedList=80, kLocalSmartActionSelfCast=85,
    kLocalSmartActionSetUnitBytes1=90, kLocalSmartActionRemoveUnitBytes1=91, kLocalSmartActionInterrupt=92, kLocalSmartActionSetDynamicFlag=94,
    kLocalSmartActionAddDynamicFlag=95, kLocalSmartActionRemoveDynamicFlag=96, kLocalSmartActionSetHealthRegen=102, kLocalSmartActionSetPower=108,
    kLocalSmartActionAddPower=109, kLocalSmartActionRemovePower=110, kLocalSmartActionRandomSound=115, kLocalSmartActionSetCorpseDelay=116,
    kLocalSmartActionDisableEvade=117, kLocalSmartActionRemoveAurasByType=120, kLocalSmartActionSetSightDist=121, kLocalSmartActionAddThreat=123,
    kLocalSmartActionTriggerRandomTimedEvent=125, kLocalSmartActionSetHealthPct=142, kLocalSmartActionSetCombatDistance=205,
    kLocalSmartActionAddImmunity=208, kLocalSmartActionRemoveImmunity=209, kLocalSmartActionSetEventFlagReset=211, kLocalSmartActionAttackStop=224,
    kLocalSmartActionSetScale=227, kLocalSmartActionPlaySpellVisual=229,
    // 2.38
    kLocalSmartActionSummonCreature=12, kLocalSmartActionFollow=29, kLocalSmartActionSetInCombatWithZone=38, kLocalSmartActionMount=43,
    kLocalSmartActionMoveForward=46, kLocalSmartActionSetVisibility=47, kLocalSmartActionEscortStart=53, kLocalSmartActionEscortPause=54,
    kLocalSmartActionEscortStop=55, kLocalSmartActionSetFly=60, kLocalSmartActionSetSwim=61, kLocalSmartActionEscortResume=65,
    kLocalSmartActionMoveToPos=69, kLocalSmartActionEquip=71, kLocalSmartActionCallRandomTimedList=87, kLocalSmartActionCallRandomRangeTimedList=88,
    kLocalSmartActionRandomMove=89, kLocalSmartActionJumpToPos=97, kLocalSmartActionSetHomePos=101, kLocalSmartActionSetRoot=103,
    kLocalSmartActionSetMovementFlags=204, kLocalSmartActionSetHover=207,
    // 2.39
    kLocalSmartActionWaypointStart=232, kLocalSmartActionWaypointDataRandom=233, kLocalSmartActionMovementStop=234,
    kLocalSmartActionMovementPause=235, kLocalSmartActionMovementResume=236, kLocalSmartActionDoAction=223, kLocalSmartActionSetInstData=34,
    kLocalSmartActionSetInstData64=35, kLocalSmartActionSetNpcFlag=81, kLocalSmartActionAddNpcFlag=82, kLocalSmartActionRemoveNpcFlag=83,
    kLocalSmartActionCrossCast=86, kLocalSmartActionSendTargetToTarget=100, kLocalSmartActionSetMovementSpeed=136, kLocalSmartActionStopMotion=212,
    kLocalSmartActionMoveToPosTarget=201;
// 2.40: the gossip family, the quest actions and the emote event.
inline constexpr uint32_t kLocalSmartEventGossipSelect=62,kLocalSmartEventGossipHello=64;
inline constexpr uint32_t kLocalSmartActionCloseGossip=72,kLocalSmartActionSendGossipMenu=98,kLocalSmartActionSetGossipMenu=240,
    kLocalSmartActionFailQuest=6,kLocalSmartActionOfferQuest=7,kLocalSmartActionAddItem=56,kLocalSmartActionRemoveItem=57;
// TempSummonType (Object.h): the eight the generator admits.
inline constexpr uint8_t kLocalSummonTimedOrDead=1, kLocalSummonTimedOrCorpse=2, kLocalSummonTimed=3, kLocalSummonTimedOutOfCombat=4,
    kLocalSummonCorpse=5, kLocalSummonCorpseTimed=6, kLocalSummonDead=7, kLocalSummonManual=8;
// 2.38: destination codes of the destination-only creature effects (a summon,
// a persistent area aura): Spell::SelectImplicitCasterDestTargets /
// TargetDestTargets / DestDestTargets. The caster itself, the summon spot
// (PET_FOLLOW_DIST in front-left), a direction from the caster at the effect
// radius, a random point within / at the radius, the explicit target's position.
inline constexpr uint8_t kLocalNpcDestCaster=1, kLocalNpcDestSummon=2, kLocalNpcDestFront=3, kLocalNpcDestBack=4, kLocalNpcDestRight=5,
    kLocalNpcDestLeft=6, kLocalNpcDestFrontRight=7, kLocalNpcDestBackRight=8, kLocalNpcDestBackLeft=9, kLocalNpcDestFrontLeft=10,
    kLocalNpcDestRandom=11, kLocalNpcDestRadius=12, kLocalNpcDestTarget=13;
// MovementGeneratorType ids MOVEMENTINFORM carries (MotionMaster.h).
inline constexpr uint32_t kLocalMotionWaypoint=2, kLocalMotionPoint=8, kLocalMotionEffect=16, kLocalMotionEscort=17;
// SmartAI's escort target list id and its point movement id.
inline constexpr uint32_t kLocalSmartEscortTargets=0xFFFFFFu, kLocalSmartRandomPoint=0xFFFFFEu;
// UNIT_FIELD_FLAGS bits the runtime honours (SET/REMOVE_UNIT_FLAG).
inline constexpr uint32_t kLocalUnitFlagNonAttackable=0x2, kLocalUnitFlagDisableMove=0x4, kLocalUnitFlagImmuneToPc=0x100,
    kLocalUnitFlagImmuneToNpc=0x200, kLocalUnitFlagSilenced=0x2000, kLocalUnitFlagPacified=0x20000, kLocalUnitFlagStunned=0x40000,
    kLocalUnitFlagInCombat=0x80000, kLocalUnitFlagNotSelectable=0x2000000;
struct LocalNpcSpellProfile {
    int64_t owner;
    uint32_t row, entry, event, p1, p2, p3, p4, p5, p6, chance, flags, phaseMask, link, action, a1, a2, a3, a4, a5, a6, target,
        t1, t2, t3, t4, unitClass, expansion, scales, spellLevel, costScales, regenMana;
    float manaModifier;
    // 2.38: the row's own coordinates (target_x..target_o): a summon or move
    // destination, an offset from a unit target, a home position, a facing.
    float x, y, z, o;
    constexpr bool casts() const { return action==kLocalSmartActionCast||action==kLocalSmartActionSelfCast; }
    /// The spell a row casts or adds as an aura (SMART_ACTION_ADD_AURA).
    constexpr uint32_t spellId() const { return casts()||action==kLocalSmartActionAddAura?a1:0; }
    constexpr uint32_t castFlags() const { return casts()?a2:0; }
    constexpr bool once() const { return (flags&kLocalSmartFlagNotRepeatable)!=0; }
    constexpr bool dontReset() const { return (flags&kLocalSmartFlagDontReset)!=0; }
};
// Both tables end with a sentinel owner no script can have, so an empty
// generated table still forms an array and every search stops before it.
inline constexpr LocalNpcSpellProfile kLocalNpcSpellProfiles[] = {
#include "game/local_npc_spell_profiles_generated.inc"
    {INT64_MAX,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0.f,0.f,0.f,0.f,0.f}
};
inline constexpr LocalNpcSpellProfile kLocalNpcSmartLists[] = {
#include "game/local_npc_spell_lists_generated.inc"
    {INT64_MAX,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0.f,0.f,0.f,0.f,0.f}
};
inline constexpr size_t kLocalMaxNpcSpellProfilesPerEntry = 24;
/// 2.38: the SmartAI escort paths (waypoints table, SmartWaypointMgr) the
/// installed ESCORT_START rows start; sorted by path then point (1-based).
struct LocalNpcWaypoint { uint32_t path, point; float x, y, z; };
inline constexpr LocalNpcWaypoint kLocalNpcWaypoints[] = {
#include "game/local_npc_waypoints_generated.inc"
    {0xffffffffu,0u,0.f,0.f,0.f}
};
using LocalNpcWaypointRange=std::pair<const LocalNpcWaypoint*,const LocalNpcWaypoint*>;
/// Every point of one path in order (empty when the path is unknown).
inline LocalNpcWaypointRange localNpcWaypointPath(uint32_t path) {
    if(!path||path==0xffffffffu)return {std::end(kLocalNpcWaypoints),std::end(kLocalNpcWaypoints)};
    const auto* begin=std::lower_bound(std::begin(kLocalNpcWaypoints),std::end(kLocalNpcWaypoints),path,
        [](const auto& w,uint32_t id){return w.path<id;});
    const auto* end=begin;
    while(end!=std::end(kLocalNpcWaypoints)&&end->path==path)++end;
    return {begin,end};
}
/// 2.39: the creature_template entries with CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ
/// (Creature::IsDamageEnoughForLootingAndReward): their deaths reward without
/// half the health dealt by players.
inline constexpr uint32_t kLocalNpcNoPlayerDamageReqEntries[] = {
#include "game/local_npc_reward_flags_generated.inc"
    0xffffffffu
};
inline bool localNpcNoPlayerDamageReq(uint32_t entry) {
    return entry&&entry!=0xffffffffu&&std::binary_search(std::begin(kLocalNpcNoPlayerDamageReqEntries),std::end(kLocalNpcNoPlayerDamageReqEntries),entry);
}
/// spell_cone degrees and spell_jump_distance yards of installed spells.
struct LocalNpcSpellGeometry { uint32_t spellId; int32_t coneDegrees; uint32_t jumpDistance; };
inline constexpr LocalNpcSpellGeometry kLocalNpcSpellGeometry[] = {
#include "game/local_npc_spell_geometry_generated.inc"
    {0u,0,0u}
};
inline const LocalNpcSpellGeometry* localNpcSpellGeometry(uint32_t spellId) {
    for(const auto& g:kLocalNpcSpellGeometry)if(g.spellId==spellId)return &g;
    return nullptr;
}
using LocalNpcSmartRange=std::pair<const LocalNpcSpellProfile*,const LocalNpcSpellProfile*>;
/// The script owner of a creature: its guid script when the spawn has one, else the entry.
inline int64_t localNpcSmartOwner(uint32_t entry,uint32_t spawnId,bool guidScripted) {
    return guidScripted&&spawnId?-int64_t(spawnId):int64_t(entry);
}
/// Every row of one script owner, in SmartAI row order.
inline LocalNpcSmartRange localNpcSmartRows(int64_t owner) {
    const auto* begin=std::lower_bound(std::begin(kLocalNpcSpellProfiles),std::end(kLocalNpcSpellProfiles),owner,
        [](const auto& profile,int64_t id){return profile.owner<id;});
    const auto* end=begin;
    while(end!=std::end(kLocalNpcSpellProfiles)&&end->owner==owner)++end;
    return {begin,end};
}
/// Every row of one creature entry's own script (older callers).
inline LocalNpcSmartRange localNpcSpellProfiles(uint32_t entry) { return localNpcSmartRows(int64_t(entry)); }
/// Every row of one timed action list, in row order.
inline LocalNpcSmartRange localNpcSmartList(uint32_t listId) {
    const auto* begin=std::lower_bound(std::begin(kLocalNpcSmartLists),std::end(kLocalNpcSmartLists),int64_t(listId),
        [](const auto& profile,int64_t id){return profile.owner<id;});
    const auto* end=begin;
    while(end!=std::end(kLocalNpcSmartLists)&&end->owner==int64_t(listId))++end;
    return {begin,end};
}
/// The first row of an owner (creature class, mana modifier, regeneration).
inline const LocalNpcSpellProfile* localNpcSpellProfile(int64_t owner) {
    const auto range=localNpcSmartRows(owner);
    return range.first!=range.second?range.first:nullptr;
}
/// The first cast row of an owner naming a spell (scaling and cost metadata);
/// a spell reached only through a list or a trigger falls back to any row of
/// the owner naming it, then to the owner's first row.
inline const LocalNpcSpellProfile* localNpcSpellRow(int64_t owner,uint32_t spellId) {
    const auto range=localNpcSmartRows(owner);
    for(const auto* r=range.first;r!=range.second;++r)if(r->spellId()==spellId)return r;
    for(const auto* r=range.first;r!=range.second;++r)if(r->action==kLocalSmartActionTimedList) {
        const auto list=localNpcSmartList(r->a1);
        for(const auto* l=list.first;l!=list.second;++l)if(l->spellId()==spellId)return l;
    }
    return nullptr;
}
// SpellEffectInfo::CalcValue: SCALES_WITH_CREATURE_LEVEL multiplies by
// creature_classlevelstats BaseDamage at the creature's level over the value
// at SpellLevel, for the creature's own class and expansion. No player SP term.
// The caller applies it only to an effect without RealPointsPerLevel.
inline float localNpcSpellDamageScaleRow(const LocalNpcSpellProfile& profile,uint32_t level,bool scales,uint32_t spellLevel) {
    if(level<1||level>83)return 0.f;
    if(!scales||spellLevel==level)return 1.f;
    static constexpr float baseDamage[4][3][84]={
#include "game/local_npc_spell_scaling_generated.inc"
    };
    const int classIndex=profile.unitClass==1?0:profile.unitClass==2?1:profile.unitClass==4?2:profile.unitClass==8?3:-1;
    if(classIndex<0||profile.expansion>2||spellLevel<1||spellLevel>83)return 0.f;
    const auto& table=baseDamage[classIndex][profile.expansion];
    return table[level]/table[spellLevel];
}
/// The scale for a spell definition cast by an owner's creature: the class and
/// expansion come from the owner's first row, SCALES_WITH_CREATURE_LEVEL and
/// SpellLevel from the spell itself (a triggered spell has no row of its own).
inline float localNpcSpellDamageScale(int64_t owner,uint32_t level,bool scales,uint32_t spellLevel) {
    const auto* row=localNpcSpellProfile(owner);
    if(!row)return 0.f;
    return localNpcSpellDamageScaleRow(*row,level,scales,spellLevel);
}
/// Creature::InitStatsForLevel: max mana = ceil(BaseMana(level,class) * ModMana).
inline uint32_t localNpcMaxMana(const LocalNpcSpellProfile& profile,uint32_t level) {
    static constexpr uint32_t baseMana[4][84]={
#include "game/local_npc_spell_mana_generated.inc"
    };
    const int classIndex=profile.unitClass==1?0:profile.unitClass==2?1:profile.unitClass==4?2:profile.unitClass==8?3:-1;
    if(classIndex<0||level<1||level>83||!(profile.manaModifier>=0.f))return 0;
    const float mana=float(baseMana[classIndex][level])*profile.manaModifier;
    return mana>0?uint32_t(std::ceil(double(mana))):0;
}
/// SpellInfo::CalcPowerCost for a creature caster: flat plus percentage of
/// its create mana, scaled by gtNPCManaCostScaler for SCALES_WITH_CREATURE_LEVEL.
inline uint32_t localNpcSpellManaCostScaled(bool costScales,uint32_t spellLevel,uint32_t level,uint32_t flat,uint32_t percent,uint32_t createMana) {
    static constexpr float scaler[100]={
#include "game/local_npc_spell_mana_scaler_generated.inc"
    };
    float cost=float(int32_t(flat)+int32_t(uint64_t(createMana)*percent/100));
    if(costScales&&spellLevel>=1&&spellLevel<=100&&level>=1&&level<=100)
        cost=float(int32_t(cost*(scaler[level-1]/scaler[spellLevel-1])));
    return cost>0?uint32_t(cost):0;
}
inline uint32_t localNpcSpellManaCost(const LocalNpcSpellProfile& profile,uint32_t level,uint32_t flat,uint32_t percent,uint32_t createMana) {
    return localNpcSpellManaCostScaled(profile.costScales!=0,profile.spellLevel,level,flat,percent,createMana);
}
}
