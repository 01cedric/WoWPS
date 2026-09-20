#include "local_group_rewards_fixture.hpp"
#include "game/local_proc_timing.hpp"
#include <iostream>

static unsigned incoming(uint32_t form,bool present,uint8_t fallback) {
    auto c=rewardContent();
    LocalSpellDefinition d;d.id=99;d.name="Incoming PPM component fixture";d.durationMs=60000;
    d.proc.effect=LocalProcEffect::HealOwner;d.proc.spellId=199;d.proc.flags=8;
    d.proc.amount=3;d.proc.ppm=6;d.proc.chance=fallback;d.range=100;d.buffSelfOnly=false;
    assert(validLocalProc(d));c->spells.push_back(d);
    auto owner=rewardPlayer(1),caster=rewardPlayer(2);
    owner.classId=8;caster.classId=11;caster.knownSpells.push_back(99);
    LocalGameplay game;game.useContent(c);std::string result;
    assert(game.execute(caster,{LocalAction::CastSpell,owner.guid,99},{&owner,&caster},result));
    assert(owner.statAuras.size()==1&&owner.statAuras[0].casterGuid==caster.guid);
    caster.formSpellId=form;
    caster.resourceType=form==768?LocalResourceType::Energy:LocalResourceType::Rage;
    auto n=rewardNpc();n.health=n.maxHealth=100000;n.targetGuid=owner.guid;n.threat[0]={owner.guid,1000};
    n.level=1;n.x=n.homeX=1;
    uint64_t seen=0,replay=0x9e3779b97f4a7c15ULL;
    for(const auto& e:game.combatEvents())seen=std::max(seen,e.sequence);
    unsigned eligible=0,procs=0;
    const std::vector<LocalRealmPlayer*> players=present?std::vector<LocalRealmPlayer*>{&owner,&caster}:std::vector<LocalRealmPlayer*>{&owner};
    for(unsigned i=0;i<512;++i) {
        owner.health=owner.maxHealth/2;n.attackTimer=0;game.setRemoteNpcs({n});game.tick(.001f,players);
        bool expected=false,actual=false;
        for(const auto& e:game.combatEvents())if(e.sequence>seen) {
            if(e.kind==LocalCombatEventKind::NpcMelee&&localProcMatches(d.proc,e,owner.guid)) {
                ++eligible;const auto timing=localProcTimingForCaster(present?&caster:nullptr,*c,e);
                assert(!present||timing.weaponPeriodMs==(form==768?1000u:2500u));
                expected=localRollProcBasisPoints(localProcChanceBasisPoints(d.proc,e,timing),replay);
            }
            if(e.kind==LocalCombatEventKind::ProcHeal) {
                assert(e.auraOwnerGuid==owner.guid&&e.auraCasterGuid==caster.guid&&e.source==caster.guid);
                assert(e.parentSequence&&e.procDepth==1);actual=true;++procs;
            }
            seen=std::max(seen,e.sequence);
        }
        assert(actual==expected);
    }
    assert(eligible>300);
    if(!present)assert(procs==(fallback?eligible:0));
    return procs;
}
static void sameAuraAcrossOwners() {
    auto c=rewardContent();
    LocalSpellDefinition d;d.id=99;d.name="Same aura different owners fixture";d.durationMs=60000;
    d.proc.effect=LocalProcEffect::HealOwner;d.proc.spellId=199;d.proc.flags=8|0x4000;
    d.proc.amount=3;d.proc.chance=100;d.proc.allowTriggered=true;d.range=100;d.buffSelfOnly=false;
    assert(validLocalProc(d));c->spells.push_back(d);
    auto owner=rewardPlayer(1),caster=rewardPlayer(2);owner.classId=caster.classId=8;
    caster.knownSpells.push_back(99);
    LocalGameplay game;game.useContent(c);std::string result;
    assert(game.execute(caster,{LocalAction::CastSpell,owner.guid,99},{&owner,&caster},result));
    caster.globalCooldownMs=0;caster.cooldowns.clear();
    assert(game.execute(caster,{LocalAction::CastSpell,caster.guid,99},{&owner,&caster},result));
    assert(owner.statAuras.size()==1&&caster.statAuras.size()==1);
    uint64_t seen=0;for(const auto& e:game.combatEvents())seen=std::max(seen,e.sequence);
    auto n=rewardNpc();n.health=n.maxHealth=100000;n.targetGuid=owner.guid;n.threat[0]={owner.guid,1000};
    n.level=1;n.x=n.homeX=1;
    unsigned procs=0;
    for(unsigned i=0;i<64&&!procs;++i) {
        owner.health=caster.health=50;n.attackTimer=0;game.setRemoteNpcs({n});game.tick(.001f,{&owner,&caster});
        for(const auto& e:game.combatEvents())if(e.sequence>seen) {
            if(e.kind==LocalCombatEventKind::ProcHeal) {
                ++procs;assert(e.auraOwnerGuid==owner.guid&&e.source==caster.guid&&e.procDepth==1);
            }
            seen=std::max(seen,e.sequence);
        }
    }
    // Distinct owner GUIDs defeat the per-owner recursion key. Source aura
    // identity must independently suppress caster2's otherwise eligible aura.
    assert(procs==1);
}
int main() {
    LocalProcDefinition p;p.flags=8;p.ppm=3;p.chance=37;
    LocalCombatEvent e;e.kind=LocalCombatEventKind::NpcMelee;e.source=10;e.target=1;
    e.attempted=e.effective=3;e.weaponPeriodMs=9000;
    assert(localProcMatches(p,e,1));
    assert(localProcChanceBasisPoints(p,e,{true,1000})==500);
    assert(localProcChanceBasisPoints(p,e,{true,2500})==1250);
    assert(localProcChanceBasisPoints(p,e,{false,2500})==3700);
    assert(localProcChanceBasisPoints(p,e,{true,0})==0);
    e.kind=LocalCombatEventKind::DirectHeal;e.attackType=LocalCombatAttackType::Magic;
    assert(localProcChanceBasisPoints(p,e,{true,9000})==750);
    e.sourceRawCastTimeMs=3000;assert(localProcChanceBasisPoints(p,e,{true,9000})==1500);
    e.kind=LocalCombatEventKind::SpellCast;assert(localProcChanceBasisPoints(p,e,{true,9000})==3700);
    e.kind=LocalCombatEventKind::SpellFinish;assert(localProcChanceBasisPoints(p,e,{true,9000})==3700);
    auto c=rewardContent();auto caster=rewardPlayer(2);caster.classId=11;caster.formSpellId=768;
    e.kind=LocalCombatEventKind::NpcMelee;e.attackType=LocalCombatAttackType::Melee;e.offHand=true;
    assert(localProcTimingForCaster(&caster,*c,e).weaponPeriodMs==1000);
    e.attackType=LocalCombatAttackType::Ranged;assert(localProcTimingForCaster(&caster,*c,e).weaponPeriodMs==2000);
    caster.formSpellId=0;e.attackType=LocalCombatAttackType::Melee;
    assert(localProcTimingForCaster(&caster,*c,e).weaponPeriodMs==2000);
    const auto cat=incoming(768,true,0),bear=incoming(5487,true,0);
    assert(cat>0&&bear>cat);incoming(768,false,0);incoming(5487,false,100);sameAuraAcrossOwners();
    std::cout<<"PASS original aura caster incoming PPM: public aura cast and 2048 real NPC swings, source/caster/recipient separation, live cat/bear base timing, exact deterministic chance replay, absent-caster fixed chance, spell cast-time floor, CAST/FINISH fallback, feral offhand and unarmed timing; same-aura recursion blocked across distinct owners\n";
}
