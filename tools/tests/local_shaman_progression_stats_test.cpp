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
    LocalRealmPlayer p;p.guid=10;p.classId=7;p.race=2;p.level=80;p.health=p.maxHealth=100;
    std::string error;
    assert(!learnLocalTalent(p,c,613,0,error)); // Tier one needs five actual earlier points.
    const auto naked=localMeleeStats(p,c);const auto baseMana=localClassBaseMana(p);
    const auto baseSpell=localSpellCritStats(p,c);const auto basePool=localResourcePools(p,c);
    const auto baseRegen=localRegenerationRates(p,c);
    assert(naked.sourceStats&&baseSpell.sourceStats&&basePool.sourceValues&&baseRegen.sourceValues);
    for(unsigned rank=1;rank<=5;++rank){
        assert(learnLocalTalent(p,c,614,rank-1,error));
        const auto stats=localMeleeStats(p,c);const int expected=int(float(naked.attributes[3])*(1+float(rank*2)/100));
        assert(stats.attributes[3]==expected&&stats.base==naked.base);
        for(unsigned stat:{0u,1u,2u,4u})assert(stats.attributes[stat]==naked.attributes[stat]);
        assert(localClassBaseMana(p)==baseMana);
        assert(localResourcePools(p,c).mana==basePool.mana+15*(expected-naked.attributes[3]));
        close(localSpellCritStats(p,c).intellectPct,baseSpell.intellectPct*expected/naked.attributes[3]);
        close(localRegenerationRates(p,c).manaPerSecond,baseRegen.manaPerSecond*std::sqrt(double(expected)/naked.attributes[3]));
        if(rank==3)assert(localTalentTotalStat(p,c,3,50)==52); // Source float then integer truncation.
    }
    // Real equipped Intellect is scaled together with the base; ownership is
    // still required and rank replacement/reset immediately removes its effect.
    const auto* ring=localMeleeItem(1449);assert(ring&&ring->stats[3]>0);
    LocalItemDefinition item;item.id=ring->id;item.inventoryType=ring->inventoryType;c.items={item};
    p.equipment[10]=ring->id;p.inventory={{ring->id,1}};
    assert(localMeleeStats(p,c).attributes[3]==int(float(naked.attributes[3]+ring->stats[3])*1.1f));
    p.inventory.clear();assert(localMeleeStats(p,c).attributes[3]==naked.attributes[3]*110/100);
    p.equipment.fill(0);
    const auto akOnly=p;const auto akMelee=localMeleeStats(p,c);const auto akSpell=localSpellCritStats(p,c);
    const auto akRanged=localRangedCritChance(p,c);
    for(unsigned rank=1;rank<=5;++rank){
        assert(learnLocalTalent(p,c,613,rank-1,error));
        close(localMeleeStats(p,c).crit,akMelee.crit+rank); // Unarmed is eligible.
        close(localMeleeStats(p,c).offHandCrit,akMelee.offHandCrit+rank);
        close(localRangedCritChance(p,c),akRanged+rank);
        const auto crit=localSpellCritStats(p,c);close(crit.talentPct,rank);close(crit.crit,akSpell.crit+rank);
        close(localSpellCritFromIntellect(p,c),localSpellCritFromIntellect(akOnly,c));
        for(unsigned school=1;school<7;++school)close(localSpellCritChance(p,c,1u<<school),crit.crit);
        assert(!crit.itemRating&&!crit.auraRating); // Talent percent never masquerades as rating.
    }
    const auto learned=p;
    // Ghost Wolf has no explicit source attack-speed override, yet usable
    // weapons are hidden by its feral state: 2s swings, no offhand/secondary
    // damage, while stored primary weapon damage and class stats remain.
    const LocalMeleeItem* weapon=nullptr;
    for(uint32_t id=1;id<60000&&!weapon;++id)if(const auto* candidate=localMeleeItem(id);
        candidate&&!candidate->scaling&&candidate->itemClass==2&&candidate->inventoryType==13&&candidate->delay&&
        candidate->damage[2]==0&&candidate->damage[3]==0)weapon=candidate;
    assert(weapon);item.id=weapon->id;item.inventoryType=weapon->inventoryType;c.items.push_back(item);
    std::sort(c.items.begin(),c.items.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    p.equipment[15]=p.equipment[16]=weapon->id;p.inventory={{weapon->id,2}};
    // Component allocation: Dual Wield's complete normal route remains gated.
    // The learned capability and its real Spirit Weapons prerequisite are
    // required; a legacy known-spell entry alone is not an equip/attack bypass.
    p.talents.emplace_back(616,1);p.talents.emplace_back(1690,1);std::sort(p.talents.begin(),p.talents.end());
    const auto weaponBefore=localWeaponAmounts(p,c);const auto statsBefore=localMeleeStats(p,c);
    assert(statsBefore.offHand&&localWeaponAmounts(p,c,true).active);
    const auto poolBefore=localResourcePools(p,c);const auto critBefore=localSpellCritStats(p,c);
    const auto* wolf=localFormProfile(2645);assert(wolf);enterLocalForm(p,*wolf);
    const auto weaponAfter=localWeaponAmounts(p,c);close(weaponAfter.low,weapon->damage[0]+statsBefore.attackPower/14*2);
    close(weaponAfter.high,weapon->damage[1]+statsBefore.attackPower/14*2);close(weaponAfter.seconds,2);
    close(localWeaponAmounts(p,c,false,true).apSeconds,2.4f);
    assert(!localMeleeStats(p,c).offHand&&!localWeaponAmounts(p,c,true).active);
    close(localMeleeStats(p,c).crit,statsBefore.crit);close(localSpellCritStats(p,c).crit,critBefore.crit);
    assert(localResourcePools(p,c).mana==poolBefore.mana);
    leaveLocalForm(p);close(localWeaponAmounts(p,c).low,weaponBefore.low);
    const LocalMeleeItem* secondary=nullptr;
    for(uint32_t id=1;id<60000&&!secondary;++id)if(const auto* candidate=localMeleeItem(id);
        candidate&&!candidate->scaling&&candidate->itemClass==2&&candidate->inventoryType==13&&candidate->delay&&
        candidate->school[0]==0&&candidate->school[1]!=0&&candidate->damage[3]>0)secondary=candidate;
    assert(secondary);item.id=secondary->id;item.inventoryType=secondary->inventoryType;c.items.push_back(item);
    std::sort(c.items.begin(),c.items.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    p.equipment[15]=secondary->id;p.equipment[16]=0;p.inventory={{secondary->id,1}};
    const auto splitBefore=localWeaponAmounts(p,c);assert(splitBefore.magicHigh>0);
    enterLocalForm(p,*wolf);const auto splitAfter=localWeaponAmounts(p,c);
    close(splitAfter.low,secondary->damage[0]+localMeleeStats(p,c).attackPower/14*2);
    close(splitAfter.high,secondary->damage[1]+localMeleeStats(p,c).attackPower/14*2);
    close(splitAfter.seconds,2);close(splitAfter.magicLow,0);close(splitAfter.magicHigh,0);
    leaveLocalForm(p);close(localWeaponAmounts(p,c).magicHigh,splitBefore.magicHigh);
    p=learned;
    auto noPassives=[&]{
        const auto stats=localMeleeStats(p,c);assert(stats.attributes[3]==stats.base[3]);
        close(localSpellCritStats(p,c).talentPct,0);close(localTalentWeaponCritPct(p,c,2,0,13),0);
    };
    p.talents.clear();noPassives();
    p=learned;p.dead=true;noPassives();
    p=learned;p.classId=8;noPassives();
    p=learned;p.level=10;noPassives(); // Over-budget allocations are inert.
    p=learned;std::reverse(p.talents.begin(),p.talents.end());noPassives();
    p=learned;
    auto* ts=const_cast<LocalSpellDefinition*>(localTalentSpell(c,613,5));assert(ts);
    const auto originalTs=*ts;
    ts->talentPrerequisites[0]=999999;close(localSpellCritStats(p,c).talentPct,0);
    close(localMeleeStats(p,c).crit,akMelee.crit);*ts=originalTs;
    ts->unsupportedReason="deliberately invalid source profile";close(localSpellCritStats(p,c).talentPct,0);*ts=originalTs;
    // Existing weapon-restricted Cruelty remains unavailable to unarmed swings.
    LocalRealmPlayer warrior=learned;warrior.classId=1;warrior.talents.clear();
    const auto* cruelty=c.spell(12320);assert(cruelty&&cruelty->passiveMeleeCritPct==1);
    for(unsigned rank=0;rank<5;++rank)assert(learnLocalTalent(warrior,c,cruelty->talentId,rank,error));
    close(localTalentWeaponCritPct(warrior,c,UINT32_MAX,UINT32_MAX,UINT32_MAX),0);
    close(localTalentWeaponCritPct(warrior,c,2,0,13),5);
    std::cout<<"PASS: actual DBC five Ancestral Knowledge ranks and five Thundering Strikes ranks learned sequentially; tier gate, current total Intellect/base separation, gear ownership, derived mana/regen/spell crit, unarmed/offhand/ranged/all magic crit, rank/reset/death/class/budget/prerequisite exclusions and existing Cruelty equipment restriction\n";
}
