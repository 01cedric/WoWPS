#include "game/local_spell_import.hpp"
#include <cassert>
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
    std::map<std::string,std::vector<uint8_t>> bytes;
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration"}) {
        std::ifstream f(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        bytes[name]={std::istreambuf_iterator<char>(f),{}};assert(tables[name].load(bytes[name]));
    }
    detail::ClientSpellTables t;t.spells=&tables["Spell"];t.ranges=&tables["SpellRange"];
    t.casts=&tables["SpellCastTimes"];t.durations=&tables["SpellDuration"];
    detail::ClientSpellTables::buildIndex(t.spells,t.spellIndex);
    detail::ClientSpellTables::buildIndex(t.ranges,t.rangeIndex);
    detail::ClientSpellTables::buildIndex(t.casts,t.castIndex);
    detail::ClientSpellTables::buildIndex(t.durations,t.durationIndex);
    auto accepted=[&](uint32_t id) {
        LocalSpellDefinition d;d.id=id;d.clientSpell=true;
        d.allowableClasses=id==974?64:id==12322?1:8;
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,id);assert(row>=0);
        if(id==974)return detail::decodeClientSpell(t,uint32_t(row),d);
        d.talentId=1;d.talentRank=1;return detail::decodeClientResourceProcTalent(t,uint32_t(row),d);
    };
    for(auto id:{974u,12322u,51634u,35541u})assert(accepted(id));
    struct Mutation {uint32_t parent,spell,column,value;};
    const Mutation cases[]={
        {974,974,72,0}, // Cannot drop Earth Shield's pushback effect.
        {974,974,111,127}, // School filtering is mandatory.
        {974,974,73,6}, // An additional effect is not discarded.
        {974,379,71,2}, // Heal child cannot become damage.
        {974,379,42,1}, // A child cost cannot be silently waived.
        {12322,12322,72,6},
        {12322,12322,69,0}, // Exact weapon-subclass constraint.
        {12322,12964,110,3}, // Rage child cannot be mislabeled energy.
        {51634,51634,35,101},
        {51634,51634,12,1}, // New form restrictions are not ignored.
        {35541,35541,34,20},
        {35541,35542,72,6},
    };
    for(const auto& mutation:cases) {
        auto edited=bytes["Spell"];
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,mutation.spell);assert(row>=0);
        const size_t offset=20+size_t(row)*t.spells->getRecordSize()+mutation.column*4;
        for(unsigned k=0;k<4;++k)edited[offset+k]=uint8_t(mutation.value>>(8*k));
        assert(tables["Spell"].load(edited));
        if(accepted(mutation.parent)){std::cerr<<"Unexpected acceptance parent="<<mutation.parent<<" changedSpell="<<mutation.spell<<" column="<<mutation.column<<"\n";return 1;}
        assert(tables["Spell"].load(bytes["Spell"]));assert(accepted(mutation.parent));
    }
    std::cout<<"PASS:4 real profile baselines;12 whole-profile rejection mutations; restored baselines\n";
}
