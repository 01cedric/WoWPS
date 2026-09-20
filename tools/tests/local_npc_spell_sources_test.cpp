#include "game/local_npc_spell_import.hpp"
#include "pipeline/dbc_loader.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace wowee;
using namespace wowee::game;
struct Table {
    std::vector<std::array<uint32_t,234>> rows;
    uint32_t getUInt32(uint32_t row,uint32_t col) const{return rows.at(row).at(col);}
    int32_t getInt32(uint32_t row,uint32_t col) const{return int32_t(getUInt32(row,col));}
    float getFloat(uint32_t row,uint32_t col) const{float f;const auto u=getUInt32(row,col);std::memcpy(&f,&u,4);return f;}
    std::string getString(uint32_t,uint32_t)const{return "Test source display name";}
    uint32_t getRecordCount()const{return uint32_t(rows.size());}
};
struct Tables {
    bool ready=true;const Table *spells=nullptr,*ranges=nullptr,*casts=nullptr;
    std::vector<std::pair<uint32_t,uint32_t>> spellIndex,rangeIndex,castIndex;
    static int32_t lookup(const std::vector<std::pair<uint32_t,uint32_t>>& index,uint32_t id){for(auto [key,row]:index)if(key==id)return int32_t(row);return -1;}
};
int main(int argc,char** argv){
    assert(argc==2);Table spells,casts,ranges;
    for(const auto& name:{"Spell","SpellRange","SpellCastTimes"}){
        std::ifstream input(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),{}};pipeline::DBCFile dbc;assert(dbc.load(bytes));
        for(uint32_t row=0;row<dbc.getRecordCount();++row){
            const auto id=dbc.getUInt32(row,0);const bool spell=std::string(name)=="Spell",cast=std::string(name)=="SpellCastTimes";
            if(spell?(id!=5401&&id!=11985):cast?(id!=5&&id!=14):(id!=4&&id!=5))continue;
            std::array<uint32_t,234> values{};for(uint32_t col=0;col<dbc.getFieldCount();++col)values[col]=dbc.getUInt32(row,col);
            (spell?spells:cast?casts:ranges).rows.push_back(values);
        }
    }
    assert(spells.rows.size()==2&&casts.rows.size()==2&&ranges.rows.size()==2);
    Tables t;t.spells=&spells;t.casts=&casts;t.ranges=&ranges;
    for(uint32_t i=0;i<2;++i){t.spellIndex.push_back({spells.rows[i][0],i});t.castIndex.push_back({casts.rows[i][0],i});t.rangeIndex.push_back({ranges.rows[i][0],i});}
    unsigned rejected=0;
    for(uint32_t id:{5401u,11985u}){
        const auto row=uint32_t(Tables::lookup(t.spellIndex,id));LocalSpellDefinition d;d.id=id;assert(decodeLocalNpcSpell(t,row,d));
        assert(d.npcOnly&&d.clientSpell&&!d.triggeredOnly&&!d.allowableClasses&&!d.mana&&!d.sourceCantReflect);
        assert(d.sourceDamageClass==1&&d.directEffectSlot==0&&d.sourceRawCastTimeMs==d.castTimeMs);
        assert(d.damage>0&&d.damageMax>=d.damage&&d.range>0&&d.unsupportedReason.empty());
        assert(d.sourceProjectileSpeed==(id==5401?0.f:24.f));
        // Every retained functional column is a fail-closed source contract,
        // including currently unused effects and targeting/trigger metadata.
        for(uint32_t col=1;col<234;++col){
            if(col>=131&&col<=203)continue;
            spells.rows[row][col]^=1;LocalSpellDefinition bad;bad.id=id;
            assert(!decodeLocalNpcSpell(t,row,bad)&&!bad.unsupportedReason.empty());++rejected;spells.rows[row][col]^=1;
        }
        for(auto pair:{std::pair<Table*,uint32_t>{&casts,uint32_t(Tables::lookup(t.castIndex,id==5401?5:14))},
                       std::pair<Table*,uint32_t>{&ranges,uint32_t(Tables::lookup(t.rangeIndex,id==5401?4:5))}}){
            for(unsigned col=1;col<=(pair.first==&casts?3u:5u);++col){
                pair.first->rows[pair.second][col]^=1;LocalSpellDefinition bad;bad.id=id;
                assert(!decodeLocalNpcSpell(t,row,bad));++rejected;pair.first->rows[pair.second][col]^=1;
            }
        }
    }
    std::vector<LocalSpellDefinition> retained;assert(importLocalNpcSpells(t,retained)==2&&retained.size()==2);
    assert(importLocalNpcSpells(t,retained)==0&&retained.size()==2); // No duplicates or replacement.
    retained[0].npcOnly=false;assert(importLocalNpcSpells(t,retained)==0&&!retained[0].npcOnly);
    t.ready=false;retained.clear();assert(importLocalNpcSpells(t,retained)==0&&retained.empty());
    assert(localNpcSpellProfile(4008)->spellId==5401&&localNpcSpellProfile(4323)->spellId==11985&&localNpcSpellProfile(5858)->spellId==11985);
    assert(!localNpcSpellProfile(0)&&!localNpcSpellProfile(4009)&&!localNpcSpellProfile(UINT32_MAX));
    assert(localNpcSpellDamageScale(4323,20,11985)==1.f&&localNpcSpellDamageScale(4323,39,11985)>1.f);
    assert(localNpcSpellDamageScale(5858,47,11985)>localNpcSpellDamageScale(4323,39,11985));
    assert(localNpcSpellDamageScale(4008,15,5401)==1.f&&!localNpcSpellDamageScale(4008,15,11985));
    assert(!localNpcSpellDamageScale(5858,0,11985)&&!localNpcSpellDamageScale(5858,84,11985));
    std::cout<<"PASS NPC whole-profile source admission, "<<rejected<<" rejected source mutations, internal isolation\n";
}
