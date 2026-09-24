// Defensive guides act as creatures, after enemy swings. A dead owner/guide or
// a phase/quest transition fails before route completion can grant its credit.
for(auto* p:players)if(p && p->escort.routeId) {
    const auto* route=content().escortRoute(p->escort.routeId);
    auto* guide=route?g.npc(NpcPrefix|route->spawnId):nullptr;
    if(!route || p->dead || p->ghost || !p->health || p->instanceId || p->flight.active || p->transportEntry || p->vehicleGuid ||
       std::none_of(p->quests.begin(),p->quests.end(),[&](const auto& q){return q.id==route->questId && q.status==LocalQuestStatus::Active;}) ||
       (guide && (guide->dead || !guide->health || guide->mapId!=p->mapId ||
        !localPhaseVisible(p->phaseMask,guide->requiredPhaseMask,guide->excludedPhaseMask)))) {
        g.failEscort(*p,players);changed=true;continue;
    }
    if(!guide || !route->combat)continue;
    const auto anchor=g.escortCombat.find(guide->guid);
    const float ax=anchor==g.escortCombat.end()?guide->x:anchor->second.anchorX;
    const float ay=anchor==g.escortCombat.end()?guide->y:anchor->second.anchorY;
    const float az=anchor==g.escortCombat.end()?guide->z:anchor->second.anchorZ;
    LocalRealmNpc* target=nullptr;float nearest=std::numeric_limits<float>::infinity();
    for(auto& enemy:g.npcs) {
        if(enemy.dead || !enemy.health || enemy.vehicleId || enemy.transportEntry || enemy.escortOwner ||
           enemy.mapId!=guide->mapId || enemy.instanceId!=guide->instanceId || !canAttack(*p,enemy))continue;
        const bool engaged=enemy.targetGuid==p->guid || enemy.targetGuid==guide->guid ||
            std::any_of(enemy.threat.begin(),enemy.threat.end(),[&](const auto& e){return e.amount && (e.guid==p->guid || e.guid==guide->guid);});
        if(!engaged || distance2(ax,ay,az,enemy.x,enemy.y,enemy.z)>route->combatChaseRadius*route->combatChaseRadius)continue;
        const auto d=distance2(guide->x,guide->y,guide->z,enemy.x,enemy.y,enemy.z);
        if(d<nearest || (d==nearest && (!target || enemy.guid<target->guid))){nearest=d;target=&enemy;}
    }
    const auto oldTarget=guide->targetGuid;guide->targetGuid=target?target->guid:0;changed=changed||oldTarget!=guide->targetGuid;
    if(target) {
        if(anchor==g.escortCombat.end())g.escortCombat.emplace(guide->guid,LocalEscortCombatState{guide->x,guide->y,guide->z});
        if(!localNpcStunned(*guide)) {
            guide->attackTimer=std::max(0.f,guide->attackTimer-dt);
            guide->orientation=std::atan2(target->y-guide->y,target->x-guide->x);
            const auto* guideDef=content().npc(guide->entry);const auto* targetDef=content().npc(target->entry);
            const auto reach=localCreatureCombatReach(guideDef),targetReach=localCreatureCombatReach(targetDef);
            if(!localWithinMeleeRange(nearest,reach,targetReach)) {
                const float length=std::sqrt(nearest),step=std::min(route->speed*dt,std::max(0.f,length-2.5f));
                if(length>0 && step>0) {
                    const float x=guide->x+(target->x-guide->x)*step/length,y=guide->y+(target->y-guide->y)*step/length,z=guide->z+(target->z-guide->z)*step/length;
                    if(localEscortMotionAllowed(g.collision,guide->mapId,guide->x,guide->y,guide->z,x,y,z)) {
                        guide->x=x;guide->y=y;guide->z=z;changed=true;
                    }
                }
            }
            if(guide->attackTimer<=0 && localWithinMeleeRange(distance2(guide->x,guide->y,guide->z,target->x,target->y,target->z),reach,targetReach) &&
               localEscortMeleeVisible(g.collision,*guide,*target)) {
                guide->attackTimer=2;g.damageNpcByEscort(*target,*guide,*p,players);changed=true;
                if(target->dead)guide->targetGuid=0;
            }
        }
    } else if(!g.escortUnderAttack(*guide) && !localCombatActive(*p,g.npcs))g.escortCombat.erase(guide->guid);
    p->escort.x=guide->x;p->escort.y=guide->y;p->escort.z=guide->z;p->escort.guideHealth=guide->health;
    // A kill callback can complete/fail the associated quest or change phase.
    // Never grant the route's own completion event after such a transition.
    if(!g.escortCombatOwner(*guide,players)){g.failEscort(*p,players);changed=true;continue;}
    changed=g.advanceEscortRoute(*p,*guide,*route,elapsedMs,dt,players)||changed;
}
