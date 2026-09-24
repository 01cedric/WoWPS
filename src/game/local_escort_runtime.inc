// Included inside LocalGameplay::Impl. Actor identity stays on the guide;
// the escorting player owns supported quest/loot/experience credit only.
LocalRealmPlayer* escortCombatOwner(const LocalRealmNpc& guide,const std::vector<LocalRealmPlayer*>& players) {
    if(!guide.escortOwner)return nullptr;
    auto* owner=player(guide.escortOwner,players);
    const auto* route=owner?content->escortRoute(owner->escort.routeId):nullptr;
    if(!owner || !route || !route->combat || guide.guid!=(NpcPrefix|route->spawnId) ||
       guide.dead || !guide.health || owner->dead || owner->ghost || !owner->health ||
       owner->flight.active || owner->transportEntry || owner->vehicleGuid || owner->instanceId ||
       owner->mapId!=guide.mapId || guide.instanceId ||
       !localPhaseVisible(owner->phaseMask,guide.requiredPhaseMask,guide.excludedPhaseMask) ||
       std::none_of(owner->quests.begin(),owner->quests.end(),[&](const auto& q){return q.id==route->questId && q.status==LocalQuestStatus::Active;}))return nullptr;
    return owner;
}
bool escortUnderAttack(const LocalRealmNpc& guide) const {
    for(const auto& enemy:npcs)if(!enemy.dead && enemy.health && enemy.mapId==guide.mapId && enemy.instanceId==guide.instanceId &&
        (enemy.targetGuid==guide.guid || std::any_of(enemy.threat.begin(),enemy.threat.end(),[&](const auto& e){return e.guid==guide.guid && e.amount;})))return true;
    return false;
}
void releaseEscortActor(LocalRealmNpc& guide,const std::vector<LocalRealmPlayer*>& players) {
    guide.escortOwner=0;guide.targetGuid=0;guide.threat={};guide.attackTimer=0;
    escortCombat.erase(guide.guid);localResetNpcSpellState(guide);
    guide.snares.clear();releaseNpcControls(guide);guide.diminishing.clear();guide.damageAuras.clear();guide.stormstrikeAuras.clear();
    // Death keeps the corpse and its normal respawn timer. A released living
    // shared guide returns home; a reconnect restores its owner's saved HP.
    if(!guide.dead){guide.combatEpoch=allocateNpcEpoch();guide.x=guide.homeX;guide.y=guide.homeY;guide.z=guide.homeZ;guide.health=guide.maxHealth;}
    for(auto& enemy:npcs)if(enemy.guid!=guide.guid) {
        bool held=enemy.targetGuid==guide.guid;
        for(auto& e:enemy.threat)if(e.guid==guide.guid){e={};held=true;}
        if(held){if(enemy.targetGuid==guide.guid)enemy.targetGuid=0;selectThreatTarget(enemy,players);}
    }
}
void failEscort(LocalRealmPlayer& owner,const std::vector<LocalRealmPlayer*>& players) {
    const auto* route=content->escortRoute(owner.escort.routeId);
    auto* guide=route?npc(NpcPrefix|route->spawnId):nullptr;
    auto staged=owner;LocalScriptActionBatch actions;
    if(route && !applyScriptTriggers(staged,*content,LocalScriptTriggerKind::EscortFail,route->id,&actions)) {
        LOG_ERROR("[LOCAL_ESCORT] failure script refused player=",owner.guid," route=",route->id);return;
    }
    std::vector<LocalRealmPlayer*> authority=players;
    for(auto*& current:authority)if(current&&current->guid==staged.guid)current=&staged;
    std::string why;
    if(!actions.empty() && !executeScriptActionsScoped(actions,{&staged},authority,why)) {
        LOG_ERROR("[LOCAL_ESCORT] failure action refused player=",owner.guid," route=",route?route->id:0," reason=",why);return;
    }
    staged.escort={};owner=std::move(staged);
    if(guide && guide->escortOwner==owner.guid)releaseEscortActor(*guide,players);
}
bool advanceEscortRoute(LocalRealmPlayer& owner,LocalRealmNpc& guide,const LocalEscortRoute& route,
                        uint32_t elapsedMs,float dt,const std::vector<LocalRealmPlayer*>& players) {
    if(distance2(owner,guide)>route.followRadius*route.followRadius || localCombatActive(owner,npcs) ||
       (route.combat && (guide.targetGuid || escortUnderAttack(guide) || localNpcStunned(guide))))return false;
    if(owner.escort.waitMs){owner.escort.waitMs-=std::min(elapsedMs,owner.escort.waitMs);return elapsedMs!=0;}
    if(owner.escort.nextPoint==route.points.size()) {
        auto staged=owner;bool scriptsOk=false;LocalScriptActionBatch actions;
        if(!applyScriptTriggers(staged,*content,LocalScriptTriggerKind::EscortComplete,route.id,&actions))return false;
        questStatus(staged,*content,true,&scriptsOk,&actions);if(!scriptsOk)return false;
        std::vector<LocalRealmPlayer*> authority=players;
        for(auto*& current:authority)if(current&&current->guid==staged.guid)current=&staged;
        std::string why;if(!actions.empty()&&!executeScriptActionsScoped(actions,{&staged},authority,why))return false;
        const auto guideGuid=guide.guid;staged.escort={};owner=std::move(staged);
        if(auto* current=npc(guideGuid))releaseEscortActor(*current,players);return true;
    }
    const auto& point=route.points[owner.escort.nextPoint];
    const float dx=point.x-guide.x,dy=point.y-guide.y,dz=point.z-guide.z;
    const float distance=std::sqrt(dx*dx+dy*dy+dz*dz),step=route.speed*dt;
    auto staged=owner;const bool arrived=distance<=step;
    if(arrived){staged.escort.x=point.x;staged.escort.y=point.y;staged.escort.z=point.z;}
    else if(step>0){staged.escort.x+=dx*step/distance;staged.escort.y+=dy*step/distance;staged.escort.z+=dz*step/distance;}
    else return false;
    if(!localEscortMotionAllowed(collision,guide.mapId,guide.x,guide.y,guide.z,staged.escort.x,staged.escort.y,staged.escort.z))return false;
    LocalScriptActionBatch actions;
    if(arrived) {
        ++staged.escort.nextPoint;staged.escort.waitMs=point.waitMs;
        if(!applyScriptTriggers(staged,*content,LocalScriptTriggerKind::EscortWaypoint,route.id,&actions))return false;
    }
    std::vector<LocalRealmPlayer*> authority=players;
    for(auto*& current:authority)if(current&&current->guid==staged.guid)current=&staged;
    std::string why;if(!actions.empty()&&!executeScriptActionsScoped(actions,{&staged},authority,why))return false;
    const auto guideGuid=guide.guid;owner=std::move(staged);auto* current=npc(guideGuid);if(!current)return false;
    if(distance>0)current->orientation=std::atan2(dy,dx);
    current->x=owner.escort.x;current->y=owner.escort.y;current->z=owner.escort.z;return true;
}
void damageEscortByNpc(LocalRealmNpc& guide,LocalRealmNpc& attacker,LocalRealmPlayer& owner,
                       const std::vector<LocalRealmPlayer*>& players) {
    const auto* attackerDef=content->npc(attacker.entry);const auto* guideDef=content->npc(guide.entry);
    if(!attackerDef || !guideDef || guide.dead || attacker.dead)return;
    const auto damage=localArmorReducedDamage(std::min(attackerDef->damage,1000000u),guideDef->armor,attacker.level);
    const auto effective=std::min(damage,guide.health);guide.health-=effective;guide.dead=!guide.health;
    if(damage)breakNpcControlsOnDamage(guide,0,nullptr);
    owner.escort.guideHealth=guide.health;
    LocalCombatEvent event{0,attacker.guid,guide.guid,0,guide.mapId,guide.instanceId,damage,effective,0,LocalCombatEventKind::NpcMelee,guide.dead};
    event.schoolMask=1;event.attackType=LocalCombatAttackType::Melee;emitCombatEvent(event,players);
    if(guide.dead) {
        guide.combatEpoch=allocateNpcEpoch();guide.lootable=false;guide.lootOwner=0;
        guide.respawnTimer=guideDef->respawnSeconds;respawnAt[guide.guid]=now+guide.respawnTimer;
        emitCombatEvent(localKillProcEvent(attacker.guid,guide.guid,guide.mapId,guide.instanceId,false,attacker.level,false),players);
        emitCombatEvent(localDeathProcEvent(guide.guid,guide.mapId,guide.instanceId,false,guide.level),players);
        failEscort(owner,players);
    }
}
void damageNpcByEscort(LocalRealmNpc& enemy,LocalRealmNpc& guide,LocalRealmPlayer& owner,
                       const std::vector<LocalRealmPlayer*>& players) {
    const auto* enemyDef=content->npc(enemy.entry);const auto* guideDef=content->npc(guide.entry);
    if(!enemyDef || !guideDef || enemy.dead || guide.dead)return;
    const auto damage=localArmorReducedDamage(std::min(guideDef->damage,1000000u),enemyDef->armor,guide.level);
    auto effective=std::min(damage,enemy.health);
    if(damage)breakNpcControlsOnDamage(enemy,0,nullptr);
    if(damage && !enemy.lootOwner)enemy.lootOwner=owner.guid;
    // The authored escort guide fights on its owner's behalf (the local 2.23
    // rule: its kills reward the owner), so its damage counts as the owner's
    // towards the player damage requirement.
    npcLowerPlayerDamageReq(enemy,damage,true);
    addThreat(enemy,guide.guid,std::max<uint64_t>(1,uint64_t(damage)*1000));selectThreatTarget(enemy,players);
    if(damage>=enemy.health) { if(!kill(enemy,owner,players,guide.guid,guide.level))effective=0; } else enemy.health-=damage;
    LocalCombatEvent event{0,guide.guid,enemy.guid,0,guide.mapId,guide.instanceId,damage,effective,0,LocalCombatEventKind::NpcMelee,enemy.dead};
    event.schoolMask=1;event.attackType=LocalCombatAttackType::Melee;emitCombatEvent(event,players);
}
