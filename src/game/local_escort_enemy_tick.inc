// An enemy's normal threat manager selected an active escort actor.
if(!target && n.targetGuid)if(auto* guide=g.npc(n.targetGuid);guide && guide->escortOwner) {
    auto* owner=g.escortCombatOwner(*guide,players);
    if(!owner || !canAttack(*owner,n)) {
        for(auto& e:n.threat)if(e.guid==guide->guid)e={};
        n.targetGuid=0;g.selectThreatTarget(n,players);changed=true;continue;
    }
    localResetNpcSpellState(n);
    if(!stunned)n.attackTimer=std::max(0.f,n.attackTimer-dt);
    auto reach=distance2(n.x,n.y,n.z,guide->x,guide->y,guide->z);
    n.orientation=std::atan2(guide->y-n.y,guide->x-n.x);
    const auto guideReach=localCreatureCombatReach(content().npc(guide->entry));
    if(!stunned && !localWithinMeleeRange(reach,localCreatureCombatReach(def),guideReach)) {
        const float length=std::sqrt(reach),step=std::min(pursuitStep,std::max(0.f,length-2.5f));
        if(length>0 && step>0) {
            const float x=n.x+(guide->x-n.x)*step/length,y=n.y+(guide->y-n.y)*step/length,z=n.z+(guide->z-n.z)*step/length;
            if(localEscortMotionAllowed(g.collision,n.mapId,n.x,n.y,n.z,x,y,z)){n.x=x;n.y=y;n.z=z;changed=true;}
        }
    }
    reach=distance2(n.x,n.y,n.z,guide->x,guide->y,guide->z);
    if(!stunned && n.attackTimer<=0 && localWithinMeleeRange(reach,localCreatureCombatReach(def),guideReach) &&
       localEscortMeleeVisible(g.collision,n,*guide)) {
        n.attackTimer=2;g.damageEscortByNpc(*guide,n,*owner,players);changed=true;
    }
    continue;
}
