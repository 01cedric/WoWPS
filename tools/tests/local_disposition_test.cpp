#include "game/local_gameplay.hpp"
#include <cassert>
#include <iostream>
using namespace wowee::game;
int main() {
 LocalGameplay game; auto content=std::make_shared<LocalWorldContent>();
 LocalNpcDefinition def;def.id=1;def.faction=2;def.aggroRadius=20;content->npcs={def};game.useContent(content);
 LocalRealmPlayer player;player.race=1;LocalRealmNpc npc;npc.entry=1;
 const auto check=[&](bool attack,bool aggressive){const auto d=game.npcDisposition(player,npc);assert(d.attackable==attack&&d.aggressive==aggressive);assert(game.canAttack(player,npc)==attack);assert(game.isAggressive(player,npc)==aggressive);};
 check(false,false);npc.hostile=true;check(true,true);npc.questGiver=true;check(false,false);npc.questGiver=false;
 content->npcs[0].aggroRadius=0;check(true,false);content->npcs[0].aggroRadius=20;
 LocalFactionTemplate friendly;friendly.id=1;friendly.faction=10;friendly.factionGroup=1;
 LocalFactionTemplate other;other.id=2;other.faction=20;other.factionGroup=2;
 std::array<uint32_t,12> races{};races[1]=1;std::string error;
 assert(game.setFactionTemplates({friendly,other},races,error));check(true,false);npc.questGiver=true;check(false,false);
 other.enemyGroup=1;assert(game.setFactionTemplates({friendly,other},races,error));check(true,true);
 other.enemyGroup=0;other.friendGroup=1;assert(game.setFactionTemplates({friendly,other},races,error));check(false,false);
 other.friendGroup=0;friendly.friends[0]=20;assert(game.setFactionTemplates({friendly,other},races,error));npc.questGiver=false;check(false,false);
 for(auto flags:{0x2u,0x100u,0x02000000u}){content->npcs[0].unitFlags=flags;check(false,false);}content->npcs[0].unitFlags=0;
 player.race=255;check(true,true);npc.entry=99;check(false,false);
 std::cout<<"PASS combined NPC disposition: fallback, neutral, quest giver, hostile/friendly/asymmetric factions, protected flags, missing NPC, invalid race\n";
}
