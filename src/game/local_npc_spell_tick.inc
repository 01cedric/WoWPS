        // Pinned whole-entry SmartAI closure: one UPDATE_IC / CAST(VICTIM)
        // action, no phase/link/CMC/trigger flags. Timers pause out of combat.
        if(const auto* profile=localNpcSpellProfile(n.entry)) {
            const auto* spell=content().spell(profile->spellId);
            if(!spell||!spell->unsupportedReason.empty()||!spell->clientSpell||!spell->npcOnly||
               spell->sourceDamageClass!=1||!spell->damage||spell->mana) {
                localResetNpcSpellState(n);
            } else {
                const auto repeatNpcSpell=[&] {
                    n.npcSpellTimerMs=profile->repeatMinMs+g.meleeRoll(profile->repeatMaxMs-profile->repeatMinMs);
                };
                if(!n.npcSpellTimerInitialized) {
                    n.npcSpellTimerInitialized=true;
                    n.npcSpellTimerMs=profile->initialMinMs+g.meleeRoll(profile->initialMaxMs-profile->initialMinMs);
                }
                n.npcSpellTimerMs-=std::min(n.npcSpellTimerMs,elapsedMs);
                const auto impactNpcSpell=[&](LocalRealmPlayer* recipient) {
                    const bool reflected=n.npcSpellReflected,missed=n.npcSpellMissed;
                    const auto originalTarget=n.npcCastTargetGuid;
                    localCancelNpcSpellCast(n);
                    const auto outcome=reflected?g.npcSpellReflectedHit(n,*spell,players,originalTarget):
                        g.npcSpellHit(n,*recipient,*spell,players,false,missed);
                    g.emitCombatEvent(localNpcSpellPhase(n,*spell,LocalCombatEventKind::SpellFinish,0,
                        outcome==LocalMeleeOutcome::Critical?LocalMeleeOutcome::Critical:LocalMeleeOutcome::Hit),players);
                    LOG_INFO("[LOCAL_NPC_SPELL] impact npc=",n.guid," entry=",n.entry," spell=",spell->id,
                             " target=",originalTarget," outcome=",int(outcome));
                };
                const auto launchNpcSpell=[&](LocalRealmPlayer& recipient) {
                    n.npcSpellReflected=g.npcSpellReflects(n,recipient,*spell);
                    n.npcSpellMissed=!n.npcSpellReflected&&g.npcSpellMisses(n,recipient,*spell);
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
                if(n.npcCastingSpellId) {
                    auto* recipient=g.player(n.npcCastTargetGuid,players);
                    if(n.npcCastingSpellId!=spell->id||(!n.npcSpellReflectReturn&&(!recipient||!canAttack(*recipient,n)||
                       (!n.npcSpellLaunched&&!localNpcSpellTargetInRange(n,*recipient,*spell))))) {
                        LOG_INFO("[LOCAL_NPC_SPELL] cancel npc=",n.guid," spell=",n.npcCastingSpellId,
                                 " target=",n.npcCastTargetGuid," reason=target-invalid");
                        localCancelNpcSpellCast(n);changed=true;
                    } else {
                        const auto overdueMs=elapsedMs>n.npcCastRemainingMs?elapsedMs-n.npcCastRemainingMs:0;
                        n.npcCastRemainingMs-=std::min(n.npcCastRemainingMs,elapsedMs);changed=true;
                        if(!n.npcSpellLaunched&&!n.npcSpellTimerMs)n.npcSpellTimerMs=1200; // UpdateTimer: busy cast RaisePriority.
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
                if(!n.npcCastingSpellId&&!n.npcSpellTimerMs) {
                    // ProcessTimedAction rearms UPDATE_IC after ProcessAction,
                    // including failed casts: its repeat overwrites RetryLater.
                    repeatNpcSpell();
                    if(canAttack(*target,n)&&localNpcSpellTargetInRange(n,*target,*spell)) {
                        n.npcCastingSpellId=spell->id;n.npcCastTargetGuid=target->guid;
                        n.npcCastRemainingMs=spell->castTimeMs;n.npcSpellLaunched=false;changed=true;
                        LOG_INFO("[LOCAL_NPC_SPELL] start npc=",n.guid," entry=",n.entry," spell=",spell->id,
                                 " target=",target->guid," castMs=",n.npcCastRemainingMs," repeatMs=",n.npcSpellTimerMs);
                        if(n.npcCastRemainingMs)continue;
                        launchNpcSpell(*target);
                        if(n.dead||target->dead||!n.targetGuid)continue;
                    }
                }
            }
        } else if(n.npcSpellTimerInitialized||n.npcCastingSpellId) {
            localResetNpcSpellState(n);
        }
