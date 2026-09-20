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
        LocalSpellDefinition d;d.id=id;d.clientSpell=true;d.allowableClasses=1;
        d.supercededBySpell=23892;
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,id);assert(row>=0);
        if(!detail::decodeClientSpell(t,uint32_t(row),d))return false;
        LocalSpellDefinition aura;
        return detail::decodeClientBloodthirst(t,uint32_t(row),d,&aura) &&
            d.damage==50 && d.meleeSpecialProfile==1 && d.triggeredAuraSpellId==23885 &&
            d.mana==20 && d.categoryCooldownMs==4000 && !d.supercededBySpell &&
            aura.triggeredOnly && aura.id==23885 && aura.durationMs==8000 &&
            aura.proc.spellId==23880 && aura.proc.amount==1 && aura.proc.charges==3 && validLocalProc(aura);
    };
    assert(accepted(23881));
    struct Mutation {uint32_t parent,spell,column,value;};
    const Mutation cases[]={
        {23881,23881,69,0},{23881,23881,80,44},{23881,23881,72,0},
        {23881,23881,87,1},{23881,23881,81,9},{23881,23881,73,6},
        {23881,23881,116,23885},{23881,23881,213,1},
        {23881,23885,36,5},{23881,23885,40,1},{23881,23885,116,57792},
        {23881,23885,34,8},{23881,23885,42,1},{23881,23885,72,6},
        {23881,23880,71,136},{23881,23880,86,21},{23881,23880,72,6},
        {23881,23880,42,1},{23881,23880,77,1065353216},
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
    std::cout<<"PASS:real three-record Bloodthirst baseline;19 complete-profile rejection mutations; restored baselines\n";
}
