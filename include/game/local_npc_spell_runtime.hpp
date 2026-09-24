#pragma once
#include "game/local_gameplay.hpp"
#include <cmath>

namespace wowee::game {
// Transient authority state. Every encounter reset also discards a prepared
// hostile spell; a replacement target never inherits another player's cast.
inline void localResetNpcSpellState(LocalRealmNpc& n) {
    n.npcCastingSpellId=0;n.npcCastRemainingMs=0;n.npcCastTargetGuid=0;
    n.npcSpellTimerMs=0;n.npcSpellTimerInitialized=false;n.npcSpellLaunched=false;
    n.npcSpellReflected=false;n.npcSpellReflectReturn=false;n.npcSpellMissed=false;n.npcSpellReturnMs=0;
    n.npcSpellExtraTimers.clear();n.npcSpellDoneMask=0;n.npcCastManaCost=0;
    n.npcNextSwingSpellId=0;n.npcNextSwingTargetGuid=0;n.npcSpellMeleeOutcome=0;
    n.npcChanneling=false;n.npcChannelRemainingMs=0;n.smartPhase=0;n.smartInvoker=0;
    n.smartListId=0;n.smartListIndex=0;n.smartListTimer=0;n.smartListTimerType=0;
    n.smartEvadeRequested=false;n.smartCallForHelpRange=0;n.smartCallForHelpEmote=false;
}
inline void localCancelNpcSpellCast(LocalRealmNpc& n) {
    n.npcCastingSpellId=0;n.npcCastRemainingMs=0;n.npcCastTargetGuid=0;n.npcSpellLaunched=false;
    n.npcSpellReflected=false;n.npcSpellReflectReturn=false;n.npcSpellMissed=false;n.npcSpellReturnMs=0;n.npcCastManaCost=0;
    n.npcSpellMeleeOutcome=0;n.npcChanneling=false;n.npcChannelRemainingMs=0;
}
/// Spell::CheckRange for a creature caster. `extraMinRange` is what the
/// minimum grows by (the melee range for ranged entries, otherwise both combat
/// reaches) and `extraMaxRange` the combat reaches IsWithinCombatRange adds to
/// the maximum. 0 = in range, 1 = too far, 2 = too close, 3 = no valid target.
inline int localNpcSpellRangeStatus(const LocalRealmNpc& n,const LocalRealmPlayer& p,
                                    const LocalSpellDefinition& d,float extraMinRange=0,float extraMaxRange=0) {
    const float minRange=d.minRange+extraMinRange,maxRange=d.range+extraMaxRange;
    if(n.dead||!n.health||n.transportEntry||p.dead||!p.health||p.flight.active||
       n.mapId!=p.mapId||n.instanceId!=p.instanceId||!std::isfinite(maxRange)||
       !std::isfinite(minRange)||d.range<=0||minRange<0||minRange>maxRange)return 3;
    const auto dx=p.x-n.x,dy=p.y-n.y,dz=p.z-n.z;
    const auto squared=dx*dx+dy*dy+dz*dz;
    if(!std::isfinite(squared))return 3;
    if(squared>maxRange*maxRange)return 1;
    if(squared<minRange*minRange)return 2;
    return 0;
}
inline bool localNpcSpellTargetInRange(const LocalRealmNpc& n,const LocalRealmPlayer& p,
                                       const LocalSpellDefinition& d,float extraMinRange=0,float extraMaxRange=0) {
    return localNpcSpellRangeStatus(n,p,d,extraMinRange,extraMaxRange)==0;
}
inline LocalCombatEvent localNpcSpellPhase(const LocalRealmNpc& n,const LocalSpellDefinition& d,
                                          LocalCombatEventKind kind,uint64_t target=0,
                                          LocalMeleeOutcome outcome=LocalMeleeOutcome::Hit) {
    LocalCombatEvent event{0,n.guid,target,d.id,n.mapId,n.instanceId,0,0,0,kind};
    event.attackType=LocalCombatAttackType::Magic;event.spellTypeMask=7;
    event.schoolMask=d.schoolMask;event.spellFamily=d.spellFamily;event.spellFamilyFlags=d.spellFamilyFlags;
    event.sourceRawCastTimeMs=d.sourceRawCastTimeMs;event.outcome=outcome;
    event.actorLevel=n.level;event.actorIsPlayer=false;
    return event;
}
} // namespace wowee::game
