#include "game/local_spell_import.hpp"
#include "game/local_ghost_wolf_import.hpp"
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
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellShapeshiftForm"}) {
        std::ifstream f(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        bytes[name]={std::istreambuf_iterator<char>(f),{}};assert(tables[name].load(bytes[name]));
    }
    detail::ClientSpellTables t;t.spells=&tables["Spell"];t.ranges=&tables["SpellRange"];
    t.casts=&tables["SpellCastTimes"];t.durations=&tables["SpellDuration"];
    detail::ClientSpellTables::buildIndex(t.spells,t.spellIndex);
    detail::ClientSpellTables::buildIndex(t.ranges,t.rangeIndex);
    detail::ClientSpellTables::buildIndex(t.casts,t.castIndex);
    detail::ClientSpellTables::buildIndex(t.durations,t.durationIndex);
    const auto parent=detail::ClientSpellTables::lookup(t.spellIndex,2645);
    const auto child=detail::ClientSpellTables::lookup(t.spellIndex,67116);
    assert(parent>=0&&child>=0);
    auto accepted=[&](){LocalSpellDefinition d;d.id=2645;return detail::decodeClientGhostWolf(t,parent,d)&&d.unsupportedReason.empty()&&d.formId==16;};
    assert(accepted());
    LocalSpellDefinition imported;imported.id=2645;
    assert(detail::decodeClientSpell(t,parent,imported));
    assert(imported.formId==16&&imported.castTimeMs==2000&&imported.manaPercent==6&&imported.baseLevel==16);
    assert(imported.globalCooldownMs==1500&&imported.buffSelfOnly&&!imported.periodicHeal&&!imported.heal);
    LocalSpellDefinition passive;passive.id=67116;assert(!detail::decodeClientSpell(t,child,passive));
    uint32_t mutations=0;
    for(const auto row:{parent,child})for(uint32_t column=1;column<234;++column) {
        if(column>=131&&column<=203)continue;
        auto edited=bytes["Spell"];const size_t offset=20+size_t(row)*t.spells->getRecordSize()+column*4;
        edited[offset]^=1;
        assert(tables["Spell"].load(edited));assert(!accepted());
        assert(tables["Spell"].load(bytes["Spell"]));assert(accepted());++mutations;
    }
    struct Mutation {const char* name;uint32_t id,column;};
    for(const auto& m:std::initializer_list<Mutation>{{"SpellRange",1,1},{"SpellRange",1,4},
        {"SpellCastTimes",5,1},{"SpellCastTimes",5,3},{"SpellCastTimes",1,1},
        {"SpellDuration",21,1},{"SpellDuration",21,3}}) {
        auto edited=bytes[m.name];int32_t row=-1;
        for(uint32_t k=0;k<tables[m.name].getRecordCount();++k)if(tables[m.name].getUInt32(k,0)==m.id)row=k;
        assert(row>=0);edited[20+size_t(row)*tables[m.name].getRecordSize()+m.column*4]^=1;
        assert(tables[m.name].load(edited));assert(!accepted());
        assert(tables[m.name].load(bytes[m.name]));assert(accepted());++mutations;
    }
    // The embedded form profile is pinned to the original table, without
    // requiring another persistent console DBC allocation for one form.
    auto& forms=tables["SpellShapeshiftForm"];bool found=false;
    for(uint32_t row=0;row<forms.getRecordCount();++row)if(forms.getUInt32(row,0)==16){
        assert(forms.getUInt32(row,1)==0&&forms.getUInt32(row,19)==216&&forms.getUInt32(row,20)==1);
        assert(forms.getUInt32(row,22)==0&&forms.getUInt32(row,23)==4613&&forms.getUInt32(row,24)==0);
        for(uint32_t column=27;column<35;++column)assert(!forms.getUInt32(row,column));found=true;
    }
    assert(found);
    LocalRealmPlayer p;p.guid=1;p.classId=7;p.race=2;p.level=16;p.resourceType=LocalResourceType::Mana;
    p.maxMana=1300;p.mana=811;p.resourceRegenRemainder=217;p.manaRegenDelayMs=4500;
    const auto* wolf=localFormProfile(2645);assert(wolf&&wolf->form==16&&wolf->clazz==7);
    assert(localFormEnvironmentReady(p,imported));
    p.movementState=kLocalMovementIndoors;assert(!localFormEnvironmentReady(p,imported));
    p.movementState=kLocalMovementInLiquid;assert(localFormEnvironmentReady(p,imported));
    p.movementState=0;
    enterLocalForm(p,*wolf);
    assert(validLocalFormState(p)&&p.resourceType==LocalResourceType::Mana&&p.maxMana==1300&&p.mana==811);
    assert(p.resourceRegenRemainder==217&&p.manaRegenDelayMs==4500&&!p.druidMana&&!p.druidManaRemainder);
    assert(localFormDisplay(p)==4613&&localFormRunPercent(p)==140.f);
    assert(localFormRunPercent(p,80)==112.f&&localFormRunPercent(p,50)==100.f&&localFormRunPercent(p,0)==100.f);
    LocalSpellDefinition spell;spell.notShapeshifted=true;assert(!localSpellFormReady(p,spell));
    spell.requiredForms=uint64_t(1)<<15;assert(localSpellFormReady(p,spell));
    spell.excludedForms=spell.requiredForms;assert(!localSpellFormReady(p,spell));
    spell={};spell.requiredForms=1;assert(!localSpellFormReady(p,spell));
    spell={};assert(localSpellFormReady(p,spell));
    p.dead=true;assert(!localActiveForm(p)&&!validLocalFormState(p)&&localFormRunPercent(p)==100.f);p.dead=false;
    leaveLocalForm(p);assert(validLocalFormState(p)&&p.mana==811&&p.maxMana==1300&&p.resourceRegenRemainder==217);
    assert(localFormRunPercent(p,50)==50.f);
    p.classId=1;p.resourceType=LocalResourceType::Rage;enterLocalForm(p,*localFormProfile(2457));
    assert(p.resourceType==LocalResourceType::Rage&&p.maxMana==100&&p.mana==0);
    spell.notShapeshifted=true;assert(localSpellFormReady(p,spell));
    std::cout<<"PASS: Ghost Wolf exact parent/passive closure; "<<mutations<<" source mutations rejected; original form model/flags; mana preservation, casting restrictions, speed floor and removal\n";
}
