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
    const uint32_t parents[]={30482,43045,43046},children[]={34913,43043,43044},damage[]={75,130,170},levels[]={62,71,79};
    auto accepted=[&](unsigned rank) {
        LocalSpellDefinition d;d.id=parents[rank];d.clientSpell=true;d.allowableClasses=128;
        auto row=detail::ClientSpellTables::lookup(t.spellIndex,d.id);assert(row>=0);
        if(!detail::decodeClientSpell(t,uint32_t(row),d))return false;
        return d.manaPercent==28 && d.durationMs==1800000 && d.baseLevel==levels[rank] &&
            d.globalCooldownMs==1500 && d.buffSelfOnly && !d.damage && !d.heal &&
            d.spiritCritRatingPct==35 && d.incomingCritReductionPct==5 && d.mageArmorGroup==1 &&
            d.sourceDamageClass==1 && !d.sourceCantCrit && d.procCanCrit &&
            d.proc.spellId==children[rank] && d.proc.amount==damage[rank] && !d.proc.charges &&
            d.proc.flags==8 && d.proc.chance==100 && d.proc.hitMask==1027 && d.proc.allowTriggered &&
            d.supercededBySpell==(rank<2?parents[rank+1]:0) && validLocalProc(d);
    };
    unsigned count=0;
    for(unsigned rank=0;rank<3;++rank) {
        assert(accepted(rank));
        LocalSpellDefinition child;child.id=children[rank];child.allowableClasses=128;
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,child.id);
        assert(!detail::decodeClientSpell(t,row,child));
        for(const auto& m:std::initializer_list<std::pair<uint32_t,uint32_t>>{{72,0},{73,0},{81,0},{82,99},{112,256},{115,3},{116,children[(rank+1)%3]},{34,8},{204,0},{40,0},{213,0},{49,2}}) {
            auto edited=bytes["Spell"];const auto source=detail::ClientSpellTables::lookup(t.spellIndex,parents[rank]);
            const size_t offset=20+size_t(source)*t.spells->getRecordSize()+m.first*4;
            for(unsigned k=0;k<4;++k)edited[offset+k]=uint8_t(m.second>>(8*k));
            assert(tables["Spell"].load(edited));assert(!accepted(rank));
            assert(tables["Spell"].load(bytes["Spell"]));assert(accepted(rank));++count;
        }
        for(const auto& m:std::initializer_list<std::pair<uint32_t,uint32_t>>{{72,6},{213,0},{6,0x20000000},{225,1},{80,999},{42,1},{116,34913}}) {
            auto edited=bytes["Spell"];const auto source=detail::ClientSpellTables::lookup(t.spellIndex,children[rank]);
            const size_t offset=20+size_t(source)*t.spells->getRecordSize()+m.first*4;
            for(unsigned k=0;k<4;++k)edited[offset+k]=uint8_t(m.second>>(8*k));
            assert(tables["Spell"].load(edited));assert(!accepted(rank));
            assert(tables["Spell"].load(bytes["Spell"]));assert(accepted(rank));++count;
        }
    }
    // Related range/cast/duration records are part of the exact closure.
    struct TableMutation {const char* name;uint32_t id,column,value;};
    for(const auto& m:std::initializer_list<TableMutation>{{"SpellRange",6,3,0x42480000},{"SpellRange",6,1,0x3f800000},
            {"SpellRange",1,4,0x3f800000},{"SpellCastTimes",1,3,1},{"SpellDuration",30,1,8000},{"SpellDuration",30,3,8000}}) {
        auto edited=bytes[m.name];int32_t row=-1;
        for(uint32_t i=0;i<tables[m.name].getRecordCount();++i)if(tables[m.name].getUInt32(i,0)==m.id){row=i;break;}
        assert(row>=0);const size_t offset=20+size_t(row)*tables[m.name].getRecordSize()+m.column*4;
        for(unsigned k=0;k<4;++k)edited[offset+k]=uint8_t(m.value>>(8*k));
        assert(tables[m.name].load(edited));for(unsigned rank=0;rank<3;++rank)assert(!accepted(rank));
        assert(tables[m.name].load(bytes[m.name]));for(unsigned rank=0;rank<3;++rank)assert(accepted(rank));++count;
    }
    std::cout<<"PASS: three real Molten Armor rank closures, internal child exclusion, "<<count<<" parent/leaf/table mutation rejections\n";
}
