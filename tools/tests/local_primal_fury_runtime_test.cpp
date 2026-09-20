#include "local_group_rewards_fixture.hpp"
#include "game/local_combo.hpp"
#include "game/local_proc_rules.hpp"
#include <iostream>

// Source profile identities/values are separately checked against real DBC by
// the import audit. This fixture exercises actual authority casts and swings.
int main() {
    auto c=rewardContent();c->npcs[0].health=1000000;
    auto& attack=c->spells[0];attack.damage=1;attack.schoolMask=1;attack.spellFamily=7;
    attack.spellFamilyFlags[0]=0x1000;attack.comboProfile=uint8_t(LocalComboProfile::Claw);attack.comboGain=1;
    LocalSpellDefinition fury;fury.id=37117;fury.passive=true;fury.talentId=801;fury.talentRank=2;fury.allowableClasses=1024;
    auto& bear=fury.proc;bear.effect=LocalProcEffect::RestorePower;bear.spellId=16959;bear.flags=87380;
    bear.amount=5;bear.chance=100;bear.resourceType=1;bear.requiredForms=144;bear.hitMask=LocalProcHitCritical;bear.spellTypeMask=1;
    auto& cat=fury.secondaryProc;cat.effect=LocalProcEffect::AddComboPoints;cat.spellId=16953;cat.flags=87376;
    cat.amount=1;cat.chance=100;cat.requiredForms=1;cat.hitMask=LocalProcHitCritical;cat.triggerSpellFamily=7;
    cat.triggerSpellFamilyFlags={233472,1024,262144};cat.range=5;
    assert(validLocalProc(fury));
    auto bad=fury;bad.secondaryProc.requiredForms=144;assert(!validLocalProc(bad));
    bad=fury;bad.secondaryProc.amount=6;assert(!validLocalProc(bad));
    bad=fury;bad.secondaryProc.charges=1;assert(!validLocalProc(bad));
    bad=fury;bad.secondaryProc.effect=LocalProcEffect::None;assert(!validLocalProc(bad));
    c->spells.push_back(fury);
    LocalGameplay game;game.useContent(c);auto p=rewardPlayer(1);p.classId=11;p.level=80;p.talents={{801,2}};
    p.formSpellId=768;p.resourceType=LocalResourceType::Energy;p.maxMana=100;p.mana=100;
    auto n=rewardNpc();n.health=n.maxHealth=1000000;n.level=1;n.attackTimer=1000;
    uint64_t sequence=0;unsigned catCrits=0,catHits=0,bearCrits=0;std::string result;
    auto cast=[&](bool expectedEnabled,bool kill=false,bool cap=false) {
        p.cooldowns.clear();p.globalCooldownMs=0;clearLocalCombo(p);game.setRemoteNpcs({n});
        if(kill){auto victim=n;victim.health=1;game.setRemoteNpcs({victim});}
        if(cap)addLocalCombo(p,game.npcs()[0],5);
        if(!game.execute(p,{LocalAction::CastSpell,10,1},{&p},result)){std::cerr<<result<<'\n';std::abort();}
        bool critical=false,landed=false;unsigned procs=0;
        for(const auto& e:game.combatEvents())if(e.sequence>sequence) {
            if(e.kind==LocalCombatEventKind::SpellDamage){critical=e.outcome==LocalMeleeOutcome::Critical;landed=e.effective>0;}
            if(e.kind==LocalCombatEventKind::ProcCombo){++procs;assert(e.spell==16953&&e.source==1&&e.target==10&&e.auraSpell==37117&&e.auraOwnerGuid==1&&e.auraCasterGuid==1&&e.parentSequence&&e.rootSequence&&e.procDepth==1);assert(e.attempted==1&&e.effective==(cap?0u:1u));assert(localProcEventFlags(e,1)==0);}
            assert(e.kind!=LocalCombatEventKind::ProcPower);
        }
        sequence=game.combatEvents().back().sequence;
        assert(procs==unsigned(expectedEnabled&&critical&&landed&&!kill));
        assert(p.comboPoints==(kill?0u:cap?5u:unsigned(landed)+procs));
        if(critical)++catCrits;else if(landed)++catHits;
    };
    for(unsigned i=0;i<256;++i)cast(true);
    assert(catCrits&&catHits);
    for(unsigned i=0;i<128;++i)cast(true,false,true);
    for(unsigned i=0;i<128;++i)cast(true,true);
    p.talents.clear();for(unsigned i=0;i<128;++i)cast(false);
    p.talents={{801,1}};for(unsigned i=0;i<128;++i)cast(false); // Current rank missing: no higher rank fallback.
    p.talents={{801,2}};c->spells[0].spellFamilyFlags={1,0,0};for(unsigned i=0;i<128;++i)cast(false);
    c->spells[0].spellFamilyFlags={0x1000,0,0};p.formSpellId=783;p.resourceType=LocalResourceType::Mana;
    for(unsigned i=0;i<128;++i)cast(false);
    // Actual autoattacks in both bear forms award five displayed rage on crit,
    // with no combo resource event. Cat autoattack crits must not add a point.
    for(auto form:{5487u,9634u,768u}) {
        p.formSpellId=form;p.resourceType=form==768?LocalResourceType::Energy:LocalResourceType::Rage;
        p.meleeForm=form;clearLocalCombo(p);p.cooldowns.clear();p.globalCooldownMs=0;
        for(unsigned i=0;i<256;++i) {
            game.setRemoteNpcs({n});p.mana=0;p.attackTimer=0;p.attackTarget=10;
            game.tick(.001f,{&p});unsigned powers=0;bool critical=false;uint32_t damage=0;
            for(const auto& e:game.combatEvents())if(e.sequence>sequence) {
                if(e.kind==LocalCombatEventKind::PlayerMelee&&e.source==1){critical=e.outcome==LocalMeleeOutcome::Critical;damage=e.effective;}
                if(e.kind==LocalCombatEventKind::ProcPower){++powers;assert(e.spell==16959&&e.effective==5&&e.auraSpell==37117);}
                assert(e.kind!=LocalCombatEventKind::ProcCombo);
            }
            sequence=game.combatEvents().back().sequence;
            assert(powers==unsigned(form!=768&&critical&&damage));
            if(form!=768){assert(p.mana==(damage?std::min(15u,damage/3+1):0)+powers*5);bearCrits+=powers;}
            assert(p.comboPoints==0);
        }
    }
    assert(bearCrits);
    std::cout<<"PASS P03 Primal Fury runtime: cat critical + base combo composition, cap, lethal hit, current rank/reset, family/form rejection, bear/dire-bear actual swings, five displayed rage, no cat autoattack combo, child attribution, bounded resource events\n";
}
