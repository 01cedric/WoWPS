#include "game/local_spell_import.hpp"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    detail::ClientSpellTables t;t.spells=&tables["Spell"];t.ranges=&tables["SpellRange"];t.casts=&tables["SpellCastTimes"];t.durations=&tables["SpellDuration"];
    detail::ClientSpellTables::buildIndex(t.spells,t.spellIndex);detail::ClientSpellTables::buildIndex(t.ranges,t.rangeIndex);
    detail::ClientSpellTables::buildIndex(t.casts,t.castIndex);detail::ClientSpellTables::buildIndex(t.durations,t.durationIndex);
    unsigned rejected=0;
    for(uint32_t id:{75u,5019u}) {
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,id);assert(row>=0);
        LocalSpellDefinition d;d.id=id;d.clientSpell=true;
        assert(detail::decodeClientSpell(t,row,d)&&d.rangedAutoProfile==(id==75?1:2));
        assert(d.allowableClasses==(id==75?4u:400u)&&d.requiredItemClass==2&&d.weaponDamage);
        for(uint32_t col=1;col<234;++col) {
            if(col>=131&&col<204)continue;
            auto changed=bytes["Spell"];const size_t offset=20+size_t(row)*234*4+col*4;
            changed[offset]^=1;assert(tables["Spell"].load(changed));
            LocalSpellDefinition bad;bad.id=id;bad.clientSpell=true;
            assert(!detail::decodeClientSpell(t,row,bad)&&!bad.unsupportedReason.empty());++rejected;
        }
        assert(tables["Spell"].load(bytes["Spell"]));
        for(const auto name:{"SpellRange","SpellCastTimes"}) {
            const auto idx=std::string(name)=="SpellRange"?detail::ClientSpellTables::lookup(t.rangeIndex,id==75?114:4):detail::ClientSpellTables::lookup(t.castIndex,id==75?18:1);
            const auto fields=tables[name].getFieldCount();
            for(uint32_t col=1;col<=(std::string(name)=="SpellRange"?5u:3u);++col) {
                auto changed=bytes[name];changed[20+size_t(idx)*fields*4+col*4]^=1;assert(tables[name].load(changed));
                LocalSpellDefinition bad;bad.id=id;bad.clientSpell=true;
                assert(!detail::decodeClientSpell(t,row,bad));++rejected;
            }
            assert(tables[name].load(bytes[name]));
        }
    }
    std::cout<<"PASS ranged source: exact75/5019 profiles and "<<rejected<<" functional/timing/range mutations rejected\n";
}
