#include "local_group_rewards_fixture.hpp"
#include "game/local_proc_talents.hpp"
#include <iostream>
int main() {
    auto c=rewardContent();c->npcs[0].health=1000;c->spells[0].damage=1;c->spells[0].schoolMask=4;
    LocalSpellDefinition shield;shield.id=99;shield.name="Runtime source proc fixture";shield.durationMs=60000;
    shield.proc.effect=LocalProcEffect::DamageAttacker;shield.proc.spellId=199;shield.proc.flags=0x10000;
    shield.proc.amount=3;shield.proc.chance=100;shield.proc.charges=2;shield.proc.range=20;shield.proc.schoolMask=4;shield.proc.allowTriggered=true;
    LocalSpellDefinition talent;talent.id=100;talent.name="Runtime passive proc fixture";talent.passive=true;talent.talentId=100;talent.talentRank=1;talent.allowableClasses=8;
    talent.proc.effect=LocalProcEffect::RestorePower;talent.proc.spellId=200;talent.proc.flags=0x10000;
    talent.proc.amount=2;talent.proc.chance=100;talent.proc.resourceType=3;talent.proc.allowTriggered=true;
    auto cast=talent;cast.id=101;cast.passive=false;cast.talentId=0;cast.durationMs=60000;cast.proc.spellId=201;cast.proc.amount=5;cast.proc.phaseMask=LocalProcPhaseCast;
    LocalSpellDefinition heal;heal.id=102;heal.name="Heal fixture";heal.heal=2;heal.range=100;
    auto recipient=shield;recipient.id=103;recipient.proc.effect=LocalProcEffect::HealOwner;recipient.proc.spellId=203;recipient.proc.flags=0x8000;recipient.proc.charges=1;recipient.proc.amount=17;
    c->spells.insert(c->spells.end(),{shield,talent,cast,heal,recipient});
    for(const auto& spell:c->spells)assert(validLocalProc(spell));
    LocalGameplay game;game.useContent(c);auto a=rewardPlayer(1),b=rewardPlayer(2);a.level=10;a.classId=4;a.resourceType=LocalResourceType::Energy;a.maxMana=100;a.mana=0;a.talents={{100,1}};
    a.knownSpells.push_back(102);a.statAuras={{99,60000,0,0,1,0,1,2},{101,60000,0,0,1}};
    auto n=rewardNpc();n.health=n.maxHealth=1000;game.setRemoteNpcs({n});std::string result;
    assert(game.execute(a,{LocalAction::CastSpell,10,1},{&a,&b},result));
    // The triggered damage may explicitly trigger the passive, once per root;
    // that same aura cannot recurse into itself or fire again for the parent.
    auto events=game.combatEvents();assert(a.mana==7);unsigned power=0,damage=0,casts=0;
    for(const auto& e:events){assert(e.rootSequence);if(e.kind==LocalCombatEventKind::ProcDamage){++damage;assert(e.spell==199&&e.schoolMask==4&&e.auraOwnerGuid==1&&e.auraCasterGuid==1&&e.parentSequence&&e.procDepth==1);}if(e.kind==LocalCombatEventKind::ProcPower){++power;assert(e.effective==2||e.effective==5);}if(e.kind==LocalCombatEventKind::SpellCast)++casts;}
    assert(damage==1&&power==2&&casts==1&&a.statAuras[0].procCharges==1);
    // Clearing the actual talent allocation disables its proc immediately.
    a.talents.clear();a.mana=0;a.cooldowns.clear();a.globalCooldownMs=0;
    assert(game.execute(a,{LocalAction::CastSpell,10,1},{&a,&b},result));assert(a.mana==5);
    // A recipient proc uses the stored caster GUID even after caster departure.
    b.health=30;b.statAuras={{103,60000,0,0,9,0,1,1,0,0,17,true}};
    a.cooldowns.clear();a.globalCooldownMs=0;
    assert(game.execute(a,{LocalAction::CastSpell,2,102},{&a,&b},result));assert(b.health==49&&b.statAuras.empty());
    events=game.combatEvents();bool found=false;for(const auto& e:events)if(e.kind==LocalCombatEventKind::ProcHeal){found=true;assert(e.source==9&&e.target==2&&e.auraCasterGuid==9&&e.auraOwnerGuid==2&&e.effective==17);}
    assert(found);
    // The persistence provider includes an offline recipient, but never adds
    // that character to the active combat roster. A later transfer removes
    // its saved aura and carries the outstanding internal cooldown forward.
    auto ec=rewardContent();auto earth=recipient;earth.id=974;earth.name="Earth Shield persistence fixture";
    earth.talentId=974;earth.talentRank=1;earth.spellFamily=11;earth.spellFamilyFlags[1]=0x400;
    earth.buffSelfOnly=false;earth.proc.flags=0x2a2a8;earth.proc.charges=6;earth.proc.cooldownMs=3000;earth.range=100;
    ec->spells.push_back(earth);LocalGameplay persisted;persisted.useContent(ec);
    auto caster=rewardPlayer(11),offline=rewardPlayer(12),online=rewardPlayer(13);
    caster.level=80;caster.classId=7;caster.knownSpells={974};caster.talents={{974,1}};
    offline.statAuras={{974,60000,0,0,11,0,1,5,900,0,17,true}};
    unsigned providerCalls=0;
    persisted.setAuraOwnerProvider([&]{++providerCalls;return std::vector<LocalRealmPlayer*>{&caster,&offline,&online};});
    if(!persisted.execute(caster,{LocalAction::CastSpell,13,974},{&caster,&online},result)){std::cerr<<result<<"\n";return 1;}
    assert(providerCalls==1&&offline.statAuras.empty()&&online.statAuras.size()==1&&online.statAuras[0].procCooldownMs==900);
    // Simulate a historical offline record too: reset must purge both stored
    // and live recipients without requiring their caster to log in with them.
    offline.statAuras={{974,60000,0,0,11,0,1,5,900,0,17,true}};
    assert(persisted.execute(caster,{LocalAction::ResetTalents},{&caster,&online},result));
    assert(providerCalls==2&&offline.statAuras.empty()&&online.statAuras.empty()&&caster.talents.empty());
    std::cout<<"PASS P03 persistence scope: offline recipient aura removed on transfer, ICD preserved, active/saved owners deduplicated, reset purges offline and live recipients\n";
    std::cout<<"PASS P03 authority runtime: actual cast/damage/heal dispatch; passive learned rank and reset; explicit triggered chain and per-root dedup; charge debit; cast-phase isolation; child identity; parent/root/caster attribution; departed original caster healing snapshot\n";
}
