#include "game/local_spell_import.hpp"
#include "game/local_talents.hpp"
#include "game/local_druid_progression_import.hpp"
#include "game/local_progression_modifiers.hpp"
#include <cmath>
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
    constexpr uint32_t ids[]={17050,17051,17069,17070,17071,17072,17073,16833,16834,16835};
    auto accepted=[&](unsigned profile) {
        const auto rank=uint8_t(profile<2?profile+1:profile<7?profile-1:profile-6);
        LocalSpellDefinition d;d.id=ids[profile];d.clientSpell=true;d.allowableClasses=1024;
        d.talentId=profile<2?821:profile<7?824:826;d.talentRank=rank;
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,d.id);assert(row>=0);
        detail::decodeClientSpell(t,uint32_t(row),d);
        if(!decodeClientDruidProgressionTalent(t,uint32_t(row),d)||!d.unsupportedReason.empty()||!d.passive)return false;
        const auto& m=d.passiveCastModifiers[0];
        if(profile<2)return d.passiveTotalStatPct==std::array<uint8_t,5>{rank,rank,rank,rank,rank}&&
            m.active&&m.operation==8&&m.percentage&&m.amount==rank*20&&m.mask==std::array<uint32_t,3>{262144,0,0};
        if(profile>=7)return !d.durationMs&&m.active&&m.operation==14&&m.percentage&&m.amount==-int32_t(rank*10)&&m.mask==std::array<uint32_t,3>{3758096384u,122880,0};
        return d.passivePhysicalDamagePct==rank*2&&m.active&&m.operation==10&&!m.percentage&&
            m.amount==-int32_t(rank*100)&&m.mask==std::array<uint32_t,3>{32,0,0};
    };
    unsigned rejected=0;
    for(unsigned profile=0;profile<10;++profile) {
        assert(accepted(profile));
        for(uint32_t column=0;column<234;++column) {
            if(column>=131&&column<=203)continue;
            auto edited=bytes["Spell"];const auto row=detail::ClientSpellTables::lookup(t.spellIndex,ids[profile]);
            const size_t offset=20+size_t(row)*t.spells->getRecordSize()+column*4;
            edited[offset]^=1;assert(tables["Spell"].load(edited));
            assert(!accepted(profile));++rejected;assert(tables["Spell"].load(bytes["Spell"]));
        }
    }
    unsigned tableRejected=0;
    for(const auto& [name,column]:std::initializer_list<std::pair<const char*,uint32_t>>{
            {"SpellRange",1},{"SpellRange",2},{"SpellRange",3},{"SpellRange",4},{"SpellRange",5},
            {"SpellCastTimes",1},{"SpellCastTimes",2},{"SpellCastTimes",3}}) {
        auto edited=bytes[name];const auto row=std::string(name)=="SpellRange"?
            detail::ClientSpellTables::lookup(t.rangeIndex,1):detail::ClientSpellTables::lookup(t.castIndex,1);
        const auto offset=20+size_t(row)*tables[name].getRecordSize()+column*4;
        edited[offset]^=1;assert(tables[name].load(edited));
        for(unsigned profile=0;profile<10;++profile){assert(!accepted(profile));++tableRejected;}
        assert(tables[name].load(bytes[name]));
    }
    for(uint32_t column=1;column<4;++column) {
        auto edited=bytes["SpellDuration"];const auto row=detail::ClientSpellTables::lookup(t.durationIndex,21);
        edited[20+size_t(row)*tables["SpellDuration"].getRecordSize()+column*4]^=1;
        assert(tables["SpellDuration"].load(edited));
        for(unsigned profile=2;profile<10;++profile){assert(!accepted(profile));++tableRejected;}
        assert(tables["SpellDuration"].load(bytes["SpellDuration"]));
    }
    for(uint32_t column=1;column<4;++column) {
        auto edited=bytes["SpellDuration"];const auto row=detail::ClientSpellTables::lookup(t.durationIndex,32);
        edited[20+size_t(row)*tables["SpellDuration"].getRecordSize()+column*4]^=1;
        assert(tables["SpellDuration"].load(edited));
        for(unsigned profile=8;profile<10;++profile){assert(!accepted(profile));++tableRejected;}
        assert(tables["SpellDuration"].load(bytes["SpellDuration"]));
    }
    const auto get=[&](const char* n){return &tables[n];};
    auto imported=importClientStarterSpells(get("Spell"),get("SpellRange"),get("SpellCastTimes"),
        get("SpellDuration"),get("SpellIcon"),get("SkillLineAbility"),get("SkillLine"),get("Talent"),
        get("SpellRuneCost"),get("SpellRadius"));
    detail::importClientTalents(imported,get("Talent"),get("TalentTab"),get("Spell"),get("SpellRange"),
        get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SpellRuneCost"),get("SpellRadius"));
    LocalWorldContent c;c.spells=std::move(imported.spells);
    for(const auto id:ids){const auto* d=c.spell(id);assert(d&&d->unsupportedReason.empty());}
    const auto* mark=c.spell(1126);const auto* touch=c.spell(5185);const auto* wrath=c.spell(5176);
    assert(mark&&touch&&wrath&&mark->unsupportedReason.empty()&&touch->unsupportedReason.empty());
    LocalRealmPlayer p;p.classId=11;p.level=23;p.health=100;p.guid=1;std::string error;
    assert(!learnLocalTalent(p,c,827,0,error));assert(!learnLocalTalent(p,c,824,0,error));
    assert(!learnLocalTalent(p,c,821,1,error));
    for(unsigned rank=0;rank<3;++rank)assert(learnLocalTalent(p,c,823,rank,error));
    assert(!learnLocalTalent(p,c,824,0,error));
    for(unsigned rank=0;rank<2;++rank)assert(learnLocalTalent(p,c,821,rank,error));
    const auto beforeNaturalist=p;
    for(unsigned rank=0;rank<5;++rank) {
        assert(!learnLocalTalent(p,c,827,0,error));
        assert(learnLocalTalent(p,c,824,rank,error));
        assert(std::abs(localTalentPhysicalDamageMultiplier(p,c)-(1.f+(rank+1)*.02f))<.00001f);
        assert(localSpellCastTime(p,c,*touch)==touch->castTimeMs-100*(rank+1));
        assert(localSpellCastTime(p,c,*wrath)==wrath->castTimeMs);
    }
    assert(localTalentPointsSpent(p)==10);
    // The minimum Omen path consumes eleven points and is legal at level 20.
    p.level=20;assert(learnLocalTalent(p,c,827,0,error));assert(localTalentPointsAvailable(p)==0);
    assert(c.spell(16864)->clearcastingProfile==2);
    p.level=23;
    for(unsigned rank=0;rank<3;++rank){if(!learnLocalTalent(p,c,826,rank,error)){std::cerr<<"Natural Shapeshifter "<<rank<<": "<<error<<"\n";std::abort();}}
    assert(localTalentPointsSpent(p)==14&&localTalentPointsAvailable(p)==0);
    assert(localTalentCastModifier(p,c,*mark,8,true)==40);
    for(size_t stat=0;stat<5;++stat)assert(localTalentTotalStat(p,c,stat,100)==102);
    auto wrongClass=beforeNaturalist;wrongClass.classId=1;assert(!learnLocalTalent(wrongClass,c,824,0,error));
    assert(localTalentPhysicalDamageMultiplier(wrongClass,c)==1);
    auto dead=p;dead.dead=true;assert(localTalentPhysicalDamageMultiplier(dead,c)==1);
    auto reset=p;reset.talents.clear();assert(localTalentPhysicalDamageMultiplier(reset,c)==1);
    assert(localSpellCastTime(reset,c,*touch)==touch->castTimeMs);
    assert(localTalentCastModifier(reset,c,*mark,8,true)==0);
    for(size_t stat=0;stat<5;++stat)assert(localTalentTotalStat(reset,c,stat,100)==100);
    // Feral progression remains an ordinary guarded tree, not a grant from the
    // Restoration implementation or an already imported internal helper.
    assert(!learnLocalTalent(reset,c,801,0,error));
    std::cout<<"PASS: 10 complete source profiles (9 new admissions); "<<rejected<<" gameplay-column and "<<tableRejected
        <<" related-table mutation rejections; Nature Focus3 + Improved Mark2 + Naturalist5 -> Omen at level20; Natural Shapeshifter3 legal; rank/class/tier/reset guards; both effects retained\n";
}
