#include "game/local_spell_import.hpp"
#include "game/local_arcane_import.hpp"
#include "game/local_arcane.hpp"
#include "game/local_talents.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace wowee;using namespace wowee::game;
const LocalSpellDefinition* LocalWorldContent::spell(uint32_t id) const {for(const auto& s:spells)if(s.id==id)return &s;return nullptr;}
int main(int argc,char** argv) {
    assert(argc==2);std::map<std::string,pipeline::DBCFile> tables;std::map<std::string,std::vector<uint8_t>> bytes;
    for(const char* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon","SpellRadius","SpellRuneCost","SkillLine","SkillLineAbility","Talent","TalentTab"}) {
        std::ifstream in(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        bytes[name]={std::istreambuf_iterator<char>(in),{}};assert(tables[name].load(bytes[name]));
    }
    detail::ClientSpellTables t;t.spells=&tables["Spell"];t.ranges=&tables["SpellRange"];t.casts=&tables["SpellCastTimes"];t.durations=&tables["SpellDuration"];
    detail::ClientSpellTables::buildIndex(t.spells,t.spellIndex);detail::ClientSpellTables::buildIndex(t.ranges,t.rangeIndex);
    detail::ClientSpellTables::buildIndex(t.casts,t.castIndex);detail::ClientSpellTables::buildIndex(t.durations,t.durationIndex);
    constexpr uint32_t ids[]={30451,42894,42896,42897,11237,12463,12464,16769,16770};
    auto accepted=[&](unsigned profile) {
        LocalSpellDefinition d;d.id=ids[profile];d.clientSpell=true;d.allowableClasses=128;
        if(profile>=4){d.talentId=80;d.talentRank=profile-3;}
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,d.id);assert(row>=0);
        detail::decodeClientSpell(t,uint32_t(row),d);
        if(profile>=4)return decodeClientArcaneStability(t,uint32_t(row),d)&&d.passivePushbackPct==20*(profile-3)&&d.unsupportedReason.empty();
        LocalSpellDefinition aura;return decodeClientArcaneBlast(t,uint32_t(row),d,&aura)&&d.arcaneBlastProfile==1&&
            d.castTimeMs==2500&&d.manaPercent==7&&d.damage&&d.damageMax>d.damage&&d.unsupportedReason.empty()&&
            aura.id==36032&&aura.arcaneBlastProfile==2&&aura.triggeredOnly&&aura.durationMs==6000&&aura.maxAuraStacks==4;
    };
    unsigned rejected=0;
    for(unsigned profile=0;profile<9;++profile) {
        assert(accepted(profile));
        for(const uint32_t column:{0,4,5,6,8,19,20,28,31,34,36,40,42,49,68,71,72,73,74,77,80,81,86,95,96,110,111,117,122,125,204,208,209,211,213,225,226,229}) {
            auto edited=bytes["Spell"];const auto row=detail::ClientSpellTables::lookup(t.spellIndex,ids[profile]);
            edited[20+size_t(row)*t.spells->getRecordSize()+column*4]^=1;assert(tables["Spell"].load(edited));
            if(accepted(profile)){std::cerr<<"mutation admitted id="<<ids[profile]<<" col="<<column<<"\n";return 1;}
            ++rejected;assert(tables["Spell"].load(bytes["Spell"]));
        }
    }
    for(const uint32_t column:{0,4,5,6,7,8,20,34,35,36,40,49,68,71,72,73,74,80,81,86,95,96,110,111,122,125,128,208,211,213,225,229}) {
        auto edited=bytes["Spell"];const auto row=detail::ClientSpellTables::lookup(t.spellIndex,36032);
        edited[20+size_t(row)*t.spells->getRecordSize()+column*4]^=1;assert(tables["Spell"].load(edited));
        for(unsigned profile=0;profile<4;++profile)assert(!accepted(profile));++rejected;assert(tables["Spell"].load(bytes["Spell"]));
    }
    struct Aux{const char* table;uint32_t id,col;};
    for(const auto& a:std::initializer_list<Aux>{{"SpellCastTimes",19,1},{"SpellCastTimes",19,2},{"SpellCastTimes",19,3},
        {"SpellCastTimes",1,3},{"SpellRange",4,1},{"SpellRange",4,2},{"SpellRange",4,3},{"SpellRange",4,4},
        {"SpellRange",4,5},{"SpellRange",1,3},{"SpellDuration",32,1},{"SpellDuration",32,2},{"SpellDuration",32,3}}) {
        auto edited=bytes[a.table];auto& table=tables[a.table];uint32_t row=0;while(table.getUInt32(row,0)!=a.id)++row;
        edited[20+size_t(row)*table.getRecordSize()+a.col*4]^=1;assert(table.load(edited));
        for(unsigned profile=0;profile<4;++profile)assert(!accepted(profile));++rejected;assert(table.load(bytes[a.table]));
    }
    const auto get=[&](const char* n){return &tables.at(n);};
    auto imported=importClientStarterSpells(get("Spell"),get("SpellRange"),get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SkillLineAbility"),get("SkillLine"),get("Talent"),get("SpellRuneCost"),get("SpellRadius"));
    detail::importClientTalents(imported,get("Talent"),get("TalentTab"),get("Spell"),get("SpellRange"),get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SpellRuneCost"),get("SpellRadius"));
    LocalWorldContent c;c.spells=imported.spells;for(const auto id:ids){assert(c.spell(id)&&c.spell(id)->unsupportedReason.empty());}
    assert(localArcaneBlastAura(c));LocalRealmPlayer p;p.guid=1;p.classId=8;p.level=19;p.health=p.maxHealth=1000;
    std::string error;assert(!learnLocalTalent(p,c,75,0,error));for(unsigned rank=0;rank<5;++rank)assert(learnLocalTalent(p,c,80,rank,error));
    for(unsigned rank=0;rank<5;++rank)assert(learnLocalTalent(p,c,75,rank,error));assert(localTalentPointsSpent(p)==10);
    assert(localTalentPushbackReduction(p,c,*c.spell(30451))==100);assert(localTalentPushbackReduction(p,c,*c.spell(133))==0);
    assert(!learnLocalTalent(p,c,80,5,error));auto wrong=p;wrong.classId=11;wrong.talents.clear();assert(!learnLocalTalent(wrong,c,80,0,error));
    p.talents.clear();assert(localTalentPushbackReduction(p,c,*c.spell(30451))==0);
    std::cout<<"PASS: Arcane Blast four ranks + child36032 + Stability five ranks, "<<rejected<<" source/table mutations rejected; normal Stability5 -> Concentration5 progression, rank/class/tier/reset guards\n";
}
