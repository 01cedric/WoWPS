        // Pinned whole-entry SmartAI closure: UPDATE_IC / CAST(VICTIM) rows with
        // no phase/link/condition. Each row keeps its own timer (row 0 in
        // npcSpellTimerMs, the rest in npcSpellExtraTimers); one cast or
        // missile is in flight at a time. Timers pause out of combat.
        const auto npcProfiles=localNpcSpellProfiles(n.entry);
        const size_t npcProfileCount=size_t(npcProfiles.second-npcProfiles.first);
        const auto npcSpellUsable=[&](const LocalSpellDefinition* candidate) {
            return candidate&&candidate->unsupportedReason.empty()&&candidate->clientSpell&&candidate->npcOnly&&
                candidate->sourceDamageClass>=1&&candidate->sourceDamageClass<=3&&
                (candidate->damage||candidate->periodicDamage||candidate->npcSlowPercent||candidate->npcArmorAmount||
                 candidate->npcArmorPercent||candidate->npcWeaponEffect||candidate->npcWeaponPercent||candidate->npcPlayerControl);
        };
        // Spell::GetMinMaxRange / CheckRange for the creature: a SPELL_RANGE_MELEE
        // entry uses the creature's own melee range, as its white swings do. Any
        // other entry reaches its maximum plus both combat reaches; its minimum
        // grows by the melee range (SPELL_RANGE_RANGED and the "npc shooter"
        // rule, minimum <= 6 yd) or by both combat reaches.
        // 0 = in range, 1 = too far, 2 = too close, 3 = no valid target.
        const auto npcSpellRange=[&](const LocalRealmPlayer& p,const LocalSpellDefinition& sp)->int {
            const float reach=localCreatureCombatReach(def);
            if(sp.sourceRangeFlags&1u) {
                if(n.dead||!n.health||n.transportEntry||p.dead||!p.health||p.flight.active||n.mapId!=p.mapId||n.instanceId!=p.instanceId)return 3;
                return localWithinMeleeRange(distance2(p,n),reach,kLocalDefaultCombatReach)?0:1;
            }
            const float extraMin=sp.minRange<=0?0.f:((sp.sourceRangeFlags&2u)||sp.minRange<=6.f)?
                localMeleeRange(reach,kLocalDefaultCombatReach):reach+kLocalDefaultCombatReach;
            return localNpcSpellRangeStatus(n,p,sp,extraMin,reach+kLocalDefaultCombatReach);
        };
        const auto npcSpellInRange=[&](const LocalRealmPlayer& p,const LocalSpellDefinition& sp){return npcSpellRange(p,sp)==0;};
        // Whole-entry admission: a row whose spell the client decoder refused
        // leaves the entire entry without SmartAI casts rather than half of it.
        const bool npcProfilesUsable=npcProfileCount&&npcProfileCount<=kLocalMaxNpcSpellProfilesPerEntry&&
            std::all_of(npcProfiles.first,npcProfiles.second,[&](const auto& row){return npcSpellUsable(content().spell(row.spellId));});
        if(npcProfilesUsable) {
            const auto initialTimer=[&](size_t i) {
                const auto& row=npcProfiles.first[i];
                return row.initialMinMs+g.meleeRoll(row.initialMaxMs-row.initialMinMs);
            };
            if(!n.npcSpellTimerInitialized) {
                n.npcSpellTimerInitialized=true;n.npcSpellTimerMs=initialTimer(0);
                n.npcSpellExtraTimers.clear();n.npcSpellDoneMask=0;
            }
            if(!n.npcRangeInit) {
                // SmartAI::InitializeAI: without a MAIN_SPELL row, the first
                // COMBAT_MOVE cast of a harmful spell sets the chase distance
                // (SetMainSpell: only a maximum range above 5 yd counts).
                n.npcRangeInit=true;n.npcRangeMode=false;n.npcAttackDistance=0;
                for(auto row=npcProfiles.first;row!=npcProfiles.second;++row)if(row->castFlags&kLocalSmartCastCombatMove) {
                    if(const auto* main=content().spell(row->spellId);main&&main->range>5.f){n.npcRangeMode=true;n.npcAttackDistance=main->range-5.f;}
                    break;
                }
            }
            // SmartAI::SetCurrentRangeMode.
            const auto setRangeMode=[&](bool on,float distance){n.npcRangeMode=on;n.npcAttackDistance=std::max(0.f,distance);changed=true;};
            while(n.npcSpellExtraTimers.size()+1<npcProfileCount)n.npcSpellExtraTimers.push_back(initialTimer(n.npcSpellExtraTimers.size()+1));
            const auto timerOf=[&](size_t i)->uint32_t&{return i?n.npcSpellExtraTimers[i-1]:n.npcSpellTimerMs;};
            for(size_t i=0;i<npcProfileCount;++i)if(!((n.npcSpellDoneMask>>i)&1u)){auto& timer=timerOf(i);timer-=std::min(timer,elapsedMs);}
            const LocalSpellDefinition* spell=n.npcCastingSpellId?content().spell(n.npcCastingSpellId):nullptr;
            const auto impactNpcSpell=[&](LocalRealmPlayer* recipient) {
                const bool reflected=n.npcSpellReflected,missed=n.npcSpellMissed;
                const uint8_t launchedMelee=n.npcSpellMeleeOutcome;
                const auto originalTarget=n.npcCastTargetGuid;
                localCancelNpcSpellCast(n);
                LocalMeleeOutcome outcome;
                // Spell::DoAllEffectOnTarget: one hit roll for every effect. The
                // direct part is dealt first; the aura part then lands on a
                // survivor. A reflected spell deals only its direct part to the
                // creature (its aura part on the caster is not modelled).
                if(spell->sourceDamageClass==2||spell->sourceDamageClass==3)outcome=g.npcMeleeSpellImpact(n,*recipient,*spell,players,launchedMelee);
                else if(!reflected&&!spell->damage&&g.npcSpellHasAura(*spell))outcome=g.npcAuraApply(n,*recipient,*spell,players,missed);
                else {
                    outcome=reflected?g.npcSpellReflectedHit(n,*spell,players,originalTarget):
                        g.npcSpellHit(n,*recipient,*spell,players,false,missed);
                    if(!reflected&&!missed&&outcome!=LocalMeleeOutcome::Miss&&g.npcSpellHasAura(*spell))
                        g.npcAuraApply(n,*recipient,*spell,players,false,false);
                }
                g.emitCombatEvent(localNpcSpellPhase(n,*spell,LocalCombatEventKind::SpellFinish,0,
                    outcome==LocalMeleeOutcome::Critical?LocalMeleeOutcome::Critical:LocalMeleeOutcome::Hit),players);
                LOG_INFO("[LOCAL_NPC_SPELL] impact npc=",n.guid," entry=",n.entry," spell=",spell->id,
                         " target=",originalTarget," outcome=",int(outcome));
            };
            const auto npcSpellCost=[&](const LocalSpellDefinition& candidate) {
                const auto* row=std::find_if(npcProfiles.first,npcProfiles.second,[&](const auto& r){return r.spellId==candidate.id;});
                return row==npcProfiles.second?0u:localNpcSpellManaCost(*row,n.level,candidate.mana,candidate.manaPercent,n.npcMaxMana);
            };
            const auto launchNpcSpell=[&](LocalRealmPlayer& recipient) {
                // Spell::cast -> TakePower: the cost is paid when the cast completes.
                if(n.npcCastManaCost) {
                    if(n.npcMana<n.npcCastManaCost){LOG_INFO("[LOCAL_NPC_SPELL] cancel npc=",n.guid," spell=",spell->id," reason=no-power");localCancelNpcSpellCast(n);return;}
                    n.npcMana-=n.npcCastManaCost;n.npcSinceManaUseMs=0;n.npcCastManaCost=0;
                }
                // A melee special rolls Unit::MeleeSpellHitResult here instead,
                // at target selection, and a missile carries it to impact.
                const bool magic=spell->sourceDamageClass==1;
                n.npcSpellReflected=magic&&g.npcSpellReflects(n,recipient,*spell);
                n.npcSpellMissed=magic&&!n.npcSpellReflected&&g.npcSpellMisses(n,recipient,*spell);
                n.npcSpellMeleeOutcome=magic?0:uint8_t(uint8_t(g.npcRollMeleeSpell(n,recipient,*spell))+1);
                g.emitCombatEvent(localNpcSpellPhase(n,*spell,LocalCombatEventKind::SpellCast,recipient.guid),players);
                if(spell->sourceProjectileSpeed>0&&std::isfinite(spell->sourceProjectileSpeed)) {
                    // Source delayed spell flight time is fixed at launch;
                    // moving beyond cast range does not cancel a missile.
                    const auto distance=std::max(5.f,std::sqrt(distance2(recipient,n)));
                    n.npcCastRemainingMs=uint32_t(std::clamp(distance/spell->sourceProjectileSpeed*1000.f,1.f,60000.f));
                    n.npcSpellReturnMs=n.npcSpellReflected?n.npcCastRemainingMs/2:0;
                    n.npcSpellLaunched=true;
                } else {
                    if(n.npcSpellReflected)g.npcSpellReflectEvent(n,recipient,*spell,players);
                    impactNpcSpell(&recipient);
                }
            };
            // AuraEffect::HandleAuraModSilence: a silence interrupts a cast bar
            // whose spell carries SPELL_PREVENTION_TYPE_SILENCE; a missile
            // already in flight is not a cast any more.
            if(n.npcCastingSpellId&&!n.npcSpellLaunched&&silenced&&spell&&spell->preventionType==kLocalPreventionSilence) {
                LOG_INFO("[LOCAL_NPC_SPELL] cancel npc=",n.guid," spell=",n.npcCastingSpellId," reason=silenced");
                localCancelNpcSpellCast(n);spell=nullptr;changed=true;
            }
            if(n.npcCastingSpellId) {
                auto* recipient=g.player(n.npcCastTargetGuid,players);
                if(!npcSpellUsable(spell)||(!n.npcSpellReflectReturn&&(!recipient||!canAttack(*recipient,n)||
                   (!n.npcSpellLaunched&&!npcSpellInRange(*recipient,*spell))))) {
                    LOG_INFO("[LOCAL_NPC_SPELL] cancel npc=",n.guid," spell=",n.npcCastingSpellId,
                             " target=",n.npcCastTargetGuid," reason=target-invalid");
                    localCancelNpcSpellCast(n);changed=true;
                } else {
                    const auto overdueMs=elapsedMs>n.npcCastRemainingMs?elapsedMs-n.npcCastRemainingMs:0;
                    n.npcCastRemainingMs-=std::min(n.npcCastRemainingMs,elapsedMs);changed=true;
                    // UpdateTimer: a row due while the cast bar runs is RaisePriority'd.
                    if(!n.npcSpellLaunched)for(size_t i=0;i<npcProfileCount;++i)
                        if(!((n.npcSpellDoneMask>>i)&1u)&&!timerOf(i))timerOf(i)=1200;
                    if(n.npcCastRemainingMs) {
                        if(!n.npcSpellLaunched)continue; // Cast bars stop movement and melee; missiles do not.
                    } else {
                        if(n.npcSpellLaunched) {
                            if(n.npcSpellReflected&&!n.npcSpellReflectReturn) {
                                g.npcSpellReflectEvent(n,*recipient,*spell,players);
                                n.npcSpellReflectReturn=true;n.npcCastRemainingMs=n.npcSpellReturnMs-std::min(n.npcSpellReturnMs,overdueMs);
                                if(!n.npcCastRemainingMs)impactNpcSpell(recipient);
                            } else impactNpcSpell(recipient);
                        } else launchNpcSpell(*recipient);
                        if(n.dead||target->dead||!n.targetGuid)continue;
                    }
                }
            }
            if(!n.npcCastingSpellId) for(size_t i=0;i<npcProfileCount;++i) {
                if(((n.npcSpellDoneMask>>i)&1u)||timerOf(i))continue;
                const auto& row=npcProfiles.first[i];
                // ProcessTimedAction rearms after ProcessAction, including failed
                // casts: its repeat overwrites RetryLater.
                timerOf(i)=row.repeatMinMs+g.meleeRoll(row.repeatMaxMs-row.repeatMinMs);
                const auto* candidate=content().spell(row.spellId);
                if(!npcSpellUsable(candidate))continue;
                // ProcessAction: event chance, then runOnce, then the cast flags.
                if(row.chance<100&&g.meleeRoll(99)>=row.chance)continue;
                if(row.once)n.npcSpellDoneMask|=uint8_t(1u<<i);
                if((row.castFlags&kLocalSmartCastAuraNotPresent)&&g.npcDebuffPresent(target->guid,row.spellId))continue;
                const bool combatMove=(row.castFlags&kLocalSmartCastCombatMove)!=0;
                if(!canAttack(*target,n))continue;
                // SMART_ACTION_CAST's own range checks come before CastSpell:
                // beyond the maximum a COMBAT_MOVE row chases back to
                // maxRange - 5; inside the minimum any row closes to melee
                // (SetCurrentRangeMode(true, 0)). Both fail the cast.
                const int rangeStatus=npcSpellRange(*target,*candidate);
                if(rangeStatus==3)continue;
                if(rangeStatus==1){if(combatMove)setRangeMode(true,candidate->range-5.f);continue;}
                if(rangeStatus==2){setRangeMode(true,0.f);continue;}
                // Any other failure (SPELL_FAILED_SILENCED, _NO_POWER) ends range
                // mode for a COMBAT_MOVE row: chase into melee and swing.
                const auto cost=npcSpellCost(*candidate);
                if((silenced&&candidate->preventionType==kLocalPreventionSilence)||n.npcMana<cost) {
                    if(combatMove)setRangeMode(false,0.f);
                    continue;
                }
                if(candidate->npcNextSwing) {
                    // Spell::prepare puts a next-swing special in CURRENT_MELEE_SPELL;
                    // the creature's next main-hand swing casts it instead of white
                    // damage. The generic cast slot stays free.
                    n.npcNextSwingSpellId=candidate->id;n.npcNextSwingTargetGuid=target->guid;changed=true;
                    LOG_INFO("[LOCAL_NPC_SPELL] queue npc=",n.guid," entry=",n.entry," spell=",candidate->id," target=",target->guid," repeatMs=",timerOf(i));
                    continue;
                }
                {
                    spell=candidate;
                    n.npcCastingSpellId=spell->id;n.npcCastTargetGuid=target->guid;
                    n.npcCastRemainingMs=spell->castTimeMs;n.npcSpellLaunched=false;n.npcCastManaCost=cost;changed=true;
                    LOG_INFO("[LOCAL_NPC_SPELL] start npc=",n.guid," entry=",n.entry," spell=",spell->id,
                             " target=",target->guid," castMs=",n.npcCastRemainingMs," repeatMs=",timerOf(i));
                    if(n.npcCastRemainingMs)break;
                    launchNpcSpell(*target);
                    break;
                }
            }
            if(n.npcCastingSpellId&&n.npcCastRemainingMs&&!n.npcSpellLaunched)continue;
            if(n.dead||target->dead||!n.targetGuid)continue;
        } else if(n.npcSpellTimerInitialized||n.npcCastingSpellId) {
            localResetNpcSpellState(n);
        }
