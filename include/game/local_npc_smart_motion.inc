    // Included inside Impl, after local_npc_smart.inc (2.38). Temporary summons
    // (TempSummon), the creature's own movement generators (MotionMaster's
    // active slot: point, escort path, random, follow, jump), SmartAI's escort
    // state machine, the despawn timer, persistent area auras (dynamic
    // objects) and the text timer / delayed talks of SmartScript.
    uint32_t nextNpcSummonSerial=0;
    uint64_t nextNpcGroundEffectId=1;
    /// A persistent area aura (Spell::EffectPersistentAA): a dynamic object of
    /// the spell's radius at its destination for the spell's duration; the
    /// enemies inside carry the spell's aura (DynObjAura::FillTargetMap every
    /// 500 ms), those leaving lose it, all of them lose it with the object.
    struct NpcGroundEffect { uint64_t id=0,caster=0;uint32_t spellId=0,mapId=0,instanceId=0,remainingMs=0,checkMs=0;float x=0,y=0,z=0,radius=0; };
    std::vector<NpcGroundEffect> npcGroundEffects;
    static constexpr size_t kLocalMaxNpcSummons=48, kLocalMaxNpcGroundEffects=32;
    static constexpr float kLocalNpcWalkSpeed=2.5f, kLocalNpcRunSpeed=7.f, kLocalEscortMaxPlayerDist=60.f, kLocalInteractionDistance=5.5f;
    // ------------------------------------------------------------- summons
    size_t npcLiveSummons() const { size_t c=0;for(const auto& m:npcs)if(m.npcSummonType&&!m.npcUnsummoned)++c;return c; }
    bool npcIsSummon(const LocalRealmNpc& n) const { return n.npcSummonType!=0; }
    /// Map::SummonCreature + TempSummon::InitStats / InitSummon for a creature
    /// summoner: the creature enters at the position with its template, the
    /// summoner's faction when asked, its own SmartAI (AI_INIT, JUST_CREATED,
    /// RESET, RESPAWN, then JUST_SUMMONED with the summoner as invoker), and
    /// the summoner's SUMMONED_UNIT rows and summon list. Returns null when
    /// the actor budget is spent or the entry has no definition.
    LocalRealmNpc* npcSummon(LocalRealmNpc& summoner,uint32_t entry,float x,float y,float z,float o,uint8_t type,uint32_t durationMs,uint32_t spellId,bool ownerFaction,
                             const std::vector<LocalRealmPlayer*>& players) {
        if(!entry||!content->npc(entry)||npcs.size()>=LocalGameplay::MaxNpcs||npcs.size()>=npcs.capacity()||npcLiveSummons()>=kLocalMaxNpcSummons) {
            LOG_WARNING("[LOCAL_SUMMON] refused entry=",entry," by=",summoner.guid," npcs=",npcs.size()," summons=",npcLiveSummons());
            return nullptr;
        }
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(o))return nullptr;
        // TempSummon::InitStats: a MANUAL type becomes DEAD_DESPAWN without a
        // duration and TIMED_DESPAWN with one.
        if(type==kLocalSummonManual)type=durationMs?kLocalSummonTimed:kLocalSummonDead;
        if(type<1||type>8)type=kLocalSummonDead;
        LocalNpcSpawn spawn;
        nextNpcSummonSerial=(nextNpcSummonSerial+1)&0x3fffffffu;if(!nextNpcSummonSerial)nextNpcSummonSerial=1;
        spawn.id=0x40000000u|nextNpcSummonSerial;spawn.entry=entry;spawn.mapId=summoner.mapId;spawn.x=x;spawn.y=y;spawn.z=z;spawn.orientation=o;
        auto n=makeNpcAtEpoch(spawn,summoner.instanceId,allocateNpcEpoch());
        if(std::any_of(npcs.begin(),npcs.end(),[&](const auto& m){return m.guid==n.guid;}))return nullptr;
        n.npcSummoner=summoner.guid;n.npcSummonType=type;n.npcSummonTimerMs=n.npcSummonLifetimeMs=durationMs;n.npcSummonSpellId=spellId;
        n.homeX=x;n.homeY=y;n.homeZ=z;n.orientation=o;
        if(ownerFaction){n.npcFactionOverride=npcFaction(summoner);n.npcSummonOwned=true;}
        // A summon never carries loot or a respawn (TempSummon::UnSummon at DEAD).
        n.lootable=false;n.respawnTimer=0;
        npcs.push_back(std::move(n));
        auto& created=npcs.back();
        LOG_INFO("[LOCAL_SUMMON] entry=",entry," guid=",created.guid," by=",summoner.guid," type=",unsigned(type)," durationMs=",durationMs," spell=",spellId," faction=",npcFaction(created));
        // SmartAI::InitializeAI on the summon, then TempSummon::InitSummon:
        // the summoner's JustSummoned and the summon's IsSummonedBy.
        smartEnsureInit(created,players);
        if(summoner.smartSummons.size()<64)summoner.smartSummons.push_back(created.guid);
        smartFireEvents(summoner,kLocalSmartEventSummonedUnit,created.guid,players);
        smartFireEvents(created,kLocalSmartEventJustSummoned,summoner.guid,players);
        return npc(created.guid);
    }
    /// TempSummon::UnSummon: the summoner's SummonedCreatureDespawn (the
    /// SUMMON_DESPAWNED rows with the entry, the summon list), then the
    /// creature leaves the world at the next stable tick boundary.
    void npcUnsummon(LocalRealmNpc& n,const std::vector<LocalRealmPlayer*>& players) {
        if(!n.npcSummonType||n.npcUnsummoned)return;
        n.npcUnsummoned=true;n.npcDespawned=true;
        npcCancelChannelOrCast(n);n.targetGuid=0;n.threat={};n.lootable=false;n.lootOwner=0;
        if(!n.dead){n.health=0;n.dead=true;n.combatEpoch=allocateNpcEpoch();localResetNpcSpellState(n);npcClearBuffs(n);}
        for(auto* p:players)if(p) {
            if(p->attackTarget==n.guid)p->attackTarget=0;
            if(p->rangedTarget==n.guid)localStopRangedAuto(*p);
            if(p->comboTarget==n.guid)clearLocalCombo(*p);
            if(p->castTarget==n.guid)clearCast(*p,LocalCastStatus::Interrupted);
        }
        for(auto& m:npcs){if(m.targetGuid==n.guid){for(auto& e:m.threat)if(e.guid==n.guid)e={};m.targetGuid=0;}if(m.followGuid==n.guid)smartStopFollow(m,false,players);}
        std::erase_if(npcGroundEffects,[&](const auto& gnd){return gnd.caster==n.guid;});
        npcSummonerDied(n,players);
        LOG_INFO("[LOCAL_SUMMON] unsummon guid=",n.guid," entry=",n.entry," by=",n.npcSummoner);
        if(auto* summoner=npc(n.npcSummoner)) {
            std::erase(summoner->smartSummons,n.guid);
            smartFireEvents(*summoner,kLocalSmartEventSummonDespawned,n.guid,players,n.entry);
        }
    }
    /// Unit::setDeathState -> RemoveAllControlled(true): the owned summons
    /// (guardians, minions) of a dying creature are unsummoned; wild ones stay.
    void npcSummonerDied(const LocalRealmNpc& summoner,const std::vector<LocalRealmPlayer*>& players) {
        for(auto& m:npcs)if(m.npcSummoner==summoner.guid&&m.npcSummonOwned&&m.npcSummonType&&!m.npcUnsummoned)npcUnsummon(m,players);
    }
    /// SmartAI::SummonedCreatureDies / SummonedCreatureEvade on the summoner.
    void npcSummonDied(LocalRealmNpc& summon,const std::vector<LocalRealmPlayer*>& players) {
        if(!summon.npcSummonType)return;
        if(auto* summoner=npc(summon.npcSummoner))smartFireEvents(*summoner,kLocalSmartEventSummonedUnitDies,summon.guid,players);
    }
    void npcSummonEvaded(LocalRealmNpc& summon,const std::vector<LocalRealmPlayer*>& players) {
        if(!summon.npcSummonType)return;
        if(auto* summoner=npc(summon.npcSummoner))smartFireEvents(*summoner,kLocalSmartEventSummonedUnitEvade,summon.guid,players);
    }
    /// TempSummon::Update: the lifetime by type. A dead summon keeps its corpse
    /// for the corpse delay (Creature::Update, DeathState::Corpse) and leaves
    /// at DEAD; CORPSE_DESPAWN / TIMED_OR_CORPSE leave at once, CORPSE_TIMED
    /// after its own timer. Returns true when the summon left.
    bool npcSummonUpdate(LocalRealmNpc& n,uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players) {
        if(!n.npcSummonType||n.npcUnsummoned)return false;
        const bool engaged=n.targetGuid||std::any_of(n.threat.begin(),n.threat.end(),[](const auto& e){return e.guid!=0;});
        const auto timed=[&](){if(n.npcSummonTimerMs<=elapsedMs){npcUnsummon(n,players);return true;}n.npcSummonTimerMs-=elapsedMs;return false;};
        if(n.dead) {
            // The first tick of the corpse: the summoner's SummonedCreatureDies,
            // the corpse delay (Corpse.Decay for a normal creature, 60 s).
            if(!n.npcCorpseRemainingMs){n.npcCorpseRemainingMs=60000;npcSummonDied(n,players);}
            switch(n.npcSummonType) {
                case kLocalSummonCorpse:case kLocalSummonTimedOrCorpse:npcUnsummon(n,players);return true;
                case kLocalSummonCorpseTimed:return timed();
                default:break;
            }
            // The corpse delay, then DEAD (RemoveCorpse -> UnSummon).
            if(n.npcCorpseRemainingMs<=elapsedMs){npcUnsummon(n,players);return true;}
            n.npcCorpseRemainingMs-=elapsedMs;
            return false;
        }
        switch(n.npcSummonType) {
            case kLocalSummonManual:case kLocalSummonDead:case kLocalSummonCorpse:case kLocalSummonCorpseTimed:return false;
            case kLocalSummonTimed:return timed();
            case kLocalSummonTimedOutOfCombat:case kLocalSummonTimedOrCorpse:case kLocalSummonTimedOrDead:
                if(!engaged)return timed();
                n.npcSummonTimerMs=n.npcSummonLifetimeMs;return false;
            default:npcUnsummon(n,players);return true;
        }
    }
    /// The position a summon or ground effect takes for a destination code
    /// (Spell::SelectImplicit*DestTargets): the caster, the summon spot
    /// (PET_FOLLOW_DIST 2 yd, front-left), a direction at the radius (at
    /// least the caster's reach; the diagonals default to 3 yd without a
    /// radius), a random point, or the explicit target's position.
    void npcDestPosition(const LocalRealmNpc& n,uint8_t dest,float radius,const LocalRealmPlayer* targetPlayer,const LocalRealmNpc* targetNpc,float& x,float& y,float& z,float& o) {
        x=n.x;y=n.y;z=n.z;o=n.orientation;
        if(dest==kLocalNpcDestTarget){if(targetPlayer){x=targetPlayer->x;y=targetPlayer->y;z=targetPlayer->z;o=targetPlayer->orientation;}else if(targetNpc){x=targetNpc->x;y=targetNpc->y;z=targetNpc->z;o=targetNpc->orientation;}return;}
        if(dest==kLocalNpcDestCaster)return;
        const float reach=localCreatureCombatReach(content->npc(n.entry));
        float dist=radius,angle=0.f;
        switch(dest) {
            case kLocalNpcDestSummon:dist=2.f;angle=3.14159265f/4;break;
            case kLocalNpcDestFront:angle=0;break;case kLocalNpcDestBack:angle=3.14159265f;break;
            case kLocalNpcDestRight:angle=-3.14159265f/2;break;case kLocalNpcDestLeft:angle=3.14159265f/2;break;
            case kLocalNpcDestFrontRight:angle=-3.14159265f/4;if(radius<=0)dist=3.f;break;
            case kLocalNpcDestBackRight:angle=-3*3.14159265f/4;if(radius<=0)dist=3.f;break;
            case kLocalNpcDestBackLeft:angle=3*3.14159265f/4;if(radius<=0)dist=3.f;break;
            case kLocalNpcDestFrontLeft:angle=3.14159265f/4;if(radius<=0)dist=3.f;break;
            case kLocalNpcDestRandom:angle=float(meleeRoll(6283))/1000.f;if(dist>reach)dist=reach+(dist-reach)*float(meleeRoll(1000))/1000.f;break;
            case kLocalNpcDestRadius:angle=float(meleeRoll(6283))/1000.f;break;
            default:break;
        }
        if(dist<reach)dist=reach;
        const float direction=n.orientation+angle;
        float tx=n.x+dist*std::cos(direction),ty=n.y+dist*std::sin(direction);
        // MovePositionToFirstCollision: an obstructed line keeps the caster's spot.
        if(!collision.isInLineOfSight(n.mapId,n.x,n.y,n.z+1,tx,ty,n.z+1,false)){tx=n.x;ty=n.y;}
        x=tx;y=ty;
    }
    /// Unit::GetRandomPoint within `range` of (x, y, z) - a random angle and distance.
    void npcRandomPoint(float cx,float cy,float cz,float range,float& x,float& y,float& z) {
        const float angle=float(meleeRoll(6283))/1000.f,dist=range*std::sqrt(float(meleeRoll(1000))/1000.f);
        x=cx+dist*std::cos(angle);y=cy+dist*std::sin(angle);z=cz;
    }
    /// Spell::EffectSummonType for a creature caster at impact: the summons at
    /// the destination (the first exactly there, the rest at random points
    /// within the radius), ally / pet ones with the summoner's faction.
    void npcSpellSummon(LocalRealmNpc& n,const LocalSpellDefinition& d,uint64_t primaryGuid,const std::vector<LocalRealmPlayer*>& players) {
        if(!d.npcSummonEntry)return; // a dead caster's triggered DEATH cast summons as well
        float x,y,z,o;
        npcDestPosition(n,d.npcSummonDest,d.npcSummonRadius,player(primaryGuid,players),npc(primaryGuid),x,y,z,o);
        const uint8_t type=d.durationMs?kLocalSummonTimed:kLocalSummonDead;
        for(uint32_t count=0;count<std::max<uint8_t>(1,d.npcSummonCount);++count) {
            float sx=x,sy=y,sz=z;
            if(count)npcRandomPoint(x,y,z,d.npcSummonRadius,sx,sy,sz);
            if(!npcSummon(n,d.npcSummonEntry,sx,sy,sz,n.orientation,type,d.durationMs,d.id,d.npcSummonOwnerFaction,players))break;
        }
    }
    // ------------------------------------------------------------- ground effects
    /// The dynamic object of a persistent area aura at impact.
    void npcSpellGroundEffect(LocalRealmNpc& n,const LocalSpellDefinition& d,uint64_t primaryGuid,const std::vector<LocalRealmPlayer*>& players) {
        if(!d.npcGroundAura||!d.durationMs||npcGroundEffects.size()>=kLocalMaxNpcGroundEffects)return;
        NpcGroundEffect gnd;gnd.id=nextNpcGroundEffectId++;if(!nextNpcGroundEffectId)nextNpcGroundEffectId=1;
        gnd.caster=n.guid;gnd.spellId=d.id;gnd.mapId=n.mapId;gnd.instanceId=n.instanceId;gnd.remainingMs=d.durationMs;gnd.radius=d.npcGroundRadius;
        float o;npcDestPosition(n,d.npcGroundDest,d.npcGroundRadius,player(primaryGuid,players),npc(primaryGuid),gnd.x,gnd.y,gnd.z,o);
        npcGroundEffects.push_back(gnd);
        LOG_INFO("[LOCAL_GROUND] spell=",d.id," caster=",n.guid," at=",gnd.x,",",gnd.y,",",gnd.z," radius=",gnd.radius," durationMs=",gnd.remainingMs);
        npcGroundEffectApply(npcGroundEffects.back(),players);
    }
    /// DynObjAura::FillTargetMap + Aura::UpdateTargetMap: the living enemies
    /// of the caster within the radius (both sizes, line of sight from the
    /// object) carry the aura; anyone else loses it.
    void npcGroundEffectApply(NpcGroundEffect& gnd,const std::vector<LocalRealmPlayer*>& players) {
        auto* caster=npc(gnd.caster);const auto* d=content->spell(gnd.spellId);
        if(!caster||!d)return;
        const float reach=gnd.radius+kLocalDefaultCombatReach;
        for(auto* p:players) {
            if(!p||p->mapId!=gnd.mapId||p->instanceId!=gnd.instanceId)continue;
            const bool inside=!p->dead&&p->health&&!p->flight.active&&npcCanAttack(*p,*caster)&&
                distance2(p->x,p->y,p->z,gnd.x,gnd.y,gnd.z)<=reach*reach&&
                (d->sourceIgnoreLineOfSight||collision.isInLineOfSight(gnd.mapId,gnd.x,gnd.y,gnd.z+2,p->x,p->y,p->z+2,false));
            const bool carries=npcDebuffPresent(p->guid,gnd.spellId);
            if(inside&&!carries&&npcSpellHasAura(*d)) {
                if(npcAuraApply(*caster,*p,*d,players,false,false)!=LocalMeleeOutcome::Hit)continue;
                // One DynObjAura for everyone inside: a unit that steps in
                // shares the object's remaining duration and its periodic
                // timer (AuraEffect::Update ticks every application at once).
                auto entry=std::find_if(npcPeriodic.begin(),npcPeriodic.end(),[&](const auto& a){return a.caster==gnd.caster&&a.target==p->guid&&a.spell==gnd.spellId;});
                if(entry==npcPeriodic.end())continue;
                entry->remaining=std::min(entry->remaining,gnd.remainingMs);
                if(entry->interval&&d->durationMs>gnd.remainingMs) {
                    const uint32_t phase=(d->durationMs-gnd.remainingMs)%entry->interval;
                    entry->next=phase?entry->interval-phase:entry->interval;
                }
            }
            else if(!inside&&carries)npcGroundEffectRemove(gnd,*p);
        }
    }
    void npcGroundEffectRemove(const NpcGroundEffect& gnd,LocalRealmPlayer& p) {
        std::erase_if(npcPeriodic,[&](const auto& a){const bool gone=a.target==p.guid&&a.spell==gnd.spellId&&a.caster==gnd.caster;if(gone)npcControlEnded(a,&p);return gone;});
    }
    /// DynamicObject::Update: the lifetime, the 500 ms target map, the removal
    /// of the aura from everyone when the object expires or its caster leaves.
    bool npcGroundEffectsTick(uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players) {
        if(npcGroundEffects.empty())return false;
        for(auto& gnd:npcGroundEffects) {
            const auto* caster=npc(gnd.caster);
            if(!caster||caster->npcUnsummoned||gnd.remainingMs<=elapsedMs){gnd.remainingMs=0;continue;}
            gnd.remainingMs-=elapsedMs;gnd.checkMs+=elapsedMs;
            if(gnd.checkMs>=500){gnd.checkMs=0;npcGroundEffectApply(gnd,players);}
        }
        for(auto& gnd:npcGroundEffects)if(!gnd.remainingMs)for(auto* p:players)if(p)npcGroundEffectRemove(gnd,*p);
        std::erase_if(npcGroundEffects,[](const auto& gnd){return !gnd.remainingMs;});
        return true;
    }
    // ------------------------------------------------------------- movement
    /// The creature's run or walk speed for its own movement (Unit::UpdateSpeed:
    /// 7 yd/s run, 2.5 yd/s walk, the creature's speed auras and DISABLE_MOVE).
    float npcMoveSpeed(const LocalRealmNpc& n,bool walk) const { return (walk?kLocalNpcWalkSpeed*n.npcWalkSpeedRate:kLocalNpcRunSpeed*n.npcRunSpeedRate)*npcSpeedMultiplier(n); }
    bool npcMovementHeld(const LocalRealmNpc& n) const {
        return n.dead||n.npcRooted||localNpcStunned(n)||(npcUnitFlags(n)&(kLocalUnitFlagStunned|kLocalUnitFlagDisableMove))||
            localNpcPursuitDistance(n,1000)<0.1f||(n.npcCastingSpellId&&(!n.npcSpellLaunched||n.npcChanneling));
    }
    /// MotionMaster::MovePoint (MOVE_TO_POS, MOVE_FORWARD, the escort's return).
    void npcMovePoint(LocalRealmNpc& n,uint32_t id,float x,float y,float z) {
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return;
        n.npcMotion=1;n.npcMotionId=id;n.npcMotionX=x;n.npcMotionY=y;n.npcMotionZ=z;n.npcMotionSpeed=0;
        LOG_INFO("[LOCAL_MOVE] point npc=",n.guid," id=",id," to=",x,",",y,",",z);
    }
    /// MotionMaster::MoveJump: an arc at speedXY towards the point
    /// (EFFECT_MOTION_TYPE, id 0 at MovementInform).
    void npcMoveJump(LocalRealmNpc& n,float x,float y,float z,float speedXY) {
        if(speedXY<=0.1f||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return;
        n.npcMotion=5;n.npcMotionId=0;n.npcMotionX=x;n.npcMotionY=y;n.npcMotionZ=z;n.npcMotionSpeed=speedXY;
    }
    /// MotionMaster::MoveIdle / StopMoving.
    /// MotionMaster::MoveIdle / the active generator expiring: the default
    /// generator resumes (DoReset: the random timer re-armed, a patrol
    /// relaunched to its node).
    void npcMoveIdle(LocalRealmNpc& n){
        n.npcMotion=0;n.npcMotionId=0;n.npcMotionSpeed=0;
        if(n.npcDefaultMotion==1){n.npcRandomMoving=false;n.npcRandomNextMoveMs=1+meleeRoll(5000);}
        if(n.npcDefaultMotion==2)n.patrolMoving=false;
    }
    // ------------------------------------------------------------- 2.39 default movement
    /// The waypoint_data paths in use (Creature::LoadPath), from the content's
    /// own table (tests) or the catalog's paths.pack; an unknown path caches
    /// as empty so the disk is asked once.
    std::map<uint32_t,std::shared_ptr<const std::vector<LocalWaypointNode>>> patrolPaths;
    const std::vector<LocalWaypointNode>* patrolPath(uint32_t pathId) {
        if(!pathId)return nullptr;
        auto it=patrolPaths.find(pathId);
        if(it!=patrolPaths.end())return it->second->empty()?nullptr:it->second.get();
        if(patrolPaths.size()>=512)prunePatrolPaths();
        std::vector<LocalWaypointNode> nodes;
        if(content) {
            const auto own=content->waypointPaths.find(pathId);
            if(own!=content->waypointPaths.end())nodes=own->second;
            else if(content->catalog){std::string error;if(!content->catalog->waypointPath(pathId,nodes,error)&&!error.empty())LOG_WARNING("[LOCAL_PATROL] path ",pathId,": ",error);}
        }
        if(nodes.size()>1024)nodes.clear();
        auto stored=std::make_shared<const std::vector<LocalWaypointNode>>(std::move(nodes));
        patrolPaths.emplace(pathId,stored);
        return stored->empty()?nullptr:stored.get();
    }
    void prunePatrolPaths() {
        std::erase_if(patrolPaths,[&](const auto& entry){
            return std::none_of(npcs.begin(),npcs.end(),[&](const auto& n){return n.patrolPathId==entry.first||n.patrolLoadedPath==entry.first;});});
    }
    /// RandomMovementGenerator (the pin): twelve destination points around the
    /// initial position at 0.5-1.0 of the wander distance (rolled once), the
    /// links from each point to the seven opposite ones, a walk to a random
    /// linked point in line of sight, and after 10 % + 25 % per move a pause
    /// of 4-8 s (else the next move at once). The z stays the initial one
    /// (the reference asks the map height); the client snaps the model to
    /// its floor.
    void npcRandomInitialize(LocalRealmNpc& n,float distance) {
        n.npcRandomInit=true;n.npcRandomCenterX=n.x;n.npcRandomCenterY=n.y;n.npcRandomCenterZ=n.z;n.npcRandomPoint=12;n.npcRandomMoveCount=0;n.npcRandomMoving=false;
        for(auto& f:n.npcRandomFactors)f=uint8_t(meleeRoll(256));
        n.npcRandomNextMoveMs=n.spawnId?1+meleeRoll(5000):0;
        (void)distance;
    }
    void npcRandomDestination(const LocalRealmNpc& n,uint8_t point,float distance,float& x,float& y) const {
        const float angle=(2.f*3.14159265f/12.f)*float(point),factor=0.5f+float(n.npcRandomFactors[point%12])/510.f;
        x=n.npcRandomCenterX+distance*std::cos(angle)*factor;y=n.npcRandomCenterY+distance*std::sin(angle)*factor;
    }
    bool npcRandomStep(LocalRealmNpc& n,float distance,uint32_t elapsedMs) {
        // MIN_WANDER_DISTANCE_GROUND for a spawned creature outside an instance.
        if(!n.npcRandomInit)npcRandomInitialize(n,distance);
        if(distance<1.f&&!n.instanceId)distance=1.f;
        if(npcMovementHeld(n)) {
            // UNIT_STATE_NOT_MOVE / casting / DISABLE_MOVE: the timer expires, the creature stops.
            const bool moved=n.npcRandomMoving;n.npcRandomMoving=false;n.npcRandomNextMoveMs=0;return moved;
        }
        if(n.npcRandomMoving) {
            const float step=npcMoveSpeed(n,true)*float(elapsedMs)/1000.f;
            if(npcStepTowards(n,n.npcMotionX,n.npcMotionY,n.npcMotionZ,step))n.npcRandomMoving=false;
            return true;
        }
        if(n.npcRandomNextMoveMs>elapsedMs){n.npcRandomNextMoveMs-=elapsedMs;return false;}
        n.npcRandomNextMoveMs=0;
        // _setRandomLocation: a linked point (any from the initial position).
        uint8_t next;
        if(n.npcRandomPoint>=12)next=uint8_t(meleeRoll(12));
        else next=uint8_t((n.npcRandomPoint+meleeRoll(7)+3)%12);
        float x,y;npcRandomDestination(n,next,distance,x,y);const float z=n.npcRandomCenterZ;
        if(!std::isfinite(x)||!std::isfinite(y)||!collision.isInLineOfSight(n.mapId,n.x,n.y,n.z+2.f,x,y,z+2.f,false)){n.npcRandomNextMoveMs=1000;return false;}
        n.npcRandomPoint=next;n.npcMotionX=x;n.npcMotionY=y;n.npcMotionZ=z;n.npcRandomMoving=true;
        ++n.npcRandomMoveCount;
        if(meleeRoll(100)<uint32_t(n.npcRandomMoveCount)*25u+10u){n.npcRandomMoveCount=0;n.npcRandomNextMoveMs=4000+meleeRoll(4001);}
        return true;
    }
    /// WaypointMovementGenerator (waypoint_data, the pin): node by node at the
    /// node's walk / run, the delay at a node, the home position following the
    /// creature, the facing of a delayed node, MovementInform(WAYPOINT, id) and
    /// WaypointReached(id, path) at every node (both raise WAYPOINT_REACHED),
    /// PathEndReached + WaypointPathEnded (both raise WAYPOINT_ENDED) when a
    /// non-repeating path ends; a pause (MOVEMENT_PAUSE) and a stall stop it.
    bool npcPatrolAllowed(const LocalRealmNpc& n) const {
        return !n.patrolStalled&&!n.patrolDone&&!n.patrolPaused&&!npcMovementHeld(n);
    }
    void npcPatrolStart(LocalRealmNpc& n,uint32_t pathId,bool repeat,uint16_t node) {
        n.npcDefaultMotion=2;n.patrolPathId=n.patrolLoadedPath=pathId;n.patrolRepeat=repeat;n.patrolNode=node;
        n.patrolDone=false;n.patrolReached=true;n.patrolMoving=false;n.patrolStalled=false;n.patrolPaused=false;n.patrolPauseMs=0;n.patrolDelayMs=0;n.patrolHasBeenStalled=false;
        if(n.npcMotion==3||n.npcMotion==1||n.npcMotion==5)n.npcMotion=0;
        LOG_INFO("[LOCAL_PATROL] start npc=",n.guid," entry=",n.entry," path=",pathId," repeat=",repeat," node=",node);
    }
    void npcPatrolArrived(LocalRealmNpc& n,const std::vector<LocalWaypointNode>& path,const std::vector<LocalRealmPlayer*>& players) {
        const auto& node=path[n.patrolNode%path.size()];
        n.patrolMoving=false;
        if(node.delayMs)n.patrolDelayMs=node.delayMs;
        const bool pathEnded=n.patrolNode+1u>=path.size()&&!n.patrolRepeat&&!n.patrolDone;
        if(pathEnded)n.patrolDone=true;
        n.homeX=node.x;n.homeY=node.y;n.homeZ=node.z;
        if(node.hasOrientation&&node.delayMs)n.orientation=node.orientation;
        const uint32_t waypointId=node.id,pathId=n.patrolPathId;
        // SmartAI::MovementInform(WAYPOINT_MOTION_TYPE, id): WAYPOINT_REACHED
        // (id, no path) and MOVEMENTINFORM; then WaypointReached(id, path).
        smartMovementInform(n,kLocalMotionWaypoint,waypointId,players);
        smartFireEvents(n,kLocalSmartEventWaypointReached,0,players,waypointId,pathId);
        if(pathEnded) {
            // PathEndReached: WAYPOINT_ENDED (0, the loaded path), LoadPath(0);
            // WaypointPathEnded: WAYPOINT_ENDED (id, path).
            smartFireEvents(n,kLocalSmartEventWaypointEnded,0,players,0,n.patrolLoadedPath);
            n.patrolLoadedPath=0;
            smartFireEvents(n,kLocalSmartEventWaypointEnded,0,players,waypointId,pathId);
            LOG_INFO("[LOCAL_PATROL] end npc=",n.guid," path=",pathId," node=",waypointId);
        }
        n.patrolNode=uint16_t((n.patrolNode+1)%path.size());
        n.patrolReached=true;
    }
    bool npcPatrolStep(LocalRealmNpc& n,uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players) {
        if(n.patrolDone)return false;
        const auto* path=patrolPath(n.patrolPathId);
        if(!path||path->empty()){n.patrolDone=true;return false;}
        if(n.patrolNode>=path->size())n.patrolNode=0;
        if(n.patrolMoving&&!npcPatrolAllowed(n)){n.patrolMoving=false;n.patrolHasBeenStalled=true;}
        // The walk along the nodes within the tick's time. A node reached
        // inside the tick hands the rest of the tick to the next one
        // (DoUpdate: ProcessWaypointArrival, then StartMove in the same
        // update) unless the node's delay, a pause, the path's end or a stall
        // keeps the creature there. The home follows the creature.
        const auto walk=[&](float budgetMs) {
            for(int guard=0;guard<64&&n.patrolMoving&&path&&n.patrolNode<path->size();++guard) {
                const auto& node=(*path)[n.patrolNode];
                const bool run=node.moveType==1||(node.moveType>=2&&!n.npcWalking);
                const float speed=std::max(0.01f,npcMoveSpeed(n,!run));
                const float dx=node.x-n.x,dy=node.y-n.y,dz=node.z-n.z;const float length=std::sqrt(dx*dx+dy*dy+dz*dz);
                const float neededMs=length/speed*1000.f;
                if(neededMs>budgetMs){npcStepTowards(n,node.x,node.y,node.z,speed*budgetMs/1000.f);n.homeX=n.x;n.homeY=n.y;n.homeZ=n.z;return;}
                budgetMs-=neededMs;
                npcStepTowards(n,node.x,node.y,node.z,length+1.f);
                n.homeX=n.x;n.homeY=n.y;n.homeZ=n.z;
                npcPatrolArrived(n,*path,players);
                // The rows at the node may have started another path or a pause.
                path=patrolPath(n.patrolPathId);
                if(!path||path->empty()||n.patrolNode>=path->size()||n.patrolDelayMs||budgetMs<=0.f||!npcPatrolAllowed(n))return;
                n.patrolMoving=true;n.patrolReached=false;n.patrolHasBeenStalled=false;
            }
        };
        if(n.patrolMoving){walk(float(elapsedMs));return true;}
        bool changed=false;uint32_t budgetMs=elapsedMs;
        if(n.patrolDelayMs){const auto used=std::min(n.patrolDelayMs,budgetMs);n.patrolDelayMs-=used;budgetMs-=used;changed=true;if(n.patrolDelayMs)return changed;}
        if(n.patrolPaused){const auto used=std::min(n.patrolPauseMs,budgetMs);n.patrolPauseMs-=used;budgetMs-=used;changed=true;if(n.patrolPauseMs)return changed;n.patrolPaused=false;}
        if(npcPatrolAllowed(n)&&(n.patrolReached||n.patrolHasBeenStalled)) {
            // StartMove: the next node, at its own speed, with what is left of the tick.
            n.patrolMoving=true;n.patrolReached=false;n.patrolHasBeenStalled=false;changed=true;
            if(budgetMs)walk(float(budgetMs));
        }
        return changed;
    }
    /// Unit::PauseMovement / ResumeMovement / StopMoving on the active or
    /// default generator (MOVEMENT_PAUSE / RESUME / STOP).
    void npcMovementPause(LocalRealmNpc& n,uint32_t timerMs) {
        if(n.npcDefaultMotion==2||n.patrolPathId) {
            n.patrolStalled=timerMs==0;n.patrolHasBeenStalled=!n.patrolReached;n.patrolPaused=timerMs!=0;n.patrolPauseMs=timerMs;
        }
        npcStopMoving(n);
    }
    void npcMovementResume(LocalRealmNpc& n,uint32_t timerMs) {
        if(n.npcDefaultMotion==2||n.patrolPathId) {
            n.patrolHasBeenStalled=!n.patrolReached;n.patrolStalled=false;
            if(timerMs){n.patrolPaused=true;n.patrolPauseMs=timerMs;}
        }
    }
    /// Unit::StopMoving: the current spline ends where the creature stands;
    /// the generators relaunch on their own terms.
    void npcStopMoving(LocalRealmNpc& n) {
        if(n.npcMotion==1||n.npcMotion==5)npcMoveIdle(n);
        n.npcRandomMoving=false;
        if(n.patrolMoving){n.patrolMoving=false;n.patrolHasBeenStalled=!n.patrolReached;}
    }
    /// The default generator's update when nothing sits on the active slot.
    bool npcDefaultMotionStep(LocalRealmNpc& n,uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players) {
        if(n.npcDefaultMotion==1)return npcRandomStep(n,n.npcWanderDistance,elapsedMs);
        if(n.npcDefaultMotion==2)return npcPatrolStep(n,elapsedMs,players);
        return false;
    }
    /// A step of `distance` towards the goal; true when the goal is reached.
    bool npcStepTowards(LocalRealmNpc& n,float gx,float gy,float gz,float distance) {
        const float dx=gx-n.x,dy=gy-n.y,dz=gz-n.z;const float len=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(len<=distance||len<0.05f){n.x=gx;n.y=gy;n.z=gz;if(len>0.05f)n.orientation=std::atan2(dy,dx);return true;}
        n.x+=dx/len*distance;n.y+=dy/len*distance;n.z+=dz/len*distance;n.orientation=std::atan2(dy,dx);
        return false;
    }
    /// The active movement generator's step (MotionMaster::UpdateMotion) for a
    /// creature that is not chasing: point / jump movement (also in combat,
    /// where MovePoint replaces the chase until it ends), the escort path,
    /// random movement and following out of combat. Returns true when it moved
    /// or something else changed.
    bool npcMotionStep(LocalRealmNpc& n,uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players,bool engaged) {
        if(n.dead||n.npcUnsummoned||n.escortOwner||n.transportEntry||n.vehicleId)return false;
        bool changed=false;
        if(n.npcMotion==1||n.npcMotion==5) {
            if(npcMovementHeld(n))return false;
            const float speed=n.npcMotion==5?n.npcMotionSpeed:npcMoveSpeed(n,n.npcWalking&&n.npcMotionId!=kLocalSmartRandomPoint?true:n.npcWalking);
            const float step=speed*float(elapsedMs)/1000.f;
            const bool arrived=npcStepTowards(n,n.npcMotionX,n.npcMotionY,n.npcMotionZ,step);
            changed=true;
            if(arrived) {
                const uint32_t type=n.npcMotion==5?kLocalMotionEffect:kLocalMotionPoint;const uint32_t id=n.npcMotionId;
                npcMoveIdle(n);
                // PointMovementGenerator::Finalize -> MovementInform(POINT_MOTION_TYPE, id).
                smartMovementInform(n,type,id,players);
            }
            return changed;
        }
        if(engaged)return false;
        if(n.escortActive)return npcEscortStep(n,elapsedMs,players);
        if(n.npcMotion==4)return npcFollowStep(n,elapsedMs,players);
        // RANDOM_MOVE's own RandomMovementGenerator on the active slot (the
        // distance the row gave, around the position it was started at).
        if(n.npcMotion==3)return npcRandomStep(n,float(n.npcRandomDistance),elapsedMs);
        // 2.39: the default generator (creature.MovementType).
        if(n.npcMotion==0)return npcDefaultMotionStep(n,elapsedMs,players);
        return false;
    }
    /// SmartAI::MovementInform: the MOVEMENTINFORM rows (type, id).
    void smartMovementInform(LocalRealmNpc& n,uint32_t type,uint32_t id,const std::vector<LocalRealmPlayer*>& players) {
        smartFireEvents(n,kLocalSmartEventMovementInform,0,players,type,id);
    }
    // ------------------------------------------------------------- escort paths
    /// SmartAI::StartPath: the path loaded, the first point taken, the
    /// ESCORT_START rows (point 1, the path).
    void smartEscortStart(LocalRealmNpc& n,uint8_t movement,uint32_t path,bool repeat,uint64_t invoker,const std::vector<LocalRealmPlayer*>& players) {
        if(n.escortActive)smartEscortStop(n,0,0,false,players);
        const auto range=localNpcWaypointPath(path);
        if(range.first==range.second)return;
        n.escortPathId=path;n.escortIndex=1;n.escortActive=true;n.escortPaused=false;n.escortForcedPause=false;n.escortReached=false;
        n.escortRepeat=repeat;n.escortMovement=movement;n.escortReturning=false;n.escortPauseMs=0;n.escortInvokerCheckMs=1000;
        n.npcMotion=2;
        (void)invoker;
        LOG_INFO("[LOCAL_ESCORT] start npc=",n.guid," entry=",n.entry," path=",path," points=",range.second-range.first," repeat=",repeat);
        smartFireEvents(n,kLocalSmartEventEscortStart,0,players,1,path);
    }
    /// SmartAI::PausePath: the pause timer; a pause between two points
    /// (forced) stops the creature where it stands.
    void smartEscortPause(LocalRealmNpc& n,uint32_t delayMs,bool forced,const std::vector<LocalRealmPlayer*>& players) {
        if(!n.escortActive||n.escortPaused)return;
        n.escortPaused=true;n.escortPauseMs=delayMs;
        if(forced&&!n.escortReached)n.escortForcedPause=true;
        smartFireEvents(n,kLocalSmartEventEscortPaused,0,players,n.escortIndex,n.escortPathId);
    }
    /// SmartAI::StopPath: the quest, the despawn time, ESCORT_STOPPED, EndPath.
    void smartEscortStop(LocalRealmNpc& n,uint32_t despawnMs,uint32_t quest,bool fail,const std::vector<LocalRealmPlayer*>& players) {
        if(!n.escortActive)return;
        if(quest)n.escortQuestId=quest;
        n.escortRepeat=false;
        smartSetDespawnTime(n,despawnMs);
        smartFireEvents(n,kLocalSmartEventEscortStopped,0,players,n.escortIndex,n.escortPathId);
        smartEscortEnd(n,fail,players);
    }
    /// SmartAI::EndPath: the escort state cleared (a failed escort processes
    /// nothing more); ESCORT_ENDED, the repeat, the armed despawn. The escort
    /// quest credit (AreaExploredOrEventHappens) has no local quest to land on:
    /// quests with the event flag are outside the catalog.
    void smartEscortEnd(LocalRealmNpc& n,bool fail,const std::vector<LocalRealmPlayer*>& players) {
        const uint32_t path=n.escortPathId,index=n.escortIndex;
        n.escortActive=false;n.escortPaused=false;n.escortForcedPause=false;n.escortReturning=false;n.escortPauseMs=0;n.escortReached=false;
        if(n.npcMotion==2)npcMoveIdle(n);
        LOG_INFO("[LOCAL_ESCORT] end npc=",n.guid," path=",path," point=",index," fail=",fail);
        if(fail){n.escortIndex=0;return;}
        smartFireEvents(n,kLocalSmartEventEscortEnded,0,players,index,path);
        n.escortIndex=0;
        if(n.escortRepeat)smartEscortStart(n,n.escortMovement,path,true,0,players);
        else n.escortPathId=0;
        if(n.smartDespawnState==1)smartStartDespawn(n);
    }
    /// SmartAI::IsEscortInvokerInRange: a player of the escort's stored
    /// targets (or its party) within 60 yd; no stored player - no check.
    bool smartEscortInvokerInRange(const LocalRealmNpc& n,const std::vector<LocalRealmPlayer*>& players) {
        const std::vector<uint64_t>* stored=nullptr;
        for(const auto& s:n.smartStoredTargets)if(s.first==kLocalSmartEscortTargets)stored=&s.second;
        if(!stored)return true;
        bool anyPlayer=false;
        for(const auto guid:*stored)if(auto* p=player(guid,players)) {
            anyPlayer=true;
            if(p->mapId==n.mapId&&p->instanceId==n.instanceId&&distance2(*p,n)<=kLocalEscortMaxPlayerDist*kLocalEscortMaxPlayerDist)return true;
            if(stored->size()==1)if(const auto group=partyOf(p->guid))for(auto* member:players)
                if(member&&partyOf(member->guid)==group&&member->mapId==n.mapId&&member->instanceId==n.instanceId&&distance2(*member,n)<=kLocalEscortMaxPlayerDist*kLocalEscortMaxPlayerDist)return true;
        }
        return !anyPlayer;
    }
    /// SmartAI::UpdatePath + MovepointReached: the invoker check each second
    /// (out of range: the escort fails, the DEATH rows run and the creature
    /// despawns), the pause, the return after combat, then the walk to the
    /// current point; at each point the ESCORT_REACHED rows, the home moved
    /// there, the next point or EndPath.
    bool npcEscortStep(LocalRealmNpc& n,uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players) {
        if(!n.escortActive)return false;
        if(n.escortInvokerCheckMs<=elapsedMs) {
            n.escortInvokerCheckMs=1000;
            if(!smartEscortInvokerInRange(n,players)) {
                smartEscortStop(n,0,n.escortQuestId,true,players);
                smartFireEvents(n,kLocalSmartEventDeath,0,players);
                if(n.npcSummonType)npcUnsummon(n,players);else{n.npcDespawnPending=true;n.npcDespawnDelayMs=1;}
                return true;
            }
        } else n.escortInvokerCheckMs-=elapsedMs;
        if(n.escortPaused) {
            if(n.escortPauseMs<=elapsedMs) {
                if(!n.escortReturning&&(n.escortReached||n.escortForcedPause)) {
                    smartFireEvents(n,kLocalSmartEventEscortResumed,0,players,n.escortIndex,n.escortPathId);
                    n.escortPaused=false;n.escortForcedPause=false;n.escortPauseMs=0;
                    if(!n.escortActive)return true; // a row ended the escort
                }
            } else n.escortPauseMs-=elapsedMs;
        }
        if(n.escortReturning){n.escortReturning=false;n.escortReached=false;}
        if(n.escortPaused||npcMovementHeld(n))return false;
        const auto range=localNpcWaypointPath(n.escortPathId);
        const size_t count=size_t(range.second-range.first);
        if(!count||!n.escortIndex){smartEscortEnd(n,true,players);return true;}
        if(n.escortReached) {
            // The point was reached during a pause that has now ended.
            n.escortReached=false;
            if(n.escortIndex>=count){smartEscortEnd(n,false,players);return true;}
            ++n.escortIndex;
        }
        const auto& wp=range.first[std::min<size_t>(n.escortIndex,count)-1];
        const bool walk=n.escortMovement==1||(n.escortMovement==0&&n.npcWalking);
        const float step=npcMoveSpeed(n,walk)*float(elapsedMs)/1000.f;
        if(!npcStepTowards(n,wp.x,wp.y,wp.z,step))return true;
        // MovepointReached: ESCORT_REACHED, the home position, then the next point.
        n.escortReached=true;n.homeX=n.x;n.homeY=n.y;n.homeZ=n.z;
        const uint32_t reached=n.escortIndex;
        smartMovementInform(n,kLocalMotionEscort,reached-1,players);
        smartFireEvents(n,kLocalSmartEventEscortReached,0,players,reached,n.escortPathId);
        if(!n.escortActive)return true;
        if(n.escortPaused)return true; // a row paused at this point: wait here
        n.escortReached=false;
        if(reached>=count)smartEscortEnd(n,false,players);
        else n.escortIndex=uint8_t(reached+1);
        return true;
    }
    // ------------------------------------------------------------- despawn timer
    /// SmartAI::SetDespawnTime / StartDespawn / UpdateDespawn: the creature
    /// hides after the time and leaves 5 s later (DespawnOrUnsummon).
    void smartSetDespawnTime(LocalRealmNpc& n,uint32_t ms){n.smartDespawnMs=ms;n.smartDespawnState=ms?1:0;}
    void smartStartDespawn(LocalRealmNpc& n){n.smartDespawnState=2;}
    bool smartUpdateDespawn(LocalRealmNpc& n,uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players) {
        if(n.smartDespawnState<2||n.smartDespawnState>3)return false;
        if(n.smartDespawnMs>elapsedMs){n.smartDespawnMs-=elapsedMs;return false;}
        if(n.smartDespawnState==2){n.npcHidden=true;n.smartDespawnMs=5000;n.smartDespawnState=3;return true;}
        n.smartDespawnState=0;n.smartDespawnMs=0;
        if(n.npcSummonType)npcUnsummon(n,players);else{n.npcDespawnPending=true;n.npcDespawnDelayMs=1;}
        return true;
    }
    // ------------------------------------------------------------- follow
    /// SmartAI::SetFollow / StopFollow: the creature follows the unit at the
    /// distance and angle; near a creature of the arrival entry (5.5 yd) the
    /// follow completes with the credit (a kill credit, or a quest event
    /// outside the catalog), a 5 s despawn and the FOLLOW_COMPLETED rows; a
    /// player more than 60 yd away ends it with a despawn.
    void smartSetFollow(LocalRealmNpc& n,uint64_t target,float dist,float angle,uint32_t credit,uint32_t endEntry,uint8_t creditType,bool aliveState) {
        if(!target)return;
        n.followGuid=target;n.followDistance=dist;n.followAngle=angle;n.followArrivedTimerMs=1000;n.followCheckMs=0;n.followCredit=credit;
        n.followEndEntry=endEntry;n.followArrivedAlive=!aliveState;n.followCreditType=creditType;
        n.npcMotion=4;
        LOG_INFO("[LOCAL_FOLLOW] npc=",n.guid," target=",target," dist=",dist," end=",endEntry," credit=",credit);
    }
    void smartStopFollow(LocalRealmNpc& n,bool complete,const std::vector<LocalRealmPlayer*>& players) {
        if(complete) {
            auto* p=player(n.followGuid,players);
            if(p&&n.followCredit&&n.followCreditType==0) {
                // Player::RewardPlayerAndGroupAtEvent(credit, me): the kill credit for the player and its party nearby.
                const auto group=partyOf(p->guid);
                for(auto* member:players)if(member&&(member==p||(group&&partyOf(member->guid)==group&&!member->dead&&member->mapId==p->mapId&&member->instanceId==p->instanceId&&distance2(member->x,member->y,member->z,p->x,p->y,p->z)<=74*74)))
                    npcKillCredit(*member,n.followCredit);
            }
            smartSetDespawnTime(n,5000);smartStartDespawn(n);
            smartFireEvents(n,kLocalSmartEventFollowCompleted,p?p->guid:0,players);
        }
        n.followGuid=0;n.followDistance=0;n.followAngle=0;n.followCredit=0;n.followArrivedTimerMs=1000;n.followEndEntry=0;n.followCreditType=0;
        if(n.npcMotion==4)npcMoveIdle(n);
    }
    bool npcFollowStep(LocalRealmNpc& n,uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players) {
        if(!n.followGuid)return false;
        // UpdateFollow: the arrival check each second, the range check on a player.
        if(n.followArrivedTimerMs<=elapsedMs) {
            n.followArrivedTimerMs=1000;
            if(n.followEndEntry)for(const auto& m:npcs)if(&m!=&n&&m.entry==n.followEndEntry&&!m.npcUnsummoned&&!m.npcDespawned&&m.mapId==n.mapId&&m.instanceId==n.instanceId&&
                (n.followArrivedAlive?!m.dead:true)&&distance2(n.x,n.y,n.z,m.x,m.y,m.z)<=kLocalInteractionDistance*kLocalInteractionDistance){smartStopFollow(n,true,players);return true;}
        } else n.followArrivedTimerMs-=elapsedMs;
        auto* p=player(n.followGuid,players);auto* m=p?nullptr:npc(n.followGuid);
        if(n.followCheckMs<=elapsedMs) {
            n.followCheckMs=1000;
            if(!p&&!m){smartStopFollow(n,false,players);return true;}
            if(p&&(p->mapId!=n.mapId||p->instanceId!=n.instanceId||distance2(*p,n)>kLocalEscortMaxPlayerDist*kLocalEscortMaxPlayerDist)) {
                smartStopFollow(n,false,players);
                if(n.npcSummonType)npcUnsummon(n,players);else{n.npcDespawnPending=true;n.npcDespawnDelayMs=1;}
                return true;
            }
        } else n.followCheckMs-=elapsedMs;
        if(npcMovementHeld(n))return false;
        float tx,ty,tz,to;
        if(p){tx=p->x;ty=p->y;tz=p->z;to=p->orientation;}else if(m){tx=m->x;ty=m->y;tz=m->z;to=m->orientation;}else return false;
        // FollowMovementGenerator: the spot at followDistance behind the target
        // at the angle (PET_FOLLOW_DIST by default), both sizes added.
        const float dist=(n.followDistance>0?n.followDistance:2.f)+localCreatureCombatReach(content->npc(n.entry))+kLocalDefaultCombatReach;
        const float angle=to+n.followAngle;
        const float gx=tx+dist*std::cos(angle),gy=ty+dist*std::sin(angle);
        const float d2=distance2(n.x,n.y,n.z,gx,gy,tz);
        if(d2<=1.f)return false;
        const float step=npcMoveSpeed(n,n.npcWalking)*float(elapsedMs)/1000.f;
        npcStepTowards(n,gx,gy,tz,step);
        return true;
    }
    // ------------------------------------------------------------- text timer
    /// SmartScript::UpdateTimers' text timer: the TEXT_OVER rows (group, the
    /// talker's entry) when the last TALK's duration ran out; then the delayed
    /// talks (CreatureAI::Talk with a delay).
    bool smartTextTimers(LocalRealmNpc& n,uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players) {
        bool changed=false;
        if(n.smartTextTimerActive) {
            if(n.smartTextTimerMs<elapsedMs||!n.smartTextTimerMs) {
                const uint32_t group=n.smartTextGroup,entry=n.smartTalkerEntry;
                n.smartTextGroup=0;n.smartTalkerEntry=0;n.smartTextTimerMs=0;n.smartTextTimerActive=false;
                smartFireEvents(n,kLocalSmartEventTextOver,0,players,group,entry);
            } else n.smartTextTimerMs-=elapsedMs;
            changed=true;
        }
        if(!n.smartPendingTalks.empty()) {
            std::vector<LocalRealmNpc::SmartPendingTalk> due;
            for(auto& talk:n.smartPendingTalks){if(talk.delayMs<=elapsedMs){talk.delayMs=0;due.push_back(talk);}else talk.delayMs-=elapsedMs;}
            std::erase_if(n.smartPendingTalks,[](const auto& talk){return !talk.delayMs;});
            for(const auto& talk:due)smartSpeak(talk.talker,talk.group,talk.target,players);
            changed=true;
        }
        return changed;
    }
    /// The talker's text group spoken to the talk target (CreatureAI::Talk).
    void smartSpeak(uint64_t talkerGuid,uint8_t group,uint64_t targetGuid,const std::vector<LocalRealmPlayer*>& players) {
        const auto* talker=npc(talkerGuid);
        if(!talker||talker->dead||talker->npcUnsummoned)return;
        const auto* text=content->creatureText(talker->entry,group);
        if(!text)return;
        LocalCreatureTalkRule rule;rule.lines=text->lines;
        speak(*talker,rule,player(targetGuid,players),players);
        LOG_INFO("[LOCAL_SMART] talk npc=",talker->guid," entry=",talker->entry," group=",unsigned(group));
    }
    // ------------------------------------------------------------- zone combat
    /// SMART_ACTION_SET_IN_COMBAT_WITH_ZONE: in an instance every living
    /// player of it, elsewhere the players within the row's range, engage the
    /// creature (Creature::SetInCombatWithZone / EngageWithTarget).
    void smartSetInCombatWithZone(LocalRealmNpc& n,float range,const std::vector<LocalRealmPlayer*>& players) {
        if(n.dead)return;
        const bool instance=n.instanceId!=0;
        for(auto* p:players)if(p&&!p->dead&&p->health&&p->mapId==n.mapId&&p->instanceId==n.instanceId&&npcCanAttack(*p,n)) {
            if(!instance&&(range<=0||distance2(*p,n)>range*range))continue;
            if(!std::any_of(n.threat.begin(),n.threat.end(),[&](const auto& e){return e.guid==p->guid;}))addThreat(n,p->guid,1);
            if(!n.targetGuid)n.targetGuid=p->guid;
        }
    }
