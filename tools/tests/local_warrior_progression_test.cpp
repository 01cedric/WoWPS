#include "game/local_spell_import.hpp"
#include "game/local_warrior_progression_import.hpp"
#include "game/local_talents.hpp"
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
int main(int argc,char** argv) {
    assert(argc==2);
    std::map<std::string,pipeline::DBCFile> tables;
    std::map<std::string,std::vector<uint8_t>> bytes;
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon",
                         "SpellRadius","SpellRuneCost","SkillLine","SkillLineAbility","Talent","TalentTab"}) {
        std::ifstream f(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        bytes[name]={std::istreambuf_iterator<char>(f),{}};assert(tables[name].load(bytes[name]));
    }
    detail::ClientSpellTables t;t.spells=&tables["Spell"];t.ranges=&tables["SpellRange"];
    t.casts=&tables["SpellCastTimes"];t.durations=&tables["SpellDuration"];
    detail::ClientSpellTables::buildIndex(t.spells,t.spellIndex);
    detail::ClientSpellTables::buildIndex(t.ranges,t.rangeIndex);
    detail::ClientSpellTables::buildIndex(t.casts,t.castIndex);
    detail::ClientSpellTables::buildIndex(t.durations,t.durationIndex);
    struct Profile {uint32_t id,talent,rank,child;};
    constexpr Profile profiles[]={
        {61216,2250,1,0},{61221,2250,2,0},{61222,2250,3,0},
        {16487,661,1,16488},{16489,661,2,16490},{16492,661,3,16491},
        {23584,1581,1,0},{23585,1581,2,0},{23586,1581,3,0},{23587,1581,4,0},{23588,1581,5,0},
        {29590,1657,1,0},{29591,1657,2,0},{29592,1657,3,0},{12292,165,1,0}};
    auto accepted=[&](const Profile& p) {
        LocalSpellDefinition d,aura;d.id=p.id;d.clientSpell=true;d.allowableClasses=1;
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,p.id);assert(row>=0);
        detail::decodeClientSpell(t,uint32_t(row),d);d.talentId=p.talent;d.talentRank=p.rank;
        if(!decodeClientWarriorProgressionTalent(t,uint32_t(row),d,&aura)||!d.unsupportedReason.empty())return false;
        if(p.talent==2250)return d.passive&&d.passiveArmorAttackPowerDivisor==108/p.rank;
        if(p.talent==1581)return d.passive&&d.passiveOffhandDamagePct==p.rank*5;
        if(p.talent==1657)return d.passive&&d.passiveWeaponHitPct==p.rank&&d.requiredItemClass==2&&d.requiredItemSubclasses==173555;
        if(p.talent==165)return !d.passive&&d.physicalDamageDonePct==20&&d.damageTakenPct==5&&d.durationMs==30000&&d.mana==10;
        return d.passive&&d.proc.spellId==p.child&&d.proc.flags==664232&&d.proc.hitMask==LocalProcHitCritical&&
            d.proc.spellTypeMask==1&&!d.proc.allowTriggered&&!d.proc.cooldownMs&&aura.id==p.child&&
            aura.triggeredOnly&&aura.procParentTalentId==661&&aura.periodicHealMaxHealthPct==1&&
            aura.periodicIntervalMs==3000/p.rank&&aura.durationMs==6000&&aura.proc.effect==LocalProcEffect::None;
    };
    unsigned rejected=0,childRejected=0;
    for(const auto& profile:profiles) {
        assert(accepted(profile));
        for(const auto id:{profile.id,profile.child})if(id) {
            const auto row=detail::ClientSpellTables::lookup(t.spellIndex,id);
            for(uint32_t col=0;col<234;++col) {
                if(col>=131&&col<=203)continue;
                auto edited=bytes["Spell"];
                edited[20+size_t(row)*t.spells->getRecordSize()+col*4]^=1;
                assert(tables["Spell"].load(edited));assert(!accepted(profile));
                if(id==profile.child)++childRejected;else ++rejected;
                assert(tables["Spell"].load(bytes["Spell"]));
            }
        }
        // Rank and class identity are additional to matching the spell bytes.
        auto row=detail::ClientSpellTables::lookup(t.spellIndex,profile.id);
        LocalSpellDefinition d;d.id=profile.id;d.talentId=profile.talent;d.talentRank=profile.rank;d.allowableClasses=2;
        assert(!decodeClientWarriorProgressionTalent(t,uint32_t(row),d));
        d.allowableClasses=1;d.talentRank=6;assert(!decodeClientWarriorProgressionTalent(t,uint32_t(row),d));
    }
    unsigned tableRejected=0;
    for(const auto& [name,column]:std::initializer_list<std::pair<const char*,uint32_t>>{
            {"SpellRange",5},{"SpellCastTimes",3}}) {
        auto edited=bytes[name];const auto row=std::string(name)=="SpellRange"?
            detail::ClientSpellTables::lookup(t.rangeIndex,1):detail::ClientSpellTables::lookup(t.castIndex,1);
        edited[20+size_t(row)*tables[name].getRecordSize()+column*4]^=1;
        assert(tables[name].load(edited));
        for(const auto& profile:profiles){assert(!accepted(profile));++tableRejected;}
        assert(tables[name].load(bytes[name]));
    }
    for(const uint32_t durationId:{9,21,32}) {
        auto edited=bytes["SpellDuration"];
        const auto row=detail::ClientSpellTables::lookup(t.durationIndex,durationId);
        edited[20+size_t(row)*tables["SpellDuration"].getRecordSize()+3*4]^=1;
        assert(tables["SpellDuration"].load(edited));
        for(const auto& profile:profiles)if((durationId==9&&profile.talent==165)||
            (durationId==21&&(profile.talent==1581||profile.talent==1657))||(durationId==32&&profile.talent==661)) {
            assert(!accepted(profile));++tableRejected;
        }
        assert(tables["SpellDuration"].load(bytes["SpellDuration"]));
    }
    const auto get=[&](const char* n){return &tables[n];};
    auto imported=importClientStarterSpells(get("Spell"),get("SpellRange"),get("SpellCastTimes"),
        get("SpellDuration"),get("SpellIcon"),get("SkillLineAbility"),get("SkillLine"),get("Talent"),
        get("SpellRuneCost"),get("SpellRadius"));
    detail::importClientTalents(imported,get("Talent"),get("TalentTab"),get("Spell"),get("SpellRange"),
        get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SpellRuneCost"),get("SpellRadius"));
    LocalWorldContent c;c.spells=std::move(imported.spells);
    for(const auto& profile:profiles) {
        const auto* d=c.spell(profile.id);assert(d&&d->unsupportedReason.empty());
        if(profile.child) {
            const auto* a=c.spell(profile.child);assert(a&&a->triggeredOnly&&a->unsupportedReason.empty());
        }
    }
    LocalRealmPlayer p;p.classId=1;p.level=40;p.health=100;p.guid=1;std::string error;
    assert(!learnLocalTalent(p,c,156,0,error));assert(!learnLocalTalent(p,c,167,0,error));
    assert(!learnLocalTalent(p,c,157,1,error));
    for(unsigned rank=0;rank<5;++rank)assert(learnLocalTalent(p,c,157,rank,error));
    for(unsigned rank=0;rank<3;++rank)assert(learnLocalTalent(p,c,2250,rank,error));
    for(unsigned rank=0;rank<5;++rank)assert(learnLocalTalent(p,c,159,rank,error));
    assert(localTalentPointsSpent(p)==13);assert(!learnLocalTalent(p,c,1581,0,error));
    for(unsigned rank=0;rank<3;++rank)assert(learnLocalTalent(p,c,661,rank,error));
    for(unsigned rank=0;rank<5;++rank)assert(learnLocalTalent(p,c,1581,rank,error));
    for(unsigned rank=0;rank<3;++rank)assert(learnLocalTalent(p,c,1657,rank,error));
    assert(localTalentPointsSpent(p)==24);assert(!learnLocalTalent(p,c,156,0,error));
    assert(learnLocalTalent(p,c,165,0,error));
    const auto beforeFlurry=p;
    for(unsigned rank=0;rank<5;++rank)assert(learnLocalTalent(p,c,156,rank,error));
    assert(learnLocalTalent(p,c,167,0,error));
    assert(localTalentPointsSpent(p)==31&&localTalentPointsAvailable(p)==0);
    auto wrongClass=beforeFlurry;wrongClass.classId=7;assert(!learnLocalTalent(wrongClass,c,156,0,error));
    // Preserve thirty Fury points but remove Death Wish's direct prerequisite.
    // Learning must reject Bloodthirst even with sufficient unrelated points.
    auto prerequisite=p;
    std::erase_if(prerequisite.talents,[](const auto& pair){return pair.first==165||pair.first==167;});
    prerequisite.talents.push_back({158,1});
    assert(localTalentPointsSpent(prerequisite)==30);
    assert(!learnLocalTalent(prerequisite,c,167,0,error));assert(error=="Required talent rank is missing");
    std::cout<<"PASS: 15 full Warrior source profiles + 3 internal regeneration children; "<<rejected<<" parent and "
        <<childRejected<<" child gameplay-column mutation rejections + "<<tableRejected<<" related-table rejections; "
        <<"normal31-point Fury path unlocks Flurry5 + Bloodthirst at level40; class/rank/tier/direct-prerequisite guards\n";
}
