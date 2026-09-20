// Exercise real gameplay authority plus save and LAN codecs.
#include "../../src/game/local_realm.cpp"
#include "local_party_instances_fixture.hpp"
#include <iostream>
int main(int argc,char** argv) {
 assert(argc==2);
 LocalGameplay game;game.useContent(rewardContent());std::string result;
 LocalGraveyardSite near;near.id=1;near.mapId=0;near.zoneId=2;near.x=1;
 LocalGraveyardSite wrong=near;wrong.zoneId=7;wrong.raceMask=2;wrong.x=2;
 LocalGraveyardSite right=near;right.zoneId=7;right.raceMask=1;right.x=100;
 assert(game.setGraveyards({near,wrong,right},result)); // repeated ID has different zone/team links
 auto p=rewardPlayer(1);p.race=1;p.zoneId=7;p.x=20;p.y=30;p.z=40;p.orientation=1;p.dead=true;p.health=0;
 std::vector<LocalRealmPlayer*> players{&p};
 assert(!game.execute(p,{LocalAction::Respawn},players,result));
 for(int i=0;i<11;++i)game.tick(.25f,players);
 assert(p.dead&&!p.ghost&&p.corpseValid&&p.corpseX==20&&p.corpseZoneId==7);
 game.tick(.25f,players);
 assert(p.dead&&p.ghost&&p.health==0&&p.x==100&&p.corpseX==20&&p.positionRevision==1);
 assert(!game.execute(p,{LocalAction::Respawn},players,result));
 assert(!game.execute(p,{LocalAction::ReclaimCorpse},players,result));
 for(unsigned version:{31u,32u}) {
  Writer w;writeProgress(w,p,version);Reader r(w.bytes.data(),w.bytes.size());LocalRealmPlayer restored;
  assert(readProgress(r,restored,version)&&r.done()&&restored.dead);
  assert(restored.ghost==(version==32)&&restored.corpseValid==(version==32));
  if(version==32)assert(restored.corpseX==20&&restored.corpseZoneId==7);
 }
 Writer w;writeNetworkVitals(w,p);Reader r(w.bytes.data(),w.bytes.size());LocalRealmPlayer remote;
 readNetworkVitals(r,remote);assert(r.done()&&remote.ghost&&remote.corpseZ==40);
 auto corrupt=w.bytes;corrupt[corrupt.size()-30]=2;Reader bad(corrupt.data(),corrupt.size());readNetworkVitals(bad,remote);assert(!bad.valid);
 p.x=30;p.y=30;p.z=40;assert(localCanReclaimCorpse(p));
 p.x=30.1f;assert(!localCanReclaimCorpse(p));p.x=20;
 p.z=51;assert(!localCanReclaimCorpse(p));p.z=40;
 p.mapId=1;assert(!localCanReclaimCorpse(p));p.mapId=0;
 p.instanceId=1;assert(!localCanReclaimCorpse(p));p.instanceId=0;
 LocalRealmCommand injected{LocalAction::ReclaimCorpse};injected.target=123;
 assert(!game.execute(p,injected,players,result));
 assert(game.execute(p,{LocalAction::ReclaimCorpse},players,result));
 assert(!p.dead&&!p.ghost&&!p.corpseValid&&p.health>0&&p.x==20&&p.y==30&&p.z==40&&p.positionRevision==2);
 assert(!game.execute(p,{LocalAction::ReclaimCorpse},players,result));
 // A later death creates a new body and a missing zone uses nearest eligible site.
 p.dead=true;p.health=0;p.deadTimer=3;p.x=60;p.zoneId=0;
 assert(game.execute(p,{LocalAction::Respawn},players,result));assert(p.corpseX==60&&p.x==100);
 // An instance body selects an exterior graveyard without losing its binding.
 p=rewardPlayer(2);p.race=1;p.dead=true;p.health=0;p.deadTimer=3;p.mapId=389;p.instanceId=22;
 p.hasInstanceReturn=true;p.returnMapId=0;p.returnX=90;players={&p};
 assert(game.execute(p,{LocalAction::Respawn},players,result));
 assert(p.mapId==0&&p.instanceId==0&&p.corpseMapId==389&&p.corpseInstanceId==22&&p.hasInstanceReturn);
 assert(!localCanReclaimCorpse(p));
 // Real catalog entrance must reuse the corpse binding, including after a ghost exit.
 instanceContent(game,argv[1]);p=rewardPlayer(3);players={&p};
 const auto binding=enterInstance(game,p);const auto corpseMap=p.mapId;
 p.dead=true;p.health=0;p.deadTimer=3;p.x+=20;const auto bodyX=p.x;
 assert(game.execute(p,{LocalAction::Respawn},players,result));
 p.x=p.y=p.z=0;p.portalCooldown=0;
 assert(!game.execute(p,{LocalAction::EnterPortal,0,78},players,result));
 assert(game.execute(p,{LocalAction::EnterPortal,0,45},players,result));
 assert(p.ghost&&p.mapId==corpseMap&&p.instanceId==binding&&game.instances().size()==1);
 assert(game.execute(p,{LocalAction::LeaveInstance},players,result));
 assert(p.ghost&&p.hasInstanceReturn&&p.instanceId==0);
 p.portalCooldown=0;
 assert(game.execute(p,{LocalAction::EnterPortal,0,45},players,result));
 p.x=bodyX;p.y=p.corpseY;p.z=p.corpseZ;
 assert(game.execute(p,{LocalAction::ReclaimCorpse},players,result));
 assert(!p.ghost&&!p.dead&&p.instanceId==binding&&game.instances().size()==1);
 std::cout<<"PASS: automatic release, faction/zone routing, corpse persistence, 10-yard/map/instance gates, repeat/injected reclaim rejection, instance entrance binding, save31 migration/save32 and LAN87 codecs\n";
}
