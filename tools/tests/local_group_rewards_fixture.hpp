#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_party.hpp"
#include <cassert>
using namespace wowee::game;
inline std::shared_ptr<LocalWorldContent> rewardContent() {
    auto c=std::make_shared<LocalWorldContent>();
    LocalItemDefinition item;item.id=117;item.name="Reward item";item.stack=20;c->items={item};
    LocalNpcDefinition enemy;enemy.id=50;enemy.name="Reward enemy";enemy.health=10;enemy.hostile=true;
    enemy.xp=101;enemy.money=7;enemy.loot={{117,1}};enemy.respawnSeconds=30;c->npcs={enemy};
    LocalQuestDefinition quest;quest.id=1;quest.title="Kill credit";quest.objectives={{LocalQuestObjective::Type::Kill,50,20}};c->quests={quest};
    LocalSpellDefinition spell;spell.id=1;spell.name="Instant hit";spell.damage=10;spell.range=100;c->spells={spell};
    return c;
}
inline LocalRealmPlayer rewardPlayer(uint64_t guid) {
    LocalRealmPlayer p;p.guid=guid;p.name="Player"+std::to_string(guid);p.x=p.y=p.z=0;p.health=p.maxHealth=100;
    p.knownSpells={1};p.quests={{1,LocalQuestStatus::Active,{0}}};p.gameplayInitialized=true;return p;
}
inline LocalRealmNpc rewardNpc(uint64_t guid=10,uint64_t tag=1) {
    LocalRealmNpc n;n.guid=guid;n.entry=50;n.name="Reward enemy";n.health=n.maxHealth=10;n.hostile=true;
    n.x=n.y=n.z=n.homeX=n.homeY=n.homeZ=0;n.lootOwner=tag;return n;
}
inline void rewardKill(LocalGameplay& game,LocalRealmPlayer& attacker,const std::vector<LocalRealmPlayer*>& players,LocalRealmNpc npc=rewardNpc()) {
    game.setRemoteNpcs({npc});attacker.globalCooldownMs=0;attacker.cooldowns.clear();
    std::string result;assert(game.execute(attacker,{LocalAction::CastSpell,npc.guid,1},players,result));
    assert(game.npcs().size()==1 && game.npcs()[0].dead);
}
