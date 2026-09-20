#!/usr/bin/env python3
"""Execute extracted production ghost/corpse selection and allocation rollback.
Rendering, appearance decoding, entity storage and addon dispatch are test doubles.
This does not claim full GameHandler integration or console graphical acceptance.
"""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
src = (root / 'src/game/game_handler_local.cpp').read_text()
start = src.index('bool GameHandler::syncLocalRealmPlayer(')
end = src.index('    auto unit = std::static_pointer_cast<Unit>', start)
sync = src[start:end] + '\n    (void)event; localEquipmentVisuals_[snapshot.guid] = snapshot.equipment; return false;\n}\n'
start = src.index('    if (fresh && character.guid != playerGuid && playerSpawnCallback_)')
end = src.index('\n}\n\nvoid GameHandler::removeLocalExplorationPlayer', start)
rollback = src[start:end]
start = src.index('void GameHandler::removeLocalExplorationPlayer(')
end = src.index('\nbool GameHandler::syncLocalRealmPlayer(', start)
remove = src[start:end]
preamble = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#define LOG_INFO(...) ((void)0)
struct LocalWorldContent {};
struct LocalRealmPlayer {
 uint64_t guid=1, attackTarget=0; uint32_t mapId=0, instanceId=0, corpseMapId=0, corpseInstanceId=0;
 bool dead=false,ghost=false,corpseValid=false; uint32_t health=100,level=1;
 float x=10,y=20,z=30,orientation=1,corpseX=111,corpseY=222,corpseZ=333,corpseOrientation=2;
 std::array<uint32_t,19> equipment{};
};
struct Character {
 uint64_t guid; uint8_t race=1,gender=0,facialFeatures=0; uint32_t appearanceBytes=0; std::string name="test";
 struct Item {uint32_t displayModel=0; uint8_t inventoryType=0;}; std::array<Item,19> equipment{};
 float x,y,z;
};
enum class LocalEquipmentSource {AuthoritySnapshot};
Character localCharacterVisual(const LocalRealmPlayer& s,const LocalWorldContent*,LocalEquipmentSource) {
 Character c{};c.guid=s.guid;c.x=s.x;c.y=s.y;c.z=s.z;return c;
}
struct LocalUnitPresentationState {uint32_t health,level;uint64_t attack;bool dead;};
int localUnitPresentationEvents(const LocalUnitPresentationState*,const LocalUnitPresentationState&){return 0;}
struct Manager {std::set<uint64_t> entities; bool getEntity(uint64_t g){return entities.count(g);} void removeEntity(uint64_t g){entities.erase(g);}};
struct Controller {Manager manager;Manager& getEntityManager(){return manager;}};
struct Spell {void removeUnitAuraCache(uint64_t){}};
struct GameHandler {
 bool localExploration_=true,releasedSpirit_=false,playerDead_=false,corpsePositionValid_=false;
 uint64_t playerGuid=1,localCorpseVisualGuid_=0,corpseGuid_=0,corpseReclaimAvailableMs_=0;
 uint32_t currentMapId_=0,corpseMapId_=0; float corpseX_=0,corpseY_=0,corpseZ_=0;
 std::set<uint64_t> localGhostUnits_;
 std::map<uint64_t,std::array<uint32_t,19>> localEquipmentVisuals_;
 std::map<uint64_t,int> localFormVisuals_;
 std::map<uint64_t,LocalUnitPresentationState> localPresentationStates_;
 Controller controller;Controller* entityController_=&controller;Spell* spellHandler_=nullptr;
 std::function<void(bool)> ghostStateCallback_;
 std::function<void(uint64_t)> playerDespawnCallback_;
 std::function<void(uint64_t,uint32_t,uint8_t,uint8_t,uint32_t,uint8_t,float,float,float,float)> playerSpawnCallback_;
 std::vector<Character> spawns;std::vector<std::string> events;
 void fireAddonEvent(const char* e,std::initializer_list<int>){events.emplace_back(e);}
 bool syncLocalRealmPlayer(const LocalRealmPlayer&,const LocalWorldContent&);
 void removeLocalExplorationPlayer(uint64_t);
 void syncLocalExplorationPlayer(const Character& character,float yaw) {
  auto& manager=controller.manager;bool fresh=!manager.getEntity(character.guid);
  manager.entities.insert(character.guid);uint32_t displayId=1;
  struct {float x,y,z;} p{character.x,character.y,character.z};
'''
suffix = r'''
  spawns.push_back(character);
 }
};
'''
tests = r'''
int main(){
 const LocalWorldContent c;
 GameHandler h;LocalRealmPlayer p; p.dead=true;p.health=0;p.corpseValid=true;
 h.syncLocalRealmPlayer(p,c);assert(!h.localCorpseVisualGuid_); // corpse not duplicated before release
 p.ghost=true; h.syncLocalRealmPlayer(p,c);
 const uint64_t body=h.localCorpseVisualGuid_;assert(body!=p.guid && body);
 assert(h.controller.manager.getEntity(body));assert(h.localGhostUnits_.count(p.guid));
 assert(!h.localGhostUnits_.count(body));assert(h.localPresentationStates_.at(body).dead);
 assert(h.localPresentationStates_.at(body).health==0);
 assert(h.spawns.size()==3); // main, recursive body, main: bounded depth
 const auto bodySpawn=h.spawns[1];
 assert(bodySpawn.x==111 && bodySpawn.y==222 && bodySpawn.z==333); // source SERVER coordinates, no swap here
 h.syncLocalRealmPlayer(p,c);assert(h.spawns.size()==4); // unchanged corpse not copied again
 p.ghost=false;p.dead=false;p.health=50;p.corpseValid=false;
 h.syncLocalRealmPlayer(p,c);assert(!h.controller.manager.getEntity(body));
 assert(!h.localCorpseVisualGuid_ && !h.corpseGuid_ && !h.localGhostUnits_.count(p.guid));
 p.dead=p.ghost=p.corpseValid=true;p.health=0;p.corpseMapId=1;
 h.syncLocalRealmPlayer(p,c);assert(!h.localCorpseVisualGuid_); // corpse on another map
 p.corpseMapId=0;p.corpseInstanceId=3;h.syncLocalRealmPlayer(p,c);assert(!h.localCorpseVisualGuid_);
 p.mapId=1;auto before=h.spawns.size();h.syncLocalRealmPlayer(p,c);assert(h.spawns.size()==before); // main map filter
 // Throw from actual extracted fresh-spawn callback, then retry same naked body.
 GameHandler retry;p.mapId=p.corpseMapId=p.corpseInstanceId=0;
 unsigned attempts=0;
 retry.playerSpawnCallback_=[&](auto...){if(++attempts==1)throw std::bad_alloc();};
 bool threw=false;try{retry.syncLocalRealmPlayer(p,c);}catch(const std::bad_alloc&){threw=true;}
 assert(threw && !retry.controller.manager.getEntity(body));
 assert(!retry.localPresentationStates_.count(body)); // baseline commits only after successful creation
 retry.syncLocalRealmPlayer(p,c);assert(attempts==2 && retry.controller.manager.getEntity(body));
 // Even a stale preexisting presentation record cannot suppress missing-entity retry.
 retry.controller.manager.removeEntity(body);retry.syncLocalRealmPlayer(p,c);
 assert(attempts==3 && retry.controller.manager.getEntity(body));
 std::cout<<"PASS production-extracted ghost/corpse transitions, map/instance filters, server coordinates, recursion bound, reclaim cleanup, spawn failure rollback and retry\n";
}
'''
with tempfile.TemporaryDirectory() as d:
    cpp=Path(d)/'test.cpp';exe=Path(d)/'test'
    cpp.write_text(preamble+rollback+suffix+remove+sync+tests)
    subprocess.run(['c++','-std=c++17','-O1','-Wall','-Wextra',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
