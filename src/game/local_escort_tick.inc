// Reserve/resume guides before actor combat. Combat routes advance only in the
// final pass, after this frame's lethal hits and phase transitions are known.
for(auto& n:g.npcs)if(n.escortOwner) {
    const auto* owner=g.player(n.escortOwner,players);
    const auto* route=owner?content().escortRoute(owner->escort.routeId):nullptr;
    if(!route || (NpcPrefix|route->spawnId)!=n.guid){g.releaseEscortActor(n,players);changed=true;}
}
for(auto* p:players)if(p && p->escort.routeId) {
    const auto* route=content().escortRoute(p->escort.routeId);
    auto* guide=route?g.npc(NpcPrefix|route->spawnId):nullptr;
    bool conflict=false;
    if(route)for(const auto* other:players)if(other && other->guid<p->guid && other->escort.routeId==route->id)conflict=true;
    if(!route || conflict || p->dead || p->ghost || !p->health || p->instanceId || p->flight.active || p->transportEntry || p->vehicleGuid ||
       p->escort.nextPoint>route->points.size() ||
       std::none_of(p->quests.begin(),p->quests.end(),[&](const auto& q){return q.id==route->questId && q.status==LocalQuestStatus::Active;})) {
        g.failEscort(*p,players);changed=true;continue;
    }
    if(!guide) {
        if(elapsedMs>=p->escort.remainingMs){g.failEscort(*p,players);changed=true;continue;}
        p->escort.remainingMs-=elapsedMs;changed=changed||elapsedMs;g.regionTimer=1;continue;
    }
    if(guide->dead || !guide->health || p->mapId!=guide->mapId || !localPhaseVisible(p->phaseMask,guide->requiredPhaseMask,guide->excludedPhaseMask) ||
       distance2(p->x,p->y,p->z,p->escort.x,p->escort.y,p->escort.z)>route->failRadius*route->failRadius ||
       elapsedMs>=p->escort.remainingMs){g.failEscort(*p,players);changed=true;continue;}
    // Restore HP only when resuming/reserving this actor, never every frame:
    // incoming combat damage must not be replaced by an earlier snapshot.
    if(guide->escortOwner!=p->guid) {
        guide->health=p->escort.guideHealth?std::min(p->escort.guideHealth,guide->maxHealth):guide->health;
        guide->escortOwner=p->guid;guide->targetGuid=0;guide->attackTimer=0;g.escortCombat.erase(guide->guid);
    }
    guide->x=p->escort.x;guide->y=p->escort.y;guide->z=p->escort.z;p->escort.guideHealth=guide->health;
    if(elapsedMs){p->escort.remainingMs-=elapsedMs;changed=true;}
    if(!route->combat)changed=g.advanceEscortRoute(*p,*guide,*route,elapsedMs,dt,players)||changed;
}
