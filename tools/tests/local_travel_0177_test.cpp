#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <iostream>
#include "local_party_instances_fixture.hpp"
using namespace wowee::game;
namespace net=wowee::net;
static sockaddr_in loopback(uint16_t port){sockaddr_in a{};initAddress(a);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);return a;}
static std::vector<std::vector<uint8_t>> drain(socket_t socket){
    std::vector<std::vector<uint8_t>> packets;
    for(;;){std::array<uint8_t,MaxPacket+1> buffer{};const auto n=::recvfrom(socket,reinterpret_cast<char*>(buffer.data()),buffer.size(),net::datagramFlags(),nullptr,nullptr);
        if(n<0){assert(net::isWouldBlock(net::lastError()));break;}assert(size_t(n)<=MaxPacket);packets.emplace_back(buffer.begin(),buffer.begin()+n);}
    return packets;
}
static void deliver(LocalRealm::Impl& guest,const std::vector<uint8_t>& bytes,uint64_t wrongToken=0){
    Reader r(bytes.data(),bytes.size());assert(r.u32()==WireMagic && r.u8()==Version);const auto type=Message(r.u8());assert(r.u16()==bytes.size());
    const auto seq=r.u32();const auto token=r.u64();guest.handleClient(type,r,guest.host,wrongToken?wrongToken:token,seq);
}

static LocalRealmNpc scopedNpc(uint32_t id,uint32_t map,uint32_t instance) {
    auto n=rewardNpc(0xf130000000000000ULL|(uint64_t(instance)<<32)|id);n.mapId=map;n.instanceId=instance;return n;
}
static Writer worldPage(uint32_t tick,const LocalRealmPlayer& p,uint8_t part,uint8_t parts,const std::vector<LocalRealmNpc>& npcs) {
    Writer w;w.u32(tick);w.u8(part);w.u8(parts);w.u8(uint8_t(npcs.size()));w.u32(p.mapId);w.u32(p.instanceId);w.u32(p.positionRevision);
    for(const auto& n:npcs)writeNpc(w,n);return w;
}
static void feed(LocalRealm::Impl& g,const Writer& w) {Reader r(w.bytes.data(),w.bytes.size());g.receiveWorld(r);}
static LocalTravelNetwork boatNetwork(){
    LocalTravelNetwork net;std::string error;
    assert(net.setClientData({{1,0,0,0,0,"Dock A"},{2,0,50,0,0,"Dock B"}},{{1,1,2,0}},
        {{1,0,0,0,0,0},{1,1,0,50,0,0},{1,2,0,50,50,0},{1,3,0,0,50,0}},error));
    assert(net.setTransportRoutes({{900001,1,1,"Test boat"}})==1);return net;
}
static void transportCases(){
    LocalGameplay game;game.useContent(rewardContent());auto network=boatNetwork();game.useTravelNetwork(network);game.setTransportTime(0);assert(game.transports().size()==1);
    auto p=rewardPlayer(1);const auto hull=game.transports()[0];p.mapId=hull.mapId;p.x=hull.x+2;p.y=hull.y;p.z=hull.z;
    std::string result;LocalRealmCommand board{LocalAction::BoardTransport,0,900001},leave{LocalAction::LeaveTransport};
    auto call=[&](LocalRealmCommand c){return game.execute(p,c,{&p},result);};
    p.castingSpellId=1;p.castRemainingMs=500;p.mountSpellId=123;p.falling=true;p.fallStartZ=1000;p.movementState=kLocalMovementFalling;
    auto malformed=board;malformed.target=1;assert(!call(malformed) && p.castingSpellId==1);
    auto rev=p.positionRevision;assert(call(board) && p.transportEntry==900001 && p.positionRevision==rev+1 && !p.castingSpellId && !p.mountSpellId && !p.falling);
    assert(!call(board));auto wrong=leave;wrong.id=1;assert(!call(wrong) && p.transportEntry==900001);
    // Force a map seam in the passenger state; sampling the real route returns
    // it to the hull map while retaining its local deck offset.
    p.mapId=1;p.castingSpellId=1;p.falling=true;p.fallStartZ=1000;p.movementState=kLocalMovementFalling;
    assert(game.tick(0,{&p}));assert(p.mapId==0 && p.transportEntry==900001 && !p.falling && !p.castingSpellId && p.fallRevision==p.positionRevision && p.fallStartZ==p.z);
    for(int i=0;i<12;++i){assert(call(leave) && !p.transportEntry && p.transportLastYaw==0 && p.transportOffsetX==0);assert(call(board));game.advanceTransportTime(1);game.tick(0,{&p});assert(std::isfinite(p.x) && std::isfinite(p.y) && p.transportEntry==900001);}
    assert(network.setTransportRoutes({})==0);game.useTravelNetwork(network);game.setTransportTime(20);p.falling=true;p.castingSpellId=1;assert(game.tick(0,{&p}));assert(!p.transportEntry && !p.transportOffsetX && !p.transportLastYaw && !p.falling && !p.castingSpellId);
    std::cout<<"PASS transport transitions: malformed/repeated requests, cast/mount/fall reset, map seam retains attachment, 12 board/leave cycles, vanished route cleanup\n";
    auto inn=rewardNpc();inn.hostile=false;inn.innkeeper=true;inn.x=p.x;inn.y=p.y;inn.z=p.z;game.setRemoteNpcs({inn});
    LocalRealmCommand bind{LocalAction::SetHome};bind.serviceNpcGuid=inn.guid;p.flight.active=true;assert(!call(bind) && !p.hasHome);p.flight={};p.transportEntry=900001;assert(!call(bind) && !p.hasHome);p.transportEntry=0;assert(call(bind) && p.hasHome);
    p.instanceId=7;p.hasInstanceReturn=true;p.returnMapId=0;p.returnInstanceId=0;p.returnX=12;p.returnY=13;p.returnZ=14;p.dead=true;p.health=0;p.deadTimer=4;assert(call({LocalAction::Respawn}));assert(p.x==12 && p.z==14 && !p.instanceId && result=="Revived at the instance entrance");
    std::cout<<"PASS inn/revival: binding rejected while flying/aboard, stationary binding succeeds, instance revival returns to entrance with correct status\n";
    char temp[]="/tmp/wowps-transport-0177-XXXXXX";auto* dir=mkdtemp(temp);assert(dir);LocalRealm host,guest;auto& h=*host.impl_;auto& g=*guest.impl_;
    h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=dir;h.self=rewardPlayer(1);g.self=rewardPlayer(2);
    auto net=boatNetwork();h.gameplay.useContent(rewardContent());g.gameplay.useContent(rewardContent());h.gameplay.useTravelNetwork(net);g.gameplay.useTravelNetwork(net);h.gameplay.setTransportTime(0);g.gameplay.setTransportTime(0);
    auto boat=h.gameplay.transports()[0];for(auto* player:{&h.self,&g.self}){player->x=boat.x;player->y=boat.y;player->z=boat.z;player->castingSpellId=1;player->castRemainingMs=500;player->falling=true;}
    h.saved={{{1,11},h.self},{{2,22},g.self}};assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.session=g.session;peer.address=loopback(g.port);peer.loading=false;h.peers.push_back(peer);h.refreshPlayers();g.players=h.players;
    auto receive=[&]{for(const auto& bytes:drain(g.socket))deliver(g,bytes);};h.history(h.peers[0]);receive();
    auto command=[&](LocalRealmCommand c){assert(guest.command(c));guest.update(.21f);h.receive();receive();assert(g.pendingCommands.empty());};auto& member=h.saved[1].player;const auto good=h.directory;
    h.directory+="/missing/board";auto before=member;command(board);assert(!h.peers[0].lastCommandSuccess && !member.transportEntry && member.castingSpellId==before.castingSpellId && member.positionRevision==before.positionRevision);assert(!host.command(board) && !h.self.transportEntry);
    h.directory=good;command(board);assert(h.peers[0].lastCommandSuccess && member.transportEntry==900001 && g.self.transportEntry==900001);assert(host.command(board));
    LocalRealm::Impl loaded,scanner;loaded.gameplay.useContent(rewardContent());assert(loaded.parseSave(good+"/realm.wprs") && scanner.parseSave(good+"/realm.wprs"));assert(loaded.findSaved(1)->player.transportEntry==900001 && loaded.findSaved(2)->player.transportEntry==900001);
    h.directory+="/missing/leave";auto revision=member.positionRevision;command(leave);assert(member.transportEntry==900001 && member.positionRevision==revision);assert(!host.command(leave) && h.self.transportEntry==900001);
    h.directory=good;command(leave);assert(!member.transportEntry && !g.self.transportEntry);assert(host.command(leave));revision=member.positionRevision;
    Writer retry;retry.u32(h.peers[0].lastCommand);retry.u8(uint8_t(LocalAction::LeaveTransport));retry.u64(0);retry.u32(0);retry.u32(0);retry.u32(0);retry.u32(0);retry.u64(0);g.send(Message::Command,g.session,retry,g.host);h.receive();receive();assert(member.positionRevision==revision);
    assert(loaded.parseSave(good+"/realm.wprs") && !loaded.findSaved(2)->player.transportEntry && loaded.findSaved(2)->player.transportLastYaw==0);
    host.stop();guest.stop();std::filesystem::remove_all(dir);
    std::cout<<"PASS transport LAN/save: host/guest board and leave save-before-ack rollback, owner replication, boarding/detached save13 and catalog-free reload, duplicate departure inert\n";
}
int main(int argc,char** argv) {
    transportCases();
    assert(argc==2);std::string result;
    LocalGameplay game;instanceContent(game,argv[1]);auto p=rewardPlayer(1);entrance(p);
    p.flight.active=true;assert(!game.execute(p,{LocalAction::EnterPortal,0,45},{&p},result));assert(game.instances().empty() && p.flight.active);
    p.flight={};p.transportEntry=99;assert(!game.execute(p,{LocalAction::EnterPortal,0,45},{&p},result));assert(game.instances().empty() && p.transportEntry==99);
    p.transportEntry=0;p.castingSpellId=1;p.castTarget=10;p.castRemainingMs=500;p.castTotalMs=1000;p.mountSpellId=123;p.movementState=kLocalMovementFalling;p.falling=true;p.fallStartZ=2000;
    assert(game.execute(p,{LocalAction::EnterPortal,0,45},{&p},result));
    assert(!p.castingSpellId && !p.mountSpellId && !p.movementState && !p.falling && p.fallRevision==p.positionRevision && p.fallStartZ==p.z);
    p.returnInstanceId=999;assert(!game.execute(p,{LocalAction::LeaveInstance},{&p},result));
    p.gameplayInitialized=false;assert(!game.validatePlayer(p,result));p.returnInstanceId=p.instanceId;assert(!game.validatePlayer(p,result));
    p.returnInstanceId=0;assert(game.validatePlayer(p,result));p.gameplayInitialized=true;
    assert(!game.execute(p,{LocalAction::LeaveInstance,5},{&p},result));
    p.castingSpellId=1;p.mountSpellId=123;p.falling=true;assert(game.execute(p,{LocalAction::LeaveInstance},{&p},result));assert(!p.castingSpellId && !p.mountSpellId && !p.falling);
    std::cout<<"PASS travel authority: taxi/transport entry rejected without allocations; cast/mount/fall cleanup; invalid return and exit arguments rejected\n";
    // Spatial revision and vitals use separate streams; they cannot split map and instance.
    LocalRealm guestRealm;auto& g=*guestRealm.impl_;g.state=LocalRealmState::Connected;g.gameplay.useContent(rewardContent());
    g.self=rewardPlayer(2);g.self.mapId=189;g.self.instanceId=7;g.self.positionRevision=10;g.self.x=20;g.self.health=20;
    g.self.hasInstanceReturn=true;g.self.returnMapId=0;g.self.returnX=14;
    auto stale=g.self;stale.mapId=0;stale.instanceId=0;stale.positionRevision=9;stale.money=77;stale.returnX=999;stale.transportEntry=42;stale.castingSpellId=1;
    g.applyProgress(stale,100);assert(g.self.mapId==189 && g.self.instanceId==7 && g.self.x==20 && g.self.money==77 && g.self.returnX==14 && !g.self.transportEntry && !g.self.castingSpellId);
    g.vitalsSequence=200;g.self.health=20;auto newer=g.self;newer.mapId=36;newer.instanceId=8;newer.positionRevision=11;newer.health=99;
    g.gameplay.setRemoteNpcs({scopedNpc(1,189,7)});g.applyProgress(newer,150);
    assert(g.self.mapId==36 && g.self.instanceId==8 && g.self.health==20 && g.gameplay.npcs().empty());
    auto equal=g.self;equal.instanceId=9;equal.x=999;g.applyProgress(equal,201);assert(g.self.instanceId==8 && g.self.x!=999);
    g.gameplay.setRemoteNpcs({scopedNpc(1,36,8)});auto relocation=g.self;relocation.mapId=189;relocation.instanceId=7;relocation.positionRevision=12;
    g.commitPlayers({relocation},202);assert(g.gameplay.npcs().empty() && g.self.instanceId==7);
    // Old/future/foreign pages, including empty pages, cannot replace the current view.
    auto n=scopedNpc(1,189,7);feed(g,worldPage(1,g.self,0,1,{n}));assert(g.gameplay.npcs().size()==1);
    auto oldScope=g.self;oldScope.positionRevision=11;feed(g,worldPage(50,oldScope,0,1,{}));assert(g.worldSequence==1 && g.gameplay.npcs().size()==1);
    auto future=g.self;future.positionRevision=13;feed(g,worldPage(51,future,0,1,{}));assert(g.worldSequence==1);
    feed(g,worldPage(52,g.self,0,1,{scopedNpc(2,189,8)}));assert(g.worldSequence==1);
    auto empty=worldPage(53,g.self,0,1,{});empty.bytes.pop_back();feed(g,empty);assert(g.worldSequence==1);
    // Ten reordered pages must contain <=128 actors. A duplicate page cannot replace accepted data.
    std::vector<LocalRealmNpc> all;for(uint32_t i=1;i<=128;++i)all.push_back(scopedNpc(i,189,7));
    auto page=[&](uint32_t tick,size_t part){const auto begin=part*14,end=std::min(begin+14,all.size());return worldPage(tick,g.self,uint8_t(part),10,{all.begin()+begin,all.begin()+end});};
    feed(g,page(60,0));auto duplicate=std::vector<LocalRealmNpc>(all.begin(),all.begin()+14);duplicate[0].health=1;
    feed(g,worldPage(60,g.self,0,10,duplicate));
    for(size_t i=9;i>0;--i)feed(g,page(60,i));assert(g.worldSequence==60 && g.gameplay.npcs().size()==128 && g.gameplay.npcs()[0].health==10);
    for(size_t i=0;i<9;++i)feed(g,page(61,i));
    feed(g,worldPage(61,g.self,9,10,duplicate));assert(g.worldSequence==60); // Would exceed 128.
    feed(g,page(61,9));assert(g.worldSequence==61 && g.gameplay.npcs().size()==128);
    auto shortPage=std::vector<LocalRealmNpc>(all.begin(),all.begin()+13);feed(g,worldPage(62,g.self,0,2,shortPage));assert(g.worldSequence==61);
    auto next=g.self;++next.positionRevision;g.applyProgress(next,203);assert(g.gameplay.npcs().empty());
    feed(g,worldPage(63,relocation,0,1,{}));assert(g.worldSequence==61);feed(g,worldPage(64,g.self,0,1,{n}));assert(g.worldSequence==64);
    std::cout<<"PASS travel replication: map/instance coherence across progress/vitals streams; obsolete return/transport/cast isolation; public relocation clears view; scoped old/future/empty/foreign/truncated rejection; reordered 128-actor assembly; duplicate/oversized/short-page guards\n";
    // Real UDP and real disk: bind/return save before ack, including rollback and retry.
    char temp[]="/tmp/wowps-travel-0170-XXXXXX";auto* directory=mkdtemp(temp);assert(directory);
    LocalRealm hostRealm;auto& h=*hostRealm.impl_;h.state=LocalRealmState::Hosting;h.directory=directory;h.realmId=g.realmId=123;
    h.self=rewardPlayer(1);g.self=rewardPlayer(2);entrance(h.self);entrance(g.self);instanceContent(h.gameplay,argv[1]);instanceContent(g.gameplay,argv[1]);
    h.saved={{{1,11},h.self},{{2,22},g.self}};assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;
    g.progressSequence=g.vitalsSequence=g.incomingSequence=0;g.worldSequence=g.collectingWorld=0;
    LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);
    h.refreshPlayers();g.players=h.players;auto& member=h.saved[1].player;
    auto receive=[&]{for(const auto& packet:drain(g.socket)){assert(packet.size()<=MaxPacket);deliver(g,packet);}};
    h.history(h.peers[0]);receive();
    auto command=[&](LocalRealmCommand cmd){assert(guestRealm.command(cmd));guestRealm.update(.21f);h.receive();receive();assert(g.pendingCommands.empty());};
    // Even an uninstanced player cannot change continents via a movement report.
    auto incoming=member;incoming.mapId=1;incoming.x=99;Writer move;writePosition(move,incoming);move.u32(member.positionRevision);move.u32(0);move.u8(0);move.u8(0);move.u32(0);move.f32(0);move.f32(0);move.f32(0);
    g.send(Message::Position,g.session,move,g.host);h.receive();assert(member.mapId==0 && member.x==0);
    auto inn=rewardNpc();inn.hostile=false;inn.innkeeper=true;inn.x=3;inn.orientation=1;h.gameplay.setRemoteNpcs({inn});
    const auto savedDirectory=h.directory;h.directory+="/missing/bind";command({LocalAction::SetHome});assert(!member.hasHome && !g.self.hasHome);
    h.directory=savedDirectory;command({LocalAction::SetHome});assert(member.hasHome && member.homeX==3 && g.self.hasHome);
    // Successful guest return reaches the inn; failure keeps position, cast, mount and cooldown.
    member.x=30;member.castingSpellId=1;member.mountSpellId=123;member.falling=true;g.self=member;
    h.directory+="/missing/hearth";command({LocalAction::ReturnHome});assert(member.x==30 && member.castingSpellId==1 && member.mountSpellId==123 && member.hearthCooldown==0);
    h.directory=savedDirectory;command({LocalAction::ReturnHome});assert(member.x==3 && !member.castingSpellId && !member.mountSpellId && !member.falling && member.hearthCooldown>0 && g.self.x==3);
    const auto committed=member.positionRevision;Writer replay;replay.u32(h.peers[0].lastCommand);replay.u8(uint8_t(LocalAction::ReturnHome));replay.u64(0);replay.u32(0);replay.u32(0);replay.u32(0);replay.u32(0);replay.u64(0);
    g.send(Message::Command,g.session,replay,g.host);h.receive();receive();assert(member.positionRevision==committed);
    // Host binds and returns through the same transactional dispatcher.
    h.directory+="/missing/host-bind";assert(!hostRealm.command({LocalAction::SetHome}));assert(!h.self.hasHome);
    h.directory=savedDirectory;assert(hostRealm.command({LocalAction::SetHome}));h.self.x=30;h.self.castingSpellId=1;h.self.mountSpellId=123;
    h.directory+="/missing/host-hearth";assert(!hostRealm.returnHome());assert(h.self.x==30 && h.self.castingSpellId==1 && h.self.mountSpellId==123 && h.self.hearthCooldown==0);
    h.directory=savedDirectory;assert(hostRealm.returnHome());assert(h.self.x==3 && !h.self.castingSpellId && !h.self.mountSpellId);
    LocalRealm::Impl loaded;instanceContent(loaded.gameplay,argv[1]);assert(loaded.parseSave(h.directory+"/realm.wprs"));
    assert(loaded.findSaved(1)->player.homeX==3 && loaded.findSaved(1)->player.x==3 && loaded.findSaved(1)->player.hearthCooldown>0);
    assert(loaded.findSaved(2)->player.homeX==3 && loaded.findSaved(2)->player.x==3 && loaded.findSaved(2)->player.hearthCooldown>0);
    // Server-generated pages carry the same context and fit the datagram budget.
    for(auto& actor:all){actor.instanceId=0;actor.mapId=0;actor.guid=0xf130000000000000ULL|uint32_t(actor.guid);}
    h.gameplay.setRemoteNpcs(all);h.world(h.peers[0],100);receive();assert(g.gameplay.npcs().size()==128);
    hostRealm.stop();guestRealm.stop();std::filesystem::remove_all(directory);
    std::cout<<"PASS travel LAN/save: continent movement refusal; host/guest bind and hearth rollback/retry/replication; command replay; save13 inn/location/cooldown reload; real 128-NPC paged UDP roundtrip within 1400 bytes\n";
}
