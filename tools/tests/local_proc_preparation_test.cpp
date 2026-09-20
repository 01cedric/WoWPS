#include "local_group_rewards_fixture.hpp"
#include "game/local_proc_rules.hpp"
#include <iostream>

// Component fixtures exercise supported runtime leaf actions through the public
// authority CastSpell API. They do not extend the imported client spell catalog.
static LocalSpellDefinition proc(uint32_t id,LocalProcEffect effect,uint32_t flags) {
    LocalSpellDefinition d;d.id=id;d.name="Prepared proc component fixture";d.durationMs=60000;
    d.proc.effect=effect;d.proc.spellId=id+1000;d.proc.flags=flags;d.proc.amount=3;
    d.proc.chance=100;d.proc.charges=2;d.proc.cooldownMs=1000;d.proc.allowTriggered=true;
    if(effect==LocalProcEffect::DamageAttacker){d.proc.schoolMask=4;d.proc.range=20;}
    assert(validLocalProc(d));return d;
}
static LocalStatAura applied(const LocalSpellDefinition& d,uint64_t caster) {
    LocalStatAura a{d.id,d.durationMs,0,0,caster};a.procCharges=d.proc.charges;
    a.procAmountSnapshot=d.proc.amount;a.hasProcAmountSnapshot=true;return a;
}
static void run(bool self,bool fullBudget,bool nestedOnly,bool lastCharge=false) {
    auto c=rewardContent();c->spells[0].damage=0;c->spells[0].heal=2;
    auto source=rewardPlayer(1),target=rewardPlayer(2);source.classId=target.classId=8;
    source.health=target.health=20;source.maxMana=target.maxMana=100;source.mana=target.mana=0;
    auto* recipient=self?&source:&target;
    auto h=proc(100,LocalProcEffect::HealOwner,0x4000);c->spells.push_back(h);
    // The stored caster is the other participant. The callback reverses the
    // done/taken roles and could steal a not-yet-prepared parent activation.
    source.statAuras.push_back(applied(h,recipient->guid));
    const unsigned count=fullBudget?16:2;
    for(unsigned i=1;i<count;++i) {
        auto d=proc(100+i,LocalProcEffect::RestoreMana,0xc000);c->spells.push_back(d);
        source.statAuras.push_back(applied(d,source.guid));
    }
    if(!self)for(unsigned i=0;i<count;++i) {
        auto d=proc(200+i,LocalProcEffect::RestoreMana,nestedOnly?0x4000:0xc000);c->spells.push_back(d);
        target.statAuras.push_back(applied(d,target.guid));
    }
    if(lastCharge)for(auto* p:{&source,&target})for(auto& a:p->statAuras)a.procCharges=1;
    LocalGameplay game;game.useContent(c);std::string result;
    assert(game.execute(source,{LocalAction::CastSpell,recipient->guid,1},{&source,&target},result));
    const auto events=game.combatEvents();uint64_t root=0,callback=0;size_t effects=0;
    bool seenTarget=false;
    for(const auto& e:events) {
        if(e.kind==LocalCombatEventKind::DirectHeal)root=e.sequence;
        if(e.kind!=LocalCombatEventKind::ProcHeal&&e.kind!=LocalCombatEventKind::ProcMana)continue;
        assert(root&&e.rootSequence==root);++effects;
        if(e.auraSpell==100){callback=e.sequence;assert(e.auraOwnerGuid==1&&e.source==recipient->guid);}
        if(e.auraOwnerGuid==2)seenTarget=true;
        else if(!nestedOnly)assert(!seenTarget); // all source callbacks before target callbacks
        if(nestedOnly&&e.auraOwnerGuid==2)assert(e.parentSequence==callback&&e.procDepth==2);
        else assert(e.parentSequence==root&&e.procDepth==1);
    }
    assert(effects==count*(self?1u:2u));
    if(lastCharge)assert(source.statAuras.empty()&&target.statAuras.empty());
    else for(auto* p:{&source,recipient})for(const auto& a:p->statAuras)
        assert(a.procCharges==1&&a.procCooldownMs==1000);
}
static void targetInvalidated() {
    auto c=rewardContent();c->spells[0].damage=1;
    auto a=rewardPlayer(1);a.classId=8;
    for(uint32_t id:{300u,301u}) {
        auto d=proc(id,LocalProcEffect::DamageAttacker,0x10000);d.proc.schoolMask=4;d.proc.range=20;
        assert(validLocalProc(d));c->spells.push_back(d);a.statAuras.push_back(applied(d,a.guid));
        a.statAuras.back().procCharges=1;
    }
    LocalGameplay game;game.useContent(c);auto n=rewardNpc();n.health=n.maxHealth=4;game.setRemoteNpcs({n});
    std::string result;assert(game.execute(a,{LocalAction::CastSpell,n.guid,1},{&a},result));
    size_t damage=0;for(const auto& e:game.combatEvents())if(e.kind==LocalCombatEventKind::ProcDamage){++damage;assert(e.auraSpell==300);}
    // Both applications were selected/debited before the first callback killed
    // the target. The second leaf cannot damage a corpse or retain a spent aura.
    assert(damage==1&&game.npcs()[0].dead&&a.statAuras.empty());
}
int main() {
    run(false,false,false);run(true,false,false);run(false,true,false);run(false,false,true);
    run(false,false,false,true);run(false,true,false,true);targetInvalidated();
    std::cout<<"PASS prepared proc dispatch: source-before-target callbacks, both owners prepared before nested callbacks, original parent attribution, self-target deduplication, 32-effect root reservation, nested-only eligibility, charge/ICD debit, last-charge retirement, target invalidation between prepared callbacks\n";
}
