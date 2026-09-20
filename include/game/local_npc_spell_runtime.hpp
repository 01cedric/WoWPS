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
}
inline void localCancelNpcSpellCast(LocalRealmNpc& n) {
    n.npcCastingSpellId=0;n.npcCastRemainingMs=0;n.npcCastTargetGuid=0;n.npcSpellLaunched=false;
    n.npcSpellReflected=false;n.npcSpellReflectReturn=false;n.npcSpellMissed=false;n.npcSpellReturnMs=0;
}
inline bool localNpcSpellTargetInRange(const LocalRealmNpc& n,const LocalRealmPlayer& p,
                                       const LocalSpellDefinition& d) {
    if(n.dead||!n.health||n.transportEntry||p.dead||!p.health||p.flight.active||
       n.mapId!=p.mapId||n.instanceId!=p.instanceId||!std::isfinite(d.range)||
       !std::isfinite(d.minRange)||d.range<=0||d.minRange<0||d.minRange>d.range)return false;
    const auto dx=p.x-n.x,dy=p.y-n.y,dz=p.z-n.z;
    const auto squared=dx*dx+dy*dy+dz*dz;
    return std::isfinite(squared)&&squared>=d.minRange*d.minRange&&squared<=d.range*d.range;
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
