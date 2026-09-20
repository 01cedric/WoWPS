#include "game/local_spell_import.hpp"
#include "game/local_feral_progression_import.hpp"
#include "game/local_talents.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace wowee;
using namespace wowee::game;
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
    constexpr uint32_t ids[]={16934,16935,16936,16937,16938,16998,16999,16929,16930,16931,
                              16942,16943,16944,17002,24866};
    auto accepted=[&](unsigned profile) {
        const auto rank=uint8_t(profile<5?profile+1:profile<7?profile-4:profile<10?profile-6:
                               profile<13?profile-9:profile-12);
        LocalSpellDefinition d;d.id=ids[profile];d.clientSpell=true;d.allowableClasses=1024;
        d.talentId=profile<5?796:profile<7?805:profile<10?794:profile<13?798:807;d.talentRank=rank;
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,d.id);assert(row>=0);
        detail::decodeClientSpell(t,uint32_t(row),d);
        if(!decodeClientFeralProgressionTalent(t,uint32_t(row),d)||!d.unsupportedReason.empty()||!d.passive)return false;
        const auto& m=d.passiveCastModifiers;
        if(profile<5)return m[0].active&&m[0].operation==14&&!m[0].percentage&&m[0].amount==-int32_t(rank*10)&&
            m[0].mask==std::array<uint32_t,3>{2048,1048640,0}&&m[1].active&&m[1].operation==14&&
            !m[1].percentage&&m[1].amount==-int32_t(rank)&&m[1].mask==std::array<uint32_t,3>{4096,1024,263168}&&!m[2].active;
        if(profile<7)return m[0].active&&m[0].operation==0&&m[0].percentage&&m[0].amount==rank*10&&
            m[0].mask==std::array<uint32_t,3>{6144,0,262144}&&m[1].active&&m[1].operation==22&&
            m[1].percentage&&m[1].amount==rank*10&&m[1].mask==std::array<uint32_t,3>{4096,0,0}&&
            m[2].active&&m[2].operation==23&&m[2].percentage&&m[2].amount==rank*10&&m[2].mask==std::array<uint32_t,3>{0,1088,0};
        if(profile<10)return d.passiveEquipmentArmorPct==rank*3+1;
        if(profile<13)return d.passiveFeralCritPct==rank*2;
        return d.passiveCatRunPct==rank*15&&d.passiveFeralDodgePct==rank*2;
    };
    unsigned rejected=0;
    for(unsigned profile=0;profile<15;++profile) {
        assert(accepted(profile));
        for(uint32_t column=0;column<234;++column) {
            if(column>=131&&column<=203)continue;
            auto edited=bytes["Spell"];const auto row=detail::ClientSpellTables::lookup(t.spellIndex,ids[profile]);
            edited[20+size_t(row)*t.spells->getRecordSize()+column*4]^=1;
            assert(tables["Spell"].load(edited));assert(!accepted(profile));++rejected;
            assert(tables["Spell"].load(bytes["Spell"]));
        }
    }
    unsigned helperRejected=0;
    for(unsigned rank=1;rank<=2;++rank) {
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,rank==1?24867:24864);
        for(uint32_t column=0;column<234;++column) {
            if(column>=131&&column<=203)continue;
            auto edited=bytes["Spell"];edited[20+size_t(row)*t.spells->getRecordSize()+column*4]^=1;
            assert(tables["Spell"].load(edited));assert(!accepted(12+rank));
            ++helperRejected;assert(tables["Spell"].load(bytes["Spell"]));
        }
        // A missing reviewed helper cannot leave the Cat-speed-only parent admitted.
        auto missing=t;std::erase_if(missing.spellIndex,[&](const auto& x){return x.first==(rank==1?24867u:24864u);});
        assert(!localFeralSwiftnessHelperMatches(missing,rank));
    }
    unsigned relatedRejected=0;
    for(const auto& [name,column]:std::initializer_list<std::pair<const char*,uint32_t>>{
            {"SpellRange",1},{"SpellRange",2},{"SpellRange",3},{"SpellRange",4},{"SpellRange",5},
            {"SpellCastTimes",1},{"SpellCastTimes",2},{"SpellCastTimes",3}}) {
        auto edited=bytes[name];const auto row=std::string(name)=="SpellRange"?
            detail::ClientSpellTables::lookup(t.rangeIndex,1):detail::ClientSpellTables::lookup(t.castIndex,1);
        edited[20+size_t(row)*tables[name].getRecordSize()+column*4]^=1;assert(tables[name].load(edited));
        for(unsigned profile=0;profile<15;++profile){assert(!accepted(profile));++relatedRejected;}
        assert(tables[name].load(bytes[name]));
    }
    for(unsigned column=1;column<=3;++column) {
        auto edited=bytes["SpellDuration"];const auto row=detail::ClientSpellTables::lookup(t.durationIndex,21);
        edited[20+size_t(row)*tables["SpellDuration"].getRecordSize()+column*4]^=1;
        assert(tables["SpellDuration"].load(edited));
        for(unsigned profile=10;profile<15;++profile){assert(!accepted(profile));++relatedRejected;}
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
    LocalRealmPlayer p;p.classId=11;p.level=26;p.health=100;p.guid=1;std::string error;
    for(const auto id:{805,794,807,798,801})assert(!learnLocalTalent(p,c,id,0,error));
    assert(!learnLocalTalent(p,c,796,1,error));
    for(unsigned rank=0;rank<5;++rank)assert(learnLocalTalent(p,c,796,rank,error));
    assert(!learnLocalTalent(p,c,798,0,error));assert(!learnLocalTalent(p,c,807,0,error));
    for(unsigned rank=0;rank<2;++rank)assert(learnLocalTalent(p,c,805,rank,error));
    for(unsigned rank=0;rank<3;++rank)assert(learnLocalTalent(p,c,794,rank,error));
    for(unsigned rank=0;rank<2;++rank)assert(learnLocalTalent(p,c,807,rank,error));
    for(unsigned rank=0;rank<3;++rank) {
        assert(!learnLocalTalent(p,c,801,0,error));
        assert(learnLocalTalent(p,c,798,rank,error));
    }
    assert(localTalentPointsSpent(p)==15);
    // At level24 the entire earlier path exhausts points; no next-tier grant.
    auto level24=p;level24.level=24;assert(!learnLocalTalent(level24,c,801,0,error));
    p.level=25;assert(learnLocalTalent(p,c,801,0,error));assert(!learnLocalTalent(p,c,801,1,error));
    p.level=26;assert(learnLocalTalent(p,c,801,1,error));
    assert(localTalentPointsSpent(p)==17&&localTalentPointsAvailable(p)==0);
    const auto* primal=c.spell(37117);assert(primal&&primal->unsupportedReason.empty());
    assert(primal->talentPrerequisites[0]==798&&primal->talentPrerequisiteRanks[0]==2);
    auto wrongClass=LocalRealmPlayer{};wrongClass.classId=1;wrongClass.level=80;
    assert(!learnLocalTalent(wrongClass,c,796,0,error));
    // Spending Restoration points cannot satisfy the actual Feral tree tiers.
    LocalRealmPlayer other;other.classId=11;other.level=80;
    for(unsigned rank=0;rank<3;++rank)assert(learnLocalTalent(other,c,823,rank,error));
    for(unsigned rank=0;rank<2;++rank)assert(learnLocalTalent(other,c,821,rank,error));
    assert(!learnLocalTalent(other,c,805,0,error));assert(!learnLocalTalent(other,c,801,0,error));
    std::cout<<"PASS: 15 Feral parent profiles +2 scripted helpers; "<<rejected<<" parent-column, "
             <<helperRejected<<" helper-column and "<<relatedRejected<<" auxiliary-table mutations rejected; "
             <<"normal Ferocity5/SavageFury2/ThickHide3/FeralSwiftness2/SharpenedClaws3 -> PrimalFury2 at level26; rank/class/tier/point guards\n";
}
