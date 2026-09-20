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
        const uint32_t cruelty[]={12320,12852,12853,12855,12856};
        const auto it=std::find(std::begin(cruelty),std::end(cruelty),id);
        const bool warrior=it!=std::end(cruelty);
        d.allowableClasses=warrior?1:1024;
        d.talentId=warrior?157:801;d.talentRank=warrior?uint8_t(it-std::begin(cruelty)+1):id==37116?1:2;
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,id);assert(row>=0);
        // Include the general decoder's intermediate state, exactly as the importer.
        detail::decodeClientSpell(t,uint32_t(row),d);
        if(warrior)return detail::decodeClientCrueltyTalent(t,uint32_t(row),d) &&
            d.passiveMeleeCritPct==d.talentRank && d.requiredItemClass==2 && d.requiredItemSubclasses==173555;
        return detail::decodeClientPrimalFuryTalent(t,uint32_t(row),d) && validLocalProc(d) &&
            d.proc.amount==5 && d.proc.requiredForms==144 && d.secondaryProc.amount==1 &&
            d.secondaryProc.requiredForms==1 && d.secondaryProc.triggerSpellFamilyFlags[0]==233472;
    };
    for(auto id:{12320u,12852u,12853u,12855u,12856u,37116u,37117u})assert(accepted(id));
    struct Mutation {uint32_t parent,spell,column,value;};
    const Mutation cases[]={
        {12320,12320,69,0}, // No equipment bypass.
        {12852,12852,80,4}, // No incorrectly mapped rank amounts.
        {12320,12320,72,6}, // Do not discard additional effects.
        {12320,12320,12,1}, // Do not discard added form requirements.
        {37116,37116,72,0}, // Both learned helper auras mandatory.
        {37117,37117,117,16952}, // Each helper must match the parent's rank.
        {37116,16958,12,1}, // Bear form requirements mandatory.
        {37116,16952,12,144}, // Cat form requirements mandatory.
        {37116,16952,116,16959}, // Cat must resolve a combo-point child.
        {37116,16959,110,3}, // Rage cannot become energy.
        {37116,16959,80,59}, // Review amount as source, not guessed.
        {37116,16953,86,1}, // Combo child targets the attacked enemy.
        {37116,16953,42,1}, // No silently waived costs.
        {37116,16953,72,6}, // No stripped child secondary effects.
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
    std::cout<<"PASS:7 real profile baselines including both dual branches;14 whole-profile rejection mutations; restored baselines\n";
}
