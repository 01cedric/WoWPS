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
    constexpr uint32_t parents[]={12319,12971,12972,12973,12974,16256,16281,16282,16283,16284};
    constexpr uint32_t children[]={12966,12967,12968,12969,12970,16257,16277,16278,16279,16280};
    auto accepted=[&](unsigned profile) {
        const bool shaman=profile>=5;const uint8_t rank=profile%5+1;
        LocalSpellDefinition d;d.id=parents[profile];d.clientSpell=true;d.allowableClasses=shaman?64:1;
        d.talentId=shaman?602:156;d.talentRank=rank;
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,d.id);assert(row>=0);
        detail::decodeClientSpell(t,uint32_t(row),d);
        LocalSpellDefinition aura;
        return decodeClientFlurryTalent(t,uint32_t(row),d,&aura)&&d.unsupportedReason.empty()&&
            d.proc.effect==LocalProcEffect::ApplyOwnerAura&&d.proc.spellId==children[profile]&&
            d.proc.allowTriggered&&d.proc.hitMask==LocalProcHitCritical&&d.proc.flags==20&&
            aura.triggeredOnly&&!aura.passive&&aura.id==children[profile]&&aura.durationMs==15000&&
            aura.meleeHastePct==(shaman?6:5)*rank&&aura.procParentTalentId==d.talentId&&
            aura.proc.effect==LocalProcEffect::ConsumeOwnerAuraCharge&&aura.proc.charges==3&&
            aura.proc.cooldownMs==(shaman?500u:0u)&&aura.proc.flags==4&&
            aura.proc.hitMask==(LocalProcHitNormal|LocalProcHitCritical|LocalProcHitAbsorb)&&
            validLocalProc(d)&&validLocalProc(aura);
    };
    for(unsigned profile=0;profile<10;++profile)assert(accepted(profile));
    struct Mutation {bool child;uint32_t column,value;};
    const Mutation cases[]={
        {false,72,6},{false,95,4},{false,86,21},{false,116,17687},{false,7,0},
        {false,34,4},{false,35,99},{false,36,1},{false,42,1},{false,68,2},
        {false,77,1065353216},{false,98,1000},{false,119,1},{false,204,1},
        {true,72,6},{true,95,31},{true,80,99},{true,34,20},{true,35,50},
        {true,36,5},{true,40,31},{true,86,21},{true,116,23880},{true,42,1},
        {true,98,1000},{true,119,1},{true,12,1},{true,226,1}};
    unsigned rejected=0;
    for(unsigned profile=0;profile<10;++profile)for(const auto& mutation:cases) {
        auto edited=bytes["Spell"];
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,mutation.child?children[profile]:parents[profile]);assert(row>=0);
        const size_t offset=20+size_t(row)*t.spells->getRecordSize()+mutation.column*4;
        for(unsigned k=0;k<4;++k)edited[offset+k]=uint8_t(mutation.value>>(8*k));
        assert(tables["Spell"].load(edited));
        if(accepted(profile)){std::cerr<<"Unexpected acceptance parent="<<parents[profile]<<" child="<<mutation.child<<" column="<<mutation.column<<"\n";return 1;}
        ++rejected;assert(tables["Spell"].load(bytes["Spell"]));
    }
    // Auxiliary duration table is part of the source closure, not a guessed timeout.
    auto edited=bytes["SpellDuration"];const auto row=detail::ClientSpellTables::lookup(t.durationIndex,8);
    const size_t offset=20+size_t(row)*t.durations->getRecordSize()+4;
    edited[offset]=0;edited[offset+1]=0;assert(tables["SpellDuration"].load(edited));
    for(unsigned profile=0;profile<10;++profile)assert(!accepted(profile));
    assert(tables["SpellDuration"].load(bytes["SpellDuration"]));
    for(unsigned profile=0;profile<10;++profile)assert(accepted(profile));
    std::cout<<"PASS: 10 real Flurry parent/child closures; "<<rejected<<" complete-profile mutation rejections; 10 duration-table rejections; restored baselines\n";
}
