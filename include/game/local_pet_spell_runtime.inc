    // Authority-only preparation and missiles never survive a save/reconnect.
    // Epochs forbid a respawned creature or replacement summon inheriting them.
    struct PetCast {uint64_t epoch=0,target=0,targetEpoch=0;uint32_t spell=0,remaining=0,cost=0;float x=0,y=0,z=0;};
    struct PetMissile {uint64_t source=0,epoch=0,target=0,targetEpoch=0;uint32_t spell=0,remaining=0,map=0,instance=0,damage=0;LocalMeleeOutcome outcome=LocalMeleeOutcome::Hit;};
    std::unordered_map<uint64_t,PetCast> petCasts;
    std::vector<PetMissile> petMissiles;
    std::unordered_map<uint64_t,double> petLastManaUse;
    void regeneratePetMana(LocalRealmPet& summon,uint32_t elapsedMs) {
        if(summon.dead||summon.entry!=416||summon.resourceType!=0||!summon.maxPower)return;
        // Creature::Regenerate(POWER_MANA), Imp branch, every 2000 ms.
        summon.powerRegenElapsedMs+=std::min(elapsedMs,2000u);
        if(summon.powerRegenElapsedMs<2000)return;
        summon.powerRegenElapsedMs-=2000;
        const auto* row=localPetLevelStats(summon.entry,summon.level);if(!row)return;
        const auto used=petLastManaUse.find(summon.guid);
        const bool lastManaUse=used!=petLastManaUse.end()&&now-used->second<5.0;
        const auto amount=uint32_t(float(row->stat(4))/(lastManaUse?8.f:5.f)+17.f);
        summon.power=std::min(summon.maxPower,summon.power+amount);
    }
    bool petSpellTargetReady(const LocalRealmPet& summon,const LocalRealmNpc& target,const LocalSpellDefinition& spell,const LocalRealmPlayer& owner,bool completion=false)const {
        if(summon.dead||!summon.health||target.dead||!target.health||target.transportEntry||
           target.mapId!=summon.mapId||target.instanceId!=summon.instanceId)return false;
        float range=localSpellMaximumRange(owner,*content,spell);
        if(completion)range+=std::min(3.f,range*.1f);
        const auto* petTemplate=localPetTemplate(summon.entry);
        const float casterReach=petTemplate?petTemplate->combatReach:kLocalWorldObjectSize;
        const float targetReach=localCreatureCombatReach(content->npc(target.entry));
        const float squared=distance2(summon.x,summon.y,summon.z,target.x,target.y,target.z);
        return localWithinCombatRange(squared,range,casterReach,targetReach)&&
            localLineOfSightReady(&collision,summon.mapId,summon.x,summon.y,summon.z,
                                 target.x,target.y,target.z,localCreatureCombatReach(content->npc(target.entry)));
    }
    bool startPetSpell(LocalRealmPet& summon,LocalRealmPlayer& owner,LocalRealmNpc& target,uint32_t spellId,std::string& why) {
        if(!localPetOwnerEligible(summon,&owner)){why="Your summon cannot act while its owner is unavailable";return false;}
        const auto* spell=content->spell(spellId);
        if(spellId!=localPetFireboltSpell(summon.entry,summon.level)||!spell||!spell->npcOnly||!spell->unsupportedReason.empty())
            {why="Your summon does not know that spell";return false;}
        if(petCasts.contains(summon.guid)){why="Your summon is already casting";return false;}
        if(petMissiles.size()>=256){why="Too many spells in flight";return false;}
        if(!petSpellTargetReady(summon,target,*spell,owner)){why="Your summon cannot see or reach that target";return false;}
        const auto cost=localSpellBaseResourceCost(owner,*content,*spell);
        if(summon.resourceType!=0||summon.power<cost){why="Your summon needs more mana";return false;}
        const uint32_t castMs=localSpellCastTime(owner,*content,*spell);
        petCasts[summon.guid]={summon.summonEpoch,target.guid,target.combatEpoch,spellId,castMs,cost,summon.x,summon.y,summon.z};
        summon.orientation=std::atan2(target.y-summon.y,target.x-summon.x);
        why="Your summon is casting "+spell->name;
        return true;
    }
    void petSpellImpact(const PetMissile& missile,const std::vector<LocalRealmPlayer*>& players) {
        auto* summon=pet(missile.source);auto* target=npc(missile.target);
        const auto* spell=content->spell(missile.spell);
        auto* owner=summon?localPetOwner(*summon,players):nullptr;
        if(!summon||!owner||summon->summonEpoch!=missile.epoch||!target||target->dead||
           target->combatEpoch!=missile.targetEpoch||target->mapId!=missile.map||target->instanceId!=missile.instance||
           summon->mapId!=missile.map||summon->instanceId!=missile.instance||!spell)return;
        const auto* definition=content->npc(target->entry);if(!definition)return;
        auto outcome=missile.outcome;
        if(!localOutcomeNullifiesDamage(outcome)&&localNpcImmuneToSpell(*definition,*spell,false))outcome=LocalMeleeOutcome::Immune;
        uint32_t damage=localOutcomeNullifiesDamage(outcome)?0:missile.damage,resisted=0;
        if(damage&&localPartialResistApplies(spell->schoolMask,true,spell->sourceNoCastLog,false)) {
            resisted=localResistedAmount(damage,localPartialResistBucket(localAverageResist(
                localResistanceForMask(definition->resistances,spell->schoolMask),summon->level,target->level),float(meleeRoll())/10000.f));
            damage-=resisted;if(resisted&&!damage)outcome=LocalMeleeOutcome::Resist;
        }
        if(!localOutcomeNullifiesDamage(outcome))breakNpcControlsOnDamage(*target,spell->id,spell);
        if(damage&&!target->lootOwner)target->lootOwner=owner->guid;
        if(!localOutcomeNullifiesDamage(outcome))addThreat(*target,summon->guid,std::max<uint64_t>(1,uint64_t(damage)*1000));
        selectThreatTarget(*target,players);
        const uint32_t effective=std::min(damage,target->health);
        if(damage>=target->health)kill(*target,*owner,players,summon->guid,summon->level);else target->health-=damage;
        LocalCombatEvent event{0,summon->guid,target->guid,spell->id,target->mapId,target->instanceId,
            damage+resisted,effective,0,LocalCombatEventKind::SpellDamage,target->dead};
        event.outcome=outcome;event.resisted=resisted;event.attackType=LocalCombatAttackType::Magic;
        event.spellTypeMask=localOutcomeNullifiesDamage(outcome)?4:1;emitCombatEvent(event,players);
        event.target=0;event.attempted=0;event.effective=0;event.resisted=0;event.kind=LocalCombatEventKind::SpellFinish;
        event.spellTypeMask=7;emitCombatEvent(event,players);
    }
    void tickPetSpells(uint32_t elapsedMs,const std::vector<LocalRealmPlayer*>& players,const LocalGameplay& gameplay) {
        // Existing missiles advance first: a newly launched missile is not
        // charged the same frame's elapsed time twice.
        for(auto& missile:petMissiles)missile.remaining-=std::min(missile.remaining,elapsedMs);
        std::vector<PetMissile> landed;
        std::erase_if(petMissiles,[&](const auto& missile){if(missile.remaining)return false;landed.push_back(missile);return true;});
        for(const auto& missile:landed)petSpellImpact(missile,players);
        for(auto it=petCasts.begin();it!=petCasts.end();) {
            auto* summon=pet(it->first);auto* target=npc(it->second.target);
            auto* owner=summon?localPetOwner(*summon,players):nullptr;
            const auto* spell=content->spell(it->second.spell);auto& cast=it->second;
            const bool moved=summon&&distance2(summon->x,summon->y,summon->z,cast.x,cast.y,cast.z)>.01f;
            if(!summon||!owner||!spell||summon->summonEpoch!=cast.epoch||!localPetOwnerEligible(*summon,owner)||
               !target||target->combatEpoch!=cast.targetEpoch||!gameplay.canAttack(*owner,*target)||moved||!petSpellTargetReady(*summon,*target,*spell,*owner,true)) {
                it=petCasts.erase(it);continue;
            }
            cast.remaining-=std::min(cast.remaining,elapsedMs);
            if(cast.remaining){++it;continue;}
            if(summon->power<cast.cost){it=petCasts.erase(it);continue;}
            summon->power-=cast.cost;petLastManaUse[summon->guid]=now;
            LocalMeleeOutcome outcome=LocalMeleeOutcome::Hit;
            const auto* definition=content->npc(target->entry);
            if(definition&&localNpcImmuneToSpell(*definition,*spell,false))outcome=LocalMeleeOutcome::Immune;
            else {
                const int difference=int(target->level)-int(summon->level);
                // Imp passive 61013 / spell_pet_hit_expertise_scalling
                // truncates the owner's spell-hit percentage to an aura int.
                const int inheritedHit=int(localSpellHitRatingPercent(*owner,*content));
                const int hit=(difference<3?96-difference:94-(difference-2)*11)+inheritedHit;
                if(meleeRoll()+1<uint32_t(10000-std::clamp(hit*100,100,10000)))outcome=LocalMeleeOutcome::Miss;
            }
            const auto level=std::min(uint32_t(summon->level),spell->maxLevel);
            const uint32_t base=spell->damage+meleeRoll(std::max(spell->damage,spell->damageMax)-spell->damage);
            uint32_t damage=uint32_t(std::clamp(int64_t(base)+int64_t(float(level>spell->baseLevel?level-spell->baseLevel:0)*spell->damagePerLevel),int64_t(0),int64_t(1000000)));
            // spell_warl_generic_scaling::CalculateSPAmount: 15% of the
            // owner's maximum Fire/Shadow spell power. The currently admitted
            // source equipment gives both schools the same ordinary SP term.
            // spell_bonus_data overrides the DBC 0.714286 with exactly 0.714
            // for every Firebolt rank; creatures have NO level penalty.
            const int32_t inheritedSpellPower=std::max(0,localWardEquipmentSpellPower(*owner,*content))*15/100;
            damage+=uint32_t(float(inheritedSpellPower)*.714f);
            // SpellModOwner is the player, while level, mana, attack timing,
            // source identity and damage recipient remain the creature's.
            damage=uint32_t(std::clamp(int64_t(damage)*(100+localTalentCastModifier(*owner,*content,*spell,0,true))/100+
                localTalentCastModifier(*owner,*content,*spell,0),int64_t(0),int64_t(1000000)));
            // Unit constructor m_baseSpellCritChance=5; SpellDoneCritChance
            // uses that for an owned creature, never the player's intellect or
            // entire player critical chance. No pet critical aura is admitted.
            if(outcome==LocalMeleeOutcome::Hit&&meleeRoll()<500) {
                outcome=LocalMeleeOutcome::Critical;damage=localMagicCriticalAmount(damage);
            }
            LocalCombatEvent event{0,summon->guid,target->guid,spell->id,summon->mapId,summon->instanceId,0,0,0,LocalCombatEventKind::SpellCast};
            event.attackType=LocalCombatAttackType::Magic;event.spellTypeMask=7;emitCombatEvent(event,players);
            if(!localOutcomeNullifiesDamage(outcome))addThreat(*target,summon->guid,uint64_t(spell->spellLevel)*1000);
            const auto travel=uint32_t(std::max(5.f,std::sqrt(distance2(summon->x,summon->y,summon->z,target->x,target->y,target->z)))*1000.f/spell->sourceProjectileSpeed);
            petMissiles.push_back({summon->guid,summon->summonEpoch,target->guid,target->combatEpoch,spell->id,travel,
                                  summon->mapId,summon->instanceId,damage,outcome});
            it=petCasts.erase(it);
        }
    }
