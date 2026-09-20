#include "game/local_spell_import.hpp"
#include "game/local_stormstrike_import.hpp"
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
    struct Profile {uint32_t id,talent,rank;std::array<uint32_t,2> closure;};
    constexpr Profile profiles[]={
        {51883,2083,1,{}},{51884,2083,2,{}},{51885,2083,3,{}},
        {29082,1643,1,{}},{29084,1643,2,{}},{29086,1643,3,{}},
        {16268,616,1,{18848,36591}},{43338,617,1,{}},
        {17364,901,1,{32175,32176}},{30798,1690,1,{674,0}},
        {30816,1692,1,{}},{30818,1692,2,{}},{30819,1692,3,{}},
        {51521,2054,1,{63375,0}},{51522,2054,2,{63375,0}}};
    const auto decoded=[&](const Profile& p,LocalSpellDefinition& d,std::array<LocalSpellDefinition,3>& children) {
        d={};d.id=p.id;d.clientSpell=true;d.allowableClasses=64;
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,p.id);assert(row>=0);
        detail::decodeClientSpell(t,uint32_t(row),d);d.talentId=p.talent;d.talentRank=p.rank;
        return decodeClientStormstrike(t,uint32_t(row),d,&children);
    };
    const auto accepted=[&](const Profile& p) {
        LocalSpellDefinition d;std::array<LocalSpellDefinition,3> children;
        if(!decoded(p,d,children)||!d.unsupportedReason.empty()||!validLocalProc(d))return false;
        for(const auto& ch:children)if(ch.id&&!validLocalProc(ch))return false;
        if(p.talent==2083)return d.passiveIntellectAttackPowerPct==(p.rank==3?100:p.rank*33);
        if(p.talent==1643)return d.passivePhysicalDamagePct==p.rank*3+1&&d.requiredItemSubclasses==42035;
        if(p.talent==616)return d.passiveCanParry&&d.passiveSchoolThreatPercent==-30&&d.passiveSchoolThreatMask==127;
        if(p.talent==617)return d.passiveCastModifiers[0].active&&d.passiveCastModifiers[0].amount==-45;
        if(p.talent==1690)return d.passiveCanDualWield;
        if(p.talent==1692)return d.passiveDualWieldHitPct==p.rank*2&&d.requiresOffHand;
        if(p.talent==901)return d.stormstrikeProfile==1&&!d.passive&&!d.damage&&d.manaPercent==8&&
            d.durationMs==12000&&children[0].id==32175&&children[1].id==32176;
        return d.stormstrikeProfile==4&&d.stormstrikeManaChancePct==50*p.rank&&
            d.proc.amount==20&&d.proc.schoolMask==8&&d.proc.spellTypeMask==7&&
            d.proc.hitMask==LocalProcSupportedHits&&children[2].id==63375;
    };
    unsigned rejected=0,childRejected=0;
    for(const auto& profile:profiles) {
        if(!accepted(profile)){std::cerr<<"Initial profile failure "<<profile.id<<"\n";return 1;}
        for(const auto id:{profile.id,profile.closure[0],profile.closure[1]})if(id) {
            const auto row=detail::ClientSpellTables::lookup(t.spellIndex,id);
            for(uint32_t col=0;col<234;++col) {
                if(col>=131&&col<=203)continue;
                auto edited=bytes["Spell"];edited[20+size_t(row)*t.spells->getRecordSize()+col*4]^=1;
                assert(tables["Spell"].load(edited));assert(!accepted(profile));
                if(id==profile.id)++rejected;else ++childRejected;
                assert(tables["Spell"].load(bytes["Spell"]));
            }
        }
        LocalSpellDefinition d;std::array<LocalSpellDefinition,3> children;assert(decoded(profile,d,children));
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,profile.id);
        d.allowableClasses=1;assert(!decodeClientStormstrike(t,row,d));
        d.allowableClasses=64;d.talentRank=0;assert(!decodeClientStormstrike(t,row,d));
        d.talentRank=profile.rank;d.talentId=0;assert(!decodeClientStormstrike(t,row,d));
    }
    unsigned tableRejected=0;
    for(const auto name:{"SpellRange","SpellCastTimes","SpellDuration"}) {
        const uint32_t id=std::string(name)=="SpellDuration"?29:1;
        const auto row=std::string(name)=="SpellDuration"?detail::ClientSpellTables::lookup(t.durationIndex,id):
            std::string(name)=="SpellRange"?detail::ClientSpellTables::lookup(t.rangeIndex,id):detail::ClientSpellTables::lookup(t.castIndex,id);
        for(uint32_t col=1;col<=(std::string(name)=="SpellRange"?5u:3u);++col) {
            auto edited=bytes[name];edited[20+size_t(row)*tables[name].getRecordSize()+col*4]^=1;
            assert(tables[name].load(edited));
            for(const auto& profile:profiles)if(std::string(name)!="SpellDuration"||profile.id==17364) {
                if(std::string(name)=="SpellRange"&&profile.id==17364)continue;
                assert(!accepted(profile));++tableRejected;
            }
            assert(tables[name].load(bytes[name]));
        }
    }
    // Real import + normal learn transaction prove the honest 28-point ceiling.
    const auto get=[&](const char* n){return &tables[n];};
    auto imported=importClientStarterSpells(get("Spell"),get("SpellRange"),get("SpellCastTimes"),
        get("SpellDuration"),get("SpellIcon"),get("SkillLineAbility"),get("SkillLine"),get("Talent"),get("SpellRuneCost"),get("SpellRadius"));
    detail::importClientTalents(imported,get("Talent"),get("TalentTab"),get("Spell"),get("SpellRange"),get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SpellRuneCost"),get("SpellRadius"));
    LocalWorldContent c;c.spells=std::move(imported.spells);
    for(const auto& profile:profiles){const auto* d=c.spell(profile.id);assert(d&&d->unsupportedReason.empty());}
    LocalRealmPlayer p;p.guid=1;p.classId=7;p.level=80;p.health=100;std::string error;
    for(const auto [talent,ranks]:std::initializer_list<std::pair<uint32_t,uint32_t>>{{614,5},{613,5},{607,3},{605,2},{602,5},{2083,3},{616,1},{617,1},{1643,3}})
        for(uint32_t rank=0;rank<ranks;++rank)assert(learnLocalTalent(p,c,talent,rank,error));
    assert(localTalentPointsSpent(p)==28);
    for(const auto talent:{901u,1690u,1692u,2054u})assert(!learnLocalTalent(p,c,talent,0,error));
    for(const auto id:{16254u,16271u,16272u,16252u,16306u,16307u,16308u,16309u}) {
        const auto* d=c.spell(id);assert(!d||!d->unsupportedReason.empty());
    }
    std::cout<<"PASS: 15 complete Stormstrike/Enhancement profiles; "<<rejected<<" parent and "<<childRejected
             <<" child gameplay-column mutation rejections; "<<tableRejected<<" auxiliary-table rejections; normal path28 remains gated before Stormstrike30, no partial Anticipation/Toughness admission\n";
}
