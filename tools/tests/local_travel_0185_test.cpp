#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include "local_group_rewards_fixture.hpp"
#include <cassert>
#include <iostream>
using namespace wowee::game;
namespace net=wowee::net;
static LocalRealmPlayer player(uint64_t id){auto p=rewardPlayer(id);p.level=40;p.money=600000;p.quests.clear();return p;}
static LocalRealmNpc teacher(){auto n=rewardNpc(10);n.hostile=false;n.professionTrainer=true;n.trainerSkill=762;n.flightMaster=true;n.taxiNodeId=1;n.x=0;return n;}
static LocalTravelNetwork network(){LocalTravelNetwork n;std::string e;assert(n.setClientData({{1,0,0,0,0,"Origin",1,1},{2,0,100,0,0,"Destination",1,1},{3,0,200,0,0,"Enemy",0,1}},{{1,1,2,25},{2,1,3,99}},{{1,0,0,0,0,0},{1,1,0,100,0,0},{2,0,0,0,0,0},{2,1,0,200,0,0}},e));return n;}
static sockaddr_in loopback(uint16_t p){sockaddr_in a{};initAddress(a);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(p);return a;}
int main(){
 auto c=rewardContent();c->quests.clear();LocalSpellDefinition mount;mount.id=900001;mount.name="Test ground mount";mount.mountDisplayId=1;mount.mountSpeedPercent=60;mount.baseLevel=20;c->spells.push_back(mount);std::sort(c->spells.begin(),c->spells.end(),[](auto&a,auto&b){return a.id<b.id;});
 LocalGameplay game;game.useContent(c);game.useTravelNetwork(network());game.setRemoteNpcs({teacher()});auto p=player(1);std::string result;
 auto run=[&](LocalRealmCommand cmd){return game.execute(p,cmd,{&p},result);};LocalRealmCommand train{LocalAction::TrainRiding,0,75};train.serviceNpcGuid=10;
 p.level=19;assert(!run(train) && p.money==600000);p.level=40;auto wrong=train;wrong.serviceNpcGuid=11;assert(!run(wrong));wrong=train;wrong.id=150;assert(!run(wrong));
 p.knownSpells.push_back(mount.id);assert(!run({LocalAction::CastSpell,p.guid,mount.id}));assert(run(train) && p.ridingSkill==75 && p.money==560000);assert(!run(train));assert(run({LocalAction::CastSpell,p.guid,mount.id}));p.mountSpellId=0;p.castingSpellId=0;p.globalCooldownMs=0;
 train.id=150;p.attackTarget=99;assert(!run(train));p.attackTarget=0;assert(run(train) && p.ridingSkill==150 && p.money==60000);
 p.migrateLegacyRiding=true;p.ridingSkill=0;game.initializePlayer(p,false);assert(p.ridingSkill==75 && !p.migrateLegacyRiding);
 std::cout<<"PASS riding: level, selected teacher, preceding rank, exact cost, duplicate/combat rejection, summon requirement and legacy learned mount migration\n";
 p=player(1);LocalRealmCommand discover{LocalAction::DiscoverTaxi};discover.serviceNpcGuid=10;assert(run(discover) && p.knownTaxiNodes==std::vector<uint32_t>{1});assert(!run(discover));
 LocalRealmCommand fly{LocalAction::TakeFlight,0,2};fly.serviceNpcGuid=10;assert(!run(fly));p.knownTaxiNodes.push_back(2);p.money=24;assert(!run(fly) && !p.flight.active && p.money==24);p.money=100;p.knownTaxiNodes.push_back(3);auto enemy=fly;enemy.id=3;assert(!run(enemy));
 p.mountSpellId=1;p.falling=true;p.movementState=kLocalMovementFalling;assert(run(fly) && p.money==75 && p.flight.active && !p.mountSpellId && !p.falling);assert(!run(fly) && p.money==75);
 game.tick(.25f,{&p});assert(p.flight.travelled==8 && p.x==8);
 std::cout<<"PASS flight purchase/discovery: explicit known nodes, faction, insufficient funds, no duplicate fare, travel-state cleanup and route advance\n";
 Writer saved;writeProgress(saved,p);Reader rr(saved.bytes.data(),saved.bytes.size());auto restored=player(1);assert(readProgress(rr,restored) && rr.done() && restored.flight.active && restored.flight.travelled==8 && restored.knownTaxiNodes==p.knownTaxiNodes);
 restored.x=p.x;LocalGameplay resume;resume.useContent(c);resume.initializePlayer(restored,false);resume.tick(.25f,{&restored});assert(restored.flight.active && restored.x==8);resume.useTravelNetwork(network());resume.tick(.25f,{&restored});assert(restored.x==16 && restored.money==75);
 for(int i=0;i<20 && restored.flight.active;++i)resume.tick(.25f,{&restored});assert(!restored.flight.active && restored.x==100 && restored.money==75 && !restored.falling);
 auto invalid=p;invalid.flight.totalLength+=10;game.tick(0,{&invalid});assert(!invalid.flight.active && invalid.x==0 && invalid.mapId==0);
 invalid=p;invalid.dead=true;invalid.health=0;game.tick(.25f,{&invalid});assert(!invalid.flight.active && invalid.dead);
 invalid=p;invalid.flight.speed=999;Writer bad;writeProgress(bad,invalid);Reader br(bad.bytes.data(),bad.bytes.size());assert(!readProgress(br,restored));
 invalid=p;invalid.knownTaxiNodes.push_back(1);Writer duplicate;writeProgress(duplicate,invalid);Reader dr(duplicate.bytes.data(),duplicate.bytes.size());assert(!readProgress(dr,restored));
 std::cout<<"PASS flight recovery: Save16 in-flight roundtrip, wait for DBC, resume without second fare, exact landing, changed-route return, death and malformed travel rejection\n";
 char temp[]="/tmp/wowps-travel-0185-XXXXXX";auto* dir=mkdtemp(temp);assert(dir);LocalRealm host,guest;auto& h=*host.impl_;auto& g=*guest.impl_;h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=dir;h.self=player(1);g.self=player(2);h.gameplay.useContent(c);g.gameplay.useContent(c);h.gameplay.useTravelNetwork(network());g.gameplay.useTravelNetwork(network());h.gameplay.setRemoteNpcs({teacher()});h.saved={{{1,11},h.self},{{2,22},g.self}};h.refreshPlayers();g.players=h.players;
 assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);auto& member=h.saved[1].player;
 auto receive=[&]{for(;;){std::array<uint8_t,MaxPacket+1> bytes{};auto n=::recvfrom(g.socket,reinterpret_cast<char*>(bytes.data()),bytes.size(),net::datagramFlags(),nullptr,nullptr);if(n<0){assert(net::isWouldBlock(net::lastError()));break;}Reader r(bytes.data(),n);assert(r.u32()==WireMagic && r.u8()==Version);auto type=Message(r.u8());assert(r.u16()==n);auto seq=r.u32();auto token=r.u64();g.handleClient(type,r,g.host,token,seq);}};
 auto settle=[&]{guest.update(.21f);h.receive();h.history(h.peers[0]);receive();assert(g.pendingCommands.empty());};const auto good=h.directory;
 h.directory+="/missing";assert(guest.trainRiding(75,10));settle();assert(!member.ridingSkill && member.money==600000);h.directory=good;assert(guest.trainRiding(75,10));settle();assert(member.ridingSkill==75 && g.self.ridingSkill==75 && member.money==560000);
 h.directory+="/missing";assert(guest.discoverTaxi(10));settle();assert(member.knownTaxiNodes.empty());h.directory=good;assert(guest.discoverTaxi(10));settle();assert(member.knownTaxiNodes==g.self.knownTaxiNodes && member.knownTaxiNodes.size()==1);
 member.knownTaxiNodes.push_back(2);g.self=member;h.directory+="/missing";assert(guest.takeFlight(2));settle();assert(!member.flight.active && member.money==560000);h.directory=good;assert(guest.takeFlight(2));settle();assert(member.flight.active && g.self.flight.active && member.money==559975);
 Writer replay;replay.u32(h.peers[0].lastCommand);replay.u8(uint8_t(LocalAction::TakeFlight));replay.u64(0);replay.u32(3);replay.u32(0);replay.u32(0);replay.u32(0);replay.u64(10);g.send(Message::Command,g.session,replay,g.host);h.receive();receive();assert(member.money==559975 && member.flight.destinationNode==2);
 auto fake=member;fake.x=999;Writer move;writePosition(move,fake);move.u32(member.positionRevision);move.u32(member.instanceId);move.u8(0);move.u8(0);move.u32(0);move.f32(0);move.f32(0);move.f32(0);g.send(Message::Position,g.session,move,g.host);h.receive();assert(member.x==0 && member.flight.active);
 LocalRealm::Impl loaded;loaded.gameplay.useContent(c);loaded.gameplay.useTravelNetwork(network());assert(loaded.parseSave(good+"/realm.wprs"));assert(loaded.findSaved(2)->player.flight.active && loaded.findSaved(2)->player.ridingSkill==75 && loaded.findSaved(2)->player.money==559975);h.resetSavedSession(2);assert(member.flight.active);h.gameplay.tick(.25f,{&member});assert(member.x==8);
 std::cout<<"PASS production UDP/save: training/discovery/flight rollback before ack, owner replication, altered duplicate fare ignored, flight rejects guest position, real reload and disconnect retention\n";
 // Maximum discovery list is transported in bounded owner chunks.
 member.flight={};member.knownTaxiNodes.clear();for(uint32_t i=1;i<=512;++i)member.knownTaxiNodes.push_back(i);h.progress(h.peers[0]);receive();assert(g.self.knownTaxiNodes.size()==512);
 Writer legacy;legacy.u32(SaveMagic);legacy.u8(15);legacy.u64(123);legacy.u16(1);legacy.u64(1);legacy.u64(11);auto old=player(1);old.knownSpells.push_back(mount.id);writePlayer(legacy,old);writeProgress(legacy,old,15);writeAppearance(legacy,old);legacy.u32(0);legacy.u8(0);writeBuyback(legacy,0,{});legacy.u8(0);legacy.u64(0);legacy.u16(0);legacy.u16(0);legacy.u16(0);legacy.u32(1);legacy.u32(1);legacy.u16(0);legacy.u32(checksum(legacy.bytes.data(),legacy.bytes.size()));
 assert(atomicWrite(good+"/legacy15.wprs",legacy.bytes,false));assert(loaded.parseSave(good+"/legacy15.wprs") && loaded.saved[0].player.migrateLegacyRiding);
 loaded.state=LocalRealmState::SinglePlayer;loaded.directory=good;loaded.self=loaded.saved[0].player;assert(loaded.saveRealm());
 LocalRealm::Impl catalogFree;assert(catalogFree.parseSave(good+"/realm.wprs") && catalogFree.saved[0].player.migrateLegacyRiding);
 auto migrated=catalogFree.saved[0].player;game.initializePlayer(migrated,false);assert(migrated.ridingSkill==75 && !migrated.migrateLegacyRiding);
 std::cout<<"PASS codec limits: 512 discoveries through production chunk assembly and genuine Save15 migration\n";
 host.stop();guest.stop();std::filesystem::remove_all(dir);
}
