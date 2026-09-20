#include "game/local_spell_import.hpp"
#include "game/local_talents.hpp"
#include "game/local_melee.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace wowee;
using namespace wowee::game;
int main(int argc,char** argv) {
    assert(argc==2);
    std::map<std::string,pipeline::DBCFile> tables;
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon","Talent","TalentTab","SpellRuneCost","SpellRadius"}) {
        std::ifstream f(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f),{}};assert(tables[name].load(bytes));
    }
    auto get=[&](const char* name){return &tables.at(name);};
    LocalSpellImport imported;
    detail::importClientTalents(imported,get("Talent"),get("TalentTab"),get("Spell"),get("SpellRange"),
        get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SpellRuneCost"),get("SpellRadius"));
    LocalWorldContent c;c.spells=std::move(imported.spells);
    std::sort(c.spells.begin(),c.spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    for(uint32_t id:{25u,35u,2092u,2210u,2504u}) {
        const auto* source=localMeleeItem(id);assert(source);
        LocalItemDefinition item;item.id=id;item.name="Source weapon";item.inventoryType=source->inventoryType;c.items.push_back(item);
    }
    LocalRealmPlayer p;p.classId=1;p.level=20;p.race=1;p.dead=false;
    p.equipment[15]=25;p.inventory={{25,1},{35,1},{2092,1},{2210,1},{2504,1}};
    const auto baseline=localMeleeStats(p,c);assert(baseline.sourceStats&&std::isfinite(baseline.crit));
    std::string error;
    assert(!learnLocalTalent(p,c,159,0,error)); // Unbridled Wrath really requires five Fury points.
    assert(!learnLocalTalent(p,c,157,1,error)); // Rank skipping remains forbidden.
    for(uint8_t rank=1;rank<=5;++rank) {
        const auto* source=localTalentSpell(c,157,rank);
        assert(source&&source->unsupportedReason.empty()&&source->passiveMeleeCritPct==rank);
        assert(source->talentTab==164&&source->talentRow==0&&source->requiredItemClass==2&&source->requiredItemSubclasses==173555);
        assert(learnLocalTalent(p,c,157,rank-1,error));
        const auto stats=localMeleeStats(p,c);
        assert(std::abs(stats.crit-baseline.crit-rank)<.0001f);
        assert(std::abs(stats.offHandCrit-baseline.offHandCrit)<.0001f); // No imaginary offhand weapon.
        assert(p.talents.size()==1&&p.talents.front().second==rank);
    }
    for(uint8_t rank=1;rank<=5;++rank)assert(learnLocalTalent(p,c,159,rank-1,error));
    assert(localTalentPointsSpent(p)==10&&validLocalTalents(p));
    assert(std::abs(localMeleeStats(p,c).crit-baseline.crit-5)<.0001f);
    assert(p.knownSpells.empty()); // Passive ranks never enter the active spellbook.
    // Hand ownership and inventory compatibility are checked by production stats.
    p.equipment[16]=2092;auto dual=localMeleeStats(p,c);assert(dual.offHand);
    assert(std::abs(dual.crit-baseline.crit-5)<.0001f&&std::abs(dual.offHandCrit-baseline.offHandCrit-5)<.0001f);
    p.equipment[15]=0;auto offOnly=localMeleeStats(p,c);
    assert(std::abs(offOnly.crit-baseline.crit)<.0001f&&std::abs(offOnly.offHandCrit-baseline.offHandCrit-5)<.0001f);
    p.equipment[15]=2092;auto duplicate=localMeleeStats(p,c);
    assert(!duplicate.offHand&&std::abs(duplicate.offHandCrit-baseline.offHandCrit)<.0001f);
    p.equipment[15]=35;auto twoHand=localMeleeStats(p,c);assert(!twoHand.offHand);
    assert(std::abs(twoHand.crit-baseline.crit-5)<.0001f);
    p.equipment[15]=25;p.equipment[16]=2210;assert(!localMeleeStats(p,c).offHand);
    p.equipment[15]=2504;assert(std::abs(localMeleeStats(p,c).crit-baseline.crit)<.0001f); // Ranged item in melee slot rejected.
    p.equipment[15]=0;p.equipment[16]=0;p.equipment[17]=2504;
    assert(std::abs(localMeleeStats(p,c).crit-baseline.crit)<.0001f); // Ranged ownership does not buff unarmed.
    assert(localTalentWeaponCritPct(p,c,2,2,15)==0&&localTalentWeaponCritPct(p,c,2,32,13)==0);
    auto wrongClass=p;wrongClass.classId=4;assert(localTalentWeaponCritPct(wrongClass,c,2,7,21)==0);
    auto dead=p;dead.dead=true;assert(localTalentWeaponCritPct(dead,c,2,7,21)==0);
    auto malformed=p;malformed.talents.push_back({157,5});assert(localTalentWeaponCritPct(malformed,c,2,7,21)==0);
    // Exercise the production attack table: main/offhand auto swings select
    // their own source-derived critical chance; combo specials use main hand.
    p.equipment[15]=0;p.equipment[16]=2092;auto earned=localMeleeStats(p,c);
    auto reset=p;reset.talents.clear();auto plain=localMeleeStats(reset,c);
    LocalRealmNpc n;n.entry=0;n.level=p.level;n.x=1;n.orientation=0;
    unsigned autoDelta=0;
    for(uint32_t roll=0;roll<10000;++roll) {
        assert(localRollPlayerMelee(p,n,earned,false,roll,9999,false)==localRollPlayerMelee(reset,n,plain,false,roll,9999,false));
        if(localRollPlayerMelee(p,n,earned,false,roll,9999,true)!=localRollPlayerMelee(reset,n,plain,false,roll,9999,true))++autoDelta;
    }
    assert(autoDelta==500);
    p.equipment[15]=25;p.equipment[16]=0;earned=localMeleeStats(p,c);reset=p;reset.talents.clear();plain=localMeleeStats(reset,c);
    unsigned specialDelta=0;autoDelta=0;
    for(uint32_t roll=0;roll<10000;++roll) {
        if(localRollPlayerMelee(p,n,earned,false,roll,9999)!=localRollPlayerMelee(reset,n,plain,false,roll,9999))++autoDelta;
        if(localRollPlayerMelee(p,n,earned,true,9999,roll)!=localRollPlayerMelee(reset,n,plain,true,9999,roll))++specialDelta;
    }
    assert(autoDelta==500&&specialDelta==500);
    p.talents.clear();assert(std::abs(localMeleeStats(p,c).crit-baseline.crit)<.0001f);
    std::cout<<"PASS: real DBC Cruelty ranks 1..5, legal Cruelty -> Unbridled Wrath ranks 1..5, no rank stacking; per-hand weapon ownership/equipment/class masks; malformed state and reset; production auto/main/offhand and special attack tables each gain exactly 500/10000 crit outcomes at rank 5\n";
}
