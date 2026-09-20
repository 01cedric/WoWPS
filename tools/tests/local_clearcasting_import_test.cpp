#include "game/local_spell_import.hpp"
#include "game/local_clearcasting_import.hpp"
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
    constexpr uint32_t parents[]={11213,12574,12575,12576,12577,16864};
    auto accepted=[&](unsigned profile) {
        const bool druid=profile==5;
        LocalSpellDefinition d;d.id=parents[profile];d.clientSpell=true;d.allowableClasses=druid?1024:128;
        d.talentId=druid?827:75;d.talentRank=druid?1:profile+1;
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,d.id);assert(row>=0);
        detail::decodeClientSpell(t,uint32_t(row),d);
        LocalSpellDefinition aura;
        return decodeClientClearcastingTalent(t,uint32_t(row),d,&aura)&&d.unsupportedReason.empty()&&
            d.clearcastingProfile==(druid?2:1)&&d.proc.effect==LocalProcEffect::ApplyOwnerAura&&
            d.proc.spellId==(druid?16870u:12536u)&&d.proc.allowTriggered&&
            d.proc.flags==(druid?81924u:87376u)&&d.proc.chance==(druid?100u:2u*(profile+1))&&
            d.proc.ppm==(druid?3.5f:0)&&d.proc.spellTypeMask==(druid?7:1)&&
            aura.triggeredOnly&&!aura.passive&&aura.durationMs==15000&&aura.proc.charges==1&&
            aura.clearcastingProfile==(druid?4:3)&&aura.chargedCostPct==(druid?-100:-1000)&&
            aura.procParentTalentId==d.talentId&&aura.proc.effect==LocalProcEffect::ConsumeSpellCostCharge&&
            aura.proc.phaseMask==LocalProcPhaseCast&&!aura.proc.allowTriggered&&
            aura.proc.attributesMask==12&&aura.proc.sourceEffectMask==1&&!aura.proc.disableEffectsMask&&
            aura.proc.triggerSpellFamilyFlags==aura.chargedCostMask&&validLocalProc(d)&&validLocalProc(aura);
    };
    for(unsigned profile=0;profile<6;++profile)assert(accepted(profile));
    struct Mutation {bool child;uint32_t column;};
    const Mutation cases[]={
        {false,72},{false,95},{false,86},{false,116},{false,7},{false,34},{false,35},
        {false,36},{false,42},{false,68},{false,77},{false,98},{false,119},{false,204},
        {true,72},{true,95},{true,80},{true,34},{true,35},{true,36},{true,40},{true,86},
        {true,116},{true,42},{true,98},{true,119},{true,12},{true,122},{true,123},{true,124},{true,226}};
    unsigned rejected=0;
    for(unsigned profile=0;profile<6;++profile)for(const auto& mutation:cases) {
        auto edited=bytes["Spell"];
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,mutation.child?(profile==5?16870:12536):parents[profile]);
        const size_t offset=20+size_t(row)*t.spells->getRecordSize()+mutation.column*4;
        edited[offset]^=1; // Always change the existing cell, including zero fields.
        assert(tables["Spell"].load(edited));
        if(accepted(profile)){std::cerr<<"Unexpected acceptance parent="<<parents[profile]<<" child="<<mutation.child<<" column="<<mutation.column<<"\n";return 1;}
        ++rejected;assert(tables["Spell"].load(bytes["Spell"]));
    }
    struct TableMutation {const char* name;uint32_t id,column;};
    for(const auto& m:std::initializer_list<TableMutation>{{"SpellRange",1,1},{"SpellRange",1,4},
        {"SpellRange",6,1},{"SpellRange",6,3},{"SpellCastTimes",1,3},{"SpellDuration",8,1},{"SpellDuration",8,3}}) {
        auto edited=bytes[m.name];int32_t row=-1;
        for(uint32_t i=0;i<tables[m.name].getRecordCount();++i)if(tables[m.name].getUInt32(i,0)==m.id){row=i;break;}
        assert(row>=0);const size_t offset=20+size_t(row)*tables[m.name].getRecordSize()+m.column*4;
        edited[offset]^=1;assert(tables[m.name].load(edited));
        for(unsigned profile=0;profile<6;++profile)assert(!accepted(profile));
        assert(tables[m.name].load(bytes[m.name]));++rejected;
    }
    // The script classifies source effects independently of decoder admission.
    auto eligible=[&](uint32_t id){LocalSpellDefinition d;const auto row=detail::ClientSpellTables::lookup(t.spellIndex,id);
        assert(row>=0);decodeClientOmenEventMetadata(t,uint32_t(row),d);return d.omenProcEligible;};
    assert(eligible(5176)); // Wrath direct damage.
    assert(eligible(5185)); // Healing Touch direct heal.
    assert(eligible(774));  // Rejuvenation initial application, not its periodic ticks.
    assert(eligible(1126)); // Mark of the Wild initial application.
    assert(!eligible(768)); // Cat Form.
    assert(!eligible(16864)); // The passive cannot proc itself.
    assert(!eligible(22568)); // Ferocious Bite is not an on-next-swing special.
    for(unsigned profile=0;profile<6;++profile)assert(accepted(profile));
    std::cout<<"PASS: six real Clearcasting parent closures, two one-charge cost children; "<<rejected
             <<" source/table mutation rejections; Omen damage/heal/aura/form/passive/special classification\n";
}
