#include "game/local_ui_changes.hpp"
#include <cassert>
#include <cstdio>
using namespace wowee::game;
int main(){
 LocalRealmPlayer p;LocalUiChanges changes;changes.observe(p,0,0,0);
 assert(changes.observe(p,0,0,0)==0);
 p.dead=true;p.health=0;p.corpseValid=true;p.corpseMapId=p.mapId;p.corpseInstanceId=p.instanceId;
 p.corpseX=p.x;p.corpseY=p.y;p.corpseZ=p.z;
 assert(changes.observe(p,0,0,0)&LocalUiChanges::Life);
 // Release changes neither dead nor health; ghost alone must republish.
 p.ghost=true;p.x+=100;
 assert(changes.observe(p,0,0,0)==LocalUiChanges::Life);
 assert(changes.observe(p,0,0,0)==0);
 p.x=p.corpseX+5;
 assert(changes.observe(p,0,0,0)==LocalUiChanges::Life);
 p.x=p.corpseX+4;assert(changes.observe(p,0,0,0)==0);
 p.instanceId=p.corpseInstanceId+1;assert(changes.observe(p,0,0,0)==LocalUiChanges::Life);
 p.instanceId=p.corpseInstanceId;assert(changes.observe(p,0,0,0)==LocalUiChanges::Life);
 p.dead=p.ghost=p.corpseValid=false;p.health=1;
 assert(changes.observe(p,0,0,0)==LocalUiChanges::Life);assert(changes.observe(p,0,0,0)==0);
 puts("PASS UI change gating republishes death, ghost release, reclaim-range/instance changes and revival; steady movement inside range stays quiet");
}
