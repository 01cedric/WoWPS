#include "game/local_spell_amount.hpp"
#include "game/local_spell_import.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace wowee;
using namespace wowee::game;
const LocalSpellDefinition* LocalWorldContent::spell(uint32_t id) const {
    for(const auto& d:spells)if(d.id==id)return &d;return nullptr;
}
namespace {
LocalSpellDefinition talent(uint32_t id) {
    LocalSpellDefinition d;d.id=id;d.talentId=id;d.talentRank=1;d.passive=true;
    d.allowableClasses=1024;d.spellFamily=7;return d;
}
void amountRules() {
    LocalWorldContent c;LocalRealmPlayer p;p.classId=11;p.level=80;
    LocalSpellDefinition cast;cast.spellFamily=7;cast.spellFamilyFlags={4096,0,0};
    cast.directEffectSlot=2;cast.periodicEffectSlot=1;
    auto all=talent(1001),slot=talent(1002),damage=talent(1003),periodic=talent(1004);
    all.passiveCastModifiers={{{8,true,true,20,{4096,0,0}},{8,false,true,10,{4096,0,0}},{}}};
    slot.passiveCastModifiers={{{23,true,true,20,{4096,0,0}},{23,true,true,30,{4096,0,0}},{23,false,true,5,{4096,0,0}}}};
    damage.passiveCastModifiers={{{0,true,true,20,{4096,0,0}},{0,true,true,30,{4096,0,0}},{0,false,true,10,{4096,0,0}}}};
    periodic.passiveCastModifiers={{{22,true,true,20,{4096,0,0}},{22,true,true,30,{4096,0,0}},{22,false,true,10,{4096,0,0}}}};
    c.spells={all,slot,damage,periodic};p.talents={{1001,1},{1002,1},{1003,1},{1004,1}};
    // Source: ALL_EFFECTS 100*1.2+10=130; EFFECT3 130*1.5+5=200.
    assert(localSpellEffectAmountAfterTalents(p,c,cast,100,false)==200);
    assert(localTalentCastModifier(p,c,cast,23,true)==50);
    cast.directPerCombo=10;assert(localSpellEffectAmountAfterTalents(p,c,cast,100,false,3)==254);
    cast.directPerCombo=0;
    // Combo/weapon bonuses enter after effect scaling. DAMAGE/DOT percentages
    // multiply, flat is added last: (200+50)*1.2*1.3+10=400.
    assert(localSpellAmountAfterTalents(p,c,cast,250,false)==400);
    assert(localSpellAmountAfterTalents(p,c,cast,250,true)==400);
    // EFFECT3 never changes effect two. ALL_EFFECTS still applies there.
    assert(localSpellEffectAmountAfterTalents(p,c,cast,100,true)==130);
    cast.directEffectSlot=0;assert(localSpellEffectAmountAfterTalents(p,c,cast,100,false)==130);
    cast.directEffectSlot=255;assert(localSpellEffectAmountAfterTalents(p,c,cast,100,false)==100);
    cast.directEffectSlot=2;c.spells[1].spellFamily=3;
    assert(localSpellEffectAmountAfterTalents(p,c,cast,100,false)==130);
    c.spells[1].spellFamily=7;c.spells[1].passiveCastModifiers[0].mask={0,4096,0};
    c.spells[1].passiveCastModifiers[1].mask={0,4096,0};
    c.spells[1].passiveCastModifiers[2].mask={0,4096,0};
    assert(localSpellEffectAmountAfterTalents(p,c,cast,100,false)==130);
    c.spells[1]=slot;c.spells[1].talentPrerequisites[0]=1005;
    assert(localSpellEffectAmountAfterTalents(p,c,cast,100,false)==130);
    c.spells[1]=slot;c.spells[1].unsupportedReason="Incomplete source profile";
    assert(localSpellEffectAmountAfterTalents(p,c,cast,100,false)==130);
    c.spells[1]=slot;p.talents.clear();
    assert(localSpellEffectAmountAfterTalents(p,c,cast,100,false)==100);
    assert(localSpellAmountAfterTalents(p,c,cast,250,true)==250);
    // All three source-specific operation numbers are mapped, with additive
    // percentages in each slot and no cross-slot leak.
    p.talents={{1002,1}};
    for(unsigned index=0;index<3;++index) {
        const uint8_t operation=std::array<uint8_t,3>{3,12,23}[index];
        for(auto& mod:c.spells[1].passiveCastModifiers)mod.operation=operation;
        cast.directEffectSlot=uint8_t(index);
        assert(localSpellEffectAmountAfterTalents(p,c,cast,100,false)==155);
        cast.directEffectSlot=uint8_t((index+1)%3);
        assert(localSpellEffectAmountAfterTalents(p,c,cast,100,false)==100);
    }
    assert(localSpellAmountAfterTalents(p,c,cast,2000000,false)==1000000);
}
}
int main(int argc,char** argv) {
    assert(argc==2);amountRules();
    std::map<std::string,pipeline::DBCFile> tables;
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon",
                         "SpellRadius","SpellRuneCost","SkillLine","SkillLineAbility"}) {
        std::ifstream f(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f),{}};assert(tables[name].load(bytes));
    }
    const auto get=[&](const char* n){return &tables.at(n);};
    auto imported=importClientStarterSpells(get("Spell"),get("SpellRange"),get("SpellCastTimes"),
        get("SpellDuration"),get("SpellIcon"),get("SkillLineAbility"),get("SkillLine"),nullptr,
        get("SpellRuneCost"),get("SpellRadius"));
    unsigned checked=0,aggregates=0;
    for(const auto& d:imported.spells) {
        if(!d.unsupportedReason.empty()||d.triggeredOnly||d.passive)continue;
        uint32_t row=0;while(row<get("Spell")->getRecordCount()&&get("Spell")->getUInt32(row,0)!=d.id)++row;
        assert(row<get("Spell")->getRecordCount());
        unsigned count=0;uint8_t direct=255,periodic=255;
        for(unsigned e=0;e<3;++e) {
            const auto type=get("Spell")->getUInt32(row,71+e),aura=get("Spell")->getUInt32(row,95+e);
            // Reviewed Auto Shot and Shoot use source weapon effects 58 and
            // 17, respectively. Both retain their actual first effect slot.
            const bool rangedWeapon=(d.id==75&&d.rangedAutoProfile==1&&type==58)||
                                    (d.id==5019&&d.rangedAutoProfile==2&&type==17);
            if(type==2||type==10||rangedWeapon||(d.comboProfile&&(type==58||type==121))) {
                direct=++count==1?uint8_t(e):255;
            }
            if(type==6&&(aura==3||aura==8))periodic=uint8_t(e);
        }
        if(d.damage||d.heal||d.weaponDamage){assert(d.directEffectSlot==direct);++checked;if(count>1)++aggregates;}
        if(d.periodicDamage||d.periodicHeal){assert(d.periodicEffectSlot==periodic);++checked;}
        if(d.id==1822)assert(d.directEffectSlot==0&&d.periodicEffectSlot==1);
        if(d.id==8921)assert(d.directEffectSlot==1&&d.periodicEffectSlot==0);
    }
    assert(checked>100);
    std::cout<<"PASS effect modifiers: additive slot percentages, multiplicative damage/DOT percentages, flat ordering, pre-combo stage, slots/family/masks/current talents; "
             <<checked<<" ordinary amount source slots checked, "<<aggregates<<" aggregate direct profiles explicitly unidentified\n";
}
