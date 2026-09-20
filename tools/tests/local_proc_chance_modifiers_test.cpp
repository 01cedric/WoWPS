#include "local_group_rewards_fixture.hpp"
#include "game/local_proc_chance_modifiers.hpp"
#include "game/local_proc_timing.hpp"
#include <iostream>

static LocalSpellDefinition modifier(uint32_t id,uint8_t operation,bool percentage,int32_t amount,
                                    std::array<uint32_t,3> mask={0,0,0x40000000}) {
    LocalSpellDefinition d;d.id=id;d.talentId=id;d.talentRank=1;d.passive=true;
    d.allowableClasses=128;d.spellFamily=3;
    d.passiveCastModifiers[0]={operation,percentage,true,amount,mask};return d;
}
static void component() {
    auto c=rewardContent();auto p=rewardPlayer(1);p.classId=8;p.level=80;
    LocalSpellDefinition aura;aura.spellFamily=3;aura.spellFamilyFlags={0,0,0x40000000};
    c->spells.push_back(modifier(100,18,false,5));
    c->spells.push_back(modifier(101,18,true,50));
    c->spells.push_back(modifier(102,18,true,25));
    c->spells.push_back(modifier(103,26,false,1));
    c->spells.push_back(modifier(104,26,true,50));
    p.talents={{100,1},{101,1},{102,1},{103,1},{104,1}};
    const auto m=localProcChanceModifiersForCaster(&p,*c,aura);
    assert(m.chanceFlat==5&&m.chanceMultiplier==1.75f&&m.ppmFlat==1&&m.ppmMultiplier==1.5f);
    LocalProcDefinition proc;proc.effect=LocalProcEffect::HealOwner;proc.spellId=2;proc.flags=8;proc.amount=1;proc.chance=10;proc.ppm=6;
    LocalCombatEvent e;e.kind=LocalCombatEventKind::NpcMelee;e.attackType=LocalCombatAttackType::Melee;
    // PPM 6*1.5+1=10; 2s yields 33.3333%; then *1.75+5=63.3333%.
    assert(localProcChanceBasisPoints(proc,e,{true,2000},m)==6333);
    assert(localProcChanceBasisPoints(proc,e,{false,2000},m)==1000);
    // CAST has no DamageInfo: op26 is skipped but op18 still applies.
    e.kind=LocalCombatEventKind::SpellCast;assert(localProcChanceBasisPoints(proc,e,{true,2000},m)==2250);
    auto ignored=aura;ignored.sourceIgnoreCasterModifiers=true;
    auto identity=localProcChanceModifiersForCaster(&p,*c,ignored);assert(identity.chanceFlat==0&&identity.ppmMultiplier==1);
    for(unsigned word=0;word<3;++word) {
        c->spells[1].passiveCastModifiers[0].mask={};c->spells[1].passiveCastModifiers[0].mask[word]=0x80000000;
        auto target=aura;target.spellFamilyFlags={};target.spellFamilyFlags[word]=0x80000000;
        assert(localProcChanceModifiersForCaster(&p,*c,target).chanceFlat==5);
        target.spellFamily=7;assert(localProcChanceModifiersForCaster(&p,*c,target).chanceFlat==0);
    }
    c->spells[1].passiveCastModifiers[0].mask=aura.spellFamilyFlags;
    c->spells[1].unsupportedReason="Unavailable source sibling";
    assert(localProcChanceModifiersForCaster(&p,*c,aura).chanceFlat==0);c->spells[1].unsupportedReason.clear();
    c->spells[1].talentPrerequisites[0]=199;
    assert(localProcChanceModifiersForCaster(&p,*c,aura).chanceFlat==0);c->spells[1].talentPrerequisites[0]=0;
    p.talents.erase(p.talents.begin());assert(localProcChanceModifiersForCaster(&p,*c,aura).chanceFlat==0);
    p.talents={{100,1},{100,1}};assert(localProcChanceModifiersForCaster(&p,*c,aura).chanceMultiplier==1);
    p.talents={{100,1}};p.dead=true;assert(localProcChanceModifiersForCaster(&p,*c,aura).chanceFlat==0);p.dead=false;
    p.classId=11;assert(localProcChanceModifiersForCaster(&p,*c,aura).chanceFlat==0);
    assert(localProcChanceModifiersForCaster(nullptr,*c,aura).chanceFlat==0);
    // Pct modifiers on zero base cannot scale an independent flat bonus.
    proc.ppm=0;proc.chance=0;e.kind=LocalCombatEventKind::NpcMelee;
    assert(localProcChanceBasisPoints(proc,e,{true,2000},m)==500);
}
static void incoming(bool modifyCaster,bool casterPresent,bool ignore,bool reset) {
    auto c=rewardContent();LocalSpellDefinition d;d.id=99;d.name="Proc modifier component";d.durationMs=60000;
    d.spellFamily=3;d.spellFamilyFlags={0,0,0x40000000};d.sourceIgnoreCasterModifiers=ignore;
    d.proc.effect=LocalProcEffect::HealOwner;d.proc.spellId=199;d.proc.flags=8;
    d.proc.amount=3;d.proc.ppm=6;d.proc.chance=10;d.range=100;d.buffSelfOnly=false;
    assert(validLocalProc(d));c->spells.push_back(d);
    c->spells.push_back(modifier(100,18,false,5));c->spells.push_back(modifier(101,18,true,50));
    c->spells.push_back(modifier(102,26,true,50));
    auto owner=rewardPlayer(1),caster=rewardPlayer(2);owner.classId=caster.classId=8;owner.level=caster.level=80;
    caster.knownSpells.push_back(99);(modifyCaster?caster:owner).talents={{100,1},{101,1},{102,1}};
    LocalGameplay game;game.useContent(c);std::string message;
    assert(game.execute(caster,{LocalAction::CastSpell,owner.guid,99},{&owner,&caster},message));
    if(reset)caster.talents.clear();
    auto n=rewardNpc();n.health=n.maxHealth=100000;n.targetGuid=owner.guid;n.threat[0]={owner.guid,1000};
    n.level=1;n.x=n.homeX=1;
    uint64_t seen=game.combatEvents().back().sequence,replay=0x9e3779b97f4a7c15ULL;
    unsigned eligible=0,procs=0;
    const auto players=casterPresent?std::vector<LocalRealmPlayer*>{&owner,&caster}:std::vector<LocalRealmPlayer*>{&owner};
    // Full original-caster pipeline: 6PPM *1.5 at2s =30%; *1.5 +5 =50%.
    const uint32_t expectedChance=!casterPresent?1000:modifyCaster&&!ignore&&!reset?5000:2000;
    for(unsigned i=0;i<512;++i) {
        owner.health=owner.maxHealth/2;n.attackTimer=0;game.setRemoteNpcs({n});game.tick(.001f,players);
        bool expected=false,actual=false;
        for(const auto& e:game.combatEvents())if(e.sequence>seen) {
            if(e.kind==LocalCombatEventKind::NpcMelee&&localProcMatches(d.proc,e,owner.guid)) {
                ++eligible;expected=localRollProcBasisPoints(expectedChance,replay);
            }
            if(e.kind==LocalCombatEventKind::ProcHeal) {
                assert(e.auraOwnerGuid==owner.guid&&e.auraCasterGuid==caster.guid);actual=true;++procs;
            }
            seen=std::max(seen,e.sequence);
        }
        assert(expected==actual);
    }
    assert(eligible>100&&procs>0);
}
int main() {
    component();incoming(true,true,false,false);incoming(false,true,false,false);
    incoming(true,false,false,false);incoming(true,true,true,false);incoming(true,true,false,true);
    std::cout<<"PASS proc modifiers: source aura family96, additive percentage then flat, full precision PPM then chance, original caster incoming dispatch, owner isolation, missing caster, ignored modifiers, reset, invalid allocations and prerequisites\n";
    return 0;
}
