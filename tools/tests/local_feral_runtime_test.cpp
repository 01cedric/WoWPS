#include "game/local_melee.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_feral_talents.hpp"
#include "game/local_progression_modifiers.hpp"
#include "game/local_stat_auras.hpp"
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
// Focused stat/form/cost harness: production decoder and talent transaction,
// actual source equipment metadata, no alternate gameplay implementation.
const LocalSpellDefinition* LocalWorldContent::spell(uint32_t id) const {
    for(const auto& d:spells)if(d.id==id)return &d;return nullptr;
}
const LocalItemDefinition* LocalWorldContent::item(uint32_t id) const {
    for(const auto& d:items)if(d.id==id)return &d;return nullptr;
}
static void close(double actual,double expected,std::source_location at=std::source_location::current()) {
    if(!std::isfinite(actual)||std::abs(actual-expected)>.0002){
        std::cerr<<"line "<<at.line()<<": actual="<<actual<<" expected="<<expected<<'\n';std::abort();
    }
}
int main(int argc,char** argv) {
    assert(argc==2);
    std::map<std::string,pipeline::DBCFile> tables;
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon","SkillLineAbility","SkillLine","Talent","TalentTab","SpellRuneCost","SpellRadius"}) {
        std::ifstream file(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};assert(tables[name].load(bytes));
    }
    auto t=[&](const char* name){return &tables.at(name);};
    auto imported=importClientStarterSpells(t("Spell"),t("SpellRange"),t("SpellCastTimes"),t("SpellDuration"),
        t("SpellIcon"),t("SkillLineAbility"),t("SkillLine"),t("Talent"),t("SpellRuneCost"),t("SpellRadius"));
    detail::importClientTalents(imported,t("Talent"),t("TalentTab"),t("Spell"),t("SpellRange"),t("SpellCastTimes"),
        t("SpellDuration"),t("SpellIcon"),t("SpellRuneCost"),t("SpellRadius"));
    LocalWorldContent c;c.spells=std::move(imported.spells);
    assert(!c.spell(24867)&&!c.spell(24864));
    LocalRealmPlayer p;p.guid=10;p.classId=11;p.race=4;p.level=80;p.health=p.maxHealth=100;p.maxMana=1000;
    std::string error;
    auto learn=[&](uint32_t id,unsigned ranks){for(unsigned rank=0;rank<ranks;++rank){
        if(!learnLocalTalent(p,c,id,rank,error)){std::cerr<<id<<' '<<rank<<' '<<error<<'\n';std::abort();}
    }};
    assert(!learnLocalTalent(p,c,798,0,error));
    const auto* claw=c.spell(1082);const auto* rake=c.spell(1822);LocalSpellDefinition maulDefinition;maulDefinition.id=6807;
    detail::ClientSpellTables source;source.spells=t("Spell");source.ranges=t("SpellRange");source.casts=t("SpellCastTimes");source.durations=t("SpellDuration");
    detail::ClientSpellTables::buildIndex(source.spells,source.spellIndex);detail::ClientSpellTables::buildIndex(source.ranges,source.rangeIndex);
    detail::ClientSpellTables::buildIndex(source.casts,source.castIndex);detail::ClientSpellTables::buildIndex(source.durations,source.durationIndex);
    detail::decodeClientSpell(source,uint32_t(detail::ClientSpellTables::lookup(source.spellIndex,6807)),maulDefinition);
    const auto* maul=&maulDefinition;const auto* wrath=c.spell(5176);
    assert(claw&&rake&&maul&&wrath&&claw->unsupportedReason.empty()&&rake->unsupportedReason.empty());
    p.resourceType=LocalResourceType::Energy;p.formSpellId=768;p.maxMana=100;
    const auto clawCost=localSpellBaseResourceCost(p,c,*claw),rakeCost=localSpellBaseResourceCost(p,c,*rake);
    const auto wrathCost=localSpellBaseResourceCost(p,c,*wrath);
    for(unsigned rank=1;rank<=5;++rank) {
        assert(learnLocalTalent(p,c,796,rank-1,error));
        assert(localSpellBaseResourceCost(p,c,*claw)==clawCost-rank);
        assert(localSpellBaseResourceCost(p,c,*rake)==rakeCost-rank);
        // Maul remains excluded as a next-swing spell; its decoded source
        // cost tests rage-unit algebra only, never spell admission/casting.
        p.formSpellId=5487;p.resourceType=LocalResourceType::Rage;
        assert(localSpellBaseResourceCost(p,c,*maul)==maul->mana-rank);
        p.formSpellId=768;p.resourceType=LocalResourceType::Energy;
        assert(localSpellBaseResourceCost(p,c,*wrath)==wrathCost);
    }
    // An operation-14 component fixture checks algebra independently of any
    // source ID: percent acts on base, flat acts afterward; cap prevents wrap.
    auto cost=*claw;cost.mana=40;
    auto* ferocity=const_cast<LocalSpellDefinition*>(localTalentSpell(c,796,5));assert(ferocity);
    const auto mods=ferocity->passiveCastModifiers;
    ferocity->passiveCastModifiers[2]=ferocity->passiveCastModifiers[1];
    ferocity->passiveCastModifiers[2].percentage=true;ferocity->passiveCastModifiers[2].amount=-50;
    assert(localSpellBaseResourceCost(p,c,cost)==15);
    cost.mana=2;assert(localSpellBaseResourceCost(p,c,cost)==0);
    ferocity->passiveCastModifiers=mods;
    p.formSpellId=0;p.resourceType=LocalResourceType::Mana;
    learn(805,2);learn(794,3);learn(807,2);learn(798,3);
    const auto naked=localMeleeStats(p,c);
    assert(localFeralCritPct(p,c)==0&&localFeralDodgePct(p,c)==0);close(localFormRunPercent(p,c),100);
    auto noTalents=p;noTalents.talents.clear();
    close(localMeleeStats(p,c).crit,localMeleeStats(noTalents,c).crit);
    close(localMeleeStats(p,c).dodge,localMeleeStats(noTalents,c).dodge);
    for(const auto id:{768u,5487u,9634u,783u,1066u}) {
        p.formSpellId=id;noTalents.formSpellId=id;
        const auto stats=localMeleeStats(p,c),baseline=localMeleeStats(noTalents,c);
        const bool feral=id==768||id==5487||id==9634;
        close(stats.crit,baseline.crit+(feral?6:0));close(stats.offHandCrit,baseline.offHandCrit+(feral?6:0));
        close(localRangedCritChance(p,c),localRangedCritChance(noTalents,c)+(feral?6:0));
        close(stats.dodge,baseline.dodge+(feral?4:0));
        close(localSpellCritStats(p,c).crit,localSpellCritStats(noTalents,c).crit);
        close(localFormRunPercent(p,c),id==768?130:localFormRunPercent(noTalents));
        close(localFormRunPercent(p,c,50),id==768?65:localFormRunPercent(noTalents,50));
    }
    p.formSpellId=768;p.dead=true;assert(localFeralCritPct(p,c)==0&&localFeralDodgePct(p,c)==0);close(localFormRunPercent(p,c),100);p.dead=false;
    p.classId=7;p.formSpellId=2645;assert(localFeralCritPct(p,c)==0&&localFeralDodgePct(p,c)==0);close(localFormRunPercent(p,c,30),100);p.classId=11;
    struct ArmorCase {uint32_t id,armor;float bonus;size_t slot;};
    // Real SQL entries cover ordinary leather, bonus armor leather, signed
    // negative shield armor, and jewelry whose positive bonus is all flat.
    for(const auto test:{ArmorCase{60,33,0,4},ArmorCase{15053,268,120,4},ArmorCase{6725,775,-1,16},ArmorCase{19065,0,20,10},ArmorCase{6804,0,1.9f,15}}) {
        const auto* metadata=localMeleeItem(test.id);assert(metadata);
        LocalItemDefinition item;item.id=test.id;item.inventoryType=metadata->inventoryType;item.armor=test.armor;
        c.items={item};p.equipment.fill(0);p.equipment[test.slot]=test.id;p.inventory={{test.id,1}};
        for(const auto form:{0u,768u,5487u,9634u}) {
            p.formSpellId=form;const auto stats=localMeleeStats(p,c);
            const auto adjusted=test.armor?int(test.armor)-int(test.bonus):0;
            const bool base=metadata->itemClass==4&&metadata->subclass!=0;
            const auto* f=localActiveForm(p);const float factor=(f?float(f->armorPercent)/100.f:1.f)*1.1f;
            const float totalValue=float(base?0:adjusted)+std::max(0.f,test.bonus);
            const auto expected=uint32_t(float(base?adjusted:0)*factor+2*stats.attributes[1]+totalValue);
            assert(localMeleeArmor(p,c)==expected);
            auto reset=p;reset.talents.clear();
            const float noTalentFactor=f?float(f->armorPercent)/100.f:1.f;
            const auto resetExpected=uint32_t(float(base?adjusted:0)*noTalentFactor+2*stats.attributes[1]+totalValue);
            assert(localMeleeArmor(reset,c)==resetExpected);
            auto unowned=p;unowned.inventory.clear();assert(localMeleeArmor(unowned,c)==uint32_t(2*localMeleeStats(unowned,c).attributes[1]));
        }
    }
    {
        // Controlled equipment-state regression using untouched source item
        // metadata. This is an armor formula fixture, not an equip-admission
        // claim for the Druid/crossbow combination or item required level.
        LocalRealmPlayer rounding;rounding.guid=123;rounding.classId=11;rounding.race=6;rounding.level=27;
        rounding.health=rounding.maxHealth=100;rounding.formSpellId=5487;
        rounding.talents={{794,1},{796,5}};
        LocalItemDefinition pants;pants.id=838;pants.inventoryType=7;pants.armor=25;
        LocalItemDefinition crossbow;crossbow.id=45570;crossbow.inventoryType=26;
        c.items={pants,crossbow};rounding.equipment[6]=838;rounding.equipment[17]=45570;
        rounding.inventory={{838,1},{45570,1}};
        assert(localMeleeStats(rounding,c).attributes[1]==70);
        // Source: (25 * (2.8f * 1.04f) + 140) + 30.2f -> 242.999984 -> 242.
        // Adding the weapon bonus before agility incorrectly rounds to 243.
        assert(localMeleeArmor(rounding,c)==242);
    }
    // A supported Mark aura remains an additive armor source outside both
    // Thick Hide and Bear's equipment multiplier.
    p.equipment.fill(0);p.inventory.clear();p.formSpellId=5487;
    const auto* mark=c.spell(1126);assert(mark&&mark->unsupportedReason.empty()&&mark->buffArmor);
    LocalStatAura aura;aura.spellId=mark->id;aura.casterGuid=p.guid;aura.remainingMs=mark->durationMs;
    aura.mapId=p.mapId;aura.instanceId=p.instanceId;aura.stacks=1;p.statAuras={aura};
    assert(localMeleeArmor(p,c)==uint32_t(2*localMeleeStats(p,c).attributes[1])+mark->buffArmor);
    // Shaman's full currently available 28-point route uses real learning.
    p={};p.guid=11;p.classId=7;p.race=2;p.level=80;p.health=p.maxHealth=100;p.maxMana=1000;c.items.clear();
    learn(614,5);learn(613,5);learn(607,3);learn(605,2);learn(602,5);
    const auto opening=p;const auto openingStats=localMeleeStats(p,c);
    for(unsigned rank=1;rank<=3;++rank) {
        assert(learnLocalTalent(p,c,2083,rank-1,error));
        const unsigned percent=rank==1?33:rank==2?66:100;
        close(localMeleeStats(p,c).attackPower,openingStats.attackPower+uint32_t(float(openingStats.attributes[3])*percent/100.f));
    }
    learn(616,1);learn(617,1);learn(1643,3);
    assert(localTalentPointsSpent(p)==28);
    assert(!learnLocalTalent(p,c,901,0,error));assert(!learnLocalTalent(p,c,1690,0,error));
    const auto learnedShaman=p;
    const LocalMeleeItem* fitting=nullptr;const LocalMeleeItem* excluded=nullptr;
    for(uint32_t id=1;id<60000&&(!fitting||!excluded);++id)if(const auto* item=localMeleeItem(id);
        item&&item->itemClass==2&&item->inventoryType==13&&item->delay&&!item->scaling&&item->school[0]==0&&item->damage[2]==0&&item->damage[3]==0) {
        if((42035u&(1u<<item->subclass))&&!fitting)fitting=item;
        if(!(42035u&(1u<<item->subclass))&&!excluded)excluded=item;
    }
    assert(fitting&&excluded);
    for(const auto* metadata:{fitting,excluded}) {
        LocalItemDefinition item;item.id=metadata->id;item.inventoryType=metadata->inventoryType;c.items.push_back(item);
    }
    p.inventory={{fitting->id,2},{excluded->id,1}};p.equipment[15]=fitting->id;p.equipment[16]=excluded->id;
    close(localWeaponTalentDamageMultiplier(p,c),1.1f);close(localWeaponTalentDamageMultiplier(p,c,true),1.f);
    close(localTalentPhysicalDamageMultiplier(p,c),1.f); // Mastery never changes spell/DoT damage globally.
    assert(localMeleeStats(p,c).parry>=5&&!localMeleeStats(p,c).offHand);
    p.knownSpells.push_back(30798);assert(!localMeleeStats(p,c).offHand);p.knownSpells.clear();
    const auto powered=localWeaponAmounts(p,c),raw=localWeaponAmounts(p,c,false,false,false);
    close(powered.low,raw.low*1.1f);close(powered.high,raw.high*1.1f);
    // Explicit component allocations for route-gated complete Dual Wield and
    // specialization profiles: no claim that normal learning passed its tier.
    p.talents.emplace_back(1690,1);p.talents.emplace_back(1692,3);std::sort(p.talents.begin(),p.talents.end());
    assert(localMeleeStats(p,c).offHand);close(localMeleeStats(p,c).talentWeaponHitPct,6.f);
    close(localWeaponTalentDamageMultiplier(p,c,true),1.f);
    std::swap(p.equipment[15],p.equipment[16]);close(localWeaponTalentDamageMultiplier(p,c),1.f);close(localWeaponTalentDamageMultiplier(p,c,true),1.1f);
    auto withoutOffhand=p;withoutOffhand.equipment[16]=0;assert(!localMeleeStats(withoutOffhand,c).offHand);close(localMeleeStats(withoutOffhand,c).talentWeaponHitPct,0.f);
    auto missingOwnership=p;missingOwnership.inventory.clear();assert(!localMeleeStats(missingOwnership,c).offHand);close(localWeaponTalentDamageMultiplier(missingOwnership,c),1.f);
    p.formSpellId=2645;assert(!localMeleeStats(p,c).offHand);close(localMeleeStats(p,c).talentWeaponHitPct,0.f);close(localWeaponTalentDamageMultiplier(p,c),1.f);
    p.formSpellId=0;p.talents.clear();assert(!localMeleeStats(p,c).offHand);close(localMeleeStats(p,c).parry,0.f);close(localWeaponTalentDamageMultiplier(p,c),1.f);
    auto resetStats=localMeleeStats(p,c);p.talents=learnedShaman.talents;const auto intellectStats=localMeleeStats(p,c);
    assert(intellectStats.attackPower>=resetStats.attackPower+intellectStats.attributes[3]);
    p.dead=true;close(localWeaponTalentDamageMultiplier(p,c),1.f);assert(!localMeleeStats(p,c).offHand);
    std::cout<<"PASS Shaman real 28-point route; Mental Dexterity rank rounding; Spirit Weapons parry/reset; Weapon Mastery fitting main/offhand once, raw bypass, global spell exclusion; Dual Wield component capability/hit, legacy spell bypass rejected, ownership/form/reset exclusions\n";
    std::cout<<"PASS Feral source learning; Ferocity energy/rage flat costs and percent-before-flat clamp; form crit/ranged/dodge, cat run/slow, death/wrong-class/reset; five real armor items including signed and zero-base bonus, ownership, Bear/Dire and additive Mark\n";
}
