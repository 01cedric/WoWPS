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

// LocalStatAura::operator== is defaulted, so it also compares the two fields
// the header marks session-only and neither the save format nor the LAN wire
// carries:
// costModGeneration (a cast reservation cannot be spent across a refresh) and
// applicationGeneration (the authority's identity for one application). A
// reloaded or replicated aura is therefore never byte-identical to the live
// record, and a test that compares the two wholesale is asserting something the
// format has promised not to do. This checks what a reload really is supposed
// to reproduce: the session identities arrive cleared and every carried field
// matches, so a genuinely dropped field still fails.
inline bool reloadedStatAurasMatch(const std::vector<LocalStatAura>& restored,
                                   const std::vector<LocalStatAura>& live) {
    if (restored.size() != live.size()) return false;
    for (size_t i = 0; i < restored.size(); ++i) {
        if (restored[i].costModGeneration || restored[i].applicationGeneration) return false;
        auto expected = live[i];
        expected.costModGeneration = 0;
        expected.applicationGeneration = 0;
        if (!(restored[i] == expected)) return false;
    }
    return true;
}
