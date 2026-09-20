#include "game/local_spell_import.hpp"
#include "game/local_talents.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace wowee;
using namespace wowee::game;
// Only content lookup is supplied by this focused harness. The decoder and
// learnLocalTalent rank/tier/prerequisite transaction are production code.
const LocalSpellDefinition* LocalWorldContent::spell(uint32_t id) const {
    for(const auto& d:spells)if(d.id==id)return &d;return nullptr;
}
int main(int argc,char** argv) {
    assert(argc==2);
    std::map<std::string,pipeline::DBCFile> tables;
    std::map<std::string,std::vector<uint8_t>> bytes;
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon",
                         "SpellRadius","SpellRuneCost","SkillLine","SkillLineAbility","Talent","TalentTab"}) {
        std::ifstream f(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        bytes[name]={std::istreambuf_iterator<char>(f),{}};assert(tables[name].load(bytes[name]));
    }
    detail::ClientSpellTables t;t.spells=&tables["Spell"];t.ranges=&tables["SpellRange"];
    t.casts=&tables["SpellCastTimes"];t.durations=&tables["SpellDuration"];
    detail::ClientSpellTables::buildIndex(t.spells,t.spellIndex);
    detail::ClientSpellTables::buildIndex(t.ranges,t.rangeIndex);
    detail::ClientSpellTables::buildIndex(t.casts,t.castIndex);
    detail::ClientSpellTables::buildIndex(t.durations,t.durationIndex);
    constexpr uint32_t ids[]={17485,17486,17487,17488,17489,16255,16302,16303,16304,16305,16262,16287};
    auto accepted=[&](unsigned profile) {
        const auto rank=uint8_t(profile<10?profile%5+1:profile-9);
        LocalSpellDefinition d;d.id=ids[profile];d.clientSpell=true;d.allowableClasses=64;
        d.talentId=profile<5?614:profile<10?613:605;d.talentRank=rank;
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,d.id);assert(row>=0);
        detail::decodeClientSpell(t,uint32_t(row),d);
        if(!decodeClientProgressionTalent(t,uint32_t(row),d)||!d.unsupportedReason.empty()||!d.passive)return false;
        if(profile<5)return d.passiveTotalStatPct==std::array<uint8_t,5>{0,0,0,uint8_t(rank*2),0};
        if(profile<10)return d.passiveMeleeCritPct==rank&&d.passiveSpellCritPct==rank&&d.requiredItemClass==-1;
        const auto& m=d.passiveCastModifiers[0];return m.active&&m.operation==10&&!m.percentage&&
            m.amount==-int32_t(rank*1000)&&m.mask==std::array<uint32_t,3>{2048,0,0}&&!d.passiveCastModifiers[1].active;
    };
    unsigned rejected=0;
    for(unsigned profile=0;profile<12;++profile) {
        assert(accepted(profile));
        for(const uint32_t column:{0,4,7,20,34,36,42,68,71,72,73,74,77,80,81,86,95,96,107,110,111,116,122,125,204,208,211,225,226}) {
            auto edited=bytes["Spell"];const auto row=detail::ClientSpellTables::lookup(t.spellIndex,ids[profile]);
            const size_t offset=20+size_t(row)*t.spells->getRecordSize()+column*4;
            edited[offset]^=1;assert(tables["Spell"].load(edited));
            assert(!accepted(profile));++rejected;assert(tables["Spell"].load(bytes["Spell"]));
        }
    }
    unsigned tableRejected=0;
    for(const auto& [name,column]:std::initializer_list<std::pair<const char*,uint32_t>>{
            {"SpellRange",5},{"SpellCastTimes",3}}) {
        auto edited=bytes[name];const auto row=std::string(name)=="SpellRange"?
            detail::ClientSpellTables::lookup(t.rangeIndex,1):detail::ClientSpellTables::lookup(t.castIndex,1);
        const auto offset=20+size_t(row)*tables[name].getRecordSize()+column*4;
        edited[offset]^=1;assert(tables[name].load(edited));
        for(unsigned profile=0;profile<12;++profile){assert(!accepted(profile));++tableRejected;}
        assert(tables[name].load(bytes[name]));
    }
    const auto get=[&](const char* n){return &tables[n];};
    auto imported=importClientStarterSpells(get("Spell"),get("SpellRange"),get("SpellCastTimes"),
        get("SpellDuration"),get("SpellIcon"),get("SkillLineAbility"),get("SkillLine"),get("Talent"),
        get("SpellRuneCost"),get("SpellRadius"));
    detail::importClientTalents(imported,get("Talent"),get("TalentTab"),get("Spell"),get("SpellRange"),
        get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SpellRuneCost"),get("SpellRadius"));
    LocalWorldContent c;c.spells=std::move(imported.spells);
    for(const auto id:ids){const auto* d=c.spell(id);assert(d&&d->unsupportedReason.empty());}
    const auto* wolf=c.spell(2645);assert(wolf&&wolf->unsupportedReason.empty()&&wolf->formId==16&&wolf->baseLevel==16);
    LocalRealmPlayer p;p.classId=7;p.level=29;p.health=100;p.guid=1;std::string error;
    assert(!learnLocalTalent(p,c,602,0,error));assert(!learnLocalTalent(p,c,613,0,error));
    assert(!learnLocalTalent(p,c,614,1,error));
    for(unsigned rank=0;rank<5;++rank)assert(learnLocalTalent(p,c,614,rank,error));
    for(unsigned rank=0;rank<5;++rank)assert(learnLocalTalent(p,c,613,rank,error));
    for(unsigned rank=0;rank<3;++rank)assert(learnLocalTalent(p,c,607,rank,error));
    assert(!learnLocalTalent(p,c,602,0,error));
    assert(learnLocalTalent(p,c,605,0,error));assert(localSpellCastTime(p,c,*wolf)==1000);
    assert(!learnLocalTalent(p,c,602,0,error));
    assert(learnLocalTalent(p,c,605,1,error));assert(localSpellCastTime(p,c,*wolf)==0);
    const auto beforeFlurry=p;
    for(unsigned rank=0;rank<5;++rank)assert(learnLocalTalent(p,c,602,rank,error));
    assert(localTalentPointsSpent(p)==20&&localTalentPointsAvailable(p)==0);
    auto wrongClass=beforeFlurry;wrongClass.classId=1;assert(!learnLocalTalent(wrongClass,c,602,0,error));
    // A malformed saved allocation retains fifteen tree points but has lost
    // the direct prerequisite's fifth rank. It may not learn Flurry rank 2.
    auto prerequisite=beforeFlurry;
    assert(learnLocalTalent(prerequisite,c,602,0,error));
    for(auto& allocated:prerequisite.talents)if(allocated.first==613)allocated.second=4;
    assert(!learnLocalTalent(prerequisite,c,602,1,error));assert(error=="Required talent rank is missing");
    std::cout<<"PASS: 12 complete source profiles, "<<rejected<<" gameplay-column and "<<tableRejected<<" related-table mutation rejections; real 15-point Enhancement path unlocks all five Flurry ranks at level29; tier/rank/class/direct-prerequisite guards; Ghost Wolf cast 2s->1s->instant\n";
}
