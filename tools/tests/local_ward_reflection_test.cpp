#include "game/local_spell_import.hpp"
#include "game/local_ward_import.hpp"
#include "game/local_ward_reflection.hpp"
#include "game/local_stat_auras.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace wowee;
using namespace wowee::game;
// Isolated rule test; final authority producer has its own integration suite.
const LocalSpellDefinition* LocalWorldContent::spell(uint32_t id) const {
    for(const auto& d:spells)if(d.id==id)return &d;return nullptr;
}
const LocalItemDefinition* LocalWorldContent::item(uint32_t id) const {
    for(const auto& d:items)if(d.id==id)return &d;return nullptr;
}
namespace {
struct SourceTable {
    const pipeline::DBCFile* file=nullptr;int64_t patchRow=-1;uint32_t patchColumn=0,patchValue=0;
    uint32_t getFieldCount()const{return file->getFieldCount();}
    uint32_t getUInt32(uint32_t row,uint32_t col)const{return row==patchRow&&col==patchColumn?patchValue:file->getUInt32(row,col);}
    int32_t getInt32(uint32_t row,uint32_t col)const{return int32_t(getUInt32(row,col));}
};
struct SourceTables {
    SourceTable *spells,*casts,*ranges,*durations;
    std::vector<std::pair<uint32_t,uint32_t>> spellIndex,castIndex,rangeIndex,durationIndex;
    static int32_t lookup(const std::vector<std::pair<uint32_t,uint32_t>>& index,uint32_t id) {
        for(const auto& p:index)if(p.first==id)return int32_t(p.second);return -1;
    }
};
}
int main(int argc,char** argv) {
    assert(argc==2);
    std::map<std::string,pipeline::DBCFile> tables;
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration"}) {
        std::ifstream f(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f),{}};assert(tables[name].load(bytes));
    }
    detail::ClientSpellTables t;t.spells=&tables["Spell"];t.casts=&tables["SpellCastTimes"];
    t.ranges=&tables["SpellRange"];t.durations=&tables["SpellDuration"];
    detail::ClientSpellTables::buildIndex(t.spells,t.spellIndex);detail::ClientSpellTables::buildIndex(t.casts,t.castIndex);
    detail::ClientSpellTables::buildIndex(t.ranges,t.rangeIndex);detail::ClientSpellTables::buildIndex(t.durations,t.durationIndex);
    SourceTable spell{t.spells},cast{t.casts},range{t.ranges},duration{t.durations};
    SourceTables s{&spell,&cast,&range,&duration};
    const auto index=[](const auto* file,auto& out){for(uint32_t r=0;r<file->getRecordCount();++r)out.emplace_back(file->getUInt32(r,0),r);};
    index(t.spells,s.spellIndex);index(t.casts,s.castIndex);index(t.ranges,s.rangeIndex);index(t.durations,s.durationIndex);
    constexpr uint32_t ids[]={543,8457,8458,10223,10225,27128,43010,6143,8461,8462,10177,28609,32796,43012,11094,13043};
    unsigned mutations=0;LocalWorldContent content;
    for(const auto id:ids) {
        assert(localWardRecordMatches(s,id));
        const auto row=SourceTables::lookup(s.spellIndex,id);
        for(uint32_t col=0;col<234;++col) {
            if(col>=131&&col<=203)continue;
            spell.patchRow=row;spell.patchColumn=col;spell.patchValue=t.spells->getUInt32(row,col)^1u;
            assert(!localWardRecordMatches(s,id));spell.patchRow=-1;++mutations;
        }
        if(id==11094||id==13043) {
            spell.patchRow=row;spell.patchColumn=133;spell.patchValue=17;
            assert(!localWardRecordMatches(s,id));spell.patchRow=-1;++mutations;
        }
        LocalSpellDefinition d;d.id=id;d.clientSpell=true;d.allowableClasses=128;
        if(localWardIdProfile(id)) {
            assert(detail::decodeClientSpell(t,uint32_t(row),d));
            assert(d.wardProfile==localWardIdProfile(id)&&d.buffAbsorb&&d.durationMs==30000&&
                d.buffSelfOnly&&d.absorbSchoolMask==(d.wardProfile==1?4u:16u)&&d.categoryCooldownMs==30000&&
                d.cooldownCategory==56&&d.globalCooldownMs==1500&&d.proc.effect==LocalProcEffect::None);
        } else {
            d.talentId=24;d.talentTab=41;d.talentRow=3;d.talentRank=id==11094?1:2;
            assert(decodeClientMoltenShields(t,uint32_t(row),d));
            assert(d.passiveCastModifiers[0].amount==15*d.talentRank&&d.moltenShieldsChancePct==50*d.talentRank);
        }
        content.spells.push_back(d);
    }
    for(auto* table:{&cast,&range,&duration}) {
        const auto id=table==&duration?9u:1u;
        const auto& idx=table==&cast?s.castIndex:table==&range?s.rangeIndex:s.durationIndex;
        const auto row=SourceTables::lookup(idx,id);const auto columns=table==&range?5u:3u;
        for(uint32_t col=1;col<=columns;++col) {
            table->patchRow=row;table->patchColumn=col;table->patchValue=table->file->getUInt32(row,col)^1u;
            for(unsigned k=0;k<(table==&duration?14u:16u);++k)assert(!localWardRecordMatches(s,ids[k]));
            table->patchRow=-1;++mutations;
        }
    }
    LocalRealmPlayer player;player.guid=1;player.classId=8;player.level=80;player.health=player.maxHealth=1000;
    const auto fire=*content.spell(43010),frost=*content.spell(43012),low=*content.spell(543);
    assert(localWardReflectChanceBasisPoints(player,content,fire)==0);
    player.talents={{24,1}};assert(localWardReflectChanceBasisPoints(player,content,fire)==1500);
    assert(localWardReflectChanceBasisPoints(player,content,frost)==1500);
    player.talents={{24,2}};assert(localWardReflectChanceBasisPoints(player,content,fire)==3000);
    auto unrelated=fire;unrelated.spellFamilyFlags={512,0,0};assert(localWardReflectChanceBasisPoints(player,content,unrelated)==0);
    LocalItemDefinition item;item.id=2312;item.inventoryType=10;content.items.push_back(item);
    const auto slot=localEquipmentIndex(LocalEquipmentSlot::Hands);player.equipment[slot]=2312;
    assert(localWardEquipmentSpellPower(player,content)==0);
    player.inventory.push_back({2312,1});assert(localWardEquipmentSpellPower(player,content)==2);
    assert(localWardAbsorbAmount(player,content,fire)==1951);
    assert(localWardAbsorbAmount(player,content,low)==165);
    player.level=20;assert(localWardAbsorbAmount(player,content,low)==166);player.level=80;
    player.equipment[slot]=0;assert(localWardEquipmentSpellPower(player,content)==0);
    LocalStatAura aura;aura.spellId=fire.id;aura.remainingMs=30000;aura.casterGuid=player.guid;
    aura.absorbRemaining=1950;aura.reflectChanceBasisPointsSnapshot=3000;player.statAuras={aura};
    LocalSpellDefinition incoming;incoming.id=5401;incoming.clientSpell=true;incoming.sourceDamageClass=1;incoming.schoolMask=4;incoming.damage=100;
    assert(localWardIncomingReflectChanceBasisPoints(player,content,incoming,2)==3000);
    player.talents.clear();assert(localWardIncomingReflectChanceBasisPoints(player,content,incoming,2)==3000); // snapshot
    assert(localWardIncomingReflectChanceBasisPoints(player,content,incoming,1)==0);
    assert(localWardIncomingReflectChanceBasisPoints(player,content,incoming,2,true)==0);
    incoming.schoolMask=16;assert(localWardIncomingReflectChanceBasisPoints(player,content,incoming,2)==0);incoming.schoolMask=4;
    incoming.sourceCantReflect=true;assert(localWardIncomingReflectChanceBasisPoints(player,content,incoming,2)==0);incoming.sourceCantReflect=false;
    incoming.sourceAlwaysHit=true;assert(localWardIncomingReflectChanceBasisPoints(player,content,incoming,2)==3000);incoming.sourceAlwaysHit=false;
    incoming.sourceDamageClass=2;assert(localWardIncomingReflectChanceBasisPoints(player,content,incoming,2)==0);incoming.sourceDamageClass=1;
    assert(!localSpellCanReflect(incoming,true));
    player.statAuras[0].instanceId=1;assert(localWardIncomingReflectChanceBasisPoints(player,content,incoming,2)==0);player.statAuras[0].instanceId=0;
    player.statAuras[0].remainingMs=0;assert(localWardIncomingReflectChanceBasisPoints(player,content,incoming,2)==0);player.statAuras[0]=aura;
    player.statAuras[0].absorbRemaining=2500;assert(localAbsorbDamage(player,content,2200,4)==0);
    assert(player.statAuras[0].absorbRemaining==300&&player.statAuras[0].reflectChanceBasisPointsSnapshot==3000);
    assert(localAbsorbDamage(player,content,400,4)==100&&player.statAuras.empty());
    LocalCombatEvent event;event.target=player.guid;event.kind=LocalCombatEventKind::NpcMelee;
    assert(localMoltenArmorEventChancePct(player,content,event)==100);
    event.spell=5401;event.kind=LocalCombatEventKind::SpellDamage;
    assert(localMoltenArmorEventChancePct(player,content,event)==0);
    player.talents={{24,1}};assert(localMoltenArmorEventChancePct(player,content,event)==50);
    player.talents={{24,2}};assert(localMoltenArmorEventChancePct(player,content,event)==100);
    event.target=2;assert(localMoltenArmorEventChancePct(player,content,event)==0);
    std::cout<<"PASS: 14 complete Ward ranks, two Molten Shields ranks, "<<mutations
             <<" functional source mutations rejected; reflection source/school/lifecycle/snapshots; scaled absorb and item ownership; Molten Armor script gating\n";
}
