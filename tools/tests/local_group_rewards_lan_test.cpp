#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <iostream>
#include "local_group_rewards_fixture.hpp"
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
int main(){
    constexpr uint64_t corpse=0xf13000000000000aULL;
    char temp[]="/tmp/wowps-group-rewards-lan-XXXXXX";const auto* directory=mkdtemp(temp);assert(directory);
    LocalRealm hostRealm,guestRealm;auto& h=*hostRealm.impl_;auto& g=*guestRealm.impl_;
    h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=directory;
    h.self=rewardPlayer(1);g.self=rewardPlayer(2);auto bystander=rewardPlayer(3);
    h.gameplay.useContent(rewardContent());g.gameplay.useContent(rewardContent());
    h.gameplay.tick(0,{});g.gameplay.tick(0,{});
    h.saved={{{1,11},h.self},{{2,22},g.self},{{3,33},bystander}};
    assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;
    LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);
    auto unrelated=peer;unrelated.guid=3;unrelated.identity={3,33};unrelated.session=888;h.peers.push_back(unrelated);
    h.refreshPlayers();g.players=h.players;
    auto receiveGuest=[&]{auto packets=drain(g.socket);for(const auto& p:packets)deliver(g,p);};
    assert(hostRealm.partyCommand(LocalPartyAction::Invite,2));h.sendParty(h.peers[0]);receiveGuest();
    assert(guestRealm.partyCommand(LocalPartyAction::Accept,0,g.localParty.inviteId));guestRealm.update(.01f);h.receive();receiveGuest();
    assert(h.localParty.members.size()==2 && g.localParty.members.size()==2);
    h.history(h.peers[0]);receiveGuest(); // Complete the normal join history/progress barrier.
    auto* member=&h.saved[1].player;
    // Real realm membership must enter gameplay, not just the party UI snapshot.
    rewardKill(h.gameplay,h.self,h.activePlayers(),rewardNpc(corpse));assert(h.self.xp==51 && member->xp==50 && h.saved[2].player.xp==0);
    assert(h.self.quests[0].progress[0]==1 && member->quests[0].progress[0]==1 && h.saved[2].player.quests[0].progress[0]==0);
    rewardKill(h.gameplay,h.self,h.activePlayers(),rewardNpc(corpse));assert(h.gameplay.npcs()[0].lootOwner==2);
    h.progress(h.peers[0]);h.world(h.peers[0],++h.worldTick);receiveGuest();
    assert(g.self.xp==100 && g.self.quests[0].progress[0]==2);
    assert(g.gameplay.npcs().size()==1 && g.gameplay.npcs()[0].lootOwner==2 && g.gameplay.npcs()[0].lootable);
    assert(std::all_of(g.gameplay.npcs()[0].lootCandidates.begin(),g.gameplay.npcs()[0].lootCandidates.end(),[](auto id){return id==0;}));
    const auto before=*member;const auto selfBefore=h.self;
    // Save error must restore both the owner's economic state AND the corpse.
    const auto savedDirectory=h.directory;h.directory+="/missing/loot";
    assert(guestRealm.command({LocalAction::Loot,corpse,0}));guestRealm.update(.01f);h.receive();receiveGuest();
    assert(member->inventory==before.inventory && member->money==before.money && h.gameplay.npcs()[0].lootable);
    assert(h.self.inventory==selfBefore.inventory && h.self.money==selfBefore.money);
    assert(g.pendingCommands.empty() && g.actionStatus.find("not saved")!=std::string::npos);h.directory=savedDirectory;
    // A fresh request can succeed after recovery; command replay cannot duplicate it.
    assert(guestRealm.command({LocalAction::Loot,corpse,0}));guestRealm.update(.21f);h.receive();receiveGuest();
    assert(member->inventory.size()==1 && member->inventory[0].itemId==117 && member->inventory[0].count==1 && member->money==3 && h.self.money==4);
    assert(g.self.inventory==member->inventory && g.self.money==member->money);
    assert(!h.gameplay.npcs()[0].lootable);
    Writer replay;replay.u32(h.peers[0].lastCommand);replay.u8(uint8_t(LocalAction::Loot));replay.u64(corpse);replay.u32(0);
    replay.u32(0);replay.u32(0);replay.u32(0);replay.u64(0);g.send(Message::Command,g.session,replay,g.host);h.receive();receiveGuest();
    assert(member->inventory[0].count==1 && member->money==3 && h.self.money==4);
    std::string result;assert(!h.runCommand(h.saved[2].player,{LocalAction::Loot,corpse,0},result));
    h.world(h.peers[0],++h.worldTick);receiveGuest();assert(!g.gameplay.npcs()[0].lootable);
    // Save-before-ack includes shared XP/quest progress and each owner's inventory.
    LocalRealm::Impl loaded;loaded.gameplay.useContent(rewardContent());assert(loaded.parseSave(h.directory+"/realm.wprs"));
    const auto* savedGuest=loaded.findSaved(2);const auto* savedHost=loaded.findSaved(1);
    assert(savedGuest && savedHost && savedGuest->player.inventory==member->inventory && savedGuest->player.money==3 && savedHost->player.money==4);
    assert(savedGuest->player.xp==100 && savedGuest->player.quests[0].progress[0]==2 && savedHost->player.xp==102);
    assert(loaded.partyDirector.parties().empty());
    // Ownership is also carried by subsequent snapshots after offline fallback.
    rewardKill(h.gameplay,h.self,h.activePlayers(),rewardNpc(corpse));assert(h.gameplay.npcs()[0].lootOwner==1);
    // Reverse roles: host collection also credits a guest's wallet and must
    // roll that guest back on disk failure, then replicate the committed value.
    h.directory+="/missing/host-loot";assert(!hostRealm.command({LocalAction::Loot,corpse,0}));
    assert(h.self.money==4 && member->money==3 && h.self.inventory.empty() && h.gameplay.npcs()[0].lootable);
    h.directory=savedDirectory;assert(hostRealm.command({LocalAction::Loot,corpse,0}));
    assert(h.self.money==8 && member->money==6 && h.self.inventory.size()==1 && member->inventory[0].count==1);
    h.progress(h.peers[0]);receiveGuest();assert(g.self.money==6 && g.self.inventory[0].count==1);
    LocalRealm::Impl reloaded;reloaded.gameplay.useContent(rewardContent());assert(reloaded.parseSave(h.directory+"/realm.wprs"));
    assert(reloaded.findSaved(1)->player.money==8 && reloaded.findSaved(2)->player.money==6);
    rewardKill(h.gameplay,h.self,h.activePlayers(),rewardNpc(corpse));assert(h.gameplay.npcs()[0].lootOwner==2);
    h.peers.erase(h.peers.begin());h.syncParty(true);assert(h.localParty.members.empty());
    assert(h.gameplay.tick(0,h.activePlayers()));assert(h.gameplay.npcs()[0].lootOwner==1);
    // With the group gone, nearby unrelated humans get no additional XP.
    const auto outsiderXp=h.saved[2].player.xp;const auto oldXp=h.self.xp;
    rewardKill(h.gameplay,h.self,h.activePlayers(),rewardNpc(corpse));assert(h.self.xp==oldXp+101 || h.self.level>1);assert(h.saved[2].player.xp==outsiderXp);
    hostRealm.stop();guestRealm.stop();std::filesystem::remove_all(directory);
    std::cout<<"PASS group reward LAN/save: real party membership feeds kill authority; owner XP/quest snapshots and outsider exclusion; assigned loot owner replication without private cohort; host/guest multi-wallet save rollback and credited-guest replication; retry/replay; save12 reload; consumed corpse snapshot; disconnect reassignment and group dissolution\n";
}
