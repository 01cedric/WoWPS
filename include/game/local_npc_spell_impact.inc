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
                binaryAverage=localAverageResist(localResistanceForMask(definition->resistances,d.schoolMask),
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
    uint32_t npcSpellRawDamage(const LocalRealmNpc& n,const LocalSpellDefinition& d) {
        const auto high=std::max(d.damage,d.damageMax);
        const uint32_t rolled=d.damage+(high>d.damage?meleeRoll(high-d.damage):0);
        const auto level=d.maxLevel?std::min(uint32_t(n.level),d.maxLevel):uint32_t(n.level);
        // CalcValue truncates the level addition before applying creature base
        // damage scaling. Scale and random amount belong to the original NPC.
        const int64_t leveled=int64_t(rolled)+int64_t(float(level>d.baseLevel?level-d.baseLevel:0)*d.damagePerLevel);
        return uint32_t(std::clamp(float(leveled)*localNpcSpellDamageScale(n.entry,n.level,d.id),0.f,1000000.f));
    }
    LocalMeleeOutcome npcSpellReflectedHit(LocalRealmNpc& n,const LocalSpellDefinition& d,
                                         const std::vector<LocalRealmPlayer*>& players,uint64_t reflectorGuid) {
        if(n.dead||!n.health||!d.npcOnly||!d.clientSpell||!d.unsupportedReason.empty()||d.sourceDamageClass!=1||!d.damage)
            return LocalMeleeOutcome::Miss;
        const uint32_t raw=npcSpellRawDamage(n,d),effective=std::min(raw,n.health);
        // Unit::SpellDoneCritChance returns -100 for an unowned creature. The
        // reflector's intellect, spellpower and talents never supply a crit.
        if(raw&&!n.lootOwner)n.lootOwner=reflectorGuid;
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
    LocalMeleeOutcome npcSpellHit(LocalRealmNpc& n,LocalRealmPlayer& target,const LocalSpellDefinition& d,
                                 const std::vector<LocalRealmPlayer*>& players,bool reflected=false,bool missed=false) {
        if(reflected)return npcSpellReflectedHit(n,d,players,target.guid);
        if(n.dead||!n.health||target.dead||!target.health||n.mapId!=target.mapId||n.instanceId!=target.instanceId||
           !d.npcOnly||!d.clientSpell||!d.unsupportedReason.empty()||d.sourceDamageClass!=1||!d.damage)
            return LocalMeleeOutcome::Miss;
        const auto outcome=missed?LocalMeleeOutcome::Miss:LocalMeleeOutcome::Hit;
        if(missed) {
            LocalCombatEvent event{0,n.guid,target.guid,d.id,n.mapId,n.instanceId,0,0,0,LocalCombatEventKind::SpellDamage};
            event.outcome=outcome;event.attackType=LocalCombatAttackType::Magic;event.spellTypeMask=4;
            emitCombatEvent(event,players);return outcome;
        }
        const uint32_t raw=npcSpellRawDamage(n,d);
        const uint32_t attempted=localFormDamage(target,localIncomingDamageAfterTalents(target,*content,raw),true);
        const uint32_t damage=localAbsorbDamage(target,*content,attempted,d.schoolMask);
        stats(target,*content,false);
        const uint32_t effective=std::min(damage,target.health);
        if(damage>=target.health) {
            emitCombatEvent(localKillProcEvent(n.guid,target.guid,target.mapId,target.instanceId,false,n.level,false),players);
            emitCombatEvent(localDeathProcEvent(target.guid,target.mapId,target.instanceId,true,target.level),players);
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
    void orphanIgniteHit(LocalRealmNpc& n,uint32_t damage,uint64_t originalCasterGuid,
                         const std::vector<LocalRealmPlayer*>& players) {
        const auto* def=content->npc(n.entry);
        if(n.dead||!n.health||!def)return;
        damage=std::min(damage,1000000u);
        const auto effective=std::min(damage,n.health);
        // The installed periodic effect already owns its damage snapshot.
        // Aura::GetCaster may be null; never recreate a player or apply their
        // current form, armor handling, Stormstrike marks, or outgoing talents.
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
