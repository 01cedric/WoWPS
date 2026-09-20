#include "game/local_melee.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_proc_talents.hpp"
#include "pipeline/dbc_loader.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <source_location>
using namespace wowee;
using namespace wowee::game;
static void close(float actual,float expected,std::source_location location=std::source_location::current()){
    if(!std::isfinite(actual)||std::abs(actual-expected)>=.00001f){
        std::cerr<<"line "<<location.line()<<": actual="<<actual<<" expected="<<expected<<'\n';std::abort();
    }
}
int main(int argc,char** argv){
    assert(argc==2);
    std::map<std::string,pipeline::DBCFile> tables;
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon","Talent","TalentTab","SpellRuneCost","SpellRadius"}){
        std::ifstream input(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> data{std::istreambuf_iterator<char>(input),{}};assert(tables[name].load(data));
    }
    auto get=[&](const char* name){return &tables.at(name);};
    LocalSpellImport imported;
    detail::importClientTalents(imported,get("Talent"),get("TalentTab"),get("Spell"),get("SpellRange"),get("SpellCastTimes"),
        get("SpellDuration"),get("SpellIcon"),get("SpellRuneCost"),get("SpellRadius"));
    LocalWorldContent c;c.spells=std::move(imported.spells);
    std::sort(c.spells.begin(),c.spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    uint32_t main=0,off=0,rating=0;
    for(uint32_t id=1;id<60000&&(!main||!off||!rating);++id)if(const auto* source=localMeleeItem(id);source&&!source->scaling){
        if(!main&&source->itemClass==2&&source->inventoryType==13&&source->delay)main=id;
        else if(!off&&source->itemClass==2&&source->inventoryType==13&&source->delay&&id!=main)off=id;
        if(!rating&&source->inventoryType==1&&source->stats[12]>0)rating=id;
    }
    assert(main&&off&&rating);
    for(auto id:{main,off,rating}){LocalItemDefinition d;d.id=id;d.inventoryType=localMeleeItem(id)->inventoryType;c.items.push_back(d);}
    std::sort(c.items.begin(),c.items.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    for(uint8_t clazz:{1,7})for(uint8_t rank=1;rank<=5;++rank){
        LocalRealmPlayer p;p.guid=123;p.classId=clazz;p.race=clazz==1?1:2;p.level=80;p.mapId=1;p.instanceId=2;
        p.equipment[15]=main;p.equipment[16]=off;p.equipment[0]=rating;p.inventory={{main,1},{off,1},{rating,1}};
        if(clazz==7)p.knownSpells.push_back(30798);
        // Component fixture selects the current allocation directly; this is
        // not evidence that the earlier unsupported talent tiers are reachable.
        const uint32_t talent=clazz==1?156:602;p.talents={{talent,rank}};
        const auto* parent=localTalentSpell(c,talent,rank);assert(parent&&parent->unsupportedReason.empty());
        for(size_t i=0;i<3;++i)if(parent->talentPrerequisites[i])p.talents.push_back({parent->talentPrerequisites[i],uint8_t(parent->talentPrerequisiteRanks[i]+1)});
        // Enhancement wields the offhand through talent 1690 itself; knowing its
        // learned spell 30798 does not by itself make the offhand usable.
        if(clazz==7&&std::none_of(p.talents.begin(),p.talents.end(),[](const auto& t){return t.first==1690;})){
            const auto* dual=localTalentSpell(c,1690,1);assert(dual&&dual->passiveCanDualWield&&dual->unsupportedReason.empty());
            p.talents.push_back({1690,1});
            for(size_t i=0;i<3;++i)if(dual->talentPrerequisites[i])p.talents.push_back({dual->talentPrerequisites[i],uint8_t(dual->talentPrerequisiteRanks[i]+1)});
        }
        std::sort(p.talents.begin(),p.talents.end());
        const auto* child=c.spell(parent->proc.spellId);assert(child&&child->triggeredOnly&&validLocalProc(*child));
        const float baseMain=localMeleeSpeed(p,c),baseOff=localMeleeSpeed(p,c,true),baseWeapon=localWeaponAmounts(p,c).seconds;
        assert(baseMain>0&&baseOff>0&&localMeleeStats(p,c).haste>0);
        close(baseMain,baseWeapon/(1+localMeleeStats(p,c).haste/100));
        LocalStatAura aura;aura.spellId=child->id;aura.remainingMs=child->durationMs;aura.mapId=p.mapId;aura.instanceId=p.instanceId;
        aura.casterGuid=p.guid;aura.procCharges=child->proc.charges;p.statAuras={aura};
        const float percent=float(rank*(clazz==1?5:6)),factor=1+percent/100;
        if(clazz==7){
            // Thundering Strikes, Shaman Flurry's direct prerequisite, is
            // supported by actual content now, so the haste component runs
            // against the real progression gate instead of a stubbed one. The
            // prerequisite still has to be allocated for the aura to do work.
            const LocalSpellDefinition* prerequisite=nullptr;
            for(const auto& spell:c.spells)if(spell.talentId==613&&spell.talentRank==5)prerequisite=&spell;
            assert(prerequisite&&prerequisite->unsupportedReason.empty());
            auto unallocated=p;std::erase_if(unallocated.talents,[](const auto& t){return t.first==613;});
            close(localMeleeAuraHastePct(unallocated,c),0);
        }
        close(localMeleeAuraHastePct(p,c),percent);close(localMeleeSpeed(p,c),baseMain/factor);close(localMeleeSpeed(p,c,true),baseOff/factor);
        close(localWeaponAmounts(p,c).seconds,baseWeapon); // PPM remains based on the original weapon period.
        p.attackTimer=baseMain*.75f;p.offHandTimer=baseOff*.25f;
        localRescaleMeleeTimers(p,baseMain,baseOff,c);
        close(p.attackTimer,baseMain*.75f/factor);close(p.offHandTimer,baseOff*.25f/factor);
        close(p.meleePeriodMain,baseMain/factor);close(p.meleePeriodOff,baseOff/factor);
        p.attackTimer=0;p.offHandTimer=0;localRescaleMeleeTimers(p,baseMain,baseOff,c);
        close(p.attackTimer,0);close(p.offHandTimer,0); // Both hands remain due in this frame.
        const auto active=p;
        p.classId=4;close(localMeleeAuraHastePct(p,c),0);p=active;
        p.guid=0;p.statAuras[0].casterGuid=0;close(localMeleeAuraHastePct(p,c),0);p=active;
        LocalStatAura consumed;consumed.spellId=23885;consumed.remainingMs=0;
        p.statAuras.push_back(consumed);close(localMeleeAuraHastePct(p,c),percent);p=active;
        auto excluded=[&](auto mutate){p=active;mutate();close(localMeleeAuraHastePct(p,c),0);close(localMeleeSpeed(p,c),baseMain);};
        excluded([&]{p.dead=true;});excluded([&]{p.statAuras[0].remainingMs=0;});
        excluded([&]{p.statAuras[0].remainingMs=child->durationMs+1;});excluded([&]{p.statAuras[0].mapId++;});
        excluded([&]{p.statAuras[0].instanceId++;});excluded([&]{p.statAuras[0].casterGuid++;});
        excluded([&]{p.statAuras[0].casterGuid=0;});excluded([&]{p.statAuras[0].procCharges=0;});
        excluded([&]{p.statAuras[0].procCharges=4;});excluded([&]{p.statAuras[0].stacks=2;});
        excluded([&]{p.statAuras.push_back(p.statAuras[0]);});
        excluded([&]{p.talents.clear();});excluded([&]{for(auto& t:p.talents)if(t.first==talent)t.second=rank==1?2:1;});
        p=active;p.attackTimer=baseMain*.5f/factor;p.offHandTimer=baseOff*.5f/factor;p.statAuras.clear();
        localRescaleMeleeTimers(p,baseMain/factor,baseOff/factor,c);close(p.attackTimer,baseMain*.5f);close(p.offHandTimer,baseOff*.5f);
        p=active;p.equipment[16]=0;localRescaleMeleeTimers(p,baseMain/factor,baseOff/factor,c);close(p.offHandTimer,0);close(p.meleePeriodOff,0);
    }
    close(localRescaledMeleeTimer(1,2,1),.5f);close(localRescaledMeleeTimer(.5f,1,2),1);
    close(localRescaledMeleeTimer(0,2,1),0);close(localRescaledMeleeTimer(-1,2,1),0);
    close(localRescaledMeleeTimer(1,2,0),0);close(localRescaledMeleeTimer(1,0,2),2);
    close(localRescaledMeleeTimer(3,2,1),1.5f);close(localRescaledMeleeTimer(3,2,4),6);
    close(localRescaledMeleeTimer(100,2,1),50);close(localRescaledMeleeTimer(1,2,100),5);
    close(localRescaledMeleeTimer(std::numeric_limits<float>::quiet_NaN(),2,1),0);
    close(localRescaledMeleeTimer(1,2,std::numeric_limits<float>::infinity()),0);
    close(localRescaledMeleeTimer(1,std::numeric_limits<float>::infinity(),2),2);
    std::cout<<"PASS: ten real-DBC Flurry profiles; actual Shaman direct-prerequisite rejection plus isolated haste-component fixture; rating/aura multiplicative periods on both hands; base-weapon PPM unchanged; current rank ownership and aura lifecycle guards including unrelated consumed tombstone; apply/remove progress preservation, both due hands remain due, offhand removal and finite timer bounds\n";
}
