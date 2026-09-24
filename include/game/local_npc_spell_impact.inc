    // Included inside Impl. Hostile spell outcomes are snapshotted at launch;
    // a ward applied after launch does not retroactively reflect a missile.
    bool npcSpellReflects(const LocalRealmNpc& n,const LocalRealmPlayer& target,const LocalSpellDefinition& d) {
        const auto chance=localWardIncomingReflectChanceBasisPoints(target,*content,d,n.guid);
        return chance&&meleeRoll()<chance;
    }
    bool npcSpellMisses(const LocalRealmNpc& n,const LocalRealmPlayer& target,const LocalSpellDefinition& d) {
        if(d.sourceAlwaysHit)return false;
        // WorldObject::MagicSpellHitResult, reference default player-target
        // multiplier 7. These whole-entry profiles have no hit/avoidance auras.
        const int32_t difference=int32_t(target.level)-int32_t(n.level);
        const int32_t hitPercent=difference<3?96-difference:94-(difference-2)*7;
        const uint32_t miss=uint32_t(10000-std::clamp(hitPercent*100,100,10000));
        // The pinned source draws 1..10000 and uses a strict less-than test.
        return meleeRoll()+1<miss;
    }
    /// WorldObject::MagicSpellHitResult in the player-to-creature direction
    /// (Object.cpp:3574-3628). The reference's per-level multiplier is 11 against
    /// a creature victim, not the 7 used against a player.
    ///
    /// P05-4 (the implementation): the caster's spell hit rating is now applied, as
    /// `HitChance = modHitChance * 100 + int32(m_modSpellHitChance * 100)`
    /// clamped to 1..100 % (:3605-3621). The rating is the same item column and
    /// class/level ratio the wand shot has read since the implementation
    /// (localSpellHitRatingPercent); before the implementation the header here said this
    /// ruleset had no spell-hit stat, which was stale from the moment the wand
    /// landed (audit D11, correction 2 of section 7). SPELLMOD_RESIST_MISS_CHANCE
    /// (op 16, :3585-3586) is applied to the percentage before the scale, as the
    /// reference does; it has 0 accepted producers. The victim-side terms -
    /// aura 186, aura 160 and CR_HIT_TAKEN_SPELL - and the caster's aura 199 and
    /// aura 55 have no producer on either side (audit section 2.5).
    LocalMeleeOutcome playerSpellHitOutcome(const LocalRealmPlayer& caster,const LocalRealmNpc& target,const LocalSpellDefinition& d) {
        if(d.sourceAlwaysHit)return LocalMeleeOutcome::Hit;
        const int32_t difference=int32_t(target.level)-int32_t(caster.level);
        int32_t hitPercent=difference<3?96-difference:94-(difference-2)*11;
        hitPercent+=localTalentCastModifier(caster,*content,d,16,false);
        const int32_t rating=int32_t(localSpellHitRatingPercent(caster,*content)*100.f);
        const uint32_t miss=uint32_t(10000-std::clamp(hitPercent*100+rating,100,10000));
        float binaryAverage=0.f;
        // This entry point is called only for a legal hostile player cast.
        if(localBinaryResistApplies(d.schoolMask,true,d.sourceNoCastLog,d.sourceBinary))
            if(const auto* definition=content->npc(target.entry))
                binaryAverage=localAverageResist(uint32_t(std::max<int64_t>(0,int64_t(localResistanceForMask(definition->resistances,d.schoolMask))+npcBuffResistance(target,d.schoolMask))),
                                                 caster.level,target.level,true);
        return localMagicHitOutcome(miss,binaryAverage,meleeRoll()+1);
    }
    void npcSpellReflectEvent(const LocalRealmNpc& n,const LocalRealmPlayer& target,const LocalSpellDefinition& d,
                             const std::vector<LocalRealmPlayer*>& players) {
        // ReflectEvent supplies only TAKEN_MAGIC_NEG, REFLECT and amount 1;
        // no DamageInfo exists, hence OTHER rather than DAMAGE spell type.
        LocalCombatEvent event{0,n.guid,target.guid,d.id,n.mapId,n.instanceId,1,0,0,LocalCombatEventKind::SpellHit};
        event.outcome=LocalMeleeOutcome::Reflect;event.attackType=LocalCombatAttackType::Magic;
        event.spellTypeMask=4;event.reflectionOnly=true;event.reflectionSource=target.guid;
        emitCombatEvent(event,players);
    }
    /// SpellEffectInfo::CalcValue for one damage effect of a creature spell:
    /// the die roll, the level term (caster level clamped to [BaseLevel,
    /// MaxLevel] minus max(BaseLevel, SpellLevel), truncated), then creature
    /// scaling only for an effect without RealPointsPerLevel.
    uint32_t npcEffectValue(const LocalRealmNpc& n,const LocalSpellDefinition& d,uint32_t low,uint32_t high,float perLevel,bool scalable=true) {
        const uint32_t rolled=low+(high>low?meleeRoll(high-low):0);
        int32_t level=int32_t(n.level);
        if(d.maxLevel&&level>int32_t(d.maxLevel))level=int32_t(d.maxLevel);
        else if(level<int32_t(d.baseLevel))level=int32_t(d.baseLevel);
        level-=int32_t(std::max<uint32_t>(d.baseLevel,d.spellLevel));
        const int64_t base=int64_t(rolled)+(perLevel!=0?int64_t(int32_t(float(level)*perLevel)):0);
        float value=float(base);
        if(perLevel==0&&scalable)value*=localNpcSpellDamageScale(smartOwner(n),n.level,d.npcScales,d.spellLevel);
        return uint32_t(std::clamp(value,0.f,1000000.f));
    }
    uint32_t npcSpellRawDamage(const LocalRealmNpc& n,const LocalSpellDefinition& d) {
        return npcEffectValue(n,d,d.damage,std::max(d.damage,d.damageMax),d.damagePerLevel);
    }
    LocalMeleeOutcome npcSpellReflectedHit(LocalRealmNpc& n,const LocalSpellDefinition& d,
                                         const std::vector<LocalRealmPlayer*>& players,uint64_t reflectorGuid) {
        if(n.dead||!n.health||!d.npcOnly||!d.clientSpell||!d.unsupportedReason.empty()||d.sourceDamageClass!=1||!d.damage)
            return LocalMeleeOutcome::Miss;
        const uint32_t raw=npcSpellRawDamage(n,d),effective=std::min(raw,n.health);
        // Unit::SpellDoneCritChance returns -100 for an unowned creature. The
        // reflector's intellect, spellpower and talents never supply a crit.
        if(raw&&!n.lootOwner)n.lootOwner=reflectorGuid;
        npcLowerPlayerDamageReq(n,raw,true); // the reflector is the attacker
        if(raw>=n.health) {
            auto* credit=player(reflectorGuid,players);
            if(!credit)credit=player(n.lootOwner,players);
            if(credit)kill(n,*credit,players);
            else if(const auto* def=content->npc(n.entry)) {
                // The returning missile survives a departed reflector. No
                // unavailable player is recreated merely to award rewards.
                n.health=0;n.dead=true;n.combatEpoch=allocateNpcEpoch();n.targetGuid=0;n.threat={};
                n.snares.clear();n.damageAuras.clear();n.stormstrikeAuras.clear();localResetNpcSpellState(n);
                n.lootable=!def->loot.empty()||def->money;n.respawnTimer=def->respawnSeconds;
                respawnAt[n.guid]=now+def->respawnSeconds;
                for(auto* p:players)if(p) {
                    if(p->attackTarget==n.guid)p->attackTarget=0;
                    if(p->rangedTarget==n.guid)localStopRangedAuto(*p);
                    if(p->comboTarget==n.guid)clearLocalCombo(*p);
                }
                emitCombatEvent(localKillProcEvent(n.guid,n.guid,n.mapId,n.instanceId,false,n.level,false),players);
                emitCombatEvent(localDeathProcEvent(n.guid,n.mapId,n.instanceId,false,n.level),players);
            }
        } else n.health-=raw;
        // Spell.cpp retains the NPC as damage/proc caster; reflectionSource is
        // explanatory attribution only and never enters the caster-owner pair.
        LocalCombatEvent event{0,n.guid,n.guid,d.id,n.mapId,n.instanceId,raw,effective,0,LocalCombatEventKind::SpellDamage,n.dead};
        event.attackType=LocalCombatAttackType::Magic;event.spellTypeMask=1;event.reflectionSource=reflectorGuid;
        emitCombatEvent(event,players);return LocalMeleeOutcome::Reflect;
    }
    /// Unit::MeleeDamageBonusTaken / SpellDamageBonusTaken on a player from the
    /// creature views it carries: flat MOD_DAMAGE_TAKEN, then MOD_DAMAGE_PERCENT_TAKEN.
    static uint32_t npcDamageTakenByPlayer(const LocalRealmPlayer& p,uint32_t damage,uint32_t schoolMask) {
        const auto m=localPlayerViewModifiers(p,uint8_t(schoolMask&127u));
        if(!m.damageTakenFlat&&!m.damageTakenPct)return damage;
        const int64_t flat=std::max<int64_t>(0,int64_t(damage)+m.damageTakenFlat);
        return uint32_t(std::clamp<int64_t>(int64_t(float(flat)*(1.f+float(std::max(-99,m.damageTakenPct))/100.f)),0,1000000));
    }
    /// Unit::DealDamage: any damage (an absorbed hit included) removes the
    /// controls carrying AURA_INTERRUPT_FLAG_TAKE_DAMAGE unless the damaging
    /// spell has SPELL_ATTR4_DAMAGE_DOESNT_BREAK_AURAS.
    void npcBreakControlsOnDamage(LocalRealmPlayer& p,const LocalSpellDefinition* damaging) {
        if(damaging&&damaging->sourceDamageDoesNotBreakAuras)return;
        // Entries end in place (remaining 0) and are erased by tickNpcPeriodic,
        // which may be iterating them right now. A control carrying Spell.dbc
        // proc charges instead spends one per damaging hit at its proc chance
        // (AuraEffect::CheckEffectProc / Aura::ConsumeProcCharges) and ends
        // with the last one.
        for(auto& a:npcPeriodic)if(a.target==p.guid&&a.control&&a.remaining) {
            bool broken=a.breakOnDamage;
            if(!broken&&a.breakCharges&&(a.breakChance>=100||meleeRoll(99)<a.breakChance)){if(!--a.breakCharges)broken=true;}
            if(!broken)continue;
            npcControlEnded(a,&p);a.remaining=0;a.control=0;
            LOG_INFO("[LOCAL_NPC_AURA] broken npc=",a.caster," spell=",a.spell," target=",p.guid);
        }
    }
    /// Spell::EffectHealthLeech / HandlePeriodicHealthLeechAuraTick: the
    /// caster heals by the damage dealt times the multiplier.
    void npcLeechHeal(LocalRealmNpc& n,uint32_t spellId,uint32_t dealt,float multiplier,const std::vector<LocalRealmPlayer*>& players,bool periodic) {
        if(n.dead||!n.health||!dealt||!(multiplier>0))return;
        const uint32_t amount=npcHealingTaken(n,uint32_t(std::min<float>(1000000.f,float(dealt)*multiplier)));
        const uint32_t effective=std::min(amount,n.maxHealth-n.health);
        n.health+=effective;
        LocalCombatEvent event{0,n.guid,n.guid,spellId,n.mapId,n.instanceId,amount,effective,0,periodic?LocalCombatEventKind::PeriodicHeal:LocalCombatEventKind::DirectHeal};
        event.attackType=LocalCombatAttackType::Magic;event.spellTypeMask=2;event.positiveSpell=true;
        emitCombatEvent(event,players);
    }
    LocalMeleeOutcome npcSpellHit(LocalRealmNpc& n,LocalRealmPlayer& target,const LocalSpellDefinition& d,
                                 const std::vector<LocalRealmPlayer*>& players,bool reflected=false,bool missed=false,float damageMultiplier=1.f,
                                 int64_t rawOverride=-1) {
        if(reflected)return npcSpellReflectedHit(n,d,players,target.guid);
        // 2.37: a NONE class spell deals school damage as a MAGIC one does
        // (Unit::SpellHitResult never misses it); a power burn or a proc
        // damage aura hands its own amount in.
        if(n.dead||!n.health||target.dead||!target.health||n.mapId!=target.mapId||n.instanceId!=target.instanceId||
           !d.npcOnly||!d.clientSpell||!d.unsupportedReason.empty()||d.sourceDamageClass>1||(!d.damage&&rawOverride<0))
            return LocalMeleeOutcome::Miss;
        const auto outcome=missed?LocalMeleeOutcome::Miss:LocalMeleeOutcome::Hit;
        if(missed) {
            LocalCombatEvent event{0,n.guid,target.guid,d.id,n.mapId,n.instanceId,0,0,0,LocalCombatEventKind::SpellDamage};
            event.outcome=outcome;event.attackType=LocalCombatAttackType::Magic;event.spellTypeMask=4;
            emitCombatEvent(event,players);return outcome;
        }
        const uint32_t raw=rawOverride>=0?uint32_t(std::min<int64_t>(1000000,rawOverride)):uint32_t(std::min<float>(1000000.f,float(npcSpellRawDamage(n,d))*std::max(0.f,damageMultiplier)));
        const uint32_t attempted=localFormDamage(target,npcDamageTakenByPlayer(target,localIncomingDamageAfterTalents(target,*content,raw),d.schoolMask),true);
        const uint32_t damage=localAbsorbDamage(target,*content,attempted,d.schoolMask);
        stats(target,*content,false);
        const uint32_t effective=std::min(damage,target.health);
        npcBreakControlsOnDamage(target,&d);
        if(d.npcLeech)npcLeechHeal(n,d.id,effective,d.npcLeechMultiplier,players,false);
        npcDealtDamage(n,target,damage,d.sourceDamageClass==1?0x10000u:0x1000u,d.schoolMask,players);
        if(damage>=target.health) {
            emitCombatEvent(localKillProcEvent(n.guid,target.guid,target.mapId,target.instanceId,false,n.level,false),players);
            emitCombatEvent(localDeathProcEvent(target.guid,target.mapId,target.instanceId,true,target.level),players);
            npcKilledPlayer(n,target,players);
            target.health=0;localStopRangedAuto(target);leaveLocalForm(target);clearLocalCombo(target);
            target.dead=true;target.mountSpellId=0;target.deadTimer=0;target.attackTarget=0;
            clearCast(target,LocalCastStatus::Interrupted);selectThreatTarget(n,players);
            if(!n.targetGuid) {
                n.threat={};n.combatEpoch=allocateNpcEpoch();n.lootOwner=0;n.health=n.maxHealth;
                n.x=n.homeX;n.y=n.homeY;n.z=n.homeZ;n.snares.clear();n.damageAuras.clear();n.stormstrikeAuras.clear();
                localResetNpcSpellState(n);
            }
        } else {
            target.health-=damage;
            // Direct school damage shares the source abort-on-hit / two-delay
            // pushback rules with melee. Full absorption still causes abort.
            if(target.castingSpellId)if(const auto* casting=content->spell(target.castingSpellId)) {
                if(casting->interruptFlags&0x10u)clearCast(target,LocalCastStatus::Interrupted);
                else if(damage&&(casting->interruptFlags&2u)&&!casting->noPushback&&target.castPushbackCount<2) {
                    ++target.castPushbackCount;
                    const auto reduction=std::min(100u,localTalentPushbackReduction(target,*content,*casting)+localProcPushbackReduction(target,*content,*casting));
                    const auto room=target.castTotalMs>target.castRemainingMs?target.castTotalMs-target.castRemainingMs:0u;
                    const auto delay=std::min(500u*(100u-reduction)/100u,room);
                    target.castRemainingMs+=delay;target.castPushbackMs+=delay;
                }
            }
            if(damage&&target.resourceType==LocalResourceType::Rage)
                target.mana=std::min(target.maxMana,target.mana+std::min(10u,damage/4+1));
        }
        LocalCombatEvent event{0,n.guid,target.guid,d.id,target.mapId,target.instanceId,attempted,effective,attempted-damage,
            LocalCombatEventKind::SpellDamage,target.dead};
        event.attackType=LocalCombatAttackType::Magic;event.spellTypeMask=1;
        emitCombatEvent(event,players);return outcome;
    }
    /// A creature melee or ranged special against a player
    /// (Spell::DoAllEffectOnTarget for DmgClass MELEE/RANGED).
    /// Unit::MeleeSpellHitResult decides the whole spell;
    /// Spell::EffectWeaponDmg is the creature's weapon damage (the local
    /// creature swing value) with its fixed bonus and percentage in effect
    /// order, school damage is CalcValue. Then MeleeDamageBonusTaken, armor for
    /// physical damage, Unit::isSpellBlocked's separate partial block and
    /// absorption; creatures never crit with spells. The aura part lands on a
    /// survivor. Parry haste belongs to white swings only.
    LocalMeleeOutcome npcRollMeleeSpell(const LocalRealmNpc& n,const LocalRealmPlayer& target,const LocalSpellDefinition& d) {
        return localRollNpcMeleeSpell(n,target,localMeleeStats(target,*content),d,meleeRoll(10000));
    }
    LocalMeleeOutcome npcMeleeSpellImpact(LocalRealmNpc& n,LocalRealmPlayer& target,const LocalSpellDefinition& d,
                                          const std::vector<LocalRealmPlayer*>& players,uint8_t launchOutcome=0,float damageMultiplier=1.f) {
        const auto* def=content->npc(n.entry);
        const bool weapon=d.npcWeaponEffect||d.npcWeaponPercent;
        // 2.37: a weapon-damage effect on a NONE / MAGIC class spell reaches
        // this path too, its hit result handed in from the magic roll.
        const bool meleeClass=d.sourceDamageClass==2||d.sourceDamageClass==3;
        if(!def||n.dead||!n.health||target.dead||!target.health||n.mapId!=target.mapId||n.instanceId!=target.instanceId||
           !d.npcOnly||!d.clientSpell||!d.unsupportedReason.empty()||(!meleeClass&&!(weapon&&launchOutcome)))return LocalMeleeOutcome::Miss;
        const auto meleeStats=localMeleeStats(target,*content);
        const auto attackType=d.sourceDamageClass==3?LocalCombatAttackType::Ranged:meleeClass?LocalCombatAttackType::Melee:LocalCombatAttackType::Magic;
        const auto outcome=launchOutcome?LocalMeleeOutcome(launchOutcome-1):localRollNpcMeleeSpell(n,target,meleeStats,d,meleeRoll(10000));
        const bool damaging=weapon||d.damage;
        if(outcome!=LocalMeleeOutcome::Hit) {
            LocalCombatEvent event{0,n.guid,target.guid,d.id,n.mapId,n.instanceId,0,0,0,
                damaging?LocalCombatEventKind::SpellDamage:LocalCombatEventKind::SpellHit};
            event.outcome=outcome;event.attackType=attackType;event.spellTypeMask=damaging?1:4;
            emitCombatEvent(event,players);
            LOG_INFO("[LOCAL_NPC_MELEE_SPELL] npc=",n.guid," spell=",d.id," target=",target.guid," outcome=",int(outcome));
            return outcome;
        }
        if(!damaging) return npcAuraApply(n,target,d,players,false,true);
        uint32_t raw=0;
        if(weapon) {
            int64_t w=int64_t(npcMeleeDamage(n,def->damage));
            const int64_t bonus=d.npcWeaponEffect?int64_t(npcEffectValue(n,d,d.npcWeaponBonus,std::max(d.npcWeaponBonus,d.npcWeaponBonusMax),
                                                                     d.npcWeaponBonusPerLevel,d.npcWeaponScales)):0;
            const float percent=d.npcWeaponPercent?float(d.npcWeaponPercent)/100.f:1.f;
            if(d.npcWeaponPercentFirst){w=int64_t(int32_t(float(w)*percent));w+=bonus;}
            else {w+=bonus;w=int64_t(int32_t(float(w)*percent));}
            raw=uint32_t(std::clamp<int64_t>(w,0,1000000));
        } else raw=npcSpellRawDamage(n,d);
        raw=uint32_t(std::min<float>(1000000.f,float(raw)*std::max(0.f,damageMultiplier)));
        const bool physical=(d.schoolMask&1u)!=0;
        uint32_t reduced=npcDamageTakenByPlayer(target,localIncomingDamageAfterTalents(target,*content,raw),d.schoolMask?d.schoolMask:1u);
        if(physical&&!d.directIgnoresArmor)reduced=localArmorReducedDamage(reduced,localMeleeArmor(target,*content),n.level);
        const uint32_t attempted=localFormDamage(target,reduced,true);
        const uint32_t blocked=physical&&meleeClass&&localNpcMeleeSpellBlocked(n,target,meleeStats,d,meleeRoll())?std::min(attempted,meleeStats.blockValue):0u;
        const uint32_t damage=localAbsorbDamage(target,*content,attempted-blocked,d.schoolMask);
        stats(target,*content,false);
        const uint32_t effective=std::min(damage,target.health);
        npcBreakControlsOnDamage(target,&d);
        if(d.npcLeech)npcLeechHeal(n,d.id,effective,d.npcLeechMultiplier,players,false);
        npcDealtDamage(n,target,damage,d.sourceDamageClass==3?0x100u:meleeClass?0x10u:d.sourceDamageClass==1?0x10000u:0x1000u,d.schoolMask?d.schoolMask:1u,players);
        if(damage>=target.health) {
            emitCombatEvent(localKillProcEvent(n.guid,target.guid,target.mapId,target.instanceId,false,n.level,false),players);
            emitCombatEvent(localDeathProcEvent(target.guid,target.mapId,target.instanceId,true,target.level),players);
            npcKilledPlayer(n,target,players);
            target.health=0;localStopRangedAuto(target);leaveLocalForm(target);clearLocalCombo(target);
            target.dead=true;target.mountSpellId=0;target.deadTimer=0;target.attackTarget=0;
            clearCast(target,LocalCastStatus::Interrupted);clearLocalTravelMotion(target);localCaptureCorpse(target);
            selectThreatTarget(n,players);
            if(!n.targetGuid) {
                n.threat={};n.combatEpoch=allocateNpcEpoch();n.lootOwner=0;n.health=n.maxHealth;
                n.x=n.homeX;n.y=n.homeY;n.z=n.homeZ;n.snares.clear();n.damageAuras.clear();n.stormstrikeAuras.clear();
                localResetNpcSpellState(n);
            }
        } else {
            target.health-=damage;
            if(target.castingSpellId)if(const auto* casting=content->spell(target.castingSpellId)) {
                if(casting->interruptFlags&0x10u)clearCast(target,LocalCastStatus::Interrupted);
                else if(damage&&(casting->interruptFlags&2u)&&!casting->noPushback&&target.castPushbackCount<2) {
                    ++target.castPushbackCount;
                    const auto reduction=std::min(100u,localTalentPushbackReduction(target,*content,*casting)+localProcPushbackReduction(target,*content,*casting));
                    const auto room=target.castTotalMs>target.castRemainingMs?target.castTotalMs-target.castRemainingMs:0u;
                    const auto delay=std::min(500u*(100u-reduction)/100u,room);
                    target.castRemainingMs+=delay;target.castPushbackMs+=delay;
                }
            }
            if(damage&&target.resourceType==LocalResourceType::Rage)
                target.mana=std::min(target.maxMana,target.mana+std::min(10u,damage/4+1));
        }
        LocalCombatEvent event{0,n.guid,target.guid,d.id,target.mapId,target.instanceId,attempted,effective,attempted-blocked-damage,
            LocalCombatEventKind::SpellDamage,target.dead,0,LocalMeleeOutcome::Hit,blocked};
        event.attackType=attackType;event.spellTypeMask=1;event.schoolMask=d.schoolMask;
        emitCombatEvent(event,players);
        LOG_INFO("[LOCAL_NPC_MELEE_SPELL] npc=",n.guid," spell=",d.id," target=",target.guid," raw=",raw," damage=",damage," blocked=",blocked);
        if(!target.dead&&npcSpellUnitAura(d))npcAuraApply(n,target,d,players,false,false);
        return LocalMeleeOutcome::Hit;
    }
    /// Unit::AttackerStateUpdate: a queued CURRENT_MELEE_SPELL is cast instead
    /// of the main-hand white swing. Spell::cast checks and takes the mana
    /// then; without it (SPELL_FAILED_NO_POWER) the swing is spent anyway. A
    /// special queued at another target is dropped and the swing is white.
    bool npcNextSwingSpecial(LocalRealmNpc& n,LocalRealmPlayer& target,const std::vector<LocalRealmPlayer*>& players) {
        if(!n.npcNextSwingSpellId)return false;
        const auto spellId=n.npcNextSwingSpellId;const auto queued=n.npcNextSwingTargetGuid;
        n.npcNextSwingSpellId=0;n.npcNextSwingTargetGuid=0;
        const auto* special=content->spell(spellId);
        if(!special||!special->npcNextSwing)return false;
        // 2.37: a self-shaped special (a next-swing buff) fires on whatever the
        // swing hits; a hostile one only at the target it was queued for.
        const bool selfShape=special->npcPositive;
        if(!selfShape&&queued!=target.guid)return false;
        const uint32_t cost=npcSpellCost(n,*special);
        if(n.npcMana<cost){LOG_INFO("[LOCAL_NPC_SPELL] cancel npc=",n.guid," spell=",spellId," reason=no-power");return true;}
        if(cost){n.npcMana-=cost;n.npcSinceManaUseMs=0;}
        const uint64_t at=selfShape?n.guid:target.guid;
        emitCombatEvent(localNpcSpellPhase(n,*special,LocalCombatEventKind::SpellCast,at),players);
        // Spell::AddUnitTarget rolls the hit result at cast (the melee roll of
        // a MELEE / RANGED class, the magic roll of a MAGIC class, none for a
        // NONE class); a cleave's chain targets and cones select their recipients then.
        uint8_t launched=0;bool missed=false;
        if(!selfShape) {
            if(special->sourceDamageClass>=2)launched=uint8_t(uint8_t(npcRollMeleeSpell(n,target,*special))+1);
            else if(special->sourceDamageClass==1)missed=npcSpellMisses(n,target,*special);
        }
        const auto outcome=npcSpellEffects(n,*special,at,players,false,missed,launched,0);
        emitCombatEvent(localNpcSpellPhase(n,*special,LocalCombatEventKind::SpellFinish,0,LocalMeleeOutcome::Hit),players);
        LOG_INFO("[LOCAL_NPC_SPELL] next-swing npc=",n.guid," spell=",spellId," target=",target.guid," outcome=",int(outcome));
        return true;
    }
    void orphanIgniteHit(LocalRealmNpc& n,uint32_t damage,uint64_t originalCasterGuid,
                         const std::vector<LocalRealmPlayer*>& players) {
        const auto* def=content->npc(n.entry);
        if(n.dead||!n.health||!def)return;
        damage=std::min(damage,1000000u);
        const auto effective=std::min(damage,n.health);
        // The installed periodic effect already owns its damage snapshot.
        // Aura::GetCaster may be null; never recreate a player or apply their
        // current form, armor handling, Stormstrike marks, or outgoing talents.
        npcLowerPlayerDamageReq(n,damage,false); // no attacker: counted, not a player's strike
        if(damage>=n.health) {
            auto* credit=player(n.lootOwner,players);
            if(credit&&(credit->mapId!=n.mapId||credit->instanceId!=n.instanceId))credit=nullptr;
            if(credit)kill(n,*credit,players);
            else {
                n.health=0;n.dead=true;n.combatEpoch=allocateNpcEpoch();n.targetGuid=0;n.threat={};
                n.snares.clear();n.damageAuras.clear();n.stormstrikeAuras.clear();localResetNpcSpellState(n);
                n.lootable=!def->loot.empty()||def->money;n.respawnTimer=def->respawnSeconds;
                respawnAt[n.guid]=now+def->respawnSeconds;
                for(auto* p:players)if(p) {
                    if(p->attackTarget==n.guid)p->attackTarget=0;
                    if(p->rangedTarget==n.guid)localStopRangedAuto(*p);
                    if(p->comboTarget==n.guid)clearLocalCombo(*p);
                }
                emitCombatEvent(localDeathProcEvent(n.guid,n.mapId,n.instanceId,false,n.level),players);
            }
        } else n.health-=damage;
        LocalCombatEvent event{0,0,n.guid,12654,n.mapId,n.instanceId,damage,effective,0,
            LocalCombatEventKind::PeriodicDamage,n.dead};
        event.schoolMask=4;event.spellFamily=3;event.attackType=LocalCombatAttackType::Magic;
        event.spellTypeMask=1;event.auraCasterGuid=originalCasterGuid;
        emitCombatEvent(event,players);
    }

    // Creature auras on players from a reviewed SmartAI cast: PERIODIC_DAMAGE,
    // MOD_DECREASE_SPEED and MOD_RESISTANCE (armor), one aura per spell and
    // caster holding every effect of the spell. AuraEffect::CalculateAmount
    // resolves the amounts once at application; periodic damage ticks at the
    // aura amplitude. The aura outlives its caster's death or evade and ends
    // with the target's death, map change or expiry. Authority-only;
    // presented as harmfulAuras (with the resolved slow and armor amounts).
    struct NpcPeriodic { uint64_t caster=0,target=0;uint32_t spell=0,remaining=0,next=0,interval=0,amount=0,mapId=0,instanceId=0,casterEntry=0;uint8_t casterLevel=1;
        uint8_t slowPercent=0;int32_t armor=0;uint8_t stacks=1;int8_t armorPercent=0;
        uint8_t control=0,drGroup=0;uint32_t durationMs=0;
        // 2.36: the remaining resolved amounts, the school they scope to, the
        // break-on-damage flag and a periodic trigger's spell.
        int32_t attackPower=0,damageDoneFlat=0,damageTakenFlat=0;int16_t damageDonePct=0,damageTakenPct=0,healingPct=0,hastePct=0;
        uint8_t schoolMask=0;bool breakOnDamage=false,leech=false;float leechMultiplier=0;uint32_t triggerSpell=0;
        // 2.37: cast speed, hit chance, dodge/parry/block, a magic resistance
        // with its school, disarm, and the proc charges a control spends on
        // damage taken (AuraEffect::CheckEffectProc) with their chance.
        int16_t castSpeedPct=0;int8_t hitChancePct=0,dodgePct=0,parryPct=0,blockPct=0;int32_t resistance=0;uint8_t resistanceSchool=0;
        bool disarmed=false;uint8_t breakChance=0,breakCharges=0; };
    /// Unit::ApplyDiminishingToDuration's removal side: the control's group
    /// stack drops so the 15 s window can start (Unit::ApplyDiminishingAura).
    void npcControlEnded(const NpcPeriodic& a,LocalRealmPlayer* target) {
        if(a.control&&a.drGroup&&target)localDiminishingApply(*target,LocalDiminishingGroup(a.drGroup),false,authorityClockMs);
    }
    std::vector<NpcPeriodic> npcPeriodic;
    static constexpr size_t MaxNpcPeriodicPerTarget=8;
    bool npcDebuffPresent(uint64_t target,uint32_t spellId) const {
        return std::any_of(npcPeriodic.begin(),npcPeriodic.end(),[&](const auto& a){return a.target==target&&a.spell==spellId&&a.remaining;});
    }
    /// 2.38: the aura part lands on the units a cast reaches unless it belongs
    /// to a persistent area aura (then the ground effect applies it).
    static bool npcSpellUnitAura(const LocalSpellDefinition& d) { return npcSpellHasAura(d)&&!d.npcGroundAura; }
    static bool npcSpellHasAura(const LocalSpellDefinition& d) {
        return (d.periodicDamage&&d.periodicIntervalMs&&d.durationMs)||d.npcSlowPercent||d.npcArmorAmount||d.npcArmorAmountMax||d.npcArmorPerLevel!=0||d.npcArmorPercent||d.npcArmorPercentWide||d.npcPlayerControl||
            d.npcDamageTakenFlat.set||d.npcDamageTakenPct.set||d.npcHealingPct.set||d.npcHaste.set||d.npcDamagePct.set||d.npcDamageFlat.set||
            d.npcAttackPower.set||d.npcCastSpeed.set||d.npcHitChance.set||d.npcDisarm||d.npcDodge.set||d.npcParry.set||d.npcBlock.set||d.npcResistance.set||
            (d.npcPeriodicTriggerSpellId&&d.npcPeriodicTriggerIntervalMs)||(d.npcCosmetic&&d.durationMs&&!d.npcPositive&&d.npcTargetShape!=1);
    }
    /// SpellEffectInfo::CalcValue for the armor effect: the die, then the
    /// caster level clamped to [BaseLevel, MaxLevel] minus max(BaseLevel,
    /// SpellLevel) times RealPointsPerLevel; no creature scaling. 2.37: any
    /// sign (a bonus on the creature itself, a reduction on players).
    int32_t npcArmorAmount(const LocalSpellDefinition& d,uint32_t casterLevel) {
        if(!d.npcArmorAmount&&!d.npcArmorAmountMax&&d.npcArmorPerLevel==0)return 0;
        int32_t level=int32_t(casterLevel);
        if(d.maxLevel&&level>int32_t(d.maxLevel))level=int32_t(d.maxLevel);
        else if(level<int32_t(d.baseLevel))level=int32_t(d.baseLevel);
        level-=int32_t(std::max<uint32_t>(d.baseLevel,d.spellLevel));
        const int32_t high=std::max(d.npcArmorAmount,d.npcArmorAmountMax);
        const int64_t rolled=int64_t(d.npcArmorAmount)+(high>d.npcArmorAmount?int64_t(meleeRoll(uint32_t(high-d.npcArmorAmount))):0);
        const int64_t base=rolled+int64_t(int32_t(float(level)*d.npcArmorPerLevel));
        return int32_t(std::clamp<int64_t>(base,-100000,100000));
    }
    /// Installs the aura part of a creature spell that has hit (the direct
    /// part, if any, has already been dealt). A pure aura spell reports its own
    /// SpellHit/miss event; a direct spell's hit was reported with its damage.
    LocalMeleeOutcome npcAuraApply(LocalRealmNpc& n,LocalRealmPlayer& target,const LocalSpellDefinition& d,
                                   const std::vector<LocalRealmPlayer*>& players,bool missed,bool reportHit=true) {
        if(n.mapId!=target.mapId||n.instanceId!=target.instanceId||
           !d.npcOnly||!d.clientSpell||!d.unsupportedReason.empty()||!npcSpellHasAura(d)||!d.durationMs)
            return LocalMeleeOutcome::Miss;
        LocalCombatEvent hit{0,n.guid,target.guid,d.id,n.mapId,n.instanceId,0,0,0,LocalCombatEventKind::SpellHit};
        hit.attackType=LocalCombatAttackType::Magic;hit.spellTypeMask=4;
        if(missed){if(reportHit){hit.outcome=LocalMeleeOutcome::Miss;emitCombatEvent(hit,players);}return LocalMeleeOutcome::Miss;}
        // A lethal direct part leaves nothing to attach the aura to.
        if(target.dead||!target.health||n.dead||!n.health)return LocalMeleeOutcome::Hit;
        const bool periodic=(d.periodicDamage&&d.periodicIntervalMs)||(d.npcPeriodicTriggerSpellId&&d.npcPeriodicTriggerIntervalMs);
        const uint32_t interval=d.periodicDamage?d.periodicIntervalMs:d.npcPeriodicTriggerIntervalMs;
        // Spell.cpp's cast-site diminishing: the level is read for this hit,
        // then raised (DRTYPE_ALL, or DRTYPE_PLAYER on a player - always here),
        // and the aura's duration shrinks to 100/50/25 %; at the immune rung
        // the aura does not land (SPELL_MISS_IMMUNE).
        uint32_t duration=d.durationMs;
        auto drGroup=LocalDiminishingGroup::None;
        if(d.npcPlayerControl) {
            drGroup=localDiminishingGroupForSpell(d,false);
            if(drGroup!=LocalDiminishingGroup::None) {
                const auto level=localDiminishingRead(target,drGroup,authorityClockMs);
                if(localDiminishingGroupType(drGroup)!=LocalDiminishingType::None)localDiminishingIncrement(target,drGroup,authorityClockMs);
                duration=uint32_t(float(duration)*localDiminishingMultiplier(drGroup,level,true,false));
                if(!duration) {
                    if(reportHit){hit.outcome=LocalMeleeOutcome::Immune;emitCombatEvent(hit,players);}
                    LOG_INFO("[LOCAL_NPC_AURA] immune npc=",n.guid," spell=",d.id," target=",target.guid," group=",int(drGroup));
                    return LocalMeleeOutcome::Immune;
                }
            }
        }
        // Aura::CanStackWith: the same spell from another caster stacks only
        // when the spell carries a periodic effect; otherwise it replaces.
        if(!periodic)std::erase_if(npcPeriodic,[&](const auto& a){
            const bool gone=a.target==target.guid&&a.spell==d.id&&a.caster!=n.guid;
            if(gone)npcControlEnded(a,&target);
            return gone;});
        auto existing=std::find_if(npcPeriodic.begin(),npcPeriodic.end(),[&](const auto& a){return a.caster==n.guid&&a.target==target.guid&&a.spell==d.id;});
        const bool created=existing==npcPeriodic.end();
        if(created) {
            if(std::count_if(npcPeriodic.begin(),npcPeriodic.end(),[&](const auto& a){return a.target==target.guid;})>=long(MaxNpcPeriodicPerTarget))
                return LocalMeleeOutcome::Miss;
            npcPeriodic.push_back({});existing=npcPeriodic.end()-1;
        }
        // Unit::_TryStackingOrRefreshingExistingAura -> Aura::ModStackAmount(1):
        // one more stack up to StackAmount (a refresh without one), the full
        // duration again, and every amount recalculated as CalcValue x stacks
        // (AuraEffect::CalculateAmount). AuraEffect::CalculatePeriodic keeps a
        // running PERIODIC_DAMAGE tick timer on reapplication.
        const uint8_t stacks=created?uint8_t(1):uint8_t(std::min<uint32_t>(uint32_t(existing->stacks)+1,std::max<uint32_t>(1,d.maxAuraStacks)));
        const uint32_t next=periodic?(created||!existing->next?interval:existing->next):0;
        uint32_t amount=0;
        if(d.periodicDamage&&d.periodicIntervalMs)amount=uint32_t(std::min<uint64_t>(1000000,uint64_t(npcEffectValue(n,d,d.periodicDamage,std::max(d.periodicDamage,d.periodicDamageMax),d.periodicDamagePerLevel))*stacks));
        // CalcValue's level term applies to every aura amount; creature
        // scaling to none of these (SpellInfo.cpp:461-515 lists neither).
        const auto levelTerm=[&](float perLevel){return npcEffectAmount(n,d,{0,0,perLevel,false,true});};
        const uint8_t slow=uint8_t(std::clamp<int32_t>((int32_t(d.npcSlowPercent)-levelTerm(d.npcSlowPerLevel))*stacks,d.npcSlowPercent?1:0,99));
        const int32_t armor=int32_t(std::max<int64_t>(-100000,int64_t(npcArmorAmount(d,n.level))*stacks));
        const int8_t armorPercent=int8_t(std::clamp<int32_t>((int32_t(d.npcArmorPercent)+levelTerm(d.npcArmorPercentPerLevel))*stacks,-99,d.npcArmorPercent?-1:0));
        if(!created)npcControlEnded(*existing,&target);
        *existing={n.guid,target.guid,d.id,duration,next,periodic?interval:0,amount,
                   target.mapId,target.instanceId,n.entry,n.level,slow,armor,stacks,armorPercent,
                   d.npcPlayerControl,uint8_t(drGroup),duration};
        const auto scaled=[&](const LocalSpellDefinition::NpcAmount& a){return a.set?int32_t(std::clamp<int64_t>(int64_t(npcEffectAmount(n,d,a))*stacks,-100000,100000)):0;};
        existing->attackPower=scaled(d.npcAttackPower);existing->damageDoneFlat=scaled(d.npcDamageFlat);existing->damageTakenFlat=scaled(d.npcDamageTakenFlat);
        existing->damageDonePct=int16_t(std::clamp(scaled(d.npcDamagePct),-99,1000));existing->damageTakenPct=int16_t(std::clamp(scaled(d.npcDamageTakenPct),-99,1000));
        existing->healingPct=int16_t(std::clamp(scaled(d.npcHealingPct),-100,0));existing->hastePct=int16_t(std::clamp(scaled(d.npcHaste),-100,1000));
        existing->castSpeedPct=int16_t(std::clamp(scaled(d.npcCastSpeed),-99,1000));existing->hitChancePct=int8_t(std::clamp(scaled(d.npcHitChance),-100,100));
        existing->dodgePct=int8_t(std::clamp(scaled(d.npcDodge),-100,100));existing->parryPct=int8_t(std::clamp(scaled(d.npcParry),-100,100));
        existing->blockPct=int8_t(std::clamp(scaled(d.npcBlock),-100,100));existing->resistance=scaled(d.npcResistance);existing->resistanceSchool=d.npcResistanceSchool;
        existing->disarmed=d.npcDisarm;existing->breakChance=d.npcProcBreakChance;existing->breakCharges=d.npcProcBreakCharges;
        existing->schoolMask=d.npcDamageTakenSchool?d.npcDamageTakenSchool:d.npcDamagePctSchool?d.npcDamagePctSchool:d.npcDamageFlatSchool;
        existing->breakOnDamage=d.npcPlayerControl&&d.npcBreakOnDamage;existing->leech=d.npcPeriodicLeech;existing->leechMultiplier=d.npcLeechMultiplier;
        existing->triggerSpell=d.npcPeriodicTriggerSpellId;
        if(d.npcPlayerControl) {
            if(drGroup!=LocalDiminishingGroup::None)localDiminishingApply(target,drGroup,true,authorityClockMs);
            // Unit::SetStunned / SetFeared / SetConfused: CastStop interrupts
            // every non-melee spell, Auto Shot included. A root only holds the
            // character in place; a silence stops a silence-prevented cast
            // (HandleAuraModSilence).
            if(d.npcPlayerControl==1||d.npcPlayerControl==3||d.npcPlayerControl==4){clearCast(target,LocalCastStatus::Interrupted);localStopRangedAuto(target);}
            if(d.npcPlayerControl==5&&target.castingSpellId)if(const auto* casting=content->spell(target.castingSpellId);casting&&casting->preventionType==kLocalPreventionSilence)clearCast(target,LocalCastStatus::Interrupted);
        }
        if(reportHit){hit.outcome=LocalMeleeOutcome::Hit;emitCombatEvent(hit,players);}
        // The authority reads the player's modifiers from the views, so they
        // are rebuilt at once: a swing later in this very tick already sees
        // the new amounts (Unit::ApplyModifier is immediate in the source).
        refreshHealingViews(players);
        LOG_INFO("[LOCAL_NPC_AURA] apply npc=",n.guid," spell=",d.id," target=",target.guid," periodic=",amount,
                 " slow=",unsigned(existing->slowPercent)," armor=",existing->armor," armorPct=",int(existing->armorPercent)," control=",unsigned(existing->control)," stacks=",unsigned(stacks)," durationMs=",duration);
        return LocalMeleeOutcome::Hit;
    }
    LocalMeleeOutcome npcPeriodicApply(LocalRealmNpc& n,LocalRealmPlayer& target,const LocalSpellDefinition& d,
                                       const std::vector<LocalRealmPlayer*>& players,bool missed) {
        return npcAuraApply(n,target,d,players,missed,true);
    }
    /// One periodic tick against a player: shapeshift/talent/absorb as the
    /// direct creature spell, then death through the ordinary player path.
    bool npcPeriodicTick(const NpcPeriodic& a,LocalRealmPlayer& target,const std::vector<LocalRealmPlayer*>& players) {
        const auto* d=content->spell(a.spell);if(!d)return false;
        if(a.triggerSpell) {
            // HandlePeriodicTriggerSpellAuraTick: the creature casts the
            // triggered spell at the aura's target.
            auto* caster=npc(a.caster);
            const auto* triggered=content->spell(a.triggerSpell);
            if(!caster||caster->dead||!npcSpellUsable(triggered,1))return false;
            emitCombatEvent(localNpcSpellPhase(*caster,*triggered,LocalCombatEventKind::SpellCast,target.guid),players);
            bool tMissed=false;uint8_t tMelee=0;
            if(triggered->npcTargetShape==0&&!triggered->npcPositive) {
                tMissed=triggered->sourceDamageClass==1&&npcSpellMisses(*caster,target,*triggered);
                tMelee=triggered->sourceDamageClass>=2?uint8_t(uint8_t(npcRollMeleeSpell(*caster,target,*triggered))+1):0;
            }
            npcSpellEffects(*caster,*triggered,target.guid,players,false,tMissed,tMelee,1);
            emitCombatEvent(localNpcSpellPhase(*caster,*triggered,LocalCombatEventKind::SpellFinish,0,LocalMeleeOutcome::Hit),players);
            return true;
        }
        if(!a.amount)return false;
        // HandlePeriodicDamageAurasTick: SpellDamageBonusTaken, then armor for a
        // physical tick unless it bleeds (Unit::IsDamageReducedByArmor).
        uint32_t reduced=npcDamageTakenByPlayer(target,localIncomingDamageAfterTalents(target,*content,a.amount),d->schoolMask);
        if((d->schoolMask&1u)&&!d->periodicIgnoresArmor)reduced=localArmorReducedDamage(reduced,localMeleeArmor(target,*content),a.casterLevel);
        const uint32_t attempted=localFormDamage(target,reduced,true);
        const uint32_t damage=localAbsorbDamage(target,*content,attempted,d->schoolMask);
        stats(target,*content,false);
        const uint32_t effective=std::min(damage,target.health);
        npcBreakControlsOnDamage(target,d);
        if(a.leech)if(auto* caster=npc(a.caster))npcLeechHeal(*caster,a.spell,effective,a.leechMultiplier,players,true);
        if(auto* caster=npc(a.caster);caster&&!caster->dead)npcDealtDamage(*caster,target,damage,0x40000u,d->schoolMask,players);
        if(damage>=target.health) {
            emitCombatEvent(localKillProcEvent(a.caster,target.guid,target.mapId,target.instanceId,false,a.casterLevel,false),players);
            emitCombatEvent(localDeathProcEvent(target.guid,target.mapId,target.instanceId,true,target.level),players);
            target.health=0;localStopRangedAuto(target);leaveLocalForm(target);clearLocalCombo(target);
            target.dead=true;target.mountSpellId=0;target.deadTimer=0;target.attackTarget=0;
            clearCast(target,LocalCastStatus::Interrupted);clearLocalTravelMotion(target);localCaptureCorpse(target);
            if(auto* caster=npc(a.caster);caster&&!caster->dead)npcKilledPlayer(*caster,target,players);
        } else {
            target.health-=damage;
            if(damage&&target.resourceType==LocalResourceType::Rage)
                target.mana=std::min(target.maxMana,target.mana+std::min(10u,damage/4+1));
        }
        LocalCombatEvent event{0,a.caster,target.guid,a.spell,target.mapId,target.instanceId,attempted,effective,attempted-damage,
            LocalCombatEventKind::PeriodicDamage,target.dead};
        event.attackType=LocalCombatAttackType::Magic;event.spellTypeMask=1;
        emitCombatEvent(event,players);
        return true;
    }
    bool tickNpcPeriodic(uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players) {
        bool changed=false;
        for(auto& a:npcPeriodic) {
            auto* target=player(a.target,players);
            if(!target||target->dead||target->ghost||!target->health||target->mapId!=a.mapId||target->instanceId!=a.instanceId){
                if(a.remaining)npcControlEnded(a,target);
                a.remaining=0;changed=true;continue;}
            if(!a.interval){a.remaining-=std::min(elapsedMs,a.remaining);if(!a.remaining)npcControlEnded(a,target);changed=true;continue;}
            uint32_t budget=std::min(elapsedMs,a.remaining);
            while(budget&&a.remaining) {
                const auto step=std::min(budget,a.next);
                a.next-=step;a.remaining-=step;budget-=step;changed=true;
                if(!a.next) {
                    npcPeriodicTick(a,*target,players);a.next=a.interval;
                    if(target->dead){a.remaining=0;break;}
                }
            }
            if(!a.remaining)npcControlEnded(a,target);
        }
        const auto before=npcPeriodic.size();
        std::erase_if(npcPeriodic,[](const auto& a){return !a.remaining;});
        return changed||before!=npcPeriodic.size();
    }
