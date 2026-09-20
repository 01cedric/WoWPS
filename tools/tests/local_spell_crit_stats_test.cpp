#include "game/local_melee.hpp"
#include "game/local_spell_import.hpp"
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
static void close(float actual,float expected,std::source_location location=std::source_location::current()){
    if(!std::isfinite(actual)||std::abs(actual-expected)>.00003f){
        std::cerr<<"line "<<location.line()<<": actual="<<actual<<" expected="<<expected<<'\n';std::abort();
    }
}
int main(int argc,char** argv){
    assert(argc==2);
    std::map<std::string,pipeline::DBCFile> tables;
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon","SkillLineAbility","SkillLine","Talent","SpellRuneCost","SpellRadius"}){
        std::ifstream file(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> data{std::istreambuf_iterator<char>(file),{}};assert(tables[name].load(data));
    }
    auto table=[&](const char* name){return &tables.at(name);};
    auto imported=importClientStarterSpells(table("Spell"),table("SpellRange"),table("SpellCastTimes"),table("SpellDuration"),
        table("SpellIcon"),table("SkillLineAbility"),table("SkillLine"),table("Talent"),table("SpellRuneCost"),table("SpellRadius"));
    LocalWorldContent c;c.spells=std::move(imported.spells);
    std::sort(c.spells.begin(),c.spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    for(uint32_t id:{30482u,43045u,43046u}){
        const auto* d=c.spell(id);assert(d&&d->unsupportedReason.empty()&&validLocalProc(*d));
        assert(c.spell(d->proc.spellId)&&c.spell(d->proc.spellId)->triggeredOnly);
    }
    uint32_t spiritItem=0;
    for(uint32_t id=1;id<60000&&!spiritItem;++id)if(const auto* item=localMeleeItem(id);
        item&&!item->scaling&&item->inventoryType==1&&item->stats[4]>0)spiritItem=id;
    assert(spiritItem);
    for(auto id:{1449u,spiritItem}){LocalItemDefinition item;item.id=id;item.inventoryType=localMeleeItem(id)->inventoryType;c.items.push_back(item);}
    std::sort(c.items.begin(),c.items.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    LocalRealmPlayer p;p.guid=10;p.classId=8;p.race=1;p.level=80;p.health=p.maxHealth=100;p.mapId=1;p.instanceId=2;
    const auto base=localSpellCritStats(p,c);assert(base.sourceStats);
    close(base.basePct,.9075f);close(base.intellectPct,localMeleeStats(p,c).attributes[3]*.006f);
    close(base.crit,base.basePct+base.intellectPct);close(localSpellCritFromIntellect(p,c),base.crit);
    assert(!base.itemRating&&!base.auraRating);
    p.equipment[10]=1449;p.inventory={{1449,1}};
    const auto equipped=localSpellCritStats(p,c);assert(equipped.itemRating==4);
    close(equipped.crit-base.crit,2*.006f+4*.02178365111f);
    assert(localRangedCritRating(p,c)==4);close(localMeleeRatingBonus(p,c,10),equipped.ratingPct);
    p.equipment[11]=1449;assert(localSpellCritStats(p,c).itemRating==4); // No duplicated unowned ring.
    p.inventory[0].count=2;assert(localSpellCritStats(p,c).itemRating==8);
    p.inventory.clear();assert(localSpellCritStats(p,c).itemRating==0);p.equipment.fill(0);
    for(uint32_t id:{30482u,43045u,43046u}){
        const auto* d=c.spell(id);assert(d);
        LocalStatAura aura;aura.spellId=id;aura.remainingMs=d->durationMs;aura.mapId=p.mapId;
        aura.instanceId=p.instanceId;aura.casterGuid=p.guid;p.statAuras={aura};
        const auto active=p;const auto s=localSpellCritStats(p,c);
        assert(s.auraRating==localMeleeStats(p,c).attributes[4]*35/100);
        close(s.ratingPct,s.auraRating*.02178365111f);close(localIncomingCritReductionPct(p,c),5);
        assert(localMeleeStats(p,c).ratings[5]==s.auraRating&&localRangedCritRating(p,c)==s.auraRating);
        close(localMeleeRatingBonus(p,c,9),localRangedCritRatingBonus(p,c));
        auto excluded=[&](auto mutate){p=active;mutate();assert(localSpellCritStats(p,c).auraRating==0);close(localIncomingCritReductionPct(p,c),0);};
        excluded([&]{p.dead=true;});excluded([&]{p.health=0;});excluded([&]{p.statAuras[0].remainingMs=0;});
        excluded([&]{p.statAuras[0].remainingMs=d->durationMs+1;});excluded([&]{p.statAuras[0].mapId++;});
        excluded([&]{p.statAuras[0].instanceId++;});excluded([&]{p.statAuras[0].casterGuid++;});
        excluded([&]{p.statAuras[0].stacks=2;});excluded([&]{p.statAuras.push_back(p.statAuras[0]);});
        excluded([&]{p.statAuras[0].procCharges=1;});
        p=active;
        auto child=std::find_if(c.spells.begin(),c.spells.end(),[&](const auto& x){return x.id==d->proc.spellId;});
        assert(child!=c.spells.end());child->unsupportedReason="deliberately missing child closure";
        assert(localSpellCritStats(p,c).auraRating==0);child->unsupportedReason.clear();
        p=active;p.equipment[0]=spiritItem;p.inventory={{spiritItem,1}};
        const auto changed=localSpellCritStats(p,c);
        assert(changed.auraRating==localMeleeStats(p,c).attributes[4]*35/100&&changed.auraRating>=s.auraRating);
        p=active;p.statAuras.clear();assert(localMeleeStats(p,c).ratings[5]==0&&localRangedCritRating(p,c)==0);
    }
    LocalRealmNpc n;n.level=80;n.entry=0;n.x=1;
    p.statAuras.clear();LocalMeleeStats attackTable;
    assert(localRollNpcMelee(n,p,attackTable,500)==LocalMeleeOutcome::Critical);
    attackTable.incomingCritReductionPct=5;
    assert(localRollNpcMelee(n,p,attackTable,500)==LocalMeleeOutcome::Hit);
    n.level=81;assert(localRollNpcMelee(n,p,attackTable,490)==LocalMeleeOutcome::Critical); // Level delta still contributes.
    assert(localNpcCreatureType(3)==6&&localNpcIsDemonOrUndead(3)&&!localNpcIsDemonOrUndead(1));
    assert(!localNpcIsDemonOrUndead(UINT32_MAX));
    assert(localSpellCritChance(p,c,0)==0&&localSpellCritChance(p,c,128)==0);
    p.level=0;assert(!localSpellCritStats(p,c).sourceStats);p.level=81;assert(!localSpellCritStats(p,c).sourceStats);
    p.level=80;p.classId=10;assert(!localSpellCritStats(p,c).sourceStats);
    std::cout<<"PASS: source level80 mage GT anchors; real crit item ownership/counts; all three real Molten ranks; current-Spirit melee/ranged/spell rating; aura lifecycle exclusions; incoming melee crit reduction preserves level delta; creature types and invalid source coordinates\n";
}
