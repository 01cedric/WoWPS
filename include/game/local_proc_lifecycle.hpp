#pragma once
#include "game/local_combat_events.hpp"

namespace wowee::game {
// Acore::XP::GetGrayLevel, pinned 9c416aa; XP/Honor proc eligibility is
// independent of actual XP rewards, tap ownership, and the level cap.
constexpr uint8_t localProcGrayLevel(uint8_t level) {
    return level<=5?0:level<=39?level-5-level/10:level<=59?level-1-level/5:level-9;
}
inline LocalCombatEvent localKillProcEvent(uint64_t killer,uint64_t victim,uint32_t map,uint32_t instance,
        bool playerActor,uint8_t actorLevel,bool xpOrHonorEligible) {
    LocalCombatEvent event;
    event.kind=LocalCombatEventKind::Kill;event.source=killer;event.target=victim;
    event.mapId=map;event.instanceId=instance;event.killed=true;
    event.attackType=LocalCombatAttackType::None;
    event.actorIsPlayer=playerActor;event.actorLevel=actorLevel;
    event.actionTargetKnown=true;event.actionTargetHonorOrXpEligible=xpOrHonorEligible;
    // Unit::Kill deliberately drops the killing spell and aura context.
    return event;
}
inline LocalCombatEvent localDeathProcEvent(uint64_t victim,uint32_t map,uint32_t instance,
        bool playerActor,uint8_t actorLevel) {
    LocalCombatEvent event;
    event.kind=LocalCombatEventKind::Death;event.source=victim;
    event.mapId=map;event.instanceId=instance;event.killed=true;
    event.attackType=LocalCombatAttackType::None;
    event.actorIsPlayer=playerActor;event.actorLevel=actorLevel;
    return event;
}
}
