#include "local_group_rewards_fixture.hpp"
#include "game/local_melee.hpp"
#include "game/local_proc_rules.hpp"
#include "game/local_stat_auras.hpp"
#include <iostream>

int main() {
    auto c=rewardContent();c->npcs[0].health=1000000;c->npcs[0].armor=0;
    LocalItemDefinition weapon;weapon.id=25;weapon.inventoryType=21;weapon.name="Worn Shortsword";weapon.stack=1;c->items.push_back(weapon);
    std::sort(c->items.begin(),c->items.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    LocalSpellDefinition parent;parent.id=23881;parent.name="Bloodthirst";parent.clientSpell=true;
    parent.meleeSpecialProfile=1;parent.triggeredAuraSpellId=23885;parent.damage=50;
    parent.schoolMask=1;parent.spellFamily=4;parent.spellFamilyFlags={0,0x400,0};parent.range=5;
    parent.resourceType=1;parent.mana=20;parent.cooldownCategory=971;parent.categoryCooldownMs=4000;parent.globalCooldownMs=1500;
    parent.allowableClasses=1;parent.talentId=167;parent.talentRank=1;
    parent.requiredItemClass=2;parent.requiredItemSubclasses=173555;
    LocalSpellDefinition aura;aura.id=23885;aura.name="Bloodthirst (triggered)";aura.clientSpell=true;aura.triggeredOnly=true;
    aura.allowableClasses=1;aura.durationMs=8000;aura.schoolMask=1;aura.spellFamily=4;
    aura.proc.effect=LocalProcEffect::HealOwnerPctMaxHealth;aura.proc.spellId=23880;
    aura.proc.flags=20;aura.proc.hitMask=LocalProcHitNormal|LocalProcHitCritical|LocalProcHitAbsorb;aura.proc.chance=100;aura.proc.charges=3;aura.proc.amount=1;aura.proc.schoolMask=1;aura.proc.spellFamily=4;
    assert(validLocalProc(parent)&&validLocalProc(aura));
    auto bad=aura;bad.triggeredOnly=false;assert(!validLocalProc(bad));bad=aura;bad.proc.allowTriggered=true;assert(!validLocalProc(bad));
    // Original spell damage retains a landed result on full block; white
    // swings do not. The generated done-event mask also admits full absorb.
    LocalCombatEvent blocked{};blocked.source=1;blocked.target=10;blocked.kind=LocalCombatEventKind::SpellDamage;
    blocked.attackType=LocalCombatAttackType::Melee;blocked.spell=23881;blocked.attempted=50;blocked.blocked=50;
    // A blocked spell still has SpellInfo: source classifies its zero damage
    // as OTHER. A spell event without an identity is not a valid fixture.
    assert(localProcEventSpellTypeMask(blocked)==4);
    assert((localProcEventHitMask(blocked)&(LocalProcHitNormal|LocalProcHitBlock))==(LocalProcHitNormal|LocalProcHitBlock));
    assert(localProcMatches(aura.proc,blocked,1));
    blocked.kind=LocalCombatEventKind::PlayerMelee;blocked.spell=0;assert(!localProcMatches(aura.proc,blocked,1));
    blocked.blocked=0;blocked.absorbed=50;assert(localProcMatches(aura.proc,blocked,1));
    c->spells.push_back(parent);c->spells.push_back(aura);
    std::sort(c->spells.begin(),c->spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    LocalGameplay game;game.useContent(c);
    auto p=rewardPlayer(1);p.classId=1;p.level=80;p.resourceType=LocalResourceType::Rage;p.maxMana=p.mana=100;
    p.health=500;p.maxHealth=1000;p.knownSpells={1,23881};p.talents={{167,1}};p.inventory={{25,1}};p.equipment[15]=25;
    auto n=rewardNpc();n.health=n.maxHealth=1000000;n.level=80;n.x=1;n.attackTimer=1000;
    std::string message;uint64_t sequence=0;unsigned hits=0,avoids=0,criticals=0;
    auto clearRecovery=[&]{p.cooldowns.clear();p.categoryCooldowns.clear();p.globalCooldownMs=0;p.attackTarget=0;p.mana=100;};
    auto cast=[&]{clearRecovery();game.setRemoteNpcs({n});if(!game.execute(p,{LocalAction::CastSpell,n.guid,23881},{&p},message)){std::cerr<<"CAST REJECTED: "<<message<<"\n";std::abort();}};
    for(unsigned iteration=0;iteration<256;++iteration) {
        p.health=500;p.maxHealth=1000;cast();
        bool landed=false,critical=false;unsigned heals=0,casts=0,finishes=0;
        for(const auto& e:game.combatEvents())if(e.sequence>sequence) {
            if(e.kind==LocalCombatEventKind::SpellDamage&&e.spell==23881) {
                critical=e.outcome==LocalMeleeOutcome::Critical;landed=!localMeleeAvoided(e.outcome);
                assert(e.attackType==LocalCombatAttackType::Melee&&e.weaponPeriodMs==1900);
                assert(e.effective+e.blocked==(landed?localMeleeSpecialAmount(localMeleeStats(p,*c).attackPower,50)*(critical?2u:1u):0));
                assert(!e.blocked||e.blocked==40);
            }
            if(e.kind==LocalCombatEventKind::ProcHeal) {
                ++heals;assert(e.spell==23880&&e.auraSpell==23885&&e.source==p.guid&&e.target==p.guid);
                assert(e.attempted==10&&e.effective==10&&e.auraCasterGuid==p.guid&&e.auraOwnerGuid==p.guid&&e.procDepth==1);
                assert(e.parentSequence&&e.rootSequence==e.parentSequence);
            }
            if(e.kind==LocalCombatEventKind::SpellCast&&e.spell==23881)++casts;
            if(e.kind==LocalCombatEventKind::SpellFinish&&e.spell==23881) {
                ++finishes;assert(casts==1&&e.target==0&&e.source==p.guid&&e.spellTypeMask==7);
                assert(localProcEventHitMask(e)==(LocalProcHitNormal|(critical?LocalProcHitCritical:0u)));
            }
        }
        sequence=game.combatEvents().back().sequence;
        assert(heals==unsigned(landed)&&casts==1&&finishes==1&&p.statAuras.size()==1&&p.statAuras[0].procCharges==(landed?2:3));
        assert(p.statAuras[0].remainingMs==8000&&p.statAuras[0].procAmountSnapshot==1&&validLocalStatAuras(p));
        assert(p.health==500+10*heals&&p.comboPoints==0&&p.globalCooldownMs==1500);
        if(landed){++hits;if(critical)++criticals;assert(p.mana==80);}else ++avoids;
    }
    assert(hits&&avoids&&criticals);
    // A rounded zero-percent heal still spends a qualifying charge exactly
    // once; zero healing cannot preserve a permanent free proc.
    bool zeroHeal=false;
    for(unsigned i=0;i<32&&!zeroHeal;++i) {
        p.maxHealth=99;p.health=50;cast();
        zeroHeal=p.statAuras[0].procCharges==2;assert(p.health==50);
    }
    assert(zeroHeal);p.maxHealth=1000;p.health=500;
    // Child is inaccessible even if a malformed client supplies it in the spellbook.
    clearRecovery();p.knownSpells.push_back(23885);const auto prior=p;
    assert(!game.execute(p,{LocalAction::CastSpell,p.guid,23885},{&p},message));assert(p.health==prior.health&&p.statAuras==prior.statAuras&&p.mana==prior.mana);
    p.knownSpells.pop_back();
    // Failure before commit must not arm/refill a proc, spend rage or touch recovery.
    for(unsigned failure=0;failure<4;++failure) {
        clearRecovery();auto candidate=p;candidate.statAuras.clear();
        if(failure==0)candidate.mana=0;
        if(failure==1)candidate.inventory.clear();
        if(failure==2)candidate.talents.clear();
        if(failure==3)candidate.x=100;
        const auto mana=candidate.mana;assert(!game.execute(candidate,{LocalAction::CastSpell,n.guid,23881},{&candidate},message));
        assert(candidate.statAuras.empty()&&candidate.mana==mana&&candidate.globalCooldownMs==0&&candidate.categoryCooldowns.empty());
        if(failure==0)assert(message=="Not enough resource");
        if(failure==1)assert(message=="Invalid equipped weapon state");
        if(failure==2)assert(message=="Learn the required root talent first");
        if(failure==3)assert(message=="Spell target out of effective range");
    }
    // Hydration bounds an internal aura against its source duration without
    // adding time to a valid short save or restoring already spent charges.
    cast();p.statAuras[0].remainingMs=60000;p.statAuras[0].procCharges=1;
    game.initializePlayer(p,false);assert(p.statAuras.size()==1&&p.statAuras[0].remainingMs==8000&&p.statAuras[0].procCharges==1);
    p.statAuras[0].remainingMs=2345;game.initializePlayer(p,false);
    assert(p.statAuras[0].remainingMs==2345&&p.statAuras[0].procCharges==1);
    // Actual white swings consume remaining charges, evaluate CURRENT maxHealth,
    // consume even on overheal, and stop exactly at exhaustion.
    cast();p.attackTarget=0;game.initializePlayer(p,false);p.maxHealth=2000;p.health=500;
    p.statAuras[0].procCharges=3;p.statAuras[0].remainingMs=8000;
    unsigned remaining=3,swings=0;sequence=game.combatEvents().back().sequence;
    while(remaining&&swings++<100) {
        p.maxHealth=remaining==1?1000000:2000;p.health=remaining==1?1000000:500;game.setRemoteNpcs({n});p.attackTimer=0;p.attackTarget=10;
        game.tick(.001f,{&p});
        for(const auto& e:game.combatEvents())if(e.sequence>sequence&&e.kind==LocalCombatEventKind::ProcHeal) {
            assert(e.attempted==p.maxHealth/100);if(remaining==1)assert(e.effective==0);--remaining;
        }
        sequence=game.combatEvents().back().sequence;
        assert(p.statAuras.size()==(remaining?1u:0u));if(remaining)assert(p.statAuras[0].procCharges==remaining);
    }
    assert(!remaining);
    // Cancel, map transition, death and expiry use the real timed-aura paths.
    cast();p.attackTarget=0;assert(game.execute(p,{LocalAction::CancelStatAura,0,23885},{&p},message));assert(p.statAuras.empty());
    cast();p.attackTarget=0;++p.mapId;game.tick(.001f,{&p});assert(p.statAuras.empty());--p.mapId;
    cast();p.attackTarget=0;p.dead=true;p.health=0;game.tick(.001f,{&p});assert(p.statAuras.empty());p.dead=false;p.health=100;
    cast();p.attackTarget=0;p.statAuras[0].remainingMs=1;game.tick(.002f,{&p});assert(p.statAuras.empty());
    cast();p.attackTarget=0;game.setRemoteNpcs({});assert(game.execute(p,{LocalAction::ResetTalents},{&p},message));assert(p.statAuras.empty());
    assert(std::find(p.knownSpells.begin(),p.knownSpells.end(),23881)==p.knownSpells.end());
    std::cout<<"PASS P03 Bloodthirst runtime: AP melee damage, current-health percent heal, parent-first charge use, miss retention, cast events, recast refresh, conservation, no combo mutation, internal child denial, precommit rejection, cancel/map/death/expiry/reset lifecycle\n";
}
