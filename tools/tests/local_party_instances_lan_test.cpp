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
int main(int argc,char** argv){
    assert(argc==2);char temp[]="/tmp/wowps-party-instances-lan-XXXXXX";const auto* directory=mkdtemp(temp);assert(directory);
    LocalRealm hostRealm,guestRealm;auto& h=*hostRealm.impl_;auto& g=*guestRealm.impl_;
    h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=directory;
    h.self=rewardPlayer(1);g.self=rewardPlayer(2);auto bystander=rewardPlayer(3);
    entrance(h.self);entrance(g.self);entrance(bystander);instanceContent(h.gameplay,argv[1]);instanceContent(g.gameplay,argv[1]);
    h.saved={{{1,11},h.self},{{2,22},g.self},{{3,33},bystander}};
    assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;
    LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);
    auto unrelated=peer;unrelated.guid=3;unrelated.identity={3,33};unrelated.session=888;h.peers.push_back(unrelated);
    h.refreshPlayers();g.players=h.players;
    auto receiveGuest=[&]{for(const auto& p:drain(g.socket))deliver(g,p);};
    auto guestCommand=[&](LocalRealmCommand cmd){assert(guestRealm.command(cmd));guestRealm.update(.21f);h.receive();receiveGuest();assert(g.pendingCommands.empty());};
    assert(hostRealm.partyCommand(LocalPartyAction::Invite,2));h.sendParty(h.peers[0]);receiveGuest();
    guestCommand({LocalAction::PartyAccept,0,g.localParty.inviteId});h.history(h.peers[0]);receiveGuest();
    assert(h.localParty.members.size()==2 && g.localParty.members.size()==2);
    auto& member=h.saved[1].player;auto& outsider=h.saved[2].player;const auto savedDirectory=h.directory;
    const auto beforeHost=h.self;
    h.directory+="/missing/host-entry";assert(!hostRealm.enterPortal(45));
    assert(h.self.instanceId==0 && h.self.x==beforeHost.x && !h.self.hasInstanceReturn && h.self.positionRevision==beforeHost.positionRevision);
    assert(h.gameplay.instances().empty() && h.saved[0].player.instanceId==0);h.directory=savedDirectory;
    // Guest first allocation rolls back both character and binding before a retry.
    h.directory+="/missing/guest-entry";guestCommand({LocalAction::EnterPortal,0,45});
    assert(member.instanceId==0 && !member.hasInstanceReturn && g.self.instanceId==0 && h.gameplay.instances().empty());
    assert(g.actionStatus.find("not saved")!=std::string::npos);h.directory=savedDirectory;
    guestCommand({LocalAction::EnterPortal,0,45});const auto instance=member.instanceId;
    assert(instance && g.self.instanceId==instance && g.self.mapId==189 && g.self.x==member.x && h.gameplay.instances().size()==1);
    // Replayed successful command cannot allocate again, move again or replace the return point.
    const auto revision=member.positionRevision;
    Writer replay;replay.u32(h.peers[0].lastCommand);replay.u8(uint8_t(LocalAction::EnterPortal));replay.u64(0);replay.u32(45);
    replay.u32(0);replay.u32(0);replay.u32(0);replay.u64(0);g.send(Message::Command,g.session,replay,g.host);h.receive();receiveGuest();
    assert(member.instanceId==instance && member.positionRevision==revision && h.gameplay.instances().size()==1);
    assert(hostRealm.enterPortal(45,true));assert(h.self.instanceId==instance);h.sendParty(h.peers[0]);receiveGuest();
    assert(g.localParty.members[0].instanceId==instance && g.localParty.members[1].instanceId==instance);
    std::string result;assert(h.runCommand(outsider,{LocalAction::EnterPortal,0,45},result));assert(outsider.instanceId!=instance);
    // Wire position reports cannot select another instance, even at the same map/coordinates.
    Writer spoof;writePosition(spoof,member);spoof.u32(member.positionRevision);spoof.u32(outsider.instanceId);spoof.u8(0);spoof.u8(0);spoof.u32(0);spoof.f32(0);spoof.f32(0);spoof.f32(0);
    g.send(Message::Position,g.session,spoof,g.host);h.receive();assert(member.instanceId==instance);
    // NPCs, kill credit and corpse money remain confined to the assigned instance.
    auto npc=rewardNpc(0xf13000000000000aULL|(uint64_t(instance)<<32));npc.mapId=189;npc.instanceId=instance;
    npc.x=npc.homeX=h.self.x;npc.y=npc.homeY=h.self.y;npc.z=npc.homeZ=h.self.z;
    rewardKill(h.gameplay,h.self,h.activePlayers(),npc);assert(h.self.xp==51 && member.xp==50 && outsider.xp==0);
    h.world(h.peers[0],++h.worldTick);receiveGuest();assert(g.gameplay.npcs().size()==1 && g.gameplay.npcs()[0].instanceId==instance);
    assert(hostRealm.command({LocalAction::Loot,npc.guid,0}));assert(h.self.money==4 && member.money==3 && outsider.money==0);
    // Atomic save12 contains both positions, both wallets and the durable owner key.
    LocalRealm::Impl loaded;instanceContent(loaded.gameplay,argv[1]);assert(loaded.parseSave(h.directory+"/realm.wprs"));
    // Startup applies the parsed directory before validating saved characters.
    assert(loaded.gameplay.restoreInstances(loaded.restoredInstances,result));
    assert(loaded.partyDirector.parties().empty() && loaded.gameplay.instances().size()==2);
    auto* savedGuest=loaded.findSaved(2);assert(savedGuest && savedGuest->player.instanceId==instance && savedGuest->player.hasInstanceReturn && savedGuest->player.money==3);
    assert(loaded.gameplay.instances()[0].groupId>>63);
    assert(loaded.gameplay.execute(savedGuest->player,{LocalAction::LeaveInstance},{&savedGuest->player},result));
    assert(!savedGuest->player.instanceId && savedGuest->player.mapId==0 && savedGuest->player.orientation==.5f);
    h.directory+="/missing/guest-exit";guestCommand({LocalAction::LeaveInstance});
    assert(member.instanceId==instance && member.hasInstanceReturn && g.self.instanceId==instance && h.gameplay.instances().size()==2);
    h.directory=savedDirectory;guestCommand({LocalAction::LeaveInstance});assert(member.instanceId==0 && g.self.instanceId==0 && !member.hasInstanceReturn && member.orientation==.5f);
    // Failed host exit also retains its saved return and binding; success commits them.
    h.directory+="/missing/host-exit";assert(!hostRealm.command({LocalAction::LeaveInstance}));assert(h.self.instanceId==instance && h.self.hasInstanceReturn);
    h.directory=savedDirectory;assert(hostRealm.command({LocalAction::LeaveInstance}));assert(!h.self.instanceId && !h.self.hasInstanceReturn);
    // Guest disconnect dissolves this pair before the host's next re-entry.
    h.peers.erase(h.peers.begin());h.syncParty(true);assert(h.localParty.members.empty());
    h.self.portalCooldown=0;assert(hostRealm.enterPortal(45));assert(h.self.instanceId==outsider.instanceId && h.self.instanceId!=instance);
    hostRealm.stop();guestRealm.stop();std::filesystem::remove_all(directory);
    std::cout<<"PASS party instances LAN/save: real UDP invite/entry/retry/replay/exit; host and guest entry/exit save rollback; positions and roster replication; foreign instance position refusal; outsider NPC/reward/copper isolation; save12 owner/position/return reload; disconnect membership pruning\n";
}
