#include "game/local_melee.hpp"
#include "game/local_class_pools.hpp"
#include "game/local_regeneration_rates.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_talents.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <source_location>
using namespace wowee;
using namespace wowee::game;
static void close(double actual,double expected,std::source_location location=std::source_location::current()){
    if(!std::isfinite(actual)||std::abs(actual-expected)>.0001){
        std::cerr<<"line "<<location.line()<<": actual="<<actual<<" expected="<<expected<<'\n';std::abort();
    }
}
int main(int argc,char** argv){
    assert(argc==2);
    std::map<std::string,pipeline::DBCFile> tables;
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon","SkillLineAbility","SkillLine","Talent","TalentTab","SpellRuneCost","SpellRadius"}){
        std::ifstream file(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> data{std::istreambuf_iterator<char>(file),{}};assert(tables[name].load(data));
    }
    auto table=[&](const char* name){return &tables.at(name);};
    auto imported=importClientStarterSpells(table("Spell"),table("SpellRange"),table("SpellCastTimes"),table("SpellDuration"),
        table("SpellIcon"),table("SkillLineAbility"),table("SkillLine"),table("Talent"),table("SpellRuneCost"),table("SpellRadius"));
    detail::importClientTalents(imported,table("Talent"),table("TalentTab"),table("Spell"),table("SpellRange"),
        table("SpellCastTimes"),table("SpellDuration"),table("SpellIcon"),table("SpellRuneCost"),table("SpellRadius"));
    LocalWorldContent c;c.spells=std::move(imported.spells);
    std::sort(c.spells.begin(),c.spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    LocalRealmPlayer p;p.guid=10;p.classId=1;p.race=1;p.level=40;p.health=p.maxHealth=100;
    std::string error;
    auto learn=[&](uint32_t id,unsigned ranks){for(unsigned rank=0;rank<ranks;++rank){
        if(!learnLocalTalent(p,c,id,rank,error)){std::cerr<<"learn "<<id<<" rank "<<rank<<": "<<error<<'\n';std::abort();}
    }};
    auto item=[&](uint32_t id){const auto* source=localMeleeItem(id);assert(source);
        LocalItemDefinition d;d.id=id;d.inventoryType=source->inventoryType;c.items.push_back(d);
        std::sort(c.items.begin(),c.items.end(),[](const auto& a,const auto& b){return a.id<b.id;});return source;};
    // This armor descriptor exercises the real inventory/slot ownership path;
    // talent amounts and Mark are imported from the untouched build-12340 DBC.
    LocalItemDefinition chest;chest.id=900001;chest.inventoryType=5;chest.armor=101;c.items.push_back(chest);
    p.equipment[4]=chest.id;p.inventory={{chest.id,1}};
    const auto naked=localMeleeStats(p,c);assert(naked.sourceStats);
    const auto armor=localMeleeArmor(p,c);assert(armor==uint32_t(naked.attributes[1])*2+101);
    const unsigned divisors[]={108,54,36};
    for(unsigned rank=0;rank<3;++rank){
        assert(learnLocalTalent(p,c,2250,rank,error));
        const auto* d=localTalentSpell(c,2250,rank+1);assert(d&&d->passiveArmorAttackPowerDivisor==divisors[rank]);
        close(localMeleeStats(p,c).attackPower,naked.attackPower+armor/divisors[rank]);
        assert(localMeleeArmor(p,c)==armor); // No armor/AP recursion or feedback.
    }
    p.inventory.clear();close(localMeleeStats(p,c).attackPower,naked.attackPower+(armor-101)/36);
    p.inventory={{chest.id,1}};
    const auto* mark=c.spell(1126);assert(mark&&mark->unsupportedReason.empty()&&mark->buffArmor);
    LocalStatAura aura;aura.spellId=mark->id;aura.remainingMs=mark->durationMs;aura.mapId=p.mapId;
    aura.instanceId=p.instanceId;aura.casterGuid=99;aura.stacks=1;
    p.statAuras={aura};close(localMeleeStats(p,c).attackPower,naked.attackPower+(armor+mark->buffArmor)/36);
    p.statAuras[0].buffArmorSnapshot=mark->buffArmor*14/10;
    close(localMeleeStats(p,c).attackPower,naked.attackPower+(armor+p.statAuras[0].buffArmorSnapshot)/36);
    p.statAuras[0].remainingMs=0;close(localMeleeStats(p,c).attackPower,naked.attackPower+armor/36);
    p.statAuras.clear();
    learn(157,5);learn(159,5);learn(661,3); // 16 actual earlier Fury points.
    const auto* main=item(25);const auto* off=item(727);assert(main->itemClass==2&&off->itemClass==2);
    p.equipment[15]=main->id;p.equipment[16]=off->id;p.inventory.push_back({main->id,1});p.inventory.push_back({off->id,1});
    assert(localMeleeStats(p,c).offHand);
    const auto baseMain=localWeaponAmounts(p,c);const auto baseOff=localWeaponAmounts(p,c,true);
    for(unsigned rank=0;rank<5;++rank){assert(learnLocalTalent(p,c,1581,rank,error));
        const float factor=1+float((rank+1)*5)/100;
        close(localWeaponAmounts(p,c).low,baseMain.low);close(localWeaponAmounts(p,c,true).low,baseOff.low*factor);
        close(localWeaponAmounts(p,c,true).high,baseOff.high*factor);
    }
    const auto stats=localMeleeStats(p,c);
    close(localWeaponAmounts(p,c,true).low,(off->damage[0]+stats.attackPower/14*(off->delay/1000.f))*.625f);
    // Source secondary magic damage bypasses primary-hand damage modifiers.
    const LocalMeleeItem* split=nullptr;
    for(uint32_t id=1;id<60000&&!split;++id)if(const auto* d=localMeleeItem(id);
        d&&!d->scaling&&d->itemClass==2&&d->inventoryType==13&&d->delay&&d->school[1]&&d->damage[3]>0)split=d;
    assert(split);item(split->id);p.equipment[16]=split->id;p.inventory.push_back({split->id,1});
    close(localWeaponAmounts(p,c,true).magicLow,split->damage[2]);
    close(localWeaponAmounts(p,c,true).magicHigh,split->damage[3]);
    p.equipment[16]=off->id;
    const auto noPrecision=p;
    for(unsigned rank=0;rank<3;++rank){assert(learnLocalTalent(p,c,1657,rank,error));
        close(localMeleeStats(p,c).hit,stats.hit+rank+1);close(localMeleeStats(p,c).talentWeaponHitPct,rank+1);
        close(localMeleeRatingBonus(p,c,5),localMeleeRatingBonus(noPrecision,c,5));
    }
    const auto learned=p;
    // Precision activates globally if any source-matching usable weapon exists.
    // Test a main-hand removal with qualifying offhand, then all hands absent.
    p.equipment[15]=0;close(localMeleeStats(p,c).talentWeaponHitPct,3);
    p.equipment[16]=0;close(localMeleeStats(p,c).talentWeaponHitPct,0);
    p=learned;p.inventory.clear();close(localMeleeStats(p,c).talentWeaponHitPct,0);
    // Ordinary ranged subclasses do not satisfy this source mask. The source
    // item table also has ranged-slot entries with qualifying melee subclasses;
    // those exercise the original any-usable-slot equipment requirement.
    const auto* precision=localTalentSpell(c,1657,3);assert(precision);
    const LocalMeleeItem *rangedMatch=nullptr,*rangedMiss=nullptr;
    for(uint32_t id=1;id<60000&&(!rangedMatch||!rangedMiss);++id)if(const auto* d=localMeleeItem(id);
        d&&!d->scaling&&d->itemClass==2&&d->delay&&(d->inventoryType==15||d->inventoryType==25||d->inventoryType==26)){
        if(precision->requiredItemSubclasses&(1u<<d->subclass)){if(!rangedMatch)rangedMatch=d;}
        else if(!rangedMiss)rangedMiss=d;
    }
    assert(rangedMatch&&rangedMiss);item(rangedMatch->id);item(rangedMiss->id);
    p=learned;p.equipment[15]=p.equipment[16]=0;p.equipment[17]=rangedMatch->id;p.inventory.push_back({rangedMatch->id,1});
    close(localMeleeStats(p,c).talentWeaponHitPct,3);
    p.equipment[17]=rangedMiss->id;p.inventory.push_back({rangedMiss->id,1});close(localMeleeStats(p,c).talentWeaponHitPct,0);
    // Exact one-roll miss boundary: 2% at 3/3 versus 5% without Precision.
    p=learned;LocalRealmNpc npc;npc.level=p.level;npc.x=10;npc.orientation=0;
    const auto hit=localMeleeStats(p,c);const auto missed=localMeleeStats(noPrecision,c);
    assert(localRollPlayerMelee(p,npc,hit,true,199,9999)==LocalMeleeOutcome::Miss);
    assert(localRollPlayerMelee(p,npc,hit,true,200,9999)!=LocalMeleeOutcome::Miss);
    assert(localRollPlayerMelee(noPrecision,npc,missed,true,200,9999)==LocalMeleeOutcome::Miss);
    assert(localRollPlayerMelee(p,npc,hit,true,700,9999)==LocalMeleeOutcome::Hit);
    assert(localRollPlayerMelee(noPrecision,npc,missed,true,700,9999)==LocalMeleeOutcome::Dodge);
    learn(165,1);learn(156,5);learn(167,1);assert(localTalentPointsSpent(p)==31);
    assert(c.spell(23881)&&localTalentSpell(c,167,1)->id==23881);
    const auto full=p;
    auto assertInactive=[&]{
        auto without=p;without.talents.clear();close(localMeleeStats(p,c).attackPower,localMeleeStats(without,c).attackPower);
        close(localMeleeStats(p,c).talentWeaponHitPct,0);
        close(localWeaponAmounts(p,c,true).low,localWeaponAmounts(without,c,true).low);
    };
    p.talents.clear();assertInactive();p=full;p.dead=true;assertInactive();p=full;p.classId=4;assertInactive();
    p=full;p.level=10;assertInactive();p=full;std::reverse(p.talents.begin(),p.talents.end());assertInactive();
    std::cout<<"PASS: actual DBC Warrior Fury stats; 31 points learned normally through all Flurry ranks and Bloodthirst at level40; armor AP divisors and integer truncation, owned gear, dynamic/snapshotted/expired Mark armor, all DWS ranks with primary offhand factor and unmodified secondary magic, all Precision ranks, any usable qualifying weapon across three slots, rating separation and exact melee miss boundary, reset/death/class/budget/order exclusions\n";
}
