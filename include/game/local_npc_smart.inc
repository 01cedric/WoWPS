    // Included inside Impl, after local_npc_spell_impact.inc. The SmartAI
    // script runtime of the generated family (SmartScript / SmartAI at the
    // pin): per-row timers, event dispatch, targets, actions, links, phases,
    // timed action lists, and the creature's own cast state machine.
    std::function<bool(const LocalRealmPlayer&,const LocalRealmNpc&)> canAttackFn;
    bool npcCanAttack(const LocalRealmPlayer& p,const LocalRealmNpc& n){return canAttackFn?canAttackFn(p,n):false;}
    struct SmartUnit { LocalRealmPlayer* p=nullptr; LocalRealmNpc* m=nullptr; uint64_t guid() const {return p?p->guid:m?m->guid:0;} };
    int64_t smartOwner(const LocalRealmNpc& n) const {
        const bool guidScripted=n.spawnId&&std::binary_search(content->creatureGuidScripts.begin(),content->creatureGuidScripts.end(),n.spawnId);
        return localNpcSmartOwner(n.entry,n.spawnId,guidScripted);
    }
    LocalNpcSmartRange smartRows(const LocalRealmNpc& n) const { return localNpcSmartRows(smartOwner(n)); }
    /// Whole-owner admission: a row whose spell the client decoder refused
    /// leaves the entire script without SmartAI actions rather than half of it.
    bool npcSpellUsable(const LocalSpellDefinition* candidate,unsigned depth=0) const {
        if(!candidate||!candidate->unsupportedReason.empty()||!candidate->clientSpell||!candidate->npcOnly)return false;
        if(candidate->sourceDamageClass>3)return false;
        if(depth==0) {
            if(candidate->npcTriggerSpellId&&!npcSpellUsable(content->spell(candidate->npcTriggerSpellId),1))return false;
            if(candidate->npcPeriodicTriggerSpellId&&!npcSpellUsable(content->spell(candidate->npcPeriodicTriggerSpellId),1))return false;
        } else if(candidate->npcTriggerSpellId||candidate->npcPeriodicTriggerSpellId)return false;
        return true;
    }
    bool smartRowsUsable(const LocalRealmNpc& n) const {
        const auto rows=smartRows(n);
        const size_t count=size_t(rows.second-rows.first);
        if(!count||count>kLocalMaxNpcSpellProfilesPerEntry)return false;
        const auto listUsable=[&](uint32_t id){
            const auto list=localNpcSmartList(id);
            if(list.first==list.second||size_t(list.second-list.first)>kLocalMaxNpcSpellProfilesPerEntry)return false;
            for(const auto* l=list.first;l!=list.second;++l)if(l->casts()&&!npcSpellUsable(content->spell(l->spellId())))return false;
            return true;
        };
        for(const auto* r=rows.first;r!=rows.second;++r) {
            if(r->casts()&&!npcSpellUsable(content->spell(r->spellId())))return false;
            if(r->action==kLocalSmartActionTimedList&&!listUsable(r->a1))return false;
            if(r->action==kLocalSmartActionCallRandomTimedList){const uint32_t ids[6]={r->a1,r->a2,r->a3,r->a4,r->a5,r->a6};for(auto id:ids)if(id&&!listUsable(id))return false;}
            if(r->action==kLocalSmartActionCallRandomRangeTimedList){if(r->a2<r->a1||r->a2-r->a1>24)return false;for(uint32_t id=r->a1;id<=r->a2;++id)if(!listUsable(id))return false;}
        }
        return true;
    }
    uint32_t& smartTimer(LocalRealmNpc& n,size_t i){return i?n.npcSpellExtraTimers[i-1]:n.npcSpellTimerMs;}
    static bool smartTimedEvent(uint32_t event) {
        switch(event){case kLocalSmartEventUpdate:case kLocalSmartEventUpdateIc:case kLocalSmartEventUpdateOoc:case kLocalSmartEventRange:
            case kLocalSmartEventAreaRange:case kLocalSmartEventAreaCasting:case kLocalSmartEventFriendlyHealthPct:return true;default:return false;}
    }
    uint32_t smartRoll(uint32_t lo,uint32_t hi){return lo>=hi?lo:lo+meleeRoll(hi-lo);}
    /// SmartScript::InitTimer: the initial timer of the timed events
    /// (IS_BEHIND_TARGET rolls its min/max too, NEAR_PLAYERS its firstTimer).
    uint32_t smartInitialTimer(const LocalNpcSpellProfile& r) {
        if(smartTimedEvent(r.event)||r.event==kLocalSmartEventIsBehindTarget)return smartRoll(r.p1,r.p2);
        if(r.event==kLocalSmartEventNearPlayers||r.event==kLocalSmartEventNearPlayersNegation)return r.p3;
        if(r.event==kLocalSmartEventDistanceCreature)return r.p4; // RecalcTimer(repeat, repeat)
        return 0;
    }
    /// SmartScript::OnReset (reset=true) / FillScript+InitTimer at creation:
    /// timers re-armed, runOnce cleared except DONT_RESET rows, phase 0.
    void smartInit(LocalRealmNpc& n,bool reset) {
        const auto rows=smartRows(n);const size_t count=size_t(rows.second-rows.first);
        if(n.npcSpellExtraTimers.size()+1<count)n.npcSpellExtraTimers.resize(count?count-1:0,0);
        if(!n.npcSpellTimerInitialized||!reset){n.npcSpellDoneMask=0;n.smartPhase=0;n.smartPhaseResetDisabled=false;n.smartCounters.clear();n.smartStoredTargets.clear();n.smartTimedEvents.clear();}
        else {
            uint32_t keep=0;size_t i=0;
            for(const auto* r=rows.first;r!=rows.second;++r,++i)if(r->dontReset())keep|=uint32_t(1u)<<i;
            n.npcSpellDoneMask&=keep;
            // SET_EVENT_FLAG_RESET(0) keeps the phase across OnReset (AllowPhaseReset).
            if(!n.smartPhaseResetDisabled)n.smartPhase=0;
        }
        size_t i=0;
        for(const auto* r=rows.first;r!=rows.second;++r,++i)if(!(reset&&r->dontReset()))smartTimer(n,i)=smartInitialTimer(*r);
        n.npcSpellTimerInitialized=true;n.smartInvoker=0;
        n.smartListId=0;n.smartListIndex=0;n.smartListTimer=0;
    }
    /// SmartAI::InitializeAI (SmartScript::OnInitialize: the timers, the AI_INIT
    /// and JUST_CREATED rows) and the creature's first JustRespawned
    /// (JustReachedHome -> OnReset: the RESET rows; then the RESPAWN rows).
    void smartEnsureInit(LocalRealmNpc& n,const std::vector<LocalRealmPlayer*>& players) {
        if(n.npcSpellTimerInitialized||n.scriptActorId||!smartRowsUsable(n))return;
        smartInit(n,false);smartInitRangeMode(n);
        LOG_DEBUG("[LOCAL_SMART] init npc=",n.guid," entry=",n.entry," spawn=",n.spawnId," summon=",unsigned(n.npcSummonType));
        smartFireEvents(n,kLocalSmartEventAiInit,0,players);smartFireEvents(n,kLocalSmartEventJustCreated,0,players);
        smartFireEvents(n,kLocalSmartEventReset,0,players);smartFireEvents(n,kLocalSmartEventRespawn,0,players);
    }
    /// SmartAI::InitializeAI's range mode: a MAIN_SPELL row, else the first
    /// COMBAT_MOVE cast of a hostile spell reaching beyond 5 yd.
    void smartInitRangeMode(LocalRealmNpc& n) {
        n.npcRangeInit=true;n.npcRangeMode=false;n.npcAttackDistance=0;
        const auto rows=smartRows(n);
        const auto choose=[&](uint32_t flag){
            for(const auto* r=rows.first;r!=rows.second;++r)if(r->casts()&&(r->castFlags()&flag)) {
                const auto* main=content->spell(r->spellId());
                if(!main||main->npcPositive)continue;
                if(main->range>5.f){n.npcRangeMode=true;n.npcAttackDistance=main->range-5.f;}
                return true;
            }
            return false;
        };
        if(!choose(kLocalSmartCastMainSpell))choose(kLocalSmartCastCombatMove);
    }
    bool smartInPhase(const LocalRealmNpc& n,uint32_t mask) const { return !mask||(n.smartPhase&&((1u<<(n.smartPhase-1))&mask)); }
    void smartSetPhase(LocalRealmNpc& n,uint32_t phase){n.smartPhase=uint8_t(std::min<uint32_t>(phase,12));}
    /// Aura count of a spell on the creature (Unit::GetAuraCount): its buffs
    /// and the player-applied auras the authority tracks.
    uint32_t npcAuraCount(const LocalRealmNpc& n,uint32_t spellId) const {
        uint32_t count=0;
        for(const auto& b:n.npcBuffs)if(b.spellId==spellId&&(b.remainingMs||b.indefinite))count+=std::max<uint8_t>(1,b.stacks);
        for(const auto& a:n.snares)if(a.spellId==spellId&&a.remainingMs)++count;
        for(const auto& a:n.controls)if(a.spellId==spellId&&a.remainingMs)++count;
        for(const auto& a:n.damageAuras)if(a.spellId==spellId)++count;
        return count;
    }
    bool npcBuffPresent(const LocalRealmNpc& n,uint32_t spellId) const {return npcAuraCount(n,spellId)>0;}
    /// Unit::HasAura on a player: its own stat auras and the creature auras it carries.
    bool playerAuraPresent(const LocalRealmPlayer& p,uint32_t spellId) const {
        for(const auto& a:p.statAuras)if(a.spellId==spellId&&a.remainingMs)return true;
        return npcDebuffPresent(p.guid,spellId);
    }
    // ------------------------------------------------------------- 2.37 state
    /// UNIT_FIELD_FLAGS the creature carries: the template's until the script
    /// touched them (SET/REMOVE_UNIT_FLAG), then the script's copy.
    uint32_t npcRawUnitFlags(const LocalRealmNpc& n) const {
        if(n.npcUnitFlagsOverride)return n.npcUnitFlags;
        const auto* def=content->npc(n.entry);return def?def->unitFlags:0u;
    }
    /// The effective UNIT_FIELD_FLAGS: the template's or the script's, plus
    /// the stun / root a creature applied to itself (2.39: Unit::SetControlled
    /// sets UNIT_FLAG_STUNNED; a root holds it where it stands).
    uint32_t npcUnitFlags(const LocalRealmNpc& n) const {
        uint32_t flags=npcRawUnitFlags(n);
        for(const auto& b:n.npcBuffs)if(b.selfControl&&(b.remainingMs||b.indefinite))flags|=b.selfControl==1?kLocalUnitFlagStunned:kLocalUnitFlagDisableMove;
        return flags;
    }
    bool npcInvisible(const LocalRealmNpc& n) const {
        for(const auto& b:n.npcBuffs)if(b.invisible&&(b.remainingMs||b.indefinite))return true;
        return false;
    }
    void npcSetUnitFlags(LocalRealmNpc& n,uint32_t flags,bool set) {
        const uint32_t current=npcRawUnitFlags(n);
        n.npcUnitFlags=set?current|flags:current&~flags;n.npcUnitFlagsOverride=true;
    }
    /// The creature's faction: SET_FACTION's override, else the template's.
    uint32_t npcFaction(const LocalRealmNpc& n) const {
        if(n.npcFactionOverride)return n.npcFactionOverride;
        const auto* def=content->npc(n.entry);return def?def->faction:0u;
    }
    /// Creature::InitializeReactState: REACT_AGGRESSIVE (2) until SET_REACT_STATE.
    uint8_t npcReactState(const LocalRealmNpc& n) const { return n.npcReactState==255?2:n.npcReactState; }
    /// Unit::UpdateSpeed for the creature's run speed from its own buffs: the
    /// largest MOD_INCREASE_SPEED bonus times the strongest MOD_DECREASE_SPEED
    /// slow; UNIT_FLAG_DISABLE_MOVE holds it in place. A player's snares ride
    /// localNpcPursuitDistance.
    float npcSpeedMultiplier(const LocalRealmNpc& n) const {
        if(npcUnitFlags(n)&kLocalUnitFlagDisableMove)return 0.f;
        int32_t bonus=0,slow=0;
        for(const auto& b:n.npcBuffs)if(b.remainingMs||b.indefinite){if(b.speedPct>0)bonus=std::max(bonus,b.speedPct);else if(b.speedPct<0)slow=std::min(slow,b.speedPct);}
        return float(100+std::min(bonus,1000))/100.f*float(100+std::max(slow,-99))/100.f;
    }
    /// MOD_RESISTANCE of the creature's buffs for a school mask.
    int32_t npcBuffResistance(const LocalRealmNpc& n,uint32_t schoolMask) const {
        int32_t sum=0;for(const auto& b:n.npcBuffs)if((b.remainingMs||b.indefinite)&&(b.resistanceSchool&schoolMask))sum+=b.resistance;return sum;
    }
    uint32_t smartCounter(const LocalRealmNpc& n,uint32_t id) const {
        for(const auto& c:n.smartCounters)if(c.first==id)return c.second;return 0;
    }
    /// SmartScript::StoreCounter, then its COUNTER_SET rows.
    void smartStoreCounter(LocalRealmNpc& n,uint32_t id,uint32_t value,bool reset,bool subtract,const std::vector<LocalRealmPlayer*>& players) {
        auto it=std::find_if(n.smartCounters.begin(),n.smartCounters.end(),[&](const auto& c){return c.first==id;});
        if(it!=n.smartCounters.end()){if(!reset&&!subtract)it->second+=value;else if(subtract)it->second-=value;else it->second=value;}
        else if(n.smartCounters.size()<32)n.smartCounters.push_back({id,value});
        smartFireEvents(n,kLocalSmartEventCounterSet,0,players,id);
    }
    /// SmartAI::SetData: the DATA_SET rows of the receiving creature, the
    /// sender as their invoker.
    void smartSetData(LocalRealmNpc& receiver,uint32_t id,uint32_t value,uint64_t sender,const std::vector<LocalRealmPlayer*>& players) {
        smartFireEvents(receiver,kLocalSmartEventDataSet,sender,players,id,value);
    }
    /// A creature buff leaving (expiry, REMOVE_AURAS, death): MOD_INCREASE_HEALTH
    /// gives its amount back (health follows, never below 1),
    /// MOD_INCREASE_HEALTH_PERCENT keeps the health percentage.
    void npcBuffRemoved(LocalRealmNpc& n,const LocalNpcBuff& b) {
        if(!b.maxHealth)return;
        const int64_t delta=b.maxHealth;
        if(b.maxHealthPct) {
            const float pct=n.maxHealth?float(n.health)/float(n.maxHealth):1.f;
            n.maxHealth=uint32_t(std::clamp<int64_t>(int64_t(n.maxHealth)-delta,1,1000000000));
            if(!n.dead)n.health=std::clamp<uint32_t>(uint32_t(float(n.maxHealth)*pct),1u,n.maxHealth);
        } else {
            n.maxHealth=uint32_t(std::clamp<int64_t>(int64_t(n.maxHealth)-delta,1,1000000000));
            if(!n.dead){if(delta>0)n.health=uint32_t(int64_t(n.health)>delta?int64_t(n.health)-delta:1);n.health=std::min(n.health,n.maxHealth);}
        }
    }
    template<class Pred> bool npcEraseBuffs(LocalRealmNpc& n,Pred pred) {
        const auto before=n.npcBuffs.size();
        for(auto& b:n.npcBuffs)if(pred(b))npcBuffRemoved(n,b);
        std::erase_if(n.npcBuffs,pred);
        return before!=n.npcBuffs.size();
    }
    void npcClearBuffs(LocalRealmNpc& n){npcEraseBuffs(n,[](const LocalNpcBuff&){return true;});}
    // ------------------------------------------------------------------ targets
    /// SmartScript::GetTargets for the supported selectors. Player selectors
    /// read the threat list (SelectTarget MaxThreat/MinThreat/Random) and the
    /// map; creature selectors the authority's creatures.
    std::vector<SmartUnit> smartTargets(LocalRealmNpc& n,const LocalNpcSpellProfile& r,uint64_t invoker,const std::vector<LocalRealmPlayer*>& players) {
        std::vector<SmartUnit> out;
        const auto onMap=[&](const LocalRealmPlayer* p){return p&&p->mapId==n.mapId&&p->instanceId==n.instanceId;};
        const auto alive=[&](const LocalRealmPlayer* p){return onMap(p)&&!p->dead&&p->health;};
        // 2.39: the selectors' powerType + 1 (t3) keeps the characters whose
        // resource it names (PowerUsersSelector: mana 1, rage 2, focus 3, energy 4, runic power 7).
        const auto powerMatches=[&](const LocalRealmPlayer* p){
            if(!r.t3)return true;
            switch(r.t3){case 1:return p->resourceType==LocalResourceType::Mana;case 2:return p->resourceType==LocalResourceType::Rage;
                case 4:return p->resourceType==LocalResourceType::Energy;case 7:return p->resourceType==LocalResourceType::RunicPower;default:return false;}
        };
        const auto threatPlayers=[&](float maxDist,bool playerOnly,bool withTank){
            std::vector<LocalRealmPlayer*> list;
            for(size_t i=0;i<n.threat.size();++i){const auto& e=n.threat[i];if(!e.guid)continue;
                auto* p=player(e.guid,players);if(!p||p->dead||!p->health||!npcCanAttack(*p,n))continue;
                if(maxDist>0&&distance2(*p,n)>maxDist*maxDist)continue;
                if(!withTank&&e.guid==n.targetGuid)continue;
                if(!powerMatches(p))continue;
                list.push_back(p);}
            (void)playerOnly;return list;
        };
        const auto sortedThreat=[&](std::vector<LocalRealmPlayer*> list){
            std::stable_sort(list.begin(),list.end(),[&](const auto* a,const auto* b){
                uint64_t ta=0,tb=0;for(const auto& e:n.threat){if(e.guid==a->guid)ta=e.amount;if(e.guid==b->guid)tb=e.amount;}
                return ta>tb;});
            return list;
        };
        switch(r.target) {
            case kLocalSmartTargetSelf:out.push_back({nullptr,&n});break;
            case kLocalSmartTargetVictim:if(auto* p=player(n.targetGuid,players))out.push_back({p,nullptr});break;
            case kLocalSmartTargetHostileSecond:{auto list=sortedThreat(threatPlayers(float(r.t1),r.t2!=0,false));if(!list.empty())out.push_back({list.front(),nullptr});break;}
            case kLocalSmartTargetHostileLast:{auto list=sortedThreat(threatPlayers(float(r.t1),r.t2!=0,true));if(!list.empty())out.push_back({list.back(),nullptr});break;}
            case kLocalSmartTargetHostileRandom:{auto list=threatPlayers(float(r.t1),r.t2!=0,true);if(!list.empty())out.push_back({list[meleeRoll(uint32_t(list.size()-1))],nullptr});break;}
            case kLocalSmartTargetHostileRandomNotTop:{auto list=threatPlayers(float(r.t1),r.t2!=0,false);if(!list.empty())out.push_back({list[meleeRoll(uint32_t(list.size()-1))],nullptr});break;}
            case kLocalSmartTargetInvoker:
                if(auto* p=player(invoker,players))out.push_back({p,nullptr});
                else if(auto* m=npc(invoker))out.push_back({nullptr,m});
                break;
            case kLocalSmartTargetClosestPlayer:{
                LocalRealmPlayer* best=nullptr;float bestD=float(r.t1)*float(r.t1);
                for(auto* p:players)if(alive(p)){const float d=distance2(*p,n);if(d<=bestD&&(!best||d<distance2(*best,n))){best=p;bestD=d;}}
                if(best)out.push_back({best,nullptr});break;}
            case kLocalSmartTargetClosestEnemy:{
                LocalRealmPlayer* best=nullptr;const float lim=r.t1?float(r.t1):100.f;float bestD=lim*lim;
                for(auto* p:players)if(alive(p)&&npcCanAttack(*p,n)){const float d=distance2(*p,n);if(d<=bestD&&(!best||d<distance2(*best,n))){best=p;bestD=d;}}
                if(best)out.push_back({best,nullptr});break;}
            case kLocalSmartTargetThreatList:{
                for(auto* p:threatPlayers(0,false,true))if(!r.t1||localWithinCombatRange(distance2(*p,n),localCreatureCombatReach(content->npc(n.entry)),kLocalDefaultCombatReach,float(r.t1)))out.push_back({p,nullptr});
                break;}
            case kLocalSmartTargetCreatureRange:case kLocalSmartTargetCreatureDistance:case kLocalSmartTargetClosestCreature:case kLocalSmartTargetCreatureGuid:{
                const uint32_t entry=r.t1;
                LocalRealmNpc* closest=nullptr;float closestD=0;
                const auto [minDist,maxDist,living]=smartTargetParams(r);
                for(auto& m:npcs) {
                    if(&m==&n||m.mapId!=n.mapId||m.instanceId!=n.instanceId||m.npcDespawned)continue;
                    if(r.target==kLocalSmartTargetCreatureGuid){if(m.spawnId!=entry||(r.t2&&m.entry!=r.t2))continue;out.push_back({nullptr,&m});break;}
                    if(entry&&m.entry!=entry)continue;
                    const float d=distance2(n.x,n.y,n.z,m.x,m.y,m.z);
                    if(living==1&&m.dead)continue;if(living==2&&!m.dead)continue;
                    if(r.target==kLocalSmartTargetClosestCreature){if(d<=maxDist*maxDist&&(!closest||d<closestD)){closest=&m;closestD=d;}continue;}
                    if(d<minDist*minDist||d>maxDist*maxDist)continue;
                    out.push_back({nullptr,&m});
                }
                if(closest)out.push_back({nullptr,closest});
                break;}
            // ---- 2.37 selectors
            case kLocalSmartTargetStored:
                // GetStoredTargetVector: the guids STORE_TARGET_LIST kept under
                // the id, resolved now (a departed unit is simply gone).
                for(const auto& stored:n.smartStoredTargets)if(stored.first==r.t1)for(const auto guid:stored.second) {
                    if(auto* p=player(guid,players))out.push_back({p,nullptr});
                    else if(auto* m=npc(guid);m&&!m->npcDespawned)out.push_back({nullptr,m});
                }
                break;
            case kLocalSmartTargetInvokerParty:{
                // The invoking player's party on the same map (includePets: no
                // owned creature is a target here), or the player alone.
                auto* p=player(invoker,players);if(!p)break;
                if(const auto group=partyOf(p->guid)) {
                    for(auto* member:players)if(member&&member->mapId==p->mapId&&member->instanceId==p->instanceId&&partyOf(member->guid)==group)out.push_back({member,nullptr});
                } else out.push_back({p,nullptr});
                break;}
            case kLocalSmartTargetPlayerRange:
                // Unit::IsInRange(minDist, maxDist) with both combat reaches; maxCount resizes at random.
                for(auto* p:players)if(onMap(p)&&npcInRange(n,*p,float(r.t1),float(r.t2)))out.push_back({p,nullptr});
                if(r.t3&&out.size()>r.t3){std::shuffle(out.begin(),out.end(),meleeRandom);out.resize(r.t3);}
                break;
            case kLocalSmartTargetPlayerDistance:
                for(auto* p:players)if(onMap(p)&&distance2(*p,n)<=float(r.t1)*float(r.t1))out.push_back({p,nullptr});
                break;
            case kLocalSmartTargetClosestFriendly:{
                // DoFindClosestFriendlyInRange(maxDist, playerOnly): a living
                // friendly unit other than the creature (the reference keeps
                // the last one its cell walk met; the closest is read here).
                LocalRealmNpc* bestM=nullptr;LocalRealmPlayer* bestP=nullptr;float bestD=float(r.t1)*float(r.t1);
                if(!r.t2)for(auto& m:npcs)if(&m!=&n&&!m.dead&&m.health&&!m.npcDespawned&&m.mapId==n.mapId&&m.instanceId==n.instanceId&&!m.scriptActorId&&npcFriendly(n,m)) {
                    const float d=distance2(n.x,n.y,n.z,m.x,m.y,m.z);if(d<=bestD){bestM=&m;bestD=d;}
                }
                for(auto* p:players)if(alive(p)&&!npcCanAttack(*p,n)){const float d=distance2(*p,n);if(d<=bestD){bestP=p;bestM=nullptr;bestD=d;}}
                if(bestP)out.push_back({bestP,nullptr});else if(bestM)out.push_back({nullptr,bestM});
                break;}
            case kLocalSmartTargetLootRecipients:{
                // GetLootRecipientGroup's members, else the recipient (the tagger).
                auto* owner=player(n.lootOwner,players);if(!owner)break;
                if(const auto group=partyOf(owner->guid)) {
                    for(auto* member:players)if(member&&partyOf(member->guid)==group)out.push_back({member,nullptr});
                } else out.push_back({owner,nullptr});
                break;}
            case kLocalSmartTargetFarthest:{
                // SelectTarget(SelectTargetMethod::MinDistance, 0, RangeSelector(maxDist,
                // playerOnly, isInLos, minDist)) at the pin: the threat list sorted by
                // distance ascending and its FRONT taken - the closest unit within
                // [minDist, maxDist] (a maxDist of 0 accepts any distance), in line
                // of sight when asked.
                LocalRealmPlayer* best=nullptr;float bestD=0;
                for(auto* p:threatPlayers(0,r.t2!=0,true)) {
                    if(r.t1&&!npcInRange(n,*p,float(r.t4),float(r.t1)))continue;
                    if(r.t3&&!collision.isInLineOfSight(n.mapId,n.x,n.y,n.z+2,p->x,p->y,p->z+2,false))continue;
                    const float d=distance2(*p,n);if(!best||d<bestD){best=p;bestD=d;}
                }
                if(best)out.push_back({best,nullptr});
                break;}
            case kLocalSmartTargetPlayerWithAura:
                // spellId, negation, distMax, distMin: living players within the
                // range whose HasAura(spell) differs from the negation flag.
                for(auto* p:players)if(alive(p)&&npcInRange(n,*p,float(r.t4),float(r.t3))&&(r.t2!=0)!=playerAuraPresent(*p,r.t1))out.push_back({p,nullptr});
                break;
            // ---- 2.38 selectors
            case kLocalSmartTargetOwnerOrSummoner:
                // The creature's summoner (TempSummon::GetSummonerUnit).
                if(auto* m=npc(n.npcSummoner);m&&!m->npcUnsummoned)out.push_back({nullptr,m});
                else if(auto* p=player(n.npcSummoner,players))out.push_back({p,nullptr});
                break;
            case kLocalSmartTargetSummonedCreatures:
                // SmartScript's summon list, filtered by entry.
                for(const auto guid:n.smartSummons)if(auto* m=npc(guid);m&&!m->npcUnsummoned&&(!r.t1||m->entry==r.t1))out.push_back({nullptr,m});
                break;
            case kLocalSmartTargetPosition:case kLocalSmartTargetRandomPoint:break; // places, not units
            default:break;
        }
        return out;
    }
    /// Creature selectors: (minDist, maxDist, livingState). SMART_TARGET_CREATURE_RANGE
    /// carries entry, minDist, maxDist, livingState; _DISTANCE entry, dist,
    /// livingState; CLOSEST_CREATURE entry, dist, dead. The generated row
    /// carries all four target parameters (t1..t4) since 2.37.
    std::tuple<float,float,uint32_t> smartTargetParams(const LocalNpcSpellProfile& r) const {
        switch(r.target) {
            case kLocalSmartTargetCreatureRange:return {float(r.t2),float(r.t3),r.t4};
            case kLocalSmartTargetCreatureDistance:return {0.f,float(r.t2),r.t3};
            // GetClosestCreatureWithEntry(entry, dist, alive = !dead): the
            // living state follows the dead flag (2.37 fix: the flag used to
            // select the living).
            case kLocalSmartTargetClosestCreature:return {0.f,r.t2?float(r.t2):100.f,r.t3?2u:1u};
            default:return {0.f,100.f,0u};
        }
    }
    // ------------------------------------------------------------------ actions
    /// SMART_ACTION_CAST / SELF_CAST for one target. Returns 0 = cast started
    /// or landed, 1 = failed (SmartAI retries the row), 2 = skipped (no retry).
    int smartCastAt(LocalRealmNpc& n,const LocalNpcSpellProfile& r,const LocalSpellDefinition& d,SmartUnit target,
                    const std::vector<LocalRealmPlayer*>& players,bool silenced,bool stunned) {
        const auto* def=content->npc(n.entry);if(!def)return 2;
        const uint32_t flags=r.castFlags();
        const bool self=target.m==&n;
        // If target has the aura, skip.
        if(flags&kLocalSmartCastAuraNotPresent) {
            if(target.p&&npcDebuffPresent(target.p->guid,d.id))return 2;
            if(target.m&&npcBuffPresent(*target.m,d.id))return 2;
        }
        if(flags&kLocalSmartCastInterruptPrevious){if(n.npcCastingSpellId&&!n.npcSpellLaunched)localCancelNpcSpellCast(n);}
        const bool combatMove=(flags&kLocalSmartCastCombatMove)!=0;
        // SMART_ACTION_CAST's own range checks come before CastSpell: beyond the
        // maximum a COMBAT_MOVE row chases back to maxRange - 5; inside the
        // minimum any row closes to melee (SetCurrentRangeMode(true, 0)).
        if(target.p) {
            const int rangeStatus=npcSpellRange(n,*def,*target.p,d);
            if(rangeStatus==3)return 2;
            if(rangeStatus==1){if(combatMove)smartSetRangeMode(n,true,d.range-5.f);return 1;}
            if(rangeStatus==2){smartSetRangeMode(n,true,0.f);return 1;}
        } else if(target.m&&!self) {
            const float dist=std::sqrt(distance2(n.x,n.y,n.z,target.m->x,target.m->y,target.m->z));
            const float reach=localCreatureCombatReach(def)+localCreatureCombatReach(content->npc(target.m->entry));
            if(d.range>0&&dist>d.range+reach){if(combatMove)smartSetRangeMode(n,true,d.range-5.f);return 1;}
        }
        // Spell::CheckCast / CheckCasterAuras: SPELL_FAILED_SPELL_IN_PROGRESS is
        // not a failure; a stun, a silence of a silence-prevented spell or a
        // missing cost are. 2.38: a dead caster (the DEATH rows) casts only a
        // triggered spell or one castable while dead (SPELL_FAILED_CASTER_DEAD).
        if(n.npcCastingSpellId&&!(flags&kLocalSmartCastTriggered))return 2;
        const bool triggered=(flags&kLocalSmartCastTriggered)!=0;
        if(n.dead&&!triggered&&!d.npcCastableWhileDead)return 2;
        if(stunned){if(combatMove)smartSetRangeMode(n,false,0.f);return 1;}
        if(silenced&&d.preventionType==kLocalPreventionSilence){if(combatMove)smartSetRangeMode(n,false,0.f);return 1;}
        const uint32_t cost=triggered?0u:npcSpellCost(n,d);
        if(n.npcMana<cost){if(combatMove)smartSetRangeMode(n,false,0.f);return 1;}
        if(d.npcNextSwing&&target.p) {
            // Spell::prepare puts a next-swing special in CURRENT_MELEE_SPELL.
            n.npcNextSwingSpellId=d.id;n.npcNextSwingTargetGuid=target.p->guid;
            LOG_INFO("[LOCAL_NPC_SPELL] queue npc=",n.guid," entry=",n.entry," spell=",d.id," target=",target.p->guid);
            return 0;
        }
        n.npcCastingSpellId=d.id;n.npcCastTargetGuid=target.guid();
        n.npcCastRemainingMs=triggered?0u:d.castTimeMs;n.npcSpellLaunched=false;n.npcCastManaCost=cost;n.npcChanneling=false;
        LOG_INFO("[LOCAL_NPC_SPELL] start npc=",n.guid," entry=",n.entry," spell=",d.id," target=",n.npcCastTargetGuid," castMs=",n.npcCastRemainingMs);
        if(!n.npcCastRemainingMs)npcSpellLaunch(n,players);
        return 0;
    }
    /// SmartAI::SetCurrentRangeMode.
    void smartSetRangeMode(LocalRealmNpc& n,bool on,float distance){n.npcRangeMode=on;n.npcAttackDistance=std::max(0.f,distance);}
    /// Spell::GetMinMaxRange / CheckRange for the creature against a player: a
    /// SPELL_RANGE_MELEE entry uses the creature's own melee range, as its
    /// white swings do. Any other entry reaches its maximum plus both combat
    /// reaches; its minimum grows by the melee range (SPELL_RANGE_RANGED and
    /// the "npc shooter" rule, minimum <= 6 yd) or by both combat reaches.
    /// 0 = in range, 1 = too far, 2 = too close, 3 = no valid target.
    int npcSpellRange(const LocalRealmNpc& n,const LocalNpcDefinition& def,const LocalRealmPlayer& p,const LocalSpellDefinition& sp) {
        const float reach=localCreatureCombatReach(&def);
        if(sp.npcTargetShape==2||sp.npcTargetShape==4||sp.npcTargetShape==5) {
            // Caster-centred: the explicit target is only the range reference of a hostile shape.
            if(n.dead||!n.health||p.dead||!p.health||n.mapId!=p.mapId||n.instanceId!=p.instanceId)return 3;
            return 0;
        }
        if(sp.sourceRangeFlags&1u) {
            if(n.dead||!n.health||n.transportEntry||p.dead||!p.health||p.flight.active||n.mapId!=p.mapId||n.instanceId!=p.instanceId)return 3;
            return localWithinMeleeRange(distance2(p,n),reach,kLocalDefaultCombatReach)?0:1;
        }
        const float extraMin=sp.minRange<=0?0.f:((sp.sourceRangeFlags&2u)||sp.minRange<=6.f)?
            localMeleeRange(reach,kLocalDefaultCombatReach):reach+kLocalDefaultCombatReach;
        return localNpcSpellRangeStatus(n,p,sp,extraMin,reach+kLocalDefaultCombatReach);
    }
    uint32_t npcSpellCost(const LocalRealmNpc& n,const LocalSpellDefinition& d) {
        return localNpcSpellManaCostScaled(d.npcCostScales,d.spellLevel,n.level,d.mana,d.manaPercent,n.npcMaxMana);
    }
    /// SmartScript::ProcessAction: runOnce, the chance roll, the invoker, the
    /// targets, the action, then the linked row. Returns false when a cast
    /// failed and the row was rescheduled (RetryLater: no link either).
    bool smartProcessAction(LocalRealmNpc& n,size_t index,const LocalNpcSpellProfile& r,uint64_t invoker,
                            const std::vector<LocalRealmPlayer*>& players,bool silenced,bool stunned,bool fromList,unsigned depth=0,
                            uint32_t var0=0,uint32_t var1=0) {
        if(!fromList)n.npcSpellDoneMask|=uint32_t(1u)<<index;
        if(r.chance<100&&meleeRoll(99)>=r.chance)return true;
        if(invoker)n.smartInvoker=invoker;
        const uint64_t lastInvoker=n.smartInvoker;
        auto targets=smartTargets(n,r,lastInvoker,players);
        bool retry=false;
        switch(r.action) {
            case kLocalSmartActionCast:case kLocalSmartActionSelfCast:{
                const auto* d=content->spell(r.spellId());
                if(targets.empty()||!npcSpellUsable(d))break;
                if(r.a4&&targets.size()>r.a4){std::shuffle(targets.begin(),targets.end(),meleeRandom);targets.resize(r.a4);}
                bool failed=false,succeeded=false;
                for(const auto& t:targets) {
                    const int result=smartCastAt(n,r,*d,t,players,silenced,stunned);
                    if(result==1)failed=true;else if(result==0)succeeded=true;
                    if(result==0&&n.npcCastingSpellId&&!(r.castFlags()&kLocalSmartCastTriggered))break; // one cast bar at a time
                }
                if(failed&&!succeeded)retry=true;
                break;}
            case kLocalSmartActionAutoAttack:n.npcAutoAttack=r.a1!=0;break;
            case kLocalSmartActionCombatMove:n.npcCombatMove=r.a1!=0;break;
            case kLocalSmartActionSetPhase:smartSetPhase(n,r.a1,players);break;
            case kLocalSmartActionIncPhase:{uint32_t phase=std::min<uint32_t>(12,n.smartPhase+r.a1);phase=r.a2>=phase?0:phase-r.a2;smartSetPhase(n,phase,players);break;}
            case kLocalSmartActionRandomPhase:{uint32_t options[4]={r.a1,r.a2,r.a3,r.a4};std::vector<uint32_t> valid;for(auto o:options)if(o)valid.push_back(o);if(!valid.empty())smartSetPhase(n,valid[meleeRoll(uint32_t(valid.size()-1))],players);break;}
            case kLocalSmartActionRandomPhaseRange:smartSetPhase(n,smartRoll(r.a1,r.a2),players);break;
            case kLocalSmartActionEvade:n.smartEvadeRequested=true;break;
            case kLocalSmartActionRemoveAuras:
                for(const auto& t:targets) {
                    if(t.m)npcEraseBuffs(*t.m,[&](const auto& b){return b.spellId==r.a1;});
                    if(t.p)std::erase_if(npcPeriodic,[&](const auto& a){const bool gone=a.target==t.p->guid&&a.spell==r.a1;if(gone)npcControlEnded(a,t.p);return gone;});
                }
                break;
            case kLocalSmartActionCallForHelp:n.smartCallForHelpRange=float(r.a1);n.smartCallForHelpEmote=r.a2!=0;break;
            case kLocalSmartActionAttackStart:
                if(!targets.empty()) {
                    const auto& t=targets[meleeRoll(uint32_t(targets.size()-1))];
                    if(t.p&&!n.targetGuid&&npcCanAttack(*t.p,n)){addThreat(n,t.p->guid,1);n.targetGuid=t.p->guid;}
                }
                break;
            case kLocalSmartActionTimedList:{
                // SmartAI::SetScript9 on each creature target (2.39), else the
                // creature itself: never from within a list; a running list is
                // kept unless the call allows an override.
                if(fromList)break;
                const auto list=localNpcSmartList(r.a1);
                if(list.first==list.second)break;
                const auto start=[&](LocalRealmNpc& m){
                    if(m.smartListId&&!r.a3)return;
                    if(!m.npcSpellTimerInitialized&&smartRowsUsable(m))smartEnsureInit(m,players);
                    m.smartListId=r.a1;m.smartListIndex=0;m.smartListTimerType=uint8_t(std::min<uint32_t>(r.a2,2));
                    m.smartListTimer=smartRoll(list.first->p1,list.first->p2);m.smartInvoker=lastInvoker;};
                if(r.target==kLocalSmartTargetSelf||r.target==kLocalSmartTargetNone)start(n);
                else for(const auto& t:targets)if(t.m&&!t.m->dead)start(*t.m);
                break;}
            case kLocalSmartActionInterrupt:
                for(const auto& t:targets) {
                    if(t.m==&n){if(n.npcCastingSpellId&&(!n.npcSpellLaunched||n.npcChanneling)&&(!r.a2||n.npcCastingSpellId==r.a2))npcCancelChannelOrCast(n);}
                    else if(t.p&&t.p->castingSpellId&&(!r.a2||t.p->castingSpellId==r.a2))clearCast(*t.p,LocalCastStatus::Interrupted);
                }
                break;
            // ---- 2.37 actions
            case kLocalSmartActionTalk:smartTalk(n,r,targets,lastInvoker,players);break;
            case kLocalSmartActionSetFaction:
                // SetFaction(id), or the template's faction back for 0.
                for(const auto& t:targets)if(t.m)t.m->npcFactionOverride=r.a1;
                break;
            case kLocalSmartActionSetReactState:for(const auto& t:targets)if(t.m)t.m->npcReactState=uint8_t(std::min<uint32_t>(r.a1,2));break;
            case kLocalSmartActionThreatSinglePct:for(const auto& t:targets)if(t.p)modifyThreatPct(n,t.p->guid,std::max<int32_t>(-100,int32_t(r.a1)-int32_t(r.a2)));break;
            case kLocalSmartActionThreatAllPct:for(const auto& e:n.threat)if(e.guid)modifyThreatPct(n,e.guid,std::max<int32_t>(-100,int32_t(r.a1)-int32_t(r.a2)));break;
            case kLocalSmartActionSetUnitFlag:case kLocalSmartActionRemoveUnitFlag:
                // type 0 = UNIT_FIELD_FLAGS (the generator installs no other).
                for(const auto& t:targets)if(t.m&&!r.a2)npcSetUnitFlags(*t.m,r.a1,r.action==kLocalSmartActionSetUnitFlag);
                break;
            case kLocalSmartActionCombatStop:
                // Unit::CombatStop(true) with evade suppressed: the victim, the
                // threat list and the cast go; no EVADE / reset runs.
                npcCancelChannelOrCast(n);n.npcNextSwingSpellId=0;n.npcNextSwingTargetGuid=0;
                n.targetGuid=0;n.threat={};n.talkEngaged=false;n.smartCombatStopped=true;
                break;
            case kLocalSmartActionCallKilledMonster:smartKilledMonster(n,r,targets,lastInvoker,players);break;
            case kLocalSmartActionDie:
                // KillSelf now, or after the delay (Unit::Kill(me, me)).
                if(r.a1){n.npcDiePending=true;n.npcDieDelayMs=r.a1;}
                else npcKillSelf(n,players);
                break;
            case kLocalSmartActionForceDespawn:
                // DespawnOrUnsummon(delay, forceRespawnTimer): at least one tick later.
                for(const auto& t:targets)if(t.m){t.m->npcDespawnPending=true;t.m->npcDespawnDelayMs=std::max<uint32_t>(1,r.a1);t.m->npcDespawnRespawnSeconds=r.a2;}
                break;
            case kLocalSmartActionSetInvincibilityHp:
                for(const auto& t:targets)if(t.m)t.m->npcInvincibleHp=r.a2?uint32_t(uint64_t(t.m->maxHealth)*std::min<uint32_t>(r.a2,100)/100):r.a1;
                break;
            case kLocalSmartActionSetData:
                for(const auto& t:targets)if(t.m&&!t.m->scriptActorId)smartSetData(*t.m,r.a1,r.a2,n.guid,players);
                break;
            case kLocalSmartActionKillUnit:
                for(const auto& t:targets){if(t.m)npcKillSelf(*t.m,players);else if(t.p)killPlayerByScript(*t.p,players);}
                break;
            case kLocalSmartActionSetCounter:
                // counterId, value, reset, subtract - on the creature targets,
                // or the script's own counters without a target.
                if(targets.empty())smartStoreCounter(n,r.a1,r.a2,r.a3!=0,r.a4!=0,players);
                else for(const auto& t:targets)if(t.m&&!t.m->scriptActorId)smartStoreCounter(*t.m,r.a1,r.a2,r.a3!=0,r.a4!=0,players);
                break;
            case kLocalSmartActionStoreTargetList:{
                std::vector<uint64_t> guids;for(const auto& t:targets)guids.push_back(t.guid());
                auto it=std::find_if(n.smartStoredTargets.begin(),n.smartStoredTargets.end(),[&](const auto& stored){return stored.first==r.a1;});
                if(it!=n.smartStoredTargets.end())it->second=std::move(guids);
                else if(n.smartStoredTargets.size()<16)n.smartStoredTargets.push_back({r.a1,std::move(guids)});
                break;}
            case kLocalSmartActionCreateTimedEvent:{
                // id, min, max, repeatMin, repeatMax, chance: an UPDATE event whose
                // action fires TIMED_EVENT_TRIGGERED(id); NOT_REPEATABLE without a repeat.
                if(n.smartTimedEvents.size()>=16)break;
                LocalRealmNpc::SmartTimedEvent ev;ev.id=r.a1;ev.timerMs=smartRoll(r.a2,r.a3);ev.repeatMinMs=r.a4;ev.repeatMaxMs=r.a5;
                ev.chance=uint8_t(r.a6?std::min<uint32_t>(r.a6,100):100);ev.once=!r.a4&&!r.a5;
                n.smartTimedEvents.push_back(ev);
                break;}
            case kLocalSmartActionTriggerTimedEvent:
                smartFireEvents(n,kLocalSmartEventTimedEventTriggered,0,players,r.a1);
                // xinef: a NOT_REPEATABLE row removes the stored event of its id.
                if(r.once())std::erase_if(n.smartTimedEvents,[&](const auto& ev){return ev.id==r.a1;});
                break;
            case kLocalSmartActionRemoveTimedEvent:std::erase_if(n.smartTimedEvents,[&](const auto& ev){return ev.id==r.a1;});break;
            case kLocalSmartActionTriggerRandomTimedEvent:smartFireEvents(n,kLocalSmartEventTimedEventTriggered,0,players,smartRoll(r.a1,r.a2));break;
            case kLocalSmartActionAddAura:
                // Unit::AddAura(spell, target): the aura part alone, no cast
                // time, cost, range or hit roll; the creature stands as caster.
                if(const auto* d=content->spell(r.spellId());npcSpellUsable(d))for(const auto& t:targets) {
                    if(t.m)npcApplyPositive(n,*t.m,*d,players,true);
                    else if(t.p&&npcSpellHasAura(*d)&&!t.p->dead&&t.p->health)npcAuraApply(n,*t.p,*d,players,false,false);
                }
                break;
            case kLocalSmartActionCallScriptReset:smartOnReset(n,players);break;
            case kLocalSmartActionSetRangedMovement:
                // MoveChase(victim, distance, angle) on the creature targets that
                // have a victim: the chase distance (the angle is not modelled).
                for(const auto& t:targets)if(t.m&&t.m->targetGuid&&t.m->npcCombatMove)smartSetRangeMode(*t.m,r.a1>0,float(r.a1));
                break;
            case kLocalSmartActionSetHealthRegen:for(const auto& t:targets)if(t.m)t.m->npcRegenDisabled=r.a1==0;break;
            case kLocalSmartActionSetPower:case kLocalSmartActionAddPower:case kLocalSmartActionRemovePower:
                for(const auto& t:targets)smartSetPower(t,r.a1,r.a2,r.action==kLocalSmartActionSetPower?0:r.action==kLocalSmartActionAddPower?1:2);
                break;
            case kLocalSmartActionSetCorpseDelay:for(const auto& t:targets)if(t.m)t.m->npcCorpseDelayOverride=r.a1;break;
            case kLocalSmartActionDisableEvade:n.npcEvadeDisabled=r.a1!=0;break;
            case kLocalSmartActionSetSightDist:for(const auto& t:targets)if(t.m)t.m->npcSightDistance=r.a1;break;
            case kLocalSmartActionAddThreat:
                // ThreatMgr::AddThreat(target, threatINC - threatDEC) in whole points.
                for(const auto& t:targets)if(t.p&&npcCanAttack(*t.p,n)) {
                    const int64_t delta=(int64_t(r.a1)-int64_t(r.a2))*1000;
                    if(delta>=0)addThreat(n,t.p->guid,uint64_t(delta));
                    else for(auto& e:n.threat)if(e.guid==t.p->guid)e.amount=uint64_t(std::max<int64_t>(0,int64_t(e.amount)+delta));
                    if(!n.targetGuid)selectThreatTarget(n,players);
                }
                break;
            case kLocalSmartActionSetHealthPct:
                for(const auto& t:targets) {
                    if(t.m&&!t.m->dead)t.m->health=std::clamp<uint32_t>(uint32_t(uint64_t(t.m->maxHealth)*std::min<uint32_t>(r.a1,100)/100),1u,t.m->maxHealth);
                    else if(t.p&&!t.p->dead)t.p->health=std::clamp<uint32_t>(uint32_t(uint64_t(t.p->maxHealth)*std::min<uint32_t>(r.a1,100)/100),1u,t.p->maxHealth);
                }
                break;
            case kLocalSmartActionSetCombatDistance:for(const auto& t:targets)if(t.m)t.m->npcCombatDistance=r.a1;break;
            case kLocalSmartActionAddImmunity:case kLocalSmartActionRemoveImmunity:
                // ApplySpellImmune(id, type, value, apply): type 2 school, 3
                // damage (school masks), 5 mechanic, 6 spell id.
                for(const auto& t:targets)if(t.m)npcApplyImmunity(*t.m,r.a2,r.a3,r.action==kLocalSmartActionAddImmunity);
                break;
            case kLocalSmartActionSetEventFlagReset:n.smartPhaseResetDisabled=r.a1==0;break;
            case kLocalSmartActionAttackStop:
                // Unit::AttackStop on the targets: a creature drops its victim
                // (the threat list re-selects one), a character its auto-attack.
                for(const auto& t:targets){if(t.m){t.m->targetGuid=0;t.m->npcNextSwingSpellId=0;}else if(t.p){t.p->attackTarget=0;localStopRangedAuto(*t.p);}}
                break;
            case kLocalSmartActionFlee:
                // SMART_ACTION_FLEE_FOR_ASSIST: Creature::DoFleeToGetAssistance
                // with the emote - the tick runs the creature-talk companion's
                // flee (an assistant within 30 yd in line of sight, else a timed
                // flight) for this request.
                n.smartFleeRequested=true;n.smartFleeEmote=r.a1!=0;
                break;
            // ---- 2.38 actions
            case kLocalSmartActionSummonCreature:{
                // creature, TempSummonType, duration, attackInvoker (2: the
                // invoker itself, 1: the target), attackScriptOwner (creature
                // against creature: not modelled): at each target's position
                // plus the row offset, at random points, or at the row position.
                const auto attack=[&](LocalRealmNpc* s,LocalRealmPlayer* t){
                    if(!s)return;
                    LocalRealmPlayer* victim=r.a4==2?player(lastInvoker,players):r.a4?t:nullptr;
                    if(victim&&!victim->dead&&npcCanAttack(*victim,*s)){addThreat(*s,victim->guid,1);s->targetGuid=victim->guid;}
                };
                if(r.target==kLocalSmartTargetRandomPoint) {
                    for(uint32_t i=0;i<r.t2&&i<10;++i) {
                        float x,y,z;
                        if(r.t3)npcRandomPoint(n.x,n.y,n.z,float(r.t1),x,y,z);else npcRandomPoint(r.x,r.y,r.z,float(r.t1),x,y,z);
                        attack(npcSummon(n,r.a1,x,y,z,n.orientation,uint8_t(r.a2),r.a3,0,false,players),player(lastInvoker,players));
                    }
                    break;
                }
                for(const auto& t:targets) {
                    float x=t.p?t.p->x:t.m->x,y=t.p?t.p->y:t.m->y,z=t.p?t.p->z:t.m->z,o=t.p?t.p->orientation:t.m->orientation;
                    attack(npcSummon(n,r.a1,x+r.x,y+r.y,z+r.z,o+r.o,uint8_t(r.a2),r.a3,0,false,players),r.a4==2?player(lastInvoker,players):t.p);
                }
                if(r.target==kLocalSmartTargetPosition)attack(npcSummon(n,r.a1,r.x,r.y,r.z,r.o,uint8_t(r.a2),r.a3,0,false,players),player(lastInvoker,players));
                break;}
            case kLocalSmartActionFollow:
                // SmartAI::SetFollow on the first unit target; none / self stops.
                if(r.target==kLocalSmartTargetNone||r.target==kLocalSmartTargetSelf){smartStopFollow(n,false,players);break;}
                for(const auto& t:targets)if(t.guid()!=n.guid) {
                    const float angle=r.a2>6?float(r.a2)*3.14159265f/180.f:float(r.a2);
                    smartSetFollow(n,t.guid(),float(r.a1)+0.1f,angle,r.a4,r.a3,uint8_t(r.a5),r.a6!=0);
                    break;
                }
                break;
            case kLocalSmartActionSetInCombatWithZone:
                if(!targets.empty())smartSetInCombatWithZone(n,float(r.t2),players);
                break;
            case kLocalSmartActionMoveForward:{
                // GetClosePoint at the distance in the facing direction.
                const float x=n.x+float(r.a1)*std::cos(n.orientation),y=n.y+float(r.a1)*std::sin(n.orientation);
                npcMovePoint(n,kLocalSmartRandomPoint,x,y,n.z);
                break;}
            case kLocalSmartActionSetVisibility:
                for(const auto& t:targets)if(t.m)t.m->npcHidden=r.a1==0;
                break;
            case kLocalSmartActionEscortStart:{
                // run/walk, pathId, canRepeat, quest, despawnTime, reactState;
                // player targets become the escort's own targets.
                bool anyPlayer=false;for(const auto& t:targets)if(t.p)anyPlayer=true;
                if(anyPlayer) {
                    std::vector<uint64_t> guids;for(const auto& t:targets)guids.push_back(t.guid());
                    auto it=std::find_if(n.smartStoredTargets.begin(),n.smartStoredTargets.end(),[&](const auto& stored){return stored.first==kLocalSmartEscortTargets;});
                    if(it!=n.smartStoredTargets.end())it->second=std::move(guids);
                    else if(n.smartStoredTargets.size()<16)n.smartStoredTargets.push_back({kLocalSmartEscortTargets,std::move(guids)});
                }
                n.npcReactState=uint8_t(std::min<uint32_t>(r.a6,2));
                smartEscortStart(n,uint8_t(r.a1==3?2:r.a1),r.a2,r.a3!=0,lastInvoker,players);
                n.escortQuestId=r.a4;smartSetDespawnTime(n,r.a5);
                break;}
            case kLocalSmartActionEscortPause:
                // Not forced from an ESCORT_REACHED row (the creature stands at the point).
                smartEscortPause(n,r.a1,r.event!=kLocalSmartEventEscortReached,players);
                break;
            case kLocalSmartActionEscortStop:smartEscortStop(n,r.a1,r.a2,r.a3!=0,players);break;
            case kLocalSmartActionEscortResume:n.escortPauseMs=0;break;
            case kLocalSmartActionMoveToPos:{
                // pointId, transport, controlled, contactDistance, combatReach,
                // disableForceDestination: the row position, a random point, or
                // a random unit target's spot plus the row offset.
                if(r.target==kLocalSmartTargetPosition){npcMovePoint(n,r.a1,r.x,r.y,r.z);break;}
                if(r.target==kLocalSmartTargetRandomPoint){float x,y,z;npcRandomPoint(r.x,r.y,r.z,float(r.t1),x,y,z);npcMovePoint(n,r.a1,x,y,z);break;}
                if(targets.empty())break;
                const auto& t=targets[meleeRoll(uint32_t(targets.size()-1))];
                float x=t.p?t.p->x:t.m->x,y=t.p?t.p->y:t.m->y,z=t.p?t.p->z:t.m->z;
                const float contact=r.a5?(t.p?kLocalDefaultCombatReach:localCreatureCombatReach(content->npc(t.m->entry)))+float(r.a4):float(r.a4);
                if(contact>0) {
                    // GetNearPoint(me, contact, target->GetAngle(me)): the spot at
                    // the contact distance on the target's side facing the creature.
                    const float dx=n.x-x,dy=n.y-y,len=std::sqrt(dx*dx+dy*dy);
                    if(len>0.001f){x+=dx/len*contact;y+=dy/len*contact;}
                }
                npcMovePoint(n,r.a1,x+r.x,y+r.y,z+r.z);
                break;}
            // ---- 2.39
            case kLocalSmartActionWaypointStart:
                // pathId, repeat: LoadPath + MoveWaypoint on the creature targets.
                if(r.a1)for(const auto& t:targets)if(t.m)npcPatrolStart(*t.m,r.a1,r.a2!=0,0);
                break;
            case kLocalSmartActionWaypointDataRandom:
                // pathId1, pathId2, repeat: a random path of the range.
                if(r.a1&&r.a2>=r.a1)for(const auto& t:targets)if(t.m)npcPatrolStart(*t.m,r.a1+meleeRoll(r.a2-r.a1),r.a3!=0,0);
                break;
            case kLocalSmartActionMovementStop:
                for(const auto& t:targets)if(t.m)npcStopMoving(*t.m);
                if(targets.empty())npcStopMoving(n);
                break;
            case kLocalSmartActionMovementPause:
                for(const auto& t:targets)if(t.m)npcMovementPause(*t.m,r.a1);
                if(targets.empty())npcMovementPause(n,r.a1);
                break;
            case kLocalSmartActionMovementResume:
                for(const auto& t:targets)if(t.m)npcMovementResume(*t.m,r.a1);
                if(targets.empty())npcMovementResume(n,r.a1);
                break;
            case kLocalSmartActionDoAction:{
                // actionId (negated with isNegative): SmartAI::DoAction on the
                // creature targets -> their ACTION_DONE rows.
                const uint32_t actionId=r.a3?uint32_t(-int32_t(r.a1)):r.a1;
                for(const auto& t:targets)if(t.m&&!t.m->dead)smartFireEvents(*t.m,kLocalSmartEventActionDone,0,players,actionId,0,depth+1);
                break;}
            case kLocalSmartActionSetInstData:case kLocalSmartActionSetInstData64:
                // InstanceScript::SetData(field, data): kept per map instance
                // for the record; no instance script consumes it locally.
                smartSetInstanceData(n,r.a1,r.a2);
                break;
            // ---- 2.40 gossip and quest actions
            case kLocalSmartActionCloseGossip:
                // PlayerTalkClass->SendCloseGossip at every player target.
                for(const auto& t:targets)if(t.p)gossipClose(*t.p);
                break;
            case kLocalSmartActionSendGossipMenu:
                // (menuId, npcTextId): the menu prepared with the quests, or
                // the menus cleared, then the text shown from this creature.
                for(const auto& t:targets)if(t.p)gossipSendMenu(*t.p,n,r.a1,r.a2);
                break;
            case kLocalSmartActionSetGossipMenu:
                for(const auto& t:targets)if(t.m){t.m->npcGossipMenuOverridden=true;t.m->npcGossipMenuOverride=r.a1;}
                break;
            case kLocalSmartActionOfferQuest:
                for(const auto& t:targets)if(t.p)gossipOfferQuest(*t.p,n,r.a1,r.a2!=0);
                break;
            case kLocalSmartActionFailQuest:
                for(const auto& t:targets)if(t.p)gossipFailQuest(*t.p,r.a1);
                break;
            case kLocalSmartActionAddItem:
                // Player::AddItem(entry, count): the catalog's item into the
                // player's bags (a full bag keeps it, as StoreNewItem refuses).
                for(const auto& t:targets)if(t.p&&!t.p->dead&&content) {
                    if(addItem(*t.p,*content,r.a1,std::max<uint32_t>(1,r.a2))){questStatus(*t.p,*content);LOG_INFO("[LOCAL_SMART] add item player=",t.p->guid," item=",r.a1," count=",std::max<uint32_t>(1,r.a2));}
                }
                break;
            case kLocalSmartActionRemoveItem:
                // Player::DestroyItemCount(entry, count): from the bags.
                for(const auto& t:targets)if(t.p&&content){removeItem(*t.p,r.a1,std::max<uint32_t>(1,r.a2));questStatus(*t.p,*content);}
                break;
            case kLocalSmartActionSetNpcFlag:case kLocalSmartActionAddNpcFlag:case kLocalSmartActionRemoveNpcFlag:
                for(const auto& t:targets)if(t.m) {
                    const uint32_t current=npcEffectiveFlags(*t.m);
                    const uint32_t next=r.action==kLocalSmartActionSetNpcFlag?r.a1:r.action==kLocalSmartActionAddNpcFlag?(current|r.a1):(current&~r.a1);
                    npcSetFlags(*t.m,next);
                }
                break;
            case kLocalSmartActionCrossCast:{
                // spell, castFlags, the casters (a creature selector) cast at the
                // row's targets (SMARTCAST_AURA_NOT_PRESENT skips carriers).
                const auto* d=content->spell(r.a1);
                if(targets.empty()||!npcSpellUsable(d))break;
                auto casterRow=r;casterRow.target=r.a3;casterRow.t1=r.a4;casterRow.t2=r.a5;casterRow.t3=r.a6;casterRow.t4=0;
                auto castRow=r;castRow.action=kLocalSmartActionCast;castRow.a2=r.a2;castRow.a4=0;
                for(const auto& c:smartTargets(n,casterRow,lastInvoker,players)) {
                    if(!c.m||c.m->dead)continue;
                    for(const auto& t:targets) {
                        const int result=smartCastAt(*c.m,castRow,*d,t,players,silenced,stunned);
                        if(result==0&&c.m->npcCastingSpellId&&!(r.a2&kLocalSmartCastTriggered))break;
                    }
                }
                break;}
            case kLocalSmartActionSendTargetToTarget:{
                // id: the creature's stored list copied to the creature targets' scripts.
                const auto stored=std::find_if(n.smartStoredTargets.begin(),n.smartStoredTargets.end(),[&](const auto& e){return e.first==r.a1;});
                if(stored==n.smartStoredTargets.end())break;
                const auto guids=stored->second;
                for(const auto& t:targets)if(t.m) {
                    auto it=std::find_if(t.m->smartStoredTargets.begin(),t.m->smartStoredTargets.end(),[&](const auto& e){return e.first==r.a1;});
                    if(it!=t.m->smartStoredTargets.end())it->second=guids;
                    else if(t.m->smartStoredTargets.size()<16)t.m->smartStoredTargets.push_back({r.a1,guids});
                }
                break;}
            case kLocalSmartActionSetMovementSpeed:{
                // movementType (0 walk, 1 run), speedInteger, speedFraction: Unit::SetSpeed's rate.
                float fraction=float(r.a3);while(fraction>=1.f)fraction/=10.f;
                const float rate=std::clamp(float(r.a2)+fraction,0.f,50.f);
                for(const auto& t:targets)if(t.m){if(r.a1)t.m->npcRunSpeedRate=rate;else t.m->npcWalkSpeedRate=rate;}
                break;}
            case kLocalSmartActionStopMotion:
                // stopMoving, movementExpired (the active generator ends).
                for(const auto& t:targets)if(t.m){if(r.a1)npcStopMoving(*t.m);if(r.a2&&(t.m->npcMotion==1||t.m->npcMotion==5||t.m->npcMotion==3))npcMoveIdle(*t.m);}
                if(targets.empty()){if(r.a1)npcStopMoving(n);if(r.a2&&(n.npcMotion==1||n.npcMotion==5||n.npcMotion==3))npcMoveIdle(n);}
                break;
            case kLocalSmartActionMoveToPosTarget:
                // pointId: the creature targets walk to the row position.
                for(const auto& t:targets)if(t.m)npcMovePoint(*t.m,r.a1,r.x,r.y,r.z);
                break;
            case kLocalSmartActionRandomMove:{
                // MoveRandom(distance) on the creature targets, or the creature
                // itself without one; 0 stops (MoveIdle).
                // MoveRandom: a RandomMovementGenerator(distance) initialised
                // where the creature stands (not a spawned creature's delay).
                const auto moveRandom=[&](LocalRealmNpc& m){
                    if(!r.a1){npcMoveIdle(m);return;}
                    m.npcMotion=3;m.npcRandomDistance=r.a1;npcRandomInitialize(m,float(r.a1));m.npcRandomNextMoveMs=0;};
                bool found=false;
                for(const auto& t:targets)if(t.m){found=true;moveRandom(*t.m);}
                if(!found&&!n.dead)moveRandom(n);
                break;}
            case kLocalSmartActionJumpToPos:{
                // speedXY, speedZ, selfJump: the creature jumps to the row point,
                // a random point, or a random target's spot plus the offset.
                if(r.target==kLocalSmartTargetRandomPoint){float x,y,z;npcRandomPoint(r.x,r.y,r.z,float(r.t1),x,y,z);npcMoveJump(n,x,y,z,float(r.a1));break;}
                if(r.target==kLocalSmartTargetPosition){npcMoveJump(n,r.x,r.y,r.z,float(r.a1));break;}
                if(targets.empty())break;
                if(r.a3) {
                    const auto& t=targets[meleeRoll(uint32_t(targets.size()-1))];
                    npcMoveJump(n,(t.p?t.p->x:t.m->x)+r.x,(t.p?t.p->y:t.m->y)+r.y,(t.p?t.p->z:t.m->z)+r.z,float(r.a1));
                } else for(const auto& t:targets)if(t.m)npcMoveJump(*t.m,r.x,r.y,r.z,float(r.a1));
                break;}
            case kLocalSmartActionSetHomePos:
                // The creature targets' spawn (a1) or current position; the row
                // position for the creature itself.
                for(const auto& t:targets)if(t.m) {
                    if(r.a1){t.m->homeX=t.m->spawnX;t.m->homeY=t.m->spawnY;t.m->homeZ=t.m->spawnZ;}
                    else {t.m->homeX=t.m->x;t.m->homeY=t.m->y;t.m->homeZ=t.m->z;}
                }
                if(targets.empty()&&r.target==kLocalSmartTargetPosition) {
                    if(r.a1){n.homeX=n.spawnX;n.homeY=n.spawnY;n.homeZ=n.spawnZ;}
                    else {n.homeX=r.x;n.homeY=r.y;n.homeZ=r.z;n.orientation=r.o;}
                }
                break;
            case kLocalSmartActionSetRoot:for(const auto& t:targets)if(t.m)t.m->npcRooted=r.a1!=0;break;
            case kLocalSmartActionSetRun:
                // SetWalk(!run) on the creature (targets or itself).
                if(targets.empty())n.npcWalking=r.a1==0;
                for(const auto& t:targets)if(t.m)t.m->npcWalking=r.a1==0;
                break;
            case kLocalSmartActionSetOrientation:{
                // random facing; a turn by the angle; the home facing (self), the
                // row facing (position), or facing the first target.
                if(r.a2){n.orientation=float(meleeRoll(6283))/1000.f;break;}
                if(r.a3){n.orientation=n.orientation+float(r.a3)*3.14159265f/180.f;break;}
                if(r.target==kLocalSmartTargetSelf)n.orientation=n.spawnOrientation;
                else if(r.target==kLocalSmartTargetPosition)n.orientation=r.o;
                else if(!targets.empty()){const auto& t=targets.front();const float tx=t.p?t.p->x:t.m->x,ty=t.p?t.p->y:t.m->y;n.orientation=std::atan2(ty-n.y,tx-n.x);}
                break;}
            case kLocalSmartActionCallRandomTimedList:case kLocalSmartActionCallRandomRangeTimedList:{
                // A random list of the ids (or of the range) on the creature
                // (SetScript9 keeps a running list unless allowOverride - here
                // the random calls override, as their action has no flag).
                if(fromList)break;
                uint32_t id=0;
                if(r.action==kLocalSmartActionCallRandomTimedList) {
                    uint32_t ids[6]={r.a1,r.a2,r.a3,r.a4,r.a5,r.a6};std::vector<uint32_t> valid;
                    for(auto v:ids)if(v)valid.push_back(v);
                    if(valid.empty())break;
                    id=valid[meleeRoll(uint32_t(valid.size()-1))];
                } else id=smartRoll(r.a1,r.a2);
                if(n.smartListId)break;
                const auto list=localNpcSmartList(id);
                if(list.first==list.second)break;
                n.smartListId=id;n.smartListIndex=0;n.smartListTimerType=0;
                n.smartListTimer=smartRoll(list.first->p1,list.first->p2);
                break;}
            // Cosmetic rows the runtime carries without a visible effect here
            // (sounds, emotes, sheath, run/walk, orientation, bytes, dynamic
            // flags, scale, spell visuals; REMOVE_AURAS_BY_TYPE has no handler
            // at the pin).
            case kLocalSmartActionSound:case kLocalSmartActionPlayEmote:case kLocalSmartActionRandomEmote:case kLocalSmartActionSetEmoteState:
            case kLocalSmartActionSetSheath:case kLocalSmartActionSetActive:
            case kLocalSmartActionSetUnitBytes1:case kLocalSmartActionRemoveUnitBytes1:case kLocalSmartActionSetDynamicFlag:case kLocalSmartActionAddDynamicFlag:
            case kLocalSmartActionRemoveDynamicFlag:case kLocalSmartActionRandomSound:case kLocalSmartActionRemoveAurasByType:case kLocalSmartActionSetScale:
            case kLocalSmartActionPlaySpellVisual:case kLocalSmartActionMount:case kLocalSmartActionSetFly:case kLocalSmartActionSetSwim:case kLocalSmartActionEquip:
            case kLocalSmartActionSetMovementFlags:case kLocalSmartActionSetHover:
                break;
            default:break;
        }
        (void)var0;(void)var1;
        if(retry) {
            // RetryLater: the row is re-armed for the next loop without a new
            // chance roll and may run again (runOnce cleared).
            if(!fromList){smartTimer(n,index)=1200;n.npcSpellDoneMask&=~(uint32_t(1u)<<index);}
            return false;
        }
        if(r.link&&r.link!=r.row&&!fromList&&depth<8) {
            const auto rows=smartRows(n);size_t j=0;
            for(const auto* l=rows.first;l!=rows.second;++l,++j)if(l->row==r.link) {
                if(l->event==kLocalSmartEventLink)smartProcessEvent(n,j,*l,invoker,players,silenced,stunned,depth+1);
                break;
            }
        }
        return true;
    }
    /// SmartScript::SetPhase, then the EVENT_PHASE_CHANGE rows whose mask
    /// holds the new phase.
    void smartSetPhase(LocalRealmNpc& n,uint32_t phase,const std::vector<LocalRealmPlayer*>& players) {
        const uint8_t before=n.smartPhase;
        n.smartPhase=uint8_t(std::min<uint32_t>(phase,12));
        if(n.smartPhase!=before)smartFireEvents(n,kLocalSmartEventEventPhaseChange,0,players);
    }
    /// SmartScript::OnReset: phase 0 (unless SET_EVENT_FLAG_RESET forbade it),
    /// timers re-armed and runOnce cleared except DONT_RESET rows, the RESET
    /// rows, then the invoker and the counters cleared.
    void smartOnReset(LocalRealmNpc& n,const std::vector<LocalRealmPlayer*>& players) {
        smartInit(n,true);
        smartFireEvents(n,kLocalSmartEventReset,0,players);
        n.smartInvoker=0;n.smartCounters.clear();
    }
    /// SmartAI::EnterEvadeMode's script side. With DISABLE_EVADE only the EVADE
    /// rows run and the creature keeps its state; otherwise EVADE, then - the
    /// creature being home at once here - SmartAI::JustReachedHome: OnReset and
    /// the REACHED_HOME rows. Returns whether the creature really evades.
    bool smartEvade(LocalRealmNpc& n,const std::vector<LocalRealmPlayer*>& players) {
        if(n.scriptActorId||!n.npcSpellTimerInitialized||!smartRowsUsable(n))return true;
        smartFireEvents(n,kLocalSmartEventEvade,0,players);
        if(n.npcEvadeDisabled)return false;
        // SmartAI::EnterEvadeMode: an escorting creature returns to its last
        // point (SMART_ESCORT_RETURNING) and resumes the path with its script
        // state intact; a following one resets its script and keeps following;
        // otherwise the way home, OnReset and JustReachedHome.
        npcSummonEvaded(n,players);
        if(n.escortActive){n.escortReturning=true;return true;}
        if(n.followGuid){smartOnReset(n,players);return true;}
        smartOnReset(n,players);
        smartFireEvents(n,kLocalSmartEventReachedHome,0,players);
        return true;
    }
    /// ThreatMgr::ModifyThreatByPercent.
    void modifyThreatPct(LocalRealmNpc& n,uint64_t guid,int32_t pct) {
        for(auto& e:n.threat)if(e.guid==guid)e.amount=uint64_t(std::max<int64_t>(0,int64_t(e.amount)*(100+pct)/100));
    }
    /// Unit::ApplySpellImmune(source, op, value, apply) on a creature.
    static void npcApplyImmunity(LocalRealmNpc& n,uint32_t type,uint32_t value,bool apply) {
        switch(type) {
            case 2:if(apply)n.npcSchoolImmunity|=uint8_t(value&127u);else n.npcSchoolImmunity&=uint8_t(~value);break;
            case 3:if(apply)n.npcDamageImmunity|=uint8_t(value&127u);else n.npcDamageImmunity&=uint8_t(~value);break;
            case 5:if(value<32){if(apply)n.npcMechanicImmunity|=1u<<value;else n.npcMechanicImmunity&=~(1u<<value);}break;
            case 6:
                if(apply){if(std::find(n.npcSpellImmunity.begin(),n.npcSpellImmunity.end(),value)==n.npcSpellImmunity.end()&&n.npcSpellImmunity.size()<16)n.npcSpellImmunity.push_back(value);}
                else std::erase(n.npcSpellImmunity,value);
                break;
            default:break;
        }
    }
    /// SET/ADD/REMOVE_POWER (Unit::SetPower) on a creature's mana or a
    /// character's own resource when the power type is its resource.
    void smartSetPower(const SmartUnit& t,uint32_t powerType,uint32_t amount,int mode) {
        if(t.m) {
            if(powerType!=0||!t.m->npcManaReady)return;
            const int64_t current=t.m->npcMana;
            const int64_t next=mode==0?int64_t(amount):mode==1?current+int64_t(amount):current-int64_t(amount);
            t.m->npcMana=uint32_t(std::clamp<int64_t>(next,0,t.m->npcMaxMana));
        } else if(t.p) {
            if(powerType!=uint32_t(t.p->resourceType))return;
            const int64_t current=t.p->mana;
            const int64_t next=mode==0?int64_t(amount):mode==1?current+int64_t(amount):current-int64_t(amount);
            t.p->mana=uint32_t(std::clamp<int64_t>(next,0,t.p->maxMana));
        }
    }
    /// SMART_ACTION_TALK: the talker is the creature itself (target none /
    /// self / a player / useTalkTarget) or the first creature target; the text
    /// group of the talker's entry is spoken to the talk target (the invoker
    /// without one). 2.38: the duration arms the script's text timer (the
    /// TEXT_OVER rows), the delay defers the speech (CreatureAI::Talk).
    void smartTalk(LocalRealmNpc& n,const LocalNpcSpellProfile& r,const std::vector<SmartUnit>& targets,uint64_t invoker,const std::vector<LocalRealmPlayer*>& players) {
        const LocalRealmNpc* talker=r.target==kLocalSmartTargetNone?&n:nullptr;
        const LocalRealmPlayer* talkTarget=nullptr;
        for(const auto& t:targets) {
            if(t.m){if(r.a3){talker=&n;}else talker=t.m;break;}
            if(t.p){talker=&n;talkTarget=t.p;break;}
        }
        if(!talkTarget)talkTarget=player(invoker,players);
        if(!talker)return;
        const uint8_t group=uint8_t(std::min<uint32_t>(r.a1,255));
        if(!content->creatureText(talker->entry,group))return;
        n.smartTalkerEntry=talker->entry;n.smartTextGroup=group;n.smartTextTimerMs=r.a2;n.smartTextTimerActive=true;
        if(r.a4) {
            if(n.smartPendingTalks.size()<16)n.smartPendingTalks.push_back({r.a4,group,talker->guid,talkTarget?talkTarget->guid:0});
            return;
        }
        smartSpeak(talker->guid,group,talkTarget?talkTarget->guid:0,players);
    }
    /// SMART_ACTION_CALL_KILLEDMONSTER: Player::RewardPlayerAndGroupAtEvent
    /// (creature, player) for the loot recipient (target none/self) or each
    /// player target: KilledMonsterCredit for the player and the living party
    /// members within the group reward distance (74 yd) of that player - the
    /// objective credit of a kill of the entry without a kill.
    void smartKilledMonster(LocalRealmNpc& n,const LocalNpcSpellProfile& r,const std::vector<SmartUnit>& targets,uint64_t invoker,const std::vector<LocalRealmPlayer*>& players) {
        (void)invoker;
        std::vector<LocalRealmPlayer*> rewarded;
        const auto withGroup=[&](LocalRealmPlayer* p){
            if(!p)return;
            const auto group=partyOf(p->guid);
            for(auto* member:players)if(member&&(member==p||(group&&partyOf(member->guid)==group&&!member->dead&&member->mapId==p->mapId&&member->instanceId==p->instanceId&&distance2(member->x,member->y,member->z,p->x,p->y,p->z)<=74*74)))
                if(std::find(rewarded.begin(),rewarded.end(),member)==rewarded.end())rewarded.push_back(member);
        };
        if(r.target==kLocalSmartTargetNone||r.target==kLocalSmartTargetSelf)withGroup(player(n.lootOwner,players));
        else for(const auto& t:targets)if(t.p)withGroup(t.p);
        for(auto* p:rewarded)npcKillCredit(*p,r.a1);
    }
    /// Player::KilledMonsterCredit(entry): the kill objective and NpcKill
    /// script triggers of the entry, no experience.
    void npcKillCredit(const LocalRealmPlayer& p,uint32_t entry) {
        if(!entry)return;
        pendingScriptCommits.push_back({p.guid,entry,0,1});
        LOG_INFO("[LOCAL_SMART] killcredit player=",p.guid," entry=",entry);
    }
    /// Unit::Kill(me, me): the creature dies by its script. With a loot
    /// recipient the death rewards as a kill by that player does; without
    /// one it is a plain death (no loot, no experience), the DEATH rows run.
    void npcKillSelf(LocalRealmNpc& n,const std::vector<LocalRealmPlayer*>& players) {
        if(n.dead)return;
        const auto* def=content->npc(n.entry);if(!def)return;
        n.npcDiePending=false;
        if(auto* credit=player(n.lootOwner,players);credit&&credit->mapId==n.mapId&&credit->instanceId==n.instanceId&&kill(n,*credit,players))return;
        npcCancelChannelOrCast(n);n.health=0;n.dead=true;n.combatEpoch=allocateNpcEpoch();n.targetGuid=0;n.threat={};
        n.snares.clear();releaseNpcControls(n);n.diminishing.clear();n.damageAuras.clear();n.stormstrikeAuras.clear();
        n.lootable=false;n.lootOwner=0;
        npcSummonerDied(n,players);
        smartFireEvents(n,kLocalSmartEventDeath,0,players);
        localResetNpcSpellState(n);npcClearBuffs(n);
        n.respawnTimer=def->respawnSeconds;respawnAt[n.guid]=now+def->respawnSeconds;
        for(auto* p:players)if(p) {
            if(p->attackTarget==n.guid)p->attackTarget=0;
            if(p->rangedTarget==n.guid)localStopRangedAuto(*p);
            if(p->comboTarget==n.guid)clearLocalCombo(*p);
        }
        emitCombatEvent(localDeathProcEvent(n.guid,n.mapId,n.instanceId,false,n.level),players);
        LOG_INFO("[LOCAL_SMART] die npc=",n.guid," entry=",n.entry);
    }
    /// Creature::DespawnOrUnsummon's ForcedDespawn: the creature dies without
    /// a kill (no DEATH rows, no loot), the corpse goes at once
    /// (RemoveCorpse: the CORPSE_REMOVED rows with the respawn delay), and it
    /// respawns after the forced timer or its template's delay.
    void npcForceDespawn(LocalRealmNpc& n,const std::vector<LocalRealmPlayer*>& players) {
        const auto* def=content->npc(n.entry);if(!def)return;
        n.npcDespawnPending=false;
        // Creature::DespawnOrUnsummon on a temporary summon: UnSummon.
        if(n.npcSummonType){npcUnsummon(n,players);return;}
        npcSummonerDied(n,players);
        if(!n.dead) {
            npcCancelChannelOrCast(n);n.health=0;n.dead=true;n.combatEpoch=allocateNpcEpoch();n.targetGuid=0;n.threat={};
            n.snares.clear();releaseNpcControls(n);n.diminishing.clear();n.damageAuras.clear();n.stormstrikeAuras.clear();
            localResetNpcSpellState(n);npcClearBuffs(n);
            for(auto* p:players)if(p) {
                if(p->attackTarget==n.guid)p->attackTarget=0;
                if(p->rangedTarget==n.guid)localStopRangedAuto(*p);
                if(p->comboTarget==n.guid)clearLocalCombo(*p);
            }
        }
        n.lootable=false;n.lootOwner=0;n.lootCandidates.fill(0);
        const uint32_t respawn=n.npcDespawnRespawnSeconds?n.npcDespawnRespawnSeconds:def->respawnSeconds;
        n.npcDespawnRespawnSeconds=0;
        n.respawnTimer=float(respawn);respawnAt[n.guid]=now+respawn;
        n.npcDespawned=true;
        smartFireEvents(n,kLocalSmartEventCorpseRemoved,0,players,respawn);
        LOG_INFO("[LOCAL_SMART] despawn npc=",n.guid," entry=",n.entry," respawn=",respawn);
    }
    /// Unit::Kill(player, player) from KILL_UNIT: the character dies where it
    /// stands, credited to nobody.
    void killPlayerByScript(LocalRealmPlayer& target,const std::vector<LocalRealmPlayer*>& players) {
        if(target.dead||!target.health)return;
        emitCombatEvent(localDeathProcEvent(target.guid,target.mapId,target.instanceId,true,target.level),players);
        target.health=0;localStopRangedAuto(target);leaveLocalForm(target);clearLocalCombo(target);
        target.dead=true;target.mountSpellId=0;target.deadTimer=0;target.attackTarget=0;
        clearCast(target,LocalCastStatus::Interrupted);clearLocalTravelMotion(target);localCaptureCorpse(target);
        for(auto& m:npcs)if(m.targetGuid==target.guid){for(auto& e:m.threat)if(e.guid==target.guid)e={};selectThreatTarget(m,players);}
    }
    /// SmartAI::KilledUnit: the creature-talk companion's KILL rows and the
    /// generated script's KILL rows (cooldown, playerOnly, creature entry).
    void npcKilledPlayer(LocalRealmNpc& n,const LocalRealmPlayer& victim,const std::vector<LocalRealmPlayer*>& players) {
        creatureTalk(n,LocalCreatureTalkEvent::Kill,&victim,0,players);
        smartFireEvents(n,kLocalSmartEventKill,victim.guid,players);
    }
    /// SmartScript::ProcessEvent's common gate for an event-driven or timed
    /// row: phase, runOnce; then the action.
    bool smartProcessEvent(LocalRealmNpc& n,size_t index,const LocalNpcSpellProfile& r,uint64_t invoker,
                           const std::vector<LocalRealmPlayer*>& players,bool silenced,bool stunned,unsigned depth=0,uint32_t var0=0,uint32_t var1=0) {
        if(!smartInPhase(n,r.phaseMask))return false;
        if(r.once()&&((n.npcSpellDoneMask>>index)&1u))return false;
        return smartProcessAction(n,index,r,invoker,players,silenced,stunned,false,depth,var0,var1);
    }
    /// SmartScript::ProcessEventsFor: every row of the event, in order, with
    /// the event's own parameter checks (SmartScript::ProcessEvent). var0/var1
    /// carry the spell id and school (SPELLHIT), the damage or heal amount
    /// (DAMAGED, DAMAGED_TARGET, RECEIVE_HEAL), the data id and value
    /// (DATA_SET), the event id (TIMED_EVENT_TRIGGERED), the counter id
    /// (COUNTER_SET), the quest id (quests) or the respawn delay (CORPSE_REMOVED).
    void smartFireEvents(LocalRealmNpc& n,uint32_t event,uint64_t invoker,const std::vector<LocalRealmPlayer*>& players,
                         uint32_t var0=0,uint32_t var1=0,unsigned depth=0) {
        if(n.scriptActorId||(n.dead&&event!=kLocalSmartEventDeath&&event!=kLocalSmartEventCorpseRemoved)||!smartRowsUsable(n)||depth>8)return;
        // A creature engaged in the tick it spawned in (AGGRO before its first
        // OnUpdate): SmartAI::InitializeAI / JustRespawned ran at creation, so
        // the script is initialised here first.
        if(!n.npcSpellTimerInitialized)smartEnsureInit(n,players);
        if(!n.npcSpellTimerInitialized)return;
        const auto rows=smartRows(n);size_t i=0;
        const bool stunned=localNpcStunned(n)||(npcUnitFlags(n)&kLocalUnitFlagStunned),silenced=localNpcSilenced(n)||(npcUnitFlags(n)&kLocalUnitFlagSilenced);
        const bool engaged=n.targetGuid||std::any_of(n.threat.begin(),n.threat.end(),[](const auto& e){return e.guid!=0;});
        for(const auto* r=rows.first;r!=rows.second;++r,++i) {
            if(r->event!=event)continue;
            auto& timer=smartTimer(n,i);
            switch(event) {
                case kLocalSmartEventSpellHit:case kLocalSmartEventSpellHitTarget:
                    if(timer)continue;
                    if((r->p1&&r->p1!=var0)||(r->p2&&!(r->p2&var1)))continue;
                    timer=smartRoll(r->p3,r->p4);
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0,var1);break;
                case kLocalSmartEventKill:{
                    if(timer)continue;
                    const auto* victim=player(invoker,players);
                    if(r->p3&&!victim)continue;
                    if(r->p4){const auto* creature=npc(invoker);if(victim||!creature||creature->entry!=r->p4)continue;}
                    timer=smartRoll(r->p1,r->p2);
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth);break;}
                case kLocalSmartEventDamaged:
                    if(timer)continue;
                    if(r->p5) {
                        // Health check mode: HealthBelowPctDamaged(rangeMin, damage) while engaged.
                        if(!engaged||!n.maxHealth)continue;
                        if(!(int64_t(n.health)-int64_t(var0)<int64_t(uint64_t(n.maxHealth)*std::min<uint32_t>(r->p5,100)/100)))continue;
                        smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0);
                    } else {
                        if(var0>r->p2||var0<r->p1)continue;
                        timer=smartRoll(r->p3,r->p4);
                        smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0);
                    }
                    break;
                case kLocalSmartEventDamagedTarget:case kLocalSmartEventReceiveHeal:
                    if(timer)continue;
                    if(var0>r->p2||var0<r->p1)continue;
                    timer=smartRoll(r->p3,r->p4);
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0);break;
                case kLocalSmartEventDataSet:
                    if(timer)continue;
                    if(r->p1!=var0||r->p2!=var1)continue;
                    timer=smartRoll(r->p3,r->p4);
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0,var1);break;
                case kLocalSmartEventTimedEventTriggered:
                    if(r->p1!=var0)continue;
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth);break;
                case kLocalSmartEventCounterSet:
                    if(timer)continue;
                    if(r->p1!=var0||smartCounter(n,r->p1)!=r->p2)continue;
                    smartTimedAction(n,i,*r,r->p3,r->p4,invoker,players,silenced,stunned);break;
                case kLocalSmartEventAcceptedQuest:case kLocalSmartEventRewardQuest:
                    if(timer)continue;
                    if(r->p1&&var0!=r->p1)continue;
                    timer=smartRoll(r->p2,r->p3);
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0);break;
                case kLocalSmartEventEventPhaseChange:
                    if(!smartInPhase(n,r->p1))continue;
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth);break;
                case kLocalSmartEventOocLos:case kLocalSmartEventIcLos:{
                    // hostilityMode (0 hostile, 1 not hostile, 2 any), maxDist,
                    // cooldownMin/Max, playerOnly; the unit is in line of sight.
                    if(timer)continue;
                    if((event==kLocalSmartEventOocLos)==engaged)continue;
                    const auto* who=player(invoker,players);if(!who)continue;
                    if(distance2(*who,n)>float(r->p2)*float(r->p2))continue;
                    if(!collision.isInLineOfSight(n.mapId,n.x,n.y,n.z+2,who->x,who->y,who->z+2,false))continue;
                    const bool hostile=npcCanAttack(*who,n);
                    if(!(r->p1==2||(r->p1==1&&!hostile)||(r->p1==0&&hostile)))continue;
                    timer=smartRoll(r->p3,r->p4);
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth);break;}
                // ---- 2.38 events
                case kLocalSmartEventSummonedUnit:case kLocalSmartEventSummonedUnitDies:case kLocalSmartEventSummonedUnitEvade:{
                    // creature entry (0 any), cooldownMin, cooldownMax; the summon is the invoker.
                    if(timer)continue;
                    const auto* summon=npc(invoker);if(!summon)continue;
                    if(r->p1&&summon->entry!=r->p1)continue;
                    timer=smartRoll(r->p2,r->p3);
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth);break;}
                case kLocalSmartEventSummonDespawned:
                    // entry (var0), cooldownMin, cooldownMax.
                    if(timer)continue;
                    if(r->p1&&r->p1!=var0)continue;
                    timer=smartRoll(r->p2,r->p3);
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0);break;
                // ---- 2.39
                case kLocalSmartEventWaypointReached:case kLocalSmartEventWaypointEnded:
                    // pointId (0 any), pathId (0 any) against the creature's
                    // loaded path (Creature::GetWaypointPath), not the event's.
                    if((r->p1&&var0!=r->p1)||(r->p2&&n.patrolLoadedPath!=r->p2))continue;
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0,var1);break;
                case kLocalSmartEventActionDone:
                    // eventId (DoAction's id), cooldownMin, cooldownMax.
                    if(timer)continue;
                    if(r->p1!=var0)continue;
                    timer=smartRoll(r->p2,r->p3);
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0);break;
                case kLocalSmartEventPassengerBoarded:case kLocalSmartEventPassengerRemoved:
                    // cooldownMin, cooldownMax; the passenger is the invoker.
                    if(timer||!invoker)continue;
                    timer=smartRoll(r->p1,r->p2);
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth);break;
                // ---- 2.40
                case kLocalSmartEventGossipHello:
                    // filter 2 (reportUse only) never fires for a creature's hello.
                    if(r->p1==2)continue;
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth);break;
                case kLocalSmartEventGossipSelect:
                    // (menuId, optionId) exactly (SmartScript: sender / action).
                    if(r->p1!=var0||r->p2!=var1)continue;
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0,var1);break;
                case kLocalSmartEventReceiveEmote:
                    // emoteId, cooldownMin, cooldownMax (SmartAI::ReceiveEmote).
                    if(timer)continue;
                    if(r->p1!=var0)continue;
                    timer=smartRoll(r->p2,r->p3);
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0);break;
                case kLocalSmartEventMovementInform:
                    // movement type (0 any), point id (0 any), path id (0 any).
                    if((r->p1&&r->p1!=var0)||(r->p2&&r->p2!=var1))continue;
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0,var1);break;
                case kLocalSmartEventEscortStart:
                    // SmartScript::ProcessEvent compares waypoint.pathID with the
                    // first point's id (the pin's own reading).
                    if(r->p2&&var0!=r->p2)continue;
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0);break;
                case kLocalSmartEventEscortReached:case kLocalSmartEventEscortPaused:case kLocalSmartEventEscortResumed:
                case kLocalSmartEventEscortStopped:case kLocalSmartEventEscortEnded:
                    // pointId (0 any), pathId (0 any: GetPathId()).
                    if((r->p1&&var0!=r->p1)||(r->p2&&n.escortPathId!=r->p2))continue;
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0,var1);break;
                case kLocalSmartEventTextOver:
                    // The text group exactly (group 0 is a group), the
                    // talker's creature entry (0 any).
                    if(var0!=r->p1||(r->p2&&var1!=r->p2))continue;
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0);break;
                default:
                    if(timer)continue; // cooldowns of the other event-driven rows (e.active)
                    smartProcessEvent(n,i,*r,invoker,players,silenced,stunned,depth,var0,var1);break;
            }
        }
    }
    /// SmartScript::ProcessTimedAction: the action, then the repeat timer.
    void smartTimedAction(LocalRealmNpc& n,size_t index,const LocalNpcSpellProfile& r,uint32_t repeatMin,uint32_t repeatMax,uint64_t invoker,
                          const std::vector<LocalRealmPlayer*>& players,bool silenced,bool stunned) {
        const bool ok=smartProcessEvent(n,index,r,invoker,players,silenced,stunned);
        // ProcessTimedAction's RecalcTimer follows ProcessAction even when a cast
        // failed: the repeat overwrites RetryLater's 1200 ms.
        (void)ok;smartTimer(n,index)=smartRoll(repeatMin,repeatMax);
    }
    /// Unit::IsInRange with both object sizes (combat reaches) added to both bounds.
    bool npcInRange(const LocalRealmNpc& n,const LocalRealmPlayer& p,float minRange,float maxRange) const {
        const float size=localCreatureCombatReach(content->npc(n.entry))+kLocalDefaultCombatReach;
        const float d2=distance2(p,n);const float lo=minRange+size,hi=maxRange+size;
        if(d2<lo*lo)return false;return d2<hi*hi;
    }
    /// SmartAI::MoveInLineOfSight for the players near a scripted creature:
    /// the OOC_LOS / IC_LOS rows once per player within the sight distance
    /// (Creature::m_SightDistance, SET_SIGHT_DIST; the default 50 yd) - the
    /// rows' own cooldowns pace them.
    void smartLineOfSight(LocalRealmNpc& n,const std::vector<LocalRealmPlayer*>& players,bool engaged) {
        const auto rows=smartRows(n);
        const uint32_t event=engaged?kLocalSmartEventIcLos:kLocalSmartEventOocLos;
        bool any=false;for(const auto* r=rows.first;r!=rows.second;++r)if(r->event==event){any=true;break;}
        if(!any)return;
        const float sight=n.npcSightDistance?float(n.npcSightDistance):50.f;
        for(auto* p:players)if(p&&!p->dead&&p->health&&p->mapId==n.mapId&&p->instanceId==n.instanceId&&distance2(*p,n)<=sight*sight)
            smartFireEvents(n,event,p->guid,players);
    }
    /// SmartScript::OnUpdate / UpdateTimer for one creature. `engaged` follows
    /// IsEngaged (a victim). Returns true when anything changed.
    bool smartUpdate(LocalRealmNpc& n,const LocalNpcDefinition& def,uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players,
                     bool engaged,LocalRealmPlayer* victim,bool silenced,bool stunned) {
        if(n.scriptActorId)return false;
        // 2.39: a creature without rows of its own still runs the timed list
        // another script handed it (SetScript9 on a creature target).
        if(!smartRowsUsable(n)){if(!n.smartListId)return false;return smartUpdateList(n,elapsedMs,players,engaged,silenced,stunned);}
        if(!n.npcSpellTimerInitialized)smartEnsureInit(n,players);
        if(!n.npcRangeInit)smartInitRangeMode(n);
        const auto rows=smartRows(n);size_t i=0;bool changed=false;
        const bool castingBar=n.npcCastingSpellId&&(!n.npcSpellLaunched||n.npcChanneling);
        for(const auto* r=rows.first;r!=rows.second;++r,++i) {
            if(r->event==kLocalSmartEventLink)continue;
            if(!smartInPhase(n,r->phaseMask))continue;
            if(r->event==kLocalSmartEventUpdateIc&&!engaged)continue;
            if(r->event==kLocalSmartEventUpdateOoc&&engaged)continue;
            auto& timer=smartTimer(n,i);
            if(timer){timer-=std::min(timer,elapsedMs);changed=true;if(timer)continue;}
            if(!smartTimedEvent(r->event)&&r->event!=kLocalSmartEventTargetManaPct&&r->event!=kLocalSmartEventFriendlyMissingBuff&&
               r->event!=kLocalSmartEventTargetBuffed&&r->event!=kLocalSmartEventIsBehindTarget&&r->event!=kLocalSmartEventNearPlayers&&
               r->event!=kLocalSmartEventNearPlayersNegation&&r->event!=kLocalSmartEventHasAura&&r->event!=kLocalSmartEventManaPct&&
               r->event!=kLocalSmartEventHealthPct&&r->event!=kLocalSmartEventTargetHealthPct&&r->event!=kLocalSmartEventVictimCasting&&
               r->event!=kLocalSmartEventFriendlyHealth&&r->event!=kLocalSmartEventFriendlyIsCc&&r->event!=kLocalSmartEventDistanceCreature)continue; // event-driven rows: their cooldown ran down (e.active)
            // UpdateTimer: a cast row due while a cast bar runs is RaisePriority'd.
            if(r->casts()&&castingBar&&!(r->castFlags()&kLocalSmartCastInterruptPrevious)){timer=1200;continue;}
            // One cast or missile in flight at a time: a row due while a
            // missile flies stays due until it lands (its timer is not spent).
            if(r->casts()&&n.npcCastingSpellId&&n.npcSpellLaunched&&!n.npcChanneling&&!(r->castFlags()&kLocalSmartCastTriggered))continue;
            if(r->once()&&((n.npcSpellDoneMask>>i)&1u))continue;
            switch(r->event) {
                case kLocalSmartEventUpdate:smartTimedAction(n,i,*r,r->p3,r->p4,0,players,silenced,stunned);break;
                case kLocalSmartEventUpdateIc:case kLocalSmartEventUpdateOoc:smartTimedAction(n,i,*r,r->p3,r->p4,0,players,silenced,stunned);break;
                case kLocalSmartEventHealthPct:{
                    if(!engaged||!n.maxHealth)break;
                    const uint32_t pct=uint32_t(uint64_t(n.health)*100/n.maxHealth);
                    if(pct>r->p2||pct<r->p1)break;
                    smartTimedAction(n,i,*r,r->p3,r->p4,0,players,silenced,stunned);break;}
                case kLocalSmartEventManaPct:{
                    if(!engaged||!n.npcMaxMana)break;
                    const uint32_t pct=uint32_t(uint64_t(n.npcMana)*100/n.npcMaxMana);
                    if(pct>r->p2||pct<r->p1)break;
                    smartTimedAction(n,i,*r,r->p3,r->p4,0,players,silenced,stunned);break;}
                case kLocalSmartEventTargetHealthPct:{
                    if(!engaged||!victim||!victim->maxHealth)break;
                    const uint32_t pct=uint32_t(uint64_t(victim->health)*100/victim->maxHealth);
                    if(pct>r->p2||pct<r->p1)break;
                    smartTimedAction(n,i,*r,r->p3,r->p4,victim->guid,players,silenced,stunned);break;}
                case kLocalSmartEventTargetManaPct:{
                    // The victim's mana percentage (a character whose resource is mana).
                    if(!engaged||!victim||victim->resourceType!=LocalResourceType::Mana||!victim->maxMana)break;
                    const uint32_t pct=uint32_t(uint64_t(victim->mana)*100/victim->maxMana);
                    if(pct>r->p2||pct<r->p1)break;
                    smartTimedAction(n,i,*r,r->p3,r->p4,victim->guid,players,silenced,stunned);break;}
                case kLocalSmartEventRange:{
                    if(!engaged||!victim)break;
                    if(npcInRange(n,*victim,float(r->p5),float(r->p6)))smartTimedAction(n,i,*r,r->p3,r->p4,victim->guid,players,silenced,stunned);
                    else timer=1200;
                    break;}
                case kLocalSmartEventVictimCasting:{
                    if(!engaged||!victim||!victim->castingSpellId||(r->p3&&victim->castingSpellId!=r->p3))break;
                    smartTimedAction(n,i,*r,r->p1,r->p2,victim->guid,players,silenced,stunned);break;}
                case kLocalSmartEventAreaCasting:case kLocalSmartEventAreaRange:{
                    if(!engaged)break;
                    LocalRealmPlayer* found=nullptr;
                    for(const auto& e:n.threat)if(e.guid)if(auto* p=player(e.guid,players)) {
                        if(p->dead||!p->health)continue;
                        if(r->event==kLocalSmartEventAreaCasting&&!p->castingSpellId)continue;
                        if(!npcInRange(n,*p,float(r->p5),float(r->p6)))continue;
                        found=p;break;
                    }
                    if(found){smartProcessEvent(n,i,*r,found->guid,players,silenced,stunned);timer=smartRoll(r->p3,r->p4);}
                    else timer=r->event==kLocalSmartEventAreaRange?1200:smartRoll(r->p3,r->p4);
                    break;}
                case kLocalSmartEventFriendlyHealth:{
                    if(!engaged)break;
                    // DoSelectLowestHpFriendly(radius, hpDeficit): the friendly creature
                    // in combat with the largest missing health above the deficit.
                    LocalRealmNpc* best=nullptr;uint32_t bestDeficit=r->p1;
                    for(auto& m:npcs)if(npcFriendlyCandidate(n,m,float(r->p2))&&m.targetGuid){const uint32_t deficit=m.maxHealth>m.health?m.maxHealth-m.health:0;if(deficit>bestDeficit||(deficit==bestDeficit&&deficit&&!best)){best=&m;bestDeficit=deficit;}}
                    if(!best){timer=smartRoll(1000,3000);break;}
                    smartTimedAction(n,i,*r,r->p3,r->p4,best->guid,players,silenced,stunned);break;}
                case kLocalSmartEventFriendlyHealthPct:{
                    if(!engaged)break;
                    LocalRealmNpc* found=nullptr;
                    if(r->target==kLocalSmartTargetSelf||r->target==kLocalSmartTargetInvoker) {
                        // DoSelectLowestHpPercentFriendly(radius, 0, hpPct).
                        uint32_t bestPct=r->p5+1;
                        for(auto& m:npcs)if(npcFriendlyCandidate(n,m,float(r->p6),true)){const uint32_t pct=m.maxHealth?uint32_t(uint64_t(m.health)*100/m.maxHealth):100;if(pct<bestPct){bestPct=pct;found=&m;}}
                    } else for(const auto& t:smartTargets(n,*r,n.smartInvoker,players))if(t.m&&!t.m->dead&&t.m->targetGuid&&npcFriendly(n,*t.m)) {
                        const uint32_t pct=t.m->maxHealth?uint32_t(uint64_t(t.m->health)*100/t.m->maxHealth):100;
                        if(pct<=r->p5){found=t.m;break;}
                    }
                    if(!found)break;
                    smartTimedAction(n,i,*r,r->p3,r->p4,found->guid,players,silenced,stunned);break;}
                case kLocalSmartEventFriendlyMissingBuff:{
                    // spell, radius, repeatMin, repeatMax, onlyInCombat:
                    // DoFindFriendlyMissingBuff - a living friendly creature in
                    // combat (the creature itself included) without the aura;
                    // one of them at random is the invoker.
                    if(r->p5&&!engaged)break;
                    std::vector<LocalRealmNpc*> found;
                    for(auto& m:npcs)if(npcFriendlyCandidate(n,m,float(r->p2),true)&&m.targetGuid&&!npcBuffPresent(m,r->p1))found.push_back(&m);
                    if(found.empty())break;
                    smartTimedAction(n,i,*r,r->p3,r->p4,found[meleeRoll(uint32_t(found.size()-1))]->guid,players,silenced,stunned);break;}
                case kLocalSmartEventFriendlyIsCc:{
                    // radius, repeatMin, repeatMax: DoFindFriendlyCC - a living
                    // friendly creature in combat within the radius under a stun,
                    // fear, confusion or root (FriendlyCCedInRange); one of them
                    // at random is the invoker; none re-arms 1-3 s.
                    if(!engaged)break;
                    std::vector<LocalRealmNpc*> found;
                    for(auto& m:npcs)if(npcFriendlyCandidate(n,m,float(r->p1),true)&&m.targetGuid&&npcCrowdControlled(m))found.push_back(&m);
                    if(found.empty()){timer=smartRoll(1000,3000);break;}
                    smartTimedAction(n,i,*r,r->p2,r->p3,found[meleeRoll(uint32_t(found.size()-1))]->guid,players,silenced,stunned);break;}
                case kLocalSmartEventDistanceCreature:{
                    // guid, entry, distance, repeat: a spawn or the first creature
                    // of the entry within the distance (no invoker).
                    bool near=false;
                    for(const auto& m:npcs) {
                        if(&m==&n||m.dead||m.npcUnsummoned||m.mapId!=n.mapId||m.instanceId!=n.instanceId)continue;
                        if(r->p1){if(m.spawnId!=r->p1||m.npcSummonType)continue;}
                        else if(m.entry!=r->p2)continue;
                        if(distance2(n.x,n.y,n.z,m.x,m.y,m.z)<=float(r->p3)*float(r->p3)){near=true;break;}
                    }
                    if(!near){timer=r->p4;break;}
                    smartTimedAction(n,i,*r,r->p4,r->p4,0,players,silenced,stunned);break;}
                case kLocalSmartEventHasAura:{
                    const uint32_t count=npcAuraCount(n,r->p1);
                    if((!r->p2&&!count)||(r->p2&&count>=r->p2))smartTimedAction(n,i,*r,r->p3,r->p4,0,players,silenced,stunned);
                    break;}
                case kLocalSmartEventTargetBuffed:{
                    // spell, count: the victim's GetAuraCount(spell) >= count.
                    if(!victim)break;
                    uint32_t count=0;
                    for(const auto& a:npcPeriodic)if(a.target==victim->guid&&a.spell==r->p1&&a.remaining)count+=std::max<uint8_t>(1,a.stacks);
                    for(const auto& a:victim->statAuras)if(a.spellId==r->p1&&a.remainingMs)count+=std::max<uint8_t>(1,a.stacks);
                    if(count<r->p2)break;
                    smartTimedAction(n,i,*r,r->p3,r->p4,victim->guid,players,silenced,stunned);break;}
                case kLocalSmartEventIsBehindTarget:{
                    // min, max (initial), repeatMin, repeatMax, rangeMin, rangeMax:
                    // the creature stands outside the victim's front half-circle
                    // within the range (a rangeMax of 0 never fires).
                    if(!victim||!r->p6){timer=smartRoll(r->p1,r->p2);break;}
                    if(npcInRange(n,*victim,float(r->p5),float(r->p6))) {
                        float angle=std::atan2(n.y-victim->y,n.x-victim->x)-victim->orientation;
                        while(angle>3.14159265f)angle-=6.2831853f;while(angle<-3.14159265f)angle+=6.2831853f;
                        if(std::fabs(angle)>3.14159265f/2){smartTimedAction(n,i,*r,r->p3,r->p4,victim->guid,players,silenced,stunned);break;}
                    }
                    timer=smartRoll(r->p1,r->p2);break;}
                case kLocalSmartEventNearPlayers:case kLocalSmartEventNearPlayersNegation:{
                    // minCount (maxCount), radius, firstTimer, repeatMin, repeatMax:
                    // the players within the radius counted.
                    uint32_t count=0;
                    for(const auto* p:players)if(p&&p->mapId==n.mapId&&p->instanceId==n.instanceId&&distance2(*p,n)<=float(r->p2)*float(r->p2))++count;
                    const bool fire=r->event==kLocalSmartEventNearPlayers?count>=r->p1:count<r->p1;
                    if(fire)smartProcessEvent(n,i,*r,0,players,silenced,stunned);
                    timer=smartRoll(r->p4,r->p5);break;}
                default:break;
            }
        }
        changed=smartUpdateStoredEvents(n,elapsedMs,players,silenced,stunned)||changed;
        changed=smartUpdateList(n,elapsedMs,players,engaged,silenced,stunned)||changed;
        changed=smartTextTimers(n,elapsedMs,players)||changed;
        smartLineOfSight(n,players,engaged);
        return changed;
    }
    /// The stored timed events (CREATE_TIMED_EVENT): each is an UPDATE row
    /// whose action fires TIMED_EVENT_TRIGGERED(id) at its chance; a repeat
    /// re-arms it, a NOT_REPEATABLE one is removed after firing.
    bool smartUpdateStoredEvents(LocalRealmNpc& n,uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players,bool silenced,bool stunned) {
        if(n.smartTimedEvents.empty())return false;
        (void)silenced;(void)stunned;
        std::vector<uint32_t> fired;
        for(auto& ev:n.smartTimedEvents) {
            ev.timerMs-=std::min(ev.timerMs,elapsedMs);
            if(ev.timerMs)continue;
            if(ev.chance>=100||meleeRoll(99)<ev.chance)fired.push_back(ev.id);
            ev.timerMs=ev.once?0:std::max<uint32_t>(1,smartRoll(ev.repeatMinMs,ev.repeatMaxMs));
        }
        for(const auto id:fired)smartFireEvents(n,kLocalSmartEventTimedEventTriggered,0,players,id);
        std::erase_if(n.smartTimedEvents,[](const auto& ev){return ev.once&&!ev.timerMs;});
        return true;
    }
    bool npcFriendly(const LocalRealmNpc& n,const LocalRealmNpc& m) const {
        const uint32_t a=npcFaction(n),b=npcFaction(m);
        return a&&b&&a==b;
    }
    /// 2.39 SET_INST_DATA: InstanceScript::SetData kept per map instance. No
    /// instance script reads it locally (doors, encounter states are outside
    /// the catalog); it is recorded so a later consumer can.
    std::map<uint64_t,std::map<uint32_t,uint32_t>> instanceData;
    void smartSetInstanceData(const LocalRealmNpc& n,uint32_t field,uint32_t data) {
        auto& fields=instanceData[(uint64_t(n.mapId)<<32)|n.instanceId];
        if(fields.size()>=256&&!fields.count(field))return;
        fields[field]=data;
        LOG_DEBUG("[LOCAL_SMART] instance data map=",n.mapId," instance=",n.instanceId," field=",field," data=",data);
    }
    /// FriendlyCCedInRange's crowd-control test on the creature controls the
    /// authority tracks: a stun (Unit::SetStunned) or a full root (a snare of
    /// 99 % or more - the reference's isFrozen / UNIT_STATE_ROOT).
    bool npcCrowdControlled(const LocalRealmNpc& m) const {
        if(localNpcStunned(m))return true;
        for(const auto& a:m.snares)if(a.remainingMs&&a.percent>=99)return true;
        return false;
    }
    bool npcFriendlyCandidate(const LocalRealmNpc& n,const LocalRealmNpc& m,float radius,bool includeSelf=true) const {
        if((&m==&n&&!includeSelf)||m.dead||!m.health||m.npcDespawned||m.mapId!=n.mapId||m.instanceId!=n.instanceId||m.scriptActorId||m.transportEntry||m.vehicleId)return false;
        if(&m!=&n&&!npcFriendly(n,m))return false;
        return distance2(n.x,n.y,n.z,m.x,m.y,m.z)<=radius*radius;
    }
    /// The running timed action list (SmartScript::OnUpdate's mTimedActionList
    /// loop): only the enabled row's timer runs, paused by the list's timer
    /// type; when it fires the next row is enabled.
    bool smartUpdateList(LocalRealmNpc& n,uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players,bool engaged,bool silenced,bool stunned) {
        if(!n.smartListId)return false;
        const auto list=localNpcSmartList(n.smartListId);
        const size_t count=size_t(list.second-list.first);
        if(n.smartListIndex>=count){n.smartListId=0;return true;}
        if(n.smartListTimerType==0&&engaged)return false;
        if(n.smartListTimerType==1&&!engaged)return false;
        n.smartListTimer-=std::min(n.smartListTimer,elapsedMs);
        if(n.smartListTimer)return true;
        const auto& r=list.first[n.smartListIndex];
        const bool castingBar=n.npcCastingSpellId&&(!n.npcSpellLaunched||n.npcChanneling);
        if(r.casts()&&castingBar&&!(r.castFlags()&kLocalSmartCastInterruptPrevious)){n.smartListTimer=1200;return true;}
        if(r.casts()&&n.npcCastingSpellId&&n.npcSpellLaunched&&!n.npcChanneling&&!(r.castFlags()&kLocalSmartCastTriggered))return true;
        smartProcessAction(n,0,r,0,players,silenced,stunned,true);
        ++n.smartListIndex;
        if(n.smartListIndex>=count)n.smartListId=0;
        else n.smartListTimer=smartRoll(list.first[n.smartListIndex].p1,list.first[n.smartListIndex].p2);
        return true;
    }
    // ------------------------------------------------------------- cast machine
    void npcCancelChannelOrCast(LocalRealmNpc& n) {
        if(n.npcChanneling)std::erase_if(npcPeriodic,[&](const auto& a){const bool gone=a.caster==n.guid&&a.spell==n.npcCastingSpellId;if(gone)npcControlEnded(a,nullptr);return gone;});
        n.npcChanneling=false;n.npcChannelRemainingMs=0;
        localCancelNpcSpellCast(n);
    }
    /// Spell::cast for the prepared creature spell: the cost is paid, the hit
    /// result of a single-target spell is rolled here (a missile carries it),
    /// then the effects land now or at the end of the flight.
    void npcSpellLaunch(LocalRealmNpc& n,const std::vector<LocalRealmPlayer*>& players) {
        const auto* spell=content->spell(n.npcCastingSpellId);
        if(!spell){localCancelNpcSpellCast(n);return;}
        if(n.npcCastManaCost) {
            if(n.npcMana<n.npcCastManaCost){LOG_INFO("[LOCAL_NPC_SPELL] cancel npc=",n.guid," spell=",spell->id," reason=no-power");localCancelNpcSpellCast(n);return;}
            n.npcMana-=n.npcCastManaCost;n.npcSinceManaUseMs=0;n.npcCastManaCost=0;
        }
        auto* recipient=player(n.npcCastTargetGuid,players);
        const bool single=spell->npcTargetShape==0&&!spell->npcChainTargets&&recipient;
        if(single) {
            const bool magic=spell->sourceDamageClass==1||spell->sourceDamageClass==0;
            n.npcSpellReflected=magic&&spell->damage&&npcSpellReflects(n,*recipient,*spell);
            n.npcSpellMissed=spell->sourceDamageClass==1&&!n.npcSpellReflected&&npcSpellMisses(n,*recipient,*spell);
            n.npcSpellMeleeOutcome=magic?0:uint8_t(uint8_t(npcRollMeleeSpell(n,*recipient,*spell))+1);
        } else {n.npcSpellReflected=false;n.npcSpellMissed=false;n.npcSpellMeleeOutcome=0;}
        emitCombatEvent(localNpcSpellPhase(n,*spell,LocalCombatEventKind::SpellCast,n.npcCastTargetGuid),players);
        if(single&&spell->sourceProjectileSpeed>0&&std::isfinite(spell->sourceProjectileSpeed)&&!spell->npcChannel) {
            // Source delayed spell flight time is fixed at launch; moving beyond
            // cast range does not cancel a missile.
            const auto distance=std::max(5.f,std::sqrt(distance2(*recipient,n)));
            n.npcCastRemainingMs=uint32_t(std::clamp(distance/spell->sourceProjectileSpeed*1000.f,1.f,60000.f));
            n.npcSpellReturnMs=n.npcSpellReflected?n.npcCastRemainingMs/2:0;
            n.npcSpellLaunched=true;
        } else {
            if(n.npcSpellReflected&&recipient)npcSpellReflectEvent(n,*recipient,*spell,players);
            npcSpellImpactAll(n,players);
        }
    }
    /// The impact of the prepared creature spell on every recipient its shape
    /// selects (Spell::SelectSpellTargets + DoAllEffectOnTarget), then the
    /// triggered spell, then a channel is opened or the cast ends.
    void npcSpellImpactAll(LocalRealmNpc& n,const std::vector<LocalRealmPlayer*>& players,unsigned depth=0) {
        const auto* spell=content->spell(n.npcCastingSpellId);
        if(!spell){localCancelNpcSpellCast(n);return;}
        const bool reflected=n.npcSpellReflected,missed=n.npcSpellMissed;
        const uint8_t launchedMelee=n.npcSpellMeleeOutcome;
        const auto primaryGuid=n.npcCastTargetGuid;
        const auto spellId=spell->id;
        const bool channel=spell->npcChannel;
        localCancelNpcSpellCast(n);
        LocalMeleeOutcome outcome=npcSpellEffects(n,*spell,primaryGuid,players,reflected,missed,launchedMelee,depth);
        emitCombatEvent(localNpcSpellPhase(n,*spell,LocalCombatEventKind::SpellFinish,0,
            outcome==LocalMeleeOutcome::Critical?LocalMeleeOutcome::Critical:LocalMeleeOutcome::Hit),players);
        LOG_INFO("[LOCAL_NPC_SPELL] impact npc=",n.guid," entry=",n.entry," spell=",spellId," target=",primaryGuid," outcome=",int(outcome));
        if(channel&&outcome==LocalMeleeOutcome::Hit&&!n.dead&&depth==0) {
            // The channel holds the creature for the aura's duration.
            n.npcCastingSpellId=spellId;n.npcCastTargetGuid=primaryGuid;n.npcChanneling=true;n.npcChannelRemainingMs=spell->durationMs;
            n.npcSpellLaunched=true;n.npcCastRemainingMs=0;
        }
    }
    /// One recipient's share of a creature spell (all effects, one hit roll).
    LocalMeleeOutcome npcSpellEffectsOn(LocalRealmNpc& n,const LocalSpellDefinition& d,LocalRealmPlayer& p,const std::vector<LocalRealmPlayer*>& players,
                                       bool reflected,bool missed,uint8_t launchedMelee,float damageMultiplier) {
        LocalMeleeOutcome outcome;
        const bool weapon=d.npcWeaponEffect||d.npcWeaponPercent;
        if(d.sourceDamageClass==2||d.sourceDamageClass==3)outcome=npcMeleeSpellImpact(n,p,d,players,launchedMelee,damageMultiplier);
        else if(weapon&&!reflected) {
            // A weapon-damage effect on a NONE / MAGIC class spell (2.37):
            // SpellHitResult decided by the magic roll (a NONE class never
            // misses), then EffectWeaponDmg as spell damage of its school.
            outcome=npcMeleeSpellImpact(n,p,d,players,uint8_t(uint8_t(missed?LocalMeleeOutcome::Miss:LocalMeleeOutcome::Hit)+1),damageMultiplier);
        } else if(!reflected&&!d.damage&&npcSpellUnitAura(d))outcome=npcAuraApply(n,p,d,players,missed);
        else if(!reflected&&!d.damage&&npcSpellHasExtras(d)) {
            // A spell of direct effects without a damage or aura part (a power
            // burn, a dispel, a kill credit): the hit result alone; a utility
            // spell is positive at the pin and never misses.
            outcome=missed&&!d.npcUtility?LocalMeleeOutcome::Miss:LocalMeleeOutcome::Hit;
            LocalCombatEvent hit{0,n.guid,p.guid,d.id,n.mapId,n.instanceId,0,0,0,LocalCombatEventKind::SpellHit};
            hit.attackType=LocalCombatAttackType::Magic;hit.spellTypeMask=4;hit.outcome=outcome;
            emitCombatEvent(hit,players);
        } else {
            outcome=reflected?npcSpellReflectedHit(n,d,players,p.guid):npcSpellHit(n,p,d,players,false,missed,damageMultiplier);
            if(!reflected&&!missed&&outcome!=LocalMeleeOutcome::Miss&&npcSpellUnitAura(d))npcAuraApply(n,p,d,players,false,false);
        }
        if(outcome!=LocalMeleeOutcome::Miss&&outcome!=LocalMeleeOutcome::Immune&&outcome!=LocalMeleeOutcome::Dodge&&outcome!=LocalMeleeOutcome::Parry&&!reflected) {
            if(!p.dead)npcSpellSideEffects(n,d,p,players);
            npcSpellExtras(n,d,p,players);
        }
        return outcome;
    }
    static bool npcSpellHasExtras(const LocalSpellDefinition& d) {
        return d.npcPowerBurn.set||d.npcPowerDrain.set||d.npcDispelType||d.npcDispelMechanic||d.npcCharge||d.npcThreatPct||d.npcKillCredit||d.npcCreateItem;
    }
    /// The remaining direct effects of a creature spell that hit a player
    /// (2.37): POWER_BURN, POWER_DRAIN, DISPEL, DISPEL_MECHANIC, CHARGE,
    /// MODIFY_THREAT_PERCENT, KILL_CREDIT2 and CREATE_ITEM.
    void npcSpellExtras(LocalRealmNpc& n,const LocalSpellDefinition& d,LocalRealmPlayer& p,const std::vector<LocalRealmPlayer*>& players) {
        if(d.npcPowerBurn.set&&!p.dead&&p.resourceType==LocalResourceType::Mana) {
            // Spell::EffectPowerBurn: the mana taken, then that amount times
            // the multiplier as damage of the spell's school.
            const uint32_t burned=std::min(p.mana,uint32_t(std::max(0,npcEffectAmount(n,d,d.npcPowerBurn))));
            p.mana-=burned;
            const uint32_t damage=uint32_t(std::min<float>(1000000.f,float(burned)*std::max(0.f,d.npcPowerBurnMultiplier)));
            LOG_INFO("[LOCAL_NPC_SPELL] powerburn npc=",n.guid," spell=",d.id," target=",p.guid," burned=",burned," damage=",damage);
            if(damage)npcSpellHit(n,p,d,players,false,false,1.f,int64_t(damage));
        }
        if(d.npcPowerDrain.set&&!p.dead&&p.resourceType==LocalResourceType::Mana) {
            // Spell::EffectPowerDrain: the mana taken; the caster gains it
            // times the multiplier when it runs on mana itself.
            const uint32_t drained=std::min(p.mana,uint32_t(std::max(0,npcEffectAmount(n,d,d.npcPowerDrain))));
            p.mana-=drained;
            if(n.npcManaReady&&n.npcMaxMana)n.npcMana=std::min(n.npcMaxMana,n.npcMana+uint32_t(std::min<float>(1000000.f,float(drained)*std::max(0.f,d.npcPowerDrainMultiplier))));
            LOG_INFO("[LOCAL_NPC_SPELL] powerdrain npc=",n.guid," spell=",d.id," target=",p.guid," drained=",drained);
        }
        if(d.npcDispelType&&!p.dead) {
            // Spell::EffectDispel by a hostile caster: up to `count` of the
            // character's helpful auras of the dispel type, drawn at random.
            std::vector<size_t> candidates;
            for(size_t i=0;i<p.statAuras.size();++i)if(const auto* a=content->spell(p.statAuras[i].spellId);a&&p.statAuras[i].remainingMs&&a->dispelType==d.npcDispelType&&!a->passive)candidates.push_back(i);
            uint32_t removed=0;
            while(!candidates.empty()&&removed<d.npcDispelCount) {
                const size_t pick=meleeRoll(uint32_t(candidates.size()-1));
                p.statAuras[candidates[pick]].remainingMs=0;candidates.erase(candidates.begin()+long(pick));++removed;
            }
            LOG_INFO("[LOCAL_NPC_SPELL] dispel npc=",n.guid," spell=",d.id," target=",p.guid," type=",unsigned(d.npcDispelType)," removed=",removed);
        }
        if(d.npcDispelMechanic&&!p.dead) {
            // Spell::EffectDispelMechanic: the creature auras of that mechanic
            // on the character (the creature-cast controls and periodics).
            uint32_t removed=0;
            for(auto& a:npcPeriodic)if(a.target==p.guid&&a.remaining&&removed<d.npcDispelMechanicCount)if(const auto* spell=content->spell(a.spell);spell&&spell->mechanic==d.npcDispelMechanic){npcControlEnded(a,&p);a.remaining=0;a.control=0;++removed;}
            for(auto& a:p.statAuras)if(a.remainingMs&&removed<d.npcDispelMechanicCount)if(const auto* spell=content->spell(a.spellId);spell&&spell->mechanic==d.npcDispelMechanic){a.remainingMs=0;++removed;}
            LOG_INFO("[LOCAL_NPC_SPELL] dispelmechanic npc=",n.guid," spell=",d.id," target=",p.guid," mechanic=",unsigned(d.npcDispelMechanic)," removed=",removed);
        }
        if(d.npcCharge&&!n.dead&&!p.dead&&!localNpcStunned(n)) {
            // Spell::EffectCharge: the creature arrives at melee reach of the target.
            const float dx=p.x-n.x,dy=p.y-n.y,len=std::sqrt(dx*dx+dy*dy);
            const float reach=localCreatureCombatReach(content->npc(n.entry))+kLocalDefaultCombatReach;
            if(len>reach+0.5f){n.x=p.x-dx/len*reach;n.y=p.y-dy/len*reach;n.z=p.z;n.orientation=std::atan2(dy,dx);}
            LOG_INFO("[LOCAL_NPC_SPELL] charge npc=",n.guid," spell=",d.id," target=",p.guid);
        }
        if(d.npcThreatPct)modifyThreatPct(n,p.guid,std::max<int32_t>(-100,d.npcThreatPct));
        if(d.npcKillCredit)npcKillCredit(p,d.npcKillCredit);
        if(d.npcCreateItem&&!p.dead) {
            // Spell::EffectCreateItem: the item goes to the target's bags (a
            // full inventory keeps the item back, as DoCreateItem's failure does).
            if(addItem(p,*content,d.npcCreateItem,d.npcCreateItemCount))questStatus(p,*content);
            LOG_INFO("[LOCAL_NPC_SPELL] createitem npc=",n.guid," spell=",d.id," target=",p.guid," item=",d.npcCreateItem);
        }
    }
    /// Spell::EffectInterruptCast / EffectKnockBack on a player who was hit.
    void npcSpellSideEffects(LocalRealmNpc& n,const LocalSpellDefinition& d,LocalRealmPlayer& p,const std::vector<LocalRealmPlayer*>& players) {
        (void)players;
        if(d.npcInterrupt&&p.castingSpellId) {
            if(const auto* casting=content->spell(p.castingSpellId)) {
                // Unit::ProhibitSpellSchool for the interrupting spell's duration.
                LocalSchoolLockout lock{uint8_t(casting->schoolMask&127u),std::min<uint32_t>(d.durationMs,600000)};
                if(lock.schoolMask&&lock.remainingMs) {
                    auto it=std::find_if(p.schoolLockouts.begin(),p.schoolLockouts.end(),[&](const auto& l){return l.schoolMask==lock.schoolMask;});
                    if(it!=p.schoolLockouts.end())it->remainingMs=std::max(it->remainingMs,lock.remainingMs);
                    else if(p.schoolLockouts.size()<kLocalMaxSchoolLockouts)p.schoolLockouts.push_back(lock);
                }
            }
            clearCast(p,LocalCastStatus::Interrupted);
        }
        if(d.npcKnockbackSpeedXY>0||d.npcKnockbackZ.set) {
            // No knockback while rooted; a cast in progress is interrupted.
            if(!(localPlayerControl(p)&2u)) {
                const float speedZ=float(npcEffectAmount(n,d,d.npcKnockbackZ))/10.f;
                if(d.npcKnockbackSpeedXY>0.1f||speedZ>0.1f) {
                    if(p.castingSpellId)clearCast(p,LocalCastStatus::Interrupted);
                    const float dx=p.x-n.x,dy=p.y-n.y,len=std::sqrt(dx*dx+dy*dy);
                    p.knockbackCos=len>0.001f?dx/len:1.f;p.knockbackSin=len>0.001f?dy/len:0.f;
                    p.knockbackSpeedXY=std::min(1000.f,d.npcKnockbackSpeedXY);p.knockbackSpeedZ=std::min(1000.f,std::max(0.f,speedZ));
                    ++p.knockbackSequence;if(!p.knockbackSequence)p.knockbackSequence=1;
                    LOG_INFO("[LOCAL_NPC_SPELL] knockback npc=",n.guid," spell=",d.id," target=",p.guid," xy=",p.knockbackSpeedXY," z=",p.knockbackSpeedZ);
                }
            }
        }
    }
    /// The recipients of a hostile creature spell by shape.
    /// Whether a creature spell does anything to the players it reaches (a
    /// pure summon or ground-aura spell reaches nobody).
    static bool npcSpellHasPlayerEffect(const LocalSpellDefinition& d) {
        return d.damage||d.npcWeaponEffect||d.npcWeaponPercent||npcSpellUnitAura(d)||npcSpellHasExtras(d)||d.npcInterrupt||d.npcKnockbackSpeedXY>0||d.npcKnockbackZ.set||
            d.npcTriggerSpellId||d.npcLeech||(d.npcCosmetic&&!d.npcSummonEntry&&!d.npcGroundAura);
    }
    std::vector<LocalRealmPlayer*> npcSpellRecipients(LocalRealmNpc& n,const LocalSpellDefinition& d,LocalRealmPlayer* primary,const std::vector<LocalRealmPlayer*>& players,uint64_t primaryGuid=0) {
        std::vector<LocalRealmPlayer*> out;
        if(!npcSpellHasPlayerEffect(d))return out;
        // 2.40: a utility spell (kill credit, item creation) reaches a friendly player too.
        const auto valid=[&](LocalRealmPlayer* p){return p&&!p->dead&&p->health&&p->mapId==n.mapId&&p->instanceId==n.instanceId&&!p->flight.active&&(d.npcUtility||npcCanAttack(*p,n));};
        const auto los=[&](const LocalRealmPlayer& p){return d.sourceIgnoreLineOfSight||collision.isInLineOfSight(n.mapId,n.x,n.y,n.z+2,p.x,p.y,p.z+2,false);};
        const float reach=localCreatureCombatReach(content->npc(n.entry));
        switch(d.npcTargetShape) {
            case 0:if(valid(primary))out.push_back(primary);break;
            case 2:case 4:{
                // Area around the caster (both combat reaches widen it, as
                // WorldObject::IsWithinDist does) or a cone in front of it.
                const float radius=d.npcAreaRadius+reach+kLocalDefaultCombatReach;
                const float half=d.npcConeDegrees>0?d.npcConeDegrees*3.14159265f/360.f:0.f;
                for(auto* p:players)if(valid(p)) {
                    const float dx=p->x-n.x,dy=p->y-n.y,dz=p->z-n.z;
                    if(dx*dx+dy*dy+dz*dz>radius*radius)continue;
                    if(d.npcTargetShape==4) {
                        float angle=std::atan2(dy,dx)-n.orientation;
                        while(angle>3.14159265f)angle-=6.2831853f;while(angle<-3.14159265f)angle+=6.2831853f;
                        if(std::fabs(angle)>half)continue;
                    }
                    if(!los(*p))continue;
                    out.push_back(p);
                }
                break;}
            case 3:{
                if(!valid(primary))break;
                const float radius=d.npcAreaRadius+kLocalDefaultCombatReach;
                for(auto* p:players)if(valid(p)&&distance2(p->x,p->y,p->z,primary->x,primary->y,primary->z)<=radius*radius&&los(*p))out.push_back(p);
                break;}
            case 6:{
                // 2.38 TARGET_UNIT_DEST_AREA_ENEMY: the enemies around the
                // destination - the explicit target's position (a player, or a
                // creature target), the caster's without one.
                float cx=n.x,cy=n.y,cz=n.z;
                if(primary){cx=primary->x;cy=primary->y;cz=primary->z;}
                else if(const auto* m=npc(primaryGuid);m&&m!=&n){cx=m->x;cy=m->y;cz=m->z;}
                const float radius=d.npcAreaRadius+kLocalDefaultCombatReach;
                for(auto* p:players)if(valid(p)&&distance2(p->x,p->y,p->z,cx,cy,cz)<=radius*radius&&los(*p))out.push_back(p);
                break;}
            default:break;
        }
        if(d.npcChainTargets>1&&d.npcTargetShape==0&&!out.empty()) {
            // Spell::SearchChainTargets: the next-closest enemies within the
            // jump radius (7.5 yd ranged, 10 yd melee and magic; spell_jump_distance
            // overrides), magic chains reaching chainTargets times as far, in
            // line of sight, melee chains only in front of the caster.
            float jump=d.sourceDamageClass==3?7.5f:10.f;
            if(d.npcJumpDistance>0)jump=d.npcJumpDistance;
            const bool far=d.sourceDamageClass==0||d.sourceDamageClass==1||(d.effectMask&0x80);
            const uint32_t jumps=d.npcChainTargets-1;
            std::vector<LocalRealmPlayer*> pool;
            const float search=far?jump*float(jumps):jump;
            for(auto* p:players)if(valid(p)&&p!=primary&&distance2(p->x,p->y,p->z,primary->x,primary->y,primary->z)<=search*search) {
                if(!far){float angle=std::atan2(p->y-n.y,p->x-n.x)-n.orientation;while(angle>3.14159265f)angle-=6.2831853f;while(angle<-3.14159265f)angle+=6.2831853f;if(std::fabs(angle)>3.14159265f/2)continue;}
                pool.push_back(p);
            }
            const LocalRealmPlayer* source=primary;
            for(uint32_t j=0;j<jumps&&!pool.empty();++j) {
                size_t best=pool.size();float bestD=jump*jump;
                for(size_t k=0;k<pool.size();++k){const float dd=distance2(pool[k]->x,pool[k]->y,pool[k]->z,source->x,source->y,source->z);if(dd<=bestD&&(best==pool.size()||dd<distance2(pool[best]->x,pool[best]->y,pool[best]->z,source->x,source->y,source->z))&&los(*pool[k])){best=k;}}
                if(best==pool.size())break;
                out.push_back(pool[best]);source=pool[best];pool.erase(pool.begin()+long(best));
            }
        }
        if(d.npcMaxTargets&&out.size()>d.npcMaxTargets){std::shuffle(out.begin(),out.end(),meleeRandom);out.resize(d.npcMaxTargets);}
        return out;
    }
    /// Spell::DoAllEffectOnTarget for every recipient; positive spells land on
    /// the creature itself, its allies or a creature target.
    LocalMeleeOutcome npcSpellEffects(LocalRealmNpc& n,const LocalSpellDefinition& d,uint64_t primaryGuid,const std::vector<LocalRealmPlayer*>& players,
                                     bool reflected,bool missed,uint8_t launchedMelee,unsigned depth) {
        LocalMeleeOutcome outcome=LocalMeleeOutcome::Miss;
        std::vector<uint64_t> triggerTargets;
        // 2.38: the destination-only effects land once per cast (SPELL_EFFECT_HANDLE_HIT).
        if(!reflected) {
            if(d.npcSummonEntry)npcSpellSummon(n,d,primaryGuid,players);
            if(d.npcGroundAura)npcSpellGroundEffect(n,d,primaryGuid,players);
        }
        if(d.npcPositive) {
            std::vector<LocalRealmNpc*> allies;
            if(d.npcTargetShape==5){for(auto& m:npcs)if(npcFriendlyCandidate(n,m,d.npcAreaRadius+localCreatureCombatReach(content->npc(n.entry))))allies.push_back(&m);}
            else if(auto* m=npc(primaryGuid);m&&!m->dead)allies.push_back(m);
            else allies.push_back(&n);
            for(auto* m:allies){npcApplyPositive(n,*m,d,players);outcome=LocalMeleeOutcome::Hit;
                // SmartAI::SpellHitTarget for every unit the spell reached.
                smartFireEvents(n,kLocalSmartEventSpellHitTarget,m->guid,players,d.id,d.schoolMask,depth+1);}
        } else if(reflected) {
            // A reflected spell deals only its direct part to the creature; the
            // reflector need not be present or alive any more.
            outcome=npcSpellReflectedHit(n,d,players,primaryGuid);
        } else {
            auto* primary=player(primaryGuid,players);
            auto recipients=npcSpellRecipients(n,d,primary,players,primaryGuid);
            float multiplier=1.f;
            for(size_t k=0;k<recipients.size();++k) {
                auto* p=recipients[k];
                const bool first=k==0;
                const auto o=npcSpellEffectsOn(n,d,*p,players,first&&reflected,first&&missed,first?launchedMelee:0,multiplier);
                if(first)outcome=o;else if(o==LocalMeleeOutcome::Hit&&outcome==LocalMeleeOutcome::Miss)outcome=o;
                // Spell::DoAllEffectOnTarget ran for this unit: the trigger
                // effect fires at it (a missed unit sees no effect at all), and
                // SmartAI::SpellHitTarget runs (the SPELLHIT_TARGET rows).
                if(o!=LocalMeleeOutcome::Miss&&o!=LocalMeleeOutcome::Immune&&o!=LocalMeleeOutcome::Dodge&&o!=LocalMeleeOutcome::Parry) {
                    triggerTargets.push_back(p->guid);
                    smartFireEvents(n,kLocalSmartEventSpellHitTarget,p->guid,players,d.id,d.schoolMask,depth+1);
                }
                if(d.npcChainTargets>1)multiplier*=d.npcChainMultiplier;
                if(n.dead)break;
            }
            if(recipients.empty()&&(d.npcTargetShape!=0||!npcSpellHasPlayerEffect(d)))outcome=LocalMeleeOutcome::Hit; // an empty area still casts
        }
        // Spell::EffectTriggerSpell: cast by the creature at every unit the
        // triggering spell reached (a positive spell: at its own target),
        // fully triggered (no cast time, no cost), never recursively.
        if(d.npcTriggerSpellId&&depth==0&&!n.dead) {
            if(const auto* triggered=content->spell(d.npcTriggerSpellId);npcSpellUsable(triggered,1)) {
                if(d.npcPositive||reflected)triggerTargets.assign(1,primaryGuid);
                for(const auto guid:triggerTargets) {
                    emitCombatEvent(localNpcSpellPhase(n,*triggered,LocalCombatEventKind::SpellCast,guid),players);
                    auto* target=player(guid,players);
                    bool tMissed=false,tReflected=false;uint8_t tMelee=0;
                    if(target&&triggered->npcTargetShape==0&&!triggered->npcPositive) {
                        tReflected=(triggered->sourceDamageClass<=1)&&triggered->damage&&npcSpellReflects(n,*target,*triggered);
                        tMissed=triggered->sourceDamageClass==1&&!tReflected&&npcSpellMisses(n,*target,*triggered);
                        tMelee=triggered->sourceDamageClass>=2?uint8_t(uint8_t(npcRollMeleeSpell(n,*target,*triggered))+1):0;
                    }
                    const auto o=npcSpellEffects(n,*triggered,guid,players,tReflected,tMissed,tMelee,depth+1);
                    emitCombatEvent(localNpcSpellPhase(n,*triggered,LocalCombatEventKind::SpellFinish,0,LocalMeleeOutcome::Hit),players);
                    (void)o;
                    if(n.dead)break;
                }
            }
        }
        return outcome;
    }
    /// A positive creature spell on a creature: heal (Spell::EffectHeal with
    /// the target's MOD_HEALING_PCT), HEAL_PCT / HEAL_MAX_HEALTH, ENERGIZE(_PCT),
    /// ADD_EXTRA_ATTACKS, and the buff auras (one buff per spell and caster,
    /// refreshed to the full duration, x stacks). `auraOnly` is Unit::AddAura
    /// (SMART_ACTION_ADD_AURA): the aura part alone.
    void npcApplyPositive(LocalRealmNpc& caster,LocalRealmNpc& m,const LocalSpellDefinition& d,const std::vector<LocalRealmPlayer*>& players,bool auraOnly=false) {
        if(m.dead||!m.health)return;
        const auto heal=[&](uint32_t amount,bool periodic){
            amount=npcHealingTaken(m,amount);
            const uint32_t effective=std::min(amount,m.maxHealth-m.health);
            m.health+=effective;
            LocalCombatEvent event{0,caster.guid,m.guid,d.id,m.mapId,m.instanceId,amount,effective,0,periodic?LocalCombatEventKind::PeriodicHeal:LocalCombatEventKind::DirectHeal};
            event.attackType=LocalCombatAttackType::Magic;event.spellTypeMask=2;event.positiveSpell=true;event.schoolMask=d.schoolMask;
            emitCombatEvent(event,players);
            LOG_INFO("[LOCAL_NPC_HEAL] npc=",caster.guid," spell=",d.id," target=",m.guid," heal=",amount," effective=",effective);
            // SmartAI::HealReceived: the RECEIVE_HEAL rows with the amount healed.
            smartFireEvents(m,kLocalSmartEventReceiveHeal,caster.guid,players,amount);
        };
        if(!auraOnly) {
            if(d.heal||d.npcHealAmount.set)heal(uint32_t(std::max(0,npcEffectAmount(caster,d,d.npcHealAmount))),false);
            // Spell::EffectHealPct / EffectHealMaxHealth (the caster's max health).
            if(d.npcHealPct.set)heal(uint32_t(uint64_t(m.maxHealth)*std::clamp(npcEffectAmount(caster,d,d.npcHealPct),0,100)/100),false);
            if(d.npcHealMax)heal(caster.maxHealth,false);
            // Spell::EffectEnergize(Pct) on the target's mana.
            if((d.npcEnergize.set||d.npcEnergizePct.set)&&m.npcManaReady&&m.npcMaxMana) {
                const uint32_t amount=d.npcEnergize.set?uint32_t(std::max(0,npcEffectAmount(caster,d,d.npcEnergize))):uint32_t(uint64_t(m.npcMaxMana)*std::clamp(npcEffectAmount(caster,d,d.npcEnergizePct),0,100)/100);
                m.npcMana=std::min(m.npcMaxMana,m.npcMana+amount);
                LOG_INFO("[LOCAL_NPC_SPELL] energize npc=",caster.guid," spell=",d.id," target=",m.guid," amount=",amount);
            }
            // Spell::EffectAddExtraAttacks: the next swing brings them.
            if(d.npcExtraAttacks)m.npcExtraAttacks=uint8_t(std::min<uint32_t>(uint32_t(m.npcExtraAttacks)+d.npcExtraAttacks,10));
        }
        const bool buff=d.npcHaste.set||d.npcDamagePct.set||d.npcDamageFlat.set||d.npcAttackPower.set||d.npcDamageTakenFlat.set||d.npcDamageTakenPct.set||
            d.npcHealingPct.set||d.periodicHeal||d.npcPeriodicTriggerSpellId||(d.npcCosmetic&&(d.durationMs||d.indefiniteDuration))||
            d.npcSpeed.set||d.npcResistance.set||d.npcCastSpeed.set||d.npcHitChance.set||d.npcDodge.set||d.npcParry.set||d.npcBlock.set||d.npcAbsorb.set||
            d.npcSchoolImmunity||d.npcDamageImmunity||d.npcMechanicImmunity||d.npcMaxHealth.set||d.npcMaxHealthPct.set||d.npcDamageShield.set||
            d.npcProcSpellId||d.npcProcDamage.set||d.npcArmorAmount||d.npcArmorAmountMax||d.npcArmorPerLevel!=0||d.npcArmorPercentWide||
            d.npcSelfControl||d.npcInvisible;
        // 2.39 Spell::EffectInstaKill on the caster itself.
        if(d.npcInstakillSelf&&&m==&caster&&!m.dead){npcKillSelf(m,players);return;}
        if(!buff)return;
        // Aura::CanStackWith: the same spell from another caster stacks only
        // when it carries a periodic effect; otherwise the new application
        // replaces the other caster's (RemoveNoStackAurasDueToAura).
        if(!d.periodicHeal&&!d.npcPeriodicTriggerSpellId)npcEraseBuffs(m,[&](const auto& b){return b.spellId==d.id&&b.casterGuid!=caster.guid;});
        auto existing=std::find_if(m.npcBuffs.begin(),m.npcBuffs.end(),[&](const auto& b){return b.spellId==d.id&&b.casterGuid==caster.guid;});
        const bool created=existing==m.npcBuffs.end();
        if(created){if(m.npcBuffs.size()>=16)return;m.npcBuffs.push_back({});existing=m.npcBuffs.end()-1;}
        else npcBuffRemoved(m,*existing); // the max-health term is recomputed below
        const uint8_t stacks=created?uint8_t(1):uint8_t(std::min<uint32_t>(uint32_t(existing->stacks)+1,std::max<uint32_t>(1,d.maxAuraStacks)));
        const uint32_t next=d.periodicHeal?(created||!existing->periodicNextMs?d.periodicIntervalMs:existing->periodicNextMs):(d.npcPeriodicTriggerSpellId?(created||!existing->periodicNextMs?d.npcPeriodicTriggerIntervalMs:existing->periodicNextMs):0);
        LocalNpcBuff b;b.spellId=d.id;b.casterGuid=caster.guid;b.stacks=stacks;b.durationMs=d.durationMs;b.remainingMs=d.durationMs;b.indefinite=!d.durationMs&&d.indefiniteDuration;
        const auto scaled=[&](const LocalSpellDefinition::NpcAmount& a){return a.set?int32_t(std::clamp<int64_t>(int64_t(npcEffectAmount(caster,d,a))*stacks,-100000,100000)):0;};
        b.hastePct=std::clamp(scaled(d.npcHaste),-99,1000);b.damagePct=std::clamp(scaled(d.npcDamagePct),-99,1000);b.damageFlat=scaled(d.npcDamageFlat);b.attackPower=scaled(d.npcAttackPower);
        b.damageTakenFlat=scaled(d.npcDamageTakenFlat);b.damageTakenPct=std::clamp(scaled(d.npcDamageTakenPct),-99,1000);b.healingPct=std::clamp(scaled(d.npcHealingPct),-100,0);
        b.damagePctSchool=d.npcDamagePctSchool;b.damageFlatSchool=d.npcDamageFlatSchool;b.damageTakenSchool=d.npcDamageTakenSchool;
        if(d.periodicHeal){b.periodicHeal=uint32_t(std::min<int64_t>(1000000,int64_t(std::max(0,npcEffectAmount(caster,d,d.npcPeriodicHealAmount)))*stacks));b.periodicIntervalMs=d.periodicIntervalMs;b.periodicNextMs=next;}
        if(d.npcPeriodicTriggerSpellId){b.periodicTriggerSpellId=d.npcPeriodicTriggerSpellId;b.periodicIntervalMs=d.npcPeriodicTriggerIntervalMs;b.periodicNextMs=next;}
        // 2.37 amounts. Speed and resistance, cast speed, hit chance and the
        // avoidance percentages; SCHOOL_ABSORB's pool; the immunities; the
        // damage shield (its own school); the proc definition.
        b.speedPct=std::clamp(scaled(d.npcSpeed),-99,1000);b.resistance=scaled(d.npcResistance);b.resistanceSchool=d.npcResistanceSchool;
        b.castSpeedPct=std::clamp(scaled(d.npcCastSpeed),-99,1000);b.hitChancePct=std::clamp(scaled(d.npcHitChance),-100,100);
        b.dodgePct=std::clamp(scaled(d.npcDodge),-100,100);b.parryPct=std::clamp(scaled(d.npcParry),-100,100);b.blockPct=std::clamp(scaled(d.npcBlock),-100,100);
        b.absorbRemaining=uint32_t(std::max(0,scaled(d.npcAbsorb)));b.absorbSchool=d.npcAbsorbSchool;
        b.schoolImmunity=d.npcSchoolImmunity;b.damageImmunity=d.npcDamageImmunity;b.mechanicImmunity=d.npcMechanicImmunity;
        b.damageShield=uint32_t(std::max(0,scaled(d.npcDamageShield)));b.damageShieldSchool=uint8_t(d.schoolMask&127u);
        b.procSpellId=d.npcProcSpellId;b.procFlags=d.npcProcFlags;b.procChance=d.npcProcChance;b.procCharges=d.npcProcCharges;
        b.procDamage=uint32_t(std::max(0,scaled(d.npcProcDamage)));b.procSchool=uint8_t(d.schoolMask&127u);
        // 2.39: a self stun / root (Unit::SetControlled on the aura owner), invisibility.
        b.selfControl=&m==&caster?d.npcSelfControl:uint8_t(0);b.invisible=d.npcInvisible;
        // The armor term on the creature itself (MOD_RESISTANCE on school 1, any
        // sign) joins its damageTaken as an armor-like flat reduction is not
        // modelled for creatures; it travels as resistance of school 1.
        if(d.npcArmorAmount||d.npcArmorAmountMax||d.npcArmorPerLevel!=0){b.resistance+=int32_t(std::clamp<int64_t>(int64_t(npcArmorAmount(d,caster.level))*stacks,-100000,100000));b.resistanceSchool|=1;}
        // MOD_INCREASE_HEALTH / _PERCENT: applied to max health now, the
        // delta remembered for the removal (Unit will keep its health
        // percentage under the percent form; the flat form adds its amount).
        if(d.npcMaxHealthPct.set) {
            const int32_t pct=std::clamp(scaled(d.npcMaxHealthPct),-99,1000);
            const float ratio=m.maxHealth?float(m.health)/float(m.maxHealth):1.f;
            const uint32_t before=m.maxHealth;
            m.maxHealth=uint32_t(std::clamp<int64_t>(int64_t(before)*(100+pct)/100,1,1000000000));
            m.health=std::clamp<uint32_t>(uint32_t(float(m.maxHealth)*ratio),1u,m.maxHealth);
            b.maxHealth=int32_t(int64_t(m.maxHealth)-int64_t(before));b.maxHealthPct=pct;
        } else if(d.npcMaxHealth.set) {
            const int32_t delta=std::clamp(scaled(d.npcMaxHealth),-1000000,1000000);
            const uint32_t before=m.maxHealth;
            m.maxHealth=uint32_t(std::clamp<int64_t>(int64_t(before)+delta,1,1000000000));
            const int64_t applied=int64_t(m.maxHealth)-int64_t(before);
            m.health=uint32_t(std::clamp<int64_t>(int64_t(m.health)+applied,1,int64_t(m.maxHealth)));
            b.maxHealth=int32_t(applied);b.maxHealthPct=0;
        }
        *existing=b;
        LOG_INFO("[LOCAL_NPC_BUFF] apply npc=",caster.guid," spell=",d.id," target=",m.guid," haste=",b.hastePct," dmgPct=",b.damagePct," dmgFlat=",b.damageFlat," ap=",b.attackPower," stacks=",unsigned(stacks)," durationMs=",b.durationMs,
                 " speed=",b.speedPct," absorb=",b.absorbRemaining," shield=",b.damageShield," proc=",b.procSpellId," maxHealth=",b.maxHealth);
    }
    /// SpellEffectInfo::CalcValue for a signed aura amount.
    int32_t npcEffectAmount(const LocalRealmNpc& n,const LocalSpellDefinition& d,const LocalSpellDefinition::NpcAmount& a) {
        if(!a.set)return 0;
        const int64_t rolled=a.low+(a.high>a.low?int64_t(meleeRoll(uint32_t(a.high-a.low))):0);
        int32_t level=int32_t(n.level);
        if(d.maxLevel&&level>int32_t(d.maxLevel))level=int32_t(d.maxLevel);
        else if(level<int32_t(d.baseLevel))level=int32_t(d.baseLevel);
        level-=int32_t(std::max<uint32_t>(d.baseLevel,d.spellLevel));
        float value=float(rolled+(a.perLevel!=0?int64_t(int32_t(float(level)*a.perLevel)):0));
        if(a.perLevel==0&&a.scales)value*=localNpcSpellDamageScale(smartOwner(n),n.level,d.npcScales,d.spellLevel);
        return int32_t(std::clamp(value,-1000000.f,1000000.f));
    }
    /// Unit::SpellHealingBonusTaken's MOD_HEALING_PCT for a creature target.
    uint32_t npcHealingTaken(const LocalRealmNpc& m,uint32_t amount) const {
        int32_t pct=0;for(const auto& b:m.npcBuffs)pct+=b.healingPct;
        return uint32_t(std::max<int64_t>(0,int64_t(amount)*(100+std::max(-100,pct))/100));
    }
    /// The creature's own melee modifiers from its buffs: attack time, damage.
    float npcMeleeHasteMultiplier(const LocalRealmNpc& n) const {
        float m=1.f;for(const auto& b:n.npcBuffs)if(b.hastePct>0)m*=100.f/float(100+std::min(b.hastePct,1000));else if(b.hastePct<0)m*=float(100-std::max(b.hastePct,-99))/100.f;
        return m;
    }
    /// Unit::UpdateDamagePhysical / MeleeDamageBonusDone for the creature's
    /// physical damage: (weapon + flat + AP/14 x 2.0) x (1 + pct).
    uint32_t npcMeleeDamage(const LocalRealmNpc& n,uint32_t weapon,uint8_t schoolMask=1) const {
        int64_t flat=0;float pct=1.f;int64_t ap=0;
        for(const auto& b:n.npcBuffs){if(b.damageFlatSchool&schoolMask)flat+=b.damageFlat;if(b.damagePctSchool&schoolMask)pct*=1.f+float(b.damagePct)/100.f;ap+=b.attackPower;}
        const int64_t base=int64_t(weapon)+flat+ap/7;
        return uint32_t(std::clamp<int64_t>(int64_t(float(std::max<int64_t>(0,base))*pct),0,1000000));
    }
    /// Unit::SpellDamageBonusTaken / MeleeDamageBonusTaken on a creature:
    /// flat and percentage damage-taken modifiers of its buffs.
    uint32_t npcDamageTaken(const LocalRealmNpc& n,uint32_t damage,uint8_t schoolMask) const {
        int64_t flat=0;float pct=1.f;
        for(const auto& b:n.npcBuffs)if(b.damageTakenSchool&schoolMask){flat+=b.damageTakenFlat;pct*=1.f+float(b.damageTakenPct)/100.f;}
        if(!flat&&pct==1.f)return damage;
        return uint32_t(std::clamp<int64_t>(int64_t(float(std::max<int64_t>(0,int64_t(damage)+flat))*pct),0,1000000));
    }
    /// The creature buffs' lifetime and periodic ticks.
    bool tickNpcBuffs(LocalRealmNpc& n,uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players) {
        if(n.npcBuffs.empty())return false;
        bool changed=false;
        for(auto& b:n.npcBuffs) {
            // Aura::UpdateOwner: the duration runs down first, then the
            // effects tick, so a tick due at the very end still lands.
            const uint32_t before=b.remainingMs;
            if(!b.indefinite){b.remainingMs-=std::min(b.remainingMs,elapsedMs);changed=true;}
            if(b.periodicIntervalMs&&(before||b.indefinite)&&!n.dead) {
                uint32_t budget=b.indefinite?elapsedMs:std::min(elapsedMs,before);
                while(budget) {
                    const auto step=std::min(budget,b.periodicNextMs);b.periodicNextMs-=step;budget-=step;
                    if(b.periodicNextMs)break;
                    b.periodicNextMs=b.periodicIntervalMs;
                    if(b.periodicHeal&&n.health<n.maxHealth) {
                        const uint32_t amount=npcHealingTaken(n,b.periodicHeal);const uint32_t effective=std::min(amount,n.maxHealth-n.health);
                        n.health+=effective;
                        LocalCombatEvent event{0,b.casterGuid,n.guid,b.spellId,n.mapId,n.instanceId,amount,effective,0,LocalCombatEventKind::PeriodicHeal};
                        event.attackType=LocalCombatAttackType::Magic;event.spellTypeMask=2;event.positiveSpell=true;
                        emitCombatEvent(event,players);
                        smartFireEvents(n,kLocalSmartEventReceiveHeal,b.casterGuid,players,amount);
                    }
                    if(b.periodicTriggerSpellId)if(const auto* triggered=content->spell(b.periodicTriggerSpellId);npcSpellUsable(triggered,1)) {
                        // The aura target (the creature) casts the triggered spell around itself.
                        emitCombatEvent(localNpcSpellPhase(n,*triggered,LocalCombatEventKind::SpellCast,n.guid),players);
                        npcSpellEffects(n,*triggered,n.targetGuid,players,false,false,0,1);
                        emitCombatEvent(localNpcSpellPhase(n,*triggered,LocalCombatEventKind::SpellFinish,0,LocalMeleeOutcome::Hit),players);
                    }
                }
            }
        }
        return npcEraseBuffs(n,[](const auto& b){return !b.remainingMs&&!b.indefinite;})||changed;
    }
    // ---------------------------------------------------- 2.37 procs, shields
    /// SmartAI::DamageDealt (the DAMAGED_TARGET rows) and Unit::ProcSkillsAndAuras'
    /// DONE side for a hit the creature landed on a player: the proc type
    /// (PROC_FLAG_DONE_*) and PROC_FLAG_DONE_MAINHAND_ATTACK for a swing.
    void npcDealtDamage(LocalRealmNpc& n,LocalRealmPlayer& target,uint32_t damage,uint32_t procFlag,uint32_t schoolMask,const std::vector<LocalRealmPlayer*>& players) {
        (void)schoolMask;
        smartFireEvents(n,kLocalSmartEventDamagedTarget,target.guid,players,damage);
        npcProcBuffs(n,procFlag,&target,players);
    }
    /// The TAKEN side for a hit a player landed on the creature (Unit::DealDamage:
    /// SmartAI::DamageTaken's DAMAGED rows, then the TAKEN procs, PROC_FLAG_TAKEN_DAMAGE included).
    void npcTakenDamage(LocalRealmNpc& n,LocalRealmPlayer& attacker,uint32_t damage,uint32_t procFlag,const std::vector<LocalRealmPlayer*>& players) {
        smartFireEvents(n,kLocalSmartEventDamaged,attacker.guid,players,damage);
        npcProcBuffs(n,procFlag|(damage?0x100000u:0u),&attacker,players);
    }
    /// Unit::ProcSkillsAndAuras -> TriggerAurasProcOnEvent for the creature's
    /// PROC_TRIGGER_SPELL / PROC_TRIGGER_DAMAGE buffs: a buff whose proc flags
    /// meet the event rolls its chance (Aura::CalcProcChance: Spell.dbc
    /// ProcChance), spends a charge (ConsumeProcCharges removes the aura at
    /// none), then casts its trigger at the other party of the event
    /// (AuraEffect::HandleProcTriggerSpellAuraProc) or deals its amount of the
    /// aura's school to it (HandleProcTriggerDamageAuraProc).
    void npcProcBuffs(LocalRealmNpc& n,uint32_t typeMask,LocalRealmPlayer* other,const std::vector<LocalRealmPlayer*>& players) {
        if(n.dead||!n.health||n.npcBuffs.empty()||!typeMask)return;
        struct Fired { uint32_t auraSpell,trigger,damage;uint8_t school; };
        std::vector<Fired> fired;
        for(auto& b:n.npcBuffs) {
            if(!(b.remainingMs||b.indefinite)||!(b.procFlags&typeMask)||(!b.procSpellId&&!b.procDamage))continue;
            if(!b.procChance||(b.procChance<100&&meleeRoll(99)>=b.procChance))continue;
            fired.push_back({b.spellId,b.procSpellId,b.procDamage,b.procSchool});
            if(b.procCharges&&!--b.procCharges){b.remainingMs=0;b.indefinite=false;}
        }
        if(fired.empty())return;
        npcEraseBuffs(n,[](const auto& b){return !b.remainingMs&&!b.indefinite;});
        for(const auto& f:fired) {
            if(n.dead)break;
            LOG_INFO("[LOCAL_NPC_PROC] npc=",n.guid," aura=",f.auraSpell," trigger=",f.trigger," damage=",f.damage," target=",other?other->guid:0);
            if(f.trigger) {
                const auto* triggered=content->spell(f.trigger);
                if(!npcSpellUsable(triggered,1))continue;
                const uint64_t at=triggered->npcPositive||!other?n.guid:other->guid;
                emitCombatEvent(localNpcSpellPhase(n,*triggered,LocalCombatEventKind::SpellCast,at),players);
                bool tMissed=false,tReflected=false;uint8_t tMelee=0;
                if(other&&triggered->npcTargetShape==0&&!triggered->npcPositive) {
                    tReflected=triggered->sourceDamageClass<=1&&triggered->damage&&npcSpellReflects(n,*other,*triggered);
                    tMissed=triggered->sourceDamageClass==1&&!tReflected&&npcSpellMisses(n,*other,*triggered);
                    tMelee=triggered->sourceDamageClass>=2?uint8_t(uint8_t(npcRollMeleeSpell(n,*other,*triggered))+1):0;
                }
                npcSpellEffects(n,*triggered,at,players,tReflected,tMissed,tMelee,1);
                emitCombatEvent(localNpcSpellPhase(n,*triggered,LocalCombatEventKind::SpellFinish,0,LocalMeleeOutcome::Hit),players);
            } else if(f.damage&&other&&!other->dead)npcAuraDamage(n,*other,f.auraSpell,f.school?f.school:1u,f.damage,players,false);
        }
    }
    /// Unit::DealMeleeDamage's SPELL_AURA_DAMAGE_SHIELD loop on a creature a
    /// player's swing damaged: each shield rolls the magic hit result against
    /// the attacker and deals its amount of its school (no bonus, no absorb).
    void npcDamageShields(LocalRealmNpc& n,LocalRealmPlayer& attacker,const std::vector<LocalRealmPlayer*>& players) {
        if(n.dead||attacker.dead||!attacker.health)return;
        struct Shield { uint32_t spell,amount;uint8_t school; };
        std::vector<Shield> shields;
        for(const auto& b:n.npcBuffs)if((b.remainingMs||b.indefinite)&&b.damageShield)shields.push_back({b.spellId,b.damageShield,b.damageShieldSchool});
        for(const auto& sh:shields) {
            if(attacker.dead)break;
            if(const auto* d=content->spell(sh.spell);d&&npcSpellMisses(n,attacker,*d)) {
                LocalCombatEvent miss{0,n.guid,attacker.guid,sh.spell,n.mapId,n.instanceId,0,0,0,LocalCombatEventKind::SpellDamage};
                miss.outcome=LocalMeleeOutcome::Miss;miss.attackType=LocalCombatAttackType::Magic;miss.spellTypeMask=4;
                emitCombatEvent(miss,players);continue;
            }
            npcAuraDamage(n,attacker,sh.spell,sh.school?sh.school:1u,sh.amount,players,true);
        }
    }
    /// School damage of a creature aura on a player outside a cast: a proc
    /// damage aura (SpellDamageBonusTaken, then absorb) or a damage shield
    /// (Unit::DealDamage straight: neither taken bonus nor absorb).
    void npcAuraDamage(LocalRealmNpc& n,LocalRealmPlayer& target,uint32_t spellId,uint32_t schoolMask,uint32_t raw,const std::vector<LocalRealmPlayer*>& players,bool shield) {
        if(target.dead||!target.health||!raw||target.mapId!=n.mapId||target.instanceId!=n.instanceId)return;
        const uint32_t attempted=shield?std::min(raw,1000000u):localFormDamage(target,npcDamageTakenByPlayer(target,localIncomingDamageAfterTalents(target,*content,std::min(raw,1000000u)),schoolMask),true);
        const uint32_t damage=shield?attempted:localAbsorbDamage(target,*content,attempted,schoolMask);
        stats(target,*content,false);
        const uint32_t effective=std::min(damage,target.health);
        npcBreakControlsOnDamage(target,content->spell(spellId));
        if(damage>=target.health) {
            emitCombatEvent(localKillProcEvent(n.guid,target.guid,target.mapId,target.instanceId,false,n.level,false),players);
            emitCombatEvent(localDeathProcEvent(target.guid,target.mapId,target.instanceId,true,target.level),players);
            npcKilledPlayer(n,target,players);
            target.health=0;localStopRangedAuto(target);leaveLocalForm(target);clearLocalCombo(target);
            target.dead=true;target.mountSpellId=0;target.deadTimer=0;target.attackTarget=0;
            clearCast(target,LocalCastStatus::Interrupted);clearLocalTravelMotion(target);localCaptureCorpse(target);
            selectThreatTarget(n,players);
        } else {
            target.health-=damage;
            if(damage&&target.resourceType==LocalResourceType::Rage)target.mana=std::min(target.maxMana,target.mana+std::min(10u,damage/4+1));
        }
        LocalCombatEvent event{0,n.guid,target.guid,spellId,target.mapId,target.instanceId,attempted,effective,attempted-damage,LocalCombatEventKind::SpellDamage,target.dead};
        event.attackType=LocalCombatAttackType::Magic;event.spellTypeMask=1;event.schoolMask=schoolMask;
        emitCombatEvent(event,players);
        LOG_INFO("[LOCAL_NPC_AURA] ",shield?"shield":"proc"," npc=",n.guid," spell=",spellId," target=",target.guid," damage=",damage);
    }
    /// SCHOOL_ABSORB on the creature (Unit::CalcAbsorbResist): its absorb
    /// buffs of the school take the damage first; an exhausted one ends.
    uint32_t npcAbsorbDamage(LocalRealmNpc& n,uint32_t damage,uint32_t schoolMask,uint32_t& absorbed) {
        absorbed=0;
        if(!damage||n.npcBuffs.empty())return damage;
        for(auto& b:n.npcBuffs) {
            if(!damage)break;
            if(!(b.remainingMs||b.indefinite)||!b.absorbRemaining||!(b.absorbSchool&schoolMask))continue;
            const uint32_t take=std::min(damage,b.absorbRemaining);
            b.absorbRemaining-=take;damage-=take;absorbed+=take;
            if(!b.absorbRemaining){b.remainingMs=0;b.indefinite=false;}
        }
        if(absorbed)npcEraseBuffs(n,[](const auto& b){return !b.remainingMs&&!b.indefinite;});
        return damage;
    }
    /// The cast bar, missile, or channel of the creature (Spell::update).
    /// Returns true when the creature is held (a cast bar or channel runs).
    bool npcCastUpdate(LocalRealmNpc& n,const LocalNpcDefinition& def,uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players,bool silenced,bool stunned,bool& changed) {
        if(!n.npcCastingSpellId)return false;
        const auto* spell=content->spell(n.npcCastingSpellId);
        if(!npcSpellUsable(spell)||stunned){npcCancelChannelOrCast(n);changed=true;return false;}
        // AuraEffect::HandleAuraModSilence: a silence interrupts a cast bar or
        // channel whose spell carries SPELL_PREVENTION_TYPE_SILENCE; a missile
        // already in flight is not a cast any more.
        if((!n.npcSpellLaunched||n.npcChanneling)&&silenced&&spell->preventionType==kLocalPreventionSilence) {
            LOG_INFO("[LOCAL_NPC_SPELL] cancel npc=",n.guid," spell=",n.npcCastingSpellId," reason=silenced");
            npcCancelChannelOrCast(n);changed=true;return false;
        }
        auto* recipient=player(n.npcCastTargetGuid,players);
        auto* creatureTarget=recipient?nullptr:npc(n.npcCastTargetGuid);
        // 2.40: a utility spell (kill credit, item creation) lands on a friendly
        // player too - a gossip's reward cast by a creature the player cannot attack.
        const bool targetValid=recipient?(!recipient->dead&&recipient->health&&(spell->npcUtility||npcCanAttack(*recipient,n)==!spell->npcPositive)&&
            (n.npcSpellLaunched||spell->npcTargetShape!=0||npcSpellRange(n,def,*recipient,*spell)==0)):
            (creatureTarget?!creatureTarget->dead:true);
        if(n.npcChanneling) {
            // Spell::update for a channel: the target must stay alive and, for
            // an explicit target, within range; the aura's ticks are its effect.
            const bool held=recipient&&!recipient->dead&&recipient->health&&recipient->mapId==n.mapId&&
                distance2(*recipient,n)<=(spell->range+localCreatureCombatReach(&def)+kLocalDefaultCombatReach+5.f)*(spell->range+localCreatureCombatReach(&def)+kLocalDefaultCombatReach+5.f);
            if(!held||!npcDebuffPresent(n.npcCastTargetGuid,spell->id)){npcCancelChannelOrCast(n);changed=true;return false;}
            n.npcChannelRemainingMs-=std::min(n.npcChannelRemainingMs,elapsedMs);changed=true;
            if(!n.npcChannelRemainingMs){n.npcChanneling=false;localCancelNpcSpellCast(n);return false;}
            return true;
        }
        if(!n.npcSpellReflectReturn&&!targetValid) {
            LOG_INFO("[LOCAL_NPC_SPELL] cancel npc=",n.guid," spell=",n.npcCastingSpellId," target=",n.npcCastTargetGuid," reason=target-invalid");
            localCancelNpcSpellCast(n);changed=true;return false;
        }
        const auto overdueMs=elapsedMs>n.npcCastRemainingMs?elapsedMs-n.npcCastRemainingMs:0;
        n.npcCastRemainingMs-=std::min(n.npcCastRemainingMs,elapsedMs);changed=true;
        if(n.npcCastRemainingMs)return !n.npcSpellLaunched; // Cast bars stop movement and melee; missiles do not.
        if(n.npcSpellLaunched) {
            if(n.npcSpellReflected&&!n.npcSpellReflectReturn&&recipient) {
                npcSpellReflectEvent(n,*recipient,*spell,players);
                n.npcSpellReflectReturn=true;n.npcCastRemainingMs=n.npcSpellReturnMs-std::min(n.npcSpellReturnMs,overdueMs);
                if(!n.npcCastRemainingMs)npcSpellImpactAll(n,players);
            } else npcSpellImpactAll(n,players);
        } else npcSpellLaunch(n,players);
        return n.npcCastingSpellId&&(!n.npcSpellLaunched||n.npcChanneling);
    }
    /// Creature::CallForHelp(radius): every free same-faction creature within
    /// the radius that can attack the caller's victim engages it at once.
    void smartCallForHelp(LocalRealmNpc& n,const std::vector<LocalRealmPlayer*>& players) {
        if(!(n.smartCallForHelpRange>0))return;
        const float radius=n.smartCallForHelpRange;const bool emote=n.smartCallForHelpEmote;
        n.smartCallForHelpRange=0;n.smartCallForHelpEmote=false;
        auto* victim=player(n.targetGuid,players);
        if(!victim||victim->dead)return;
        for(auto& m:npcs) {
            if(&m==&n||m.dead||!m.health||m.mapId!=n.mapId||m.instanceId!=n.instanceId||m.targetGuid||m.scriptActorId||m.escortOwner||m.vehicleId||m.transportEntry)continue;
            if(std::any_of(m.threat.begin(),m.threat.end(),[](const auto& e){return e.guid!=0;}))continue;
            if(!npcFriendly(n,m)||!npcCanAttack(*victim,m))continue;
            if(distance2(n.x,n.y,n.z,m.x,m.y,m.z)>radius*radius)continue;
            addThreat(m,victim->guid,1);m.targetGuid=victim->guid;
        }
        if(emote)for(const auto* viewer:players) {
            if(!viewer||viewer->mapId!=n.mapId||viewer->instanceId!=n.instanceId)continue;
            const float dx=viewer->x-n.x,dy=viewer->y-n.y,dz=viewer->z-n.z;
            if(!std::isfinite(dx+dy+dz)||dx*dx+dy*dy+dz*dz>25.f*25.f)continue;
            appendNpcDialogue(n.guid,*viewer,n.mapId,n.instanceId,n.name+" calls for help!",kLocalChatMonsterEmote);
        }
    }
