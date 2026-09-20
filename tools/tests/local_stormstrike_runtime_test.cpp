#include "local_group_rewards_fixture.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_stormstrike_import.hpp"
#include "game/local_stormstrike.hpp"
#include "game/local_proc_rules.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace wowee;
int main(int argc,char** argv) {
    assert(argc==2);std::map<std::string,pipeline::DBCFile> tables;
    for(const char* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration"}) {
        std::ifstream in(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),{}};assert(tables[name].load(bytes));
    }
    detail::ClientSpellTables t;t.spells=&tables["Spell"];t.ranges=&tables["SpellRange"];t.casts=&tables["SpellCastTimes"];t.durations=&tables["SpellDuration"];
    detail::ClientSpellTables::buildIndex(t.spells,t.spellIndex);detail::ClientSpellTables::buildIndex(t.ranges,t.rangeIndex);
    detail::ClientSpellTables::buildIndex(t.casts,t.castIndex);detail::ClientSpellTables::buildIndex(t.durations,t.durationIndex);
    auto c=rewardContent();c->npcs[0].health=1000000;c->npcs[0].armor=0;
    const auto add=[&](uint32_t id,uint32_t talent,uint8_t rank) {
        LocalSpellDefinition d;d.id=id;d.clientSpell=true;d.allowableClasses=64;d.talentId=talent;d.talentRank=rank;
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,id);assert(row>=0);detail::decodeClientSpell(t,row,d);
        std::array<LocalSpellDefinition,3> children{};assert(decodeClientStormstrike(t,row,d,&children));
        c->spells.push_back(d);for(auto& child:children)if(child.id)c->spells.push_back(child);
    };
    add(17364,901,1);add(51522,2054,2);add(30798,1690,1);
    LocalSpellDefinition nature;nature.id=900001;nature.name="Nature fixture";nature.damage=nature.damageMax=100;
    nature.range=5;nature.schoolMask=8;nature.spellFamily=11;nature.spellFamilyFlags={1,0,0};nature.clientSpell=true;nature.sourceDamageClass=1;nature.sourceCantCrit=true;
    c->spells.push_back(nature);auto outside=nature;outside.id=900002;outside.spellFamilyFlags={0,0,0};c->spells.push_back(outside);
    std::sort(c->spells.begin(),c->spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    for(auto id:{36u,810u}){LocalItemDefinition item;item.id=id;item.name="Source mace";item.stack=1;item.inventoryType=id==36?21:13;c->items.push_back(item);}
    std::sort(c->items.begin(),c->items.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    LocalGameplay game;game.useContent(c);
    auto p=rewardPlayer(1);p.classId=7;p.race=2;p.level=80;p.resourceType=LocalResourceType::Mana;p.maxMana=p.mana=10000;
    p.health=p.maxHealth=10000;p.knownSpells={17364,900001,900002};p.talents={{901,1},{1690,1},{2054,2}};p.inventory={{36,1},{810,1}};p.equipment[15]=36;p.equipment[16]=810;
    auto n=rewardNpc();n.level=80;n.health=n.maxHealth=1000000;n.x=n.homeX=1;n.attackTimer=10000;n.combatEpoch=1;
    std::string result;uint64_t seq=0;unsigned landed=0,avoided=0,crits=0;
    const auto reset=[&]{p.cooldowns.clear();p.categoryCooldowns.clear();p.globalCooldownMs=0;p.attackTarget=0;p.mana=5000;};
    for(unsigned i=0;i<128;++i) {
        reset();game.setRemoteNpcs({n});assert(game.execute(p,{LocalAction::CastSpell,n.guid,17364},{&p},result));
        unsigned children=0,procs=0;uint64_t parentCast=0,parentHit=0,lastChild=0;bool miss=false;
        for(const auto& e:game.combatEvents())if(e.sequence>seq) {
            if(e.kind==LocalCombatEventKind::SpellCast&&e.spell==17364){parentCast=e.sequence;assert(e.attackType==LocalCombatAttackType::Melee);}
            if(e.kind==LocalCombatEventKind::ProcMana){++procs;assert(e.spell==63375&&e.attempted==localBaseMana(p)*20/100&&e.parentSequence==parentCast);}
            if(e.spell==32175||e.spell==32176){++children;lastChild=e.sequence;assert(e.kind==LocalCombatEventKind::SpellDamage&&!e.blocked&&!localMeleeAvoided(e.outcome));if(e.outcome==LocalMeleeOutcome::Critical)++crits;}
            if(e.spell==17364&&e.kind==LocalCombatEventKind::SpellDamage)miss=localMeleeAvoided(e.outcome);
            if(e.spell==17364&&e.kind==LocalCombatEventKind::SpellHit)parentHit=e.sequence;
        }
        assert(procs==1&&p.mana==5000-localBaseMana(p)*8/100+localBaseMana(p)*20/100);
        if(miss){++avoided;assert(!children&&game.npcs()[0].stormstrikeAuras.empty());}
        else {++landed;assert(children==2&&parentHit>lastChild&&lastChild>parentCast);assert(game.npcs()[0].stormstrikeAuras.size()==1&&game.npcs()[0].stormstrikeAuras[0].charges==4);}
        seq=game.combatEvents().back().sequence;
    }
    assert(landed&&avoided&&crits);
    // Authored component allocations above intentionally do not assert a reachable talent tree.
    auto auraTarget=n;auraTarget.stormstrikeAuras={{17364,12000,p.guid,4,p.positionRevision},{17364,12000,2,4,0}};
    assert(validLocalNpcStormstrikeAuras(auraTarget,*c));
    for(unsigned invalid=0;invalid<5;++invalid) {
        auto traveller=p;if(invalid==0)traveller.dead=true;if(invalid==1)traveller.flight.active=true;
        if(invalid==2)traveller.transportEntry=1;if(invalid==3)++traveller.mapId;if(invalid==4)++traveller.instanceId;
        assert(localStormstrikeDamage(auraTarget,traveller,&nature,100)==100);
        consumeLocalStormstrikeCharge(auraTarget,traveller,&nature,100,LocalMeleeOutcome::Hit,0);
        assert(auraTarget.stormstrikeAuras[0].charges==4);
    }
    assert(localStormstrikeDamage(auraTarget,p,&nature,100)==120);
    assert(localStormstrikeDamage(auraTarget,p,&outside,100)==100);
    assert(localStormstrikeDamage(auraTarget,p,&nature,100,12000)==100);
    consumeLocalStormstrikeCharge(auraTarget,p,&nature,100,LocalMeleeOutcome::Hit,0,true);assert(auraTarget.stormstrikeAuras[0].charges==4);
    consumeLocalStormstrikeCharge(auraTarget,p,&nature,0,LocalMeleeOutcome::Hit,0);assert(auraTarget.stormstrikeAuras[0].charges==4);
    consumeLocalStormstrikeCharge(auraTarget,p,&nature,100,LocalMeleeOutcome::Miss,0);assert(auraTarget.stormstrikeAuras[0].charges==4);
    consumeLocalStormstrikeCharge(auraTarget,p,&nature,100,LocalMeleeOutcome::Hit,17364);assert(auraTarget.stormstrikeAuras[0].charges==4);
    consumeLocalStormstrikeCharge(auraTarget,p,&outside,100,LocalMeleeOutcome::Critical,324);assert(auraTarget.stormstrikeAuras[0].charges==3);
    for(unsigned i=0;i<3;++i){assert(localStormstrikeDamage(auraTarget,p,&nature,100)==120);consumeLocalStormstrikeCharge(auraTarget,p,&nature,120,LocalMeleeOutcome::Hit,0);}
    assert(auraTarget.stormstrikeAuras.size()==1&&auraTarget.stormstrikeAuras[0].casterGuid==2);
    // Actual casts use the fourth charge, then lose the bonus; another caster survives.
    auraTarget=n;auraTarget.stormstrikeAuras={{17364,12000,p.guid,4,p.positionRevision},{17364,12000,2,4,0}};game.setRemoteNpcs({auraTarget});
    // the implementation gave player-to-creature magic its own hit roll, so this component
    // cast can be resisted; a resisted cast deals nothing and (asserted above at
    // LocalMeleeOutcome::Miss) spends no charge, so retry until it lands. The
    // per-charge damage assertion itself is unchanged.
    for(unsigned i=0;i<5;++i){
        uint32_t dealt=0;
        for(unsigned attempt=0;attempt<64&&!dealt;++attempt){
            reset();const auto hp=game.npcs()[0].health;
            assert(game.execute(p,{LocalAction::CastSpell,n.guid,900001},{&p},result));
            dealt=uint32_t(hp-game.npcs()[0].health);
        }
        assert(dealt==(i<4?120u:100u));
    }
    assert(game.npcs()[0].stormstrikeAuras.size()==1&&game.npcs()[0].stormstrikeAuras[0].casterGuid==2);
    // No offhand still permits exactly one child; missing main hand fails before mana/cooldown.
    p.equipment[16]=0;bool oneHand=false;
    for(unsigned i=0;i<32&&!oneHand;++i){reset();game.setRemoteNpcs({n});assert(game.execute(p,{LocalAction::CastSpell,n.guid,17364},{&p},result));unsigned count=0;for(const auto& e:game.combatEvents())if(e.sequence>seq&&(e.spell==32175||e.spell==32176))++count;oneHand=!game.npcs()[0].stormstrikeAuras.empty();if(oneHand)assert(count==1);seq=game.combatEvents().back().sequence;}
    assert(oneHand);reset();p.equipment[15]=0;const auto mana=p.mana;assert(!game.execute(p,{LocalAction::CastSpell,n.guid,17364},{&p},result));assert(p.mana==mana&&p.cooldowns.empty());
    p.equipment[15]=36;p.talents.clear();assert(!game.execute(p,{LocalAction::CastSpell,n.guid,17364},{&p},result));
    // Resolve the initial authored-region selection before installing transient
    // component NPCs; these fixtures deliberately have no catalog spawn.
    game.tick(0,{&p});
    // Encounter reset/owner departure and finite duration cannot leave a durable debuff.
    auraTarget=n;auraTarget.stormstrikeAuras={{17364,1,p.guid,4,p.positionRevision}};game.setRemoteNpcs({auraTarget});p.attackTarget=0;game.tick(.01f,{&p});assert(game.npcs().size()==1&&game.npcs()[0].stormstrikeAuras.empty());
    auraTarget.stormstrikeAuras={{17364,12000,p.guid,4,p.positionRevision}};game.setRemoteNpcs({auraTarget});++p.positionRevision;game.tick(.01f,{&p});assert(game.npcs().size()==1&&game.npcs()[0].stormstrikeAuras.empty());
    std::cout<<"PASS: Stormstrike source-decoded runtime component: parent CAST mana proc on hits/misses, two independent crit children, launch-before-aura, single-hand cast, four charges/family/caster/periodic/triggered distinctions, precommit and lifecycle guards. Normal Stormstrike progression remains locked.\n";
}
