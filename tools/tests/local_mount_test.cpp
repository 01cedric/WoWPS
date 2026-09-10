#include "game/local_mount.hpp"
#include "game/local_spell_import.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace wowee;
using namespace wowee::game;
static std::vector<uint8_t> table(const std::vector<uint32_t>& fields,const std::string& strings=std::string(1,'\0')) {
    std::vector<uint8_t> bytes(20+fields.size()*4+strings.size());
    const uint32_t header[]={0x43424457,1,uint32_t(fields.size()),uint32_t(fields.size()*4),uint32_t(strings.size())};
    std::memcpy(bytes.data(),header,20);std::memcpy(bytes.data()+20,fields.data(),fields.size()*4);
    std::memcpy(bytes.data()+20+fields.size()*4,strings.data(),strings.size());return bytes;
}
int main() {
    std::vector<uint32_t> row(234);row[0]=6648;row[28]=1;row[39]=20;row[136]=1;
    row[71]=row[72]=6;row[86]=row[87]=1;row[95]=78;row[96]=32;row[80+1]=59;row[110]=284;
    pipeline::DBCFile spells,casts;assert(spells.load(table(row,std::string("\0Test mount\0",12))));assert(casts.load(table({1,0,0,0})));
    detail::ClientSpellTables source;source.spells=&spells;source.casts=&casts;source.castIndex={{1,0}};
    LocalSpellDefinition mount;assert(detail::decodeClientGroundMount(source,0,mount));
    assert(mount.mountDisplayId==2404 && mount.mountCreatureId==284 && mount.mountSpeedPercent==60);
    row[96]=206;assert(spells.load(table(row,std::string("\0Flight mount\0",14))));
    LocalSpellDefinition flight;assert(!detail::decodeClientGroundMount(source,0,flight));
    row[96]=32;row[110]=999999;assert(spells.load(table(row,std::string("\0Missing mount\0",15))));
    LocalSpellDefinition missing;assert(!detail::decodeClientGroundMount(source,0,missing));
    auto content=std::make_shared<LocalWorldContent>();content->spells.push_back(mount);
    LocalSpellDefinition rune;rune.id=900001;rune.name="Rune authority test";rune.clientSpell=true;
    rune.resourceType=5;rune.allowableClasses=1u<<5;rune.runeCost={1,0,0};rune.runicPowerGain=10;
    rune.heal=1;rune.range=20;content->spells.push_back(rune);
    auto castRune=rune;castRune.id=900002;castRune.castTimeMs=1000;content->spells.push_back(castRune);
    auto castMount=mount;castMount.id=900003;castMount.castTimeMs=1000;content->spells.push_back(castMount);
    LocalItemDefinition item;item.id=5655;item.name="Mount item";item.stack=1;content->items.push_back(item);
    LocalGameplay game;game.useContent(content);LocalRealmPlayer p;p.guid=1;p.name="Human";p.level=20;p.ridingSkill=75;p.inventory={{5655,1}};
    std::vector<LocalRealmPlayer*> players{&p};std::string result;
    assert(localMountSupported(*content,5655));
    p.level=1;assert(!game.execute(p,{LocalAction::UseItem,0,5655},players,result) && p.inventory.size()==1);
    p.level=20;p.race=2;assert(!game.execute(p,{LocalAction::UseItem,0,5655},players,result));p.race=1;
    assert(game.execute(p,{LocalAction::UseItem,0,5655},players,result));
    assert(p.inventory.empty() && p.knownSpells==std::vector<uint32_t>{6648});
    assert(!game.execute(p,{LocalAction::UseItem,0,5655},players,result));
    assert(game.execute(p,{LocalAction::CastSpell,p.guid,6648},players,result) && p.mountSpellId==6648);
    assert(localActiveMount(*content,p)==content->spell(6648));
    assert(game.execute(p,{LocalAction::CastSpell,p.guid,6648},players,result) && !p.mountSpellId);
    p.attackTarget=99;assert(!game.execute(p,{LocalAction::CastSpell,p.guid,6648},players,result));p.attackTarget=0;
    p.movementState=kLocalMovementInLiquid;assert(!game.execute(p,{LocalAction::CastSpell,p.guid,6648},players,result));p.movementState=0;
    assert(game.execute(p,{LocalAction::CastSpell,p.guid,6648},players,result));
    assert(game.execute(p,{LocalAction::Dismount,0,0},players,result) && !p.mountSpellId);
    // Character re-entry clears active riding but preserves the learned spell.
    p.gameplayInitialized=true;p.mountSpellId=6648;game.initializePlayer(p,false);
    assert(!p.mountSpellId && p.knownSpells==std::vector<uint32_t>{6648});
    // Travel branches must clear riding before their early return. A taxi or
    // transport must not restore an earlier mount when the trip finishes.
    p.mountSpellId=6648;p.flight.active=true;game.tick(.25f,players);assert(!p.mountSpellId);
    p.flight.active=false;p.mountSpellId=6648;p.transportEntry=999999;game.tick(.25f,players);assert(!p.mountSpellId);
    p.transportEntry=0;p.knownSpells.push_back(900003);p.globalCooldownMs=0;
    assert(game.execute(p,{LocalAction::CastSpell,p.guid,900003},players,result));assert(!p.mountSpellId);
    p.x+=1;game.tick(.25f,players);assert(!p.castingSpellId && !p.mountSpellId);
    assert(game.execute(p,{LocalAction::CastSpell,p.guid,900003},players,result));
    for(int i=0;i<4;++i)game.tick(.25f,players);
    assert(p.mountSpellId==900003 && !p.castingSpellId);
    p.dead=true;game.tick(.25f,players);assert(!p.mountSpellId);

    LocalRealmPlayer dk;dk.guid=2;dk.name="Rune test";dk.classId=6;
    dk.resourceType=LocalResourceType::RunicPower;dk.mana=0;dk.maxMana=100;
    dk.health=50;dk.maxHealth=100;dk.knownSpells={900001,900002};
    LocalRealmPlayer distant;distant.guid=3;distant.x=1000;distant.health=50;distant.maxHealth=100;
    std::vector<LocalRealmPlayer*> runePlayers{&dk,&distant};
    assert(game.execute(dk,{LocalAction::CastSpell,dk.guid,900001},runePlayers,result));
    assert(dk.runeCooldownMs[0]==10000 && dk.runeCooldownMs[1]==0 && dk.mana==10);
    assert(game.execute(dk,{LocalAction::CastSpell,dk.guid,900001},runePlayers,result));
    assert(dk.runeCooldownMs[1]==10000 && dk.mana==20);
    assert(!game.execute(dk,{LocalAction::CastSpell,dk.guid,900001},runePlayers,result));
    assert(dk.mana==20 && dk.health==52);
    for(int i=0;i<40;++i)game.tick(.25f,runePlayers);
    assert(dk.runeCooldownMs==LocalRuneCooldowns{});
    const auto mana=dk.mana;
    assert(!game.execute(dk,{LocalAction::CastSpell,distant.guid,900001},runePlayers,result));
    assert(dk.runeCooldownMs==LocalRuneCooldowns{} && dk.mana==mana);
    assert(game.execute(dk,{LocalAction::CastSpell,dk.guid,900002},runePlayers,result));
    assert(dk.runeCooldownMs==LocalRuneCooldowns{}); // Resources commit at completion.
    dk.x+=1;game.tick(.25f,runePlayers);
    assert(!dk.castingSpellId && dk.runeCooldownMs==LocalRuneCooldowns{} && dk.mana==mana);
    assert(game.execute(dk,{LocalAction::CastSpell,dk.guid,900002},runePlayers,result));
    for(int i=0;i<4;++i)game.tick(.25f,runePlayers);
    assert(!dk.castingSpellId && dk.runeCooldownMs[0]==10000 && dk.mana==mana+10);
    std::cout<<"PASS mount authority: source model mapping, ground/flight filter, item ownership/level/race, learning, cast/toggle/dismount, combat/liquid/travel/death gates, interrupted cast, re-entry knowledge\n";
    std::cout<<"PASS rune authority: two rune consumption, exhaustion, recharge, resource gain, failed range, interrupted cast and completion commit\n";
}
