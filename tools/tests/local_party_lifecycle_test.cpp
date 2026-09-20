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
static void lifecycleCases(){
 for(int mode=0;mode<3;++mode){
    char temp[]="/tmp/wowps-party-0176-XXXXXX";auto* dir=mkdtemp(temp);assert(dir);
    LocalRealm host,guest;auto& h=*host.impl_;auto& g=*guest.impl_;h.gameplay.useContent(rewardContent());g.gameplay.useContent(rewardContent());h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=123;h.directory=dir;
    h.self.guid=1;h.self.name="Host";h.self.x=h.self.y=h.self.z=0;g.self=h.self;g.self.guid=2;g.self.name="Guest";
    h.saved={{{1,11},h.self},{{2,22},g.self}};
    for(uint64_t id:{3,4}){auto p=h.self;p.guid=id;p.name="Member"+std::to_string(id);h.saved.push_back({{id,id+10},p});}
    assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;
    for(uint64_t id:{2,3,4}){LocalRealm::Impl::Peer peer;peer.guid=id;peer.identity=h.saved[id-1].identity;peer.address=loopback(g.port);peer.session=id==2?987:1000+id;peer.joinNonce=55+id;peer.loading=false;peer.lastSeen=100;h.peers.push_back(peer);}
    auto bot=h.self;bot.guid=kLocalBotGuidPrefix|1;h.botPlayers={bot};h.now=100;h.refreshPlayers();g.players=h.players;h.refreshStatus();g.refreshStatus();assert(h.status.find("4/")!=std::string::npos && g.status.find("4/")!=std::string::npos);
    std::string result;auto run=[&](LocalPartyAction action,uint64_t who,uint64_t target=0,uint32_t token=0){return h.partyDirector.execute(action,who,target,token,h.now,h.partyActors(),result);};
    assert(run(LocalPartyAction::Invite,1,2));assert(run(LocalPartyAction::Accept,2,0,h.partyDirector.invitation(2)->id));
    assert(run(LocalPartyAction::Invite,1,3));assert(run(LocalPartyAction::Accept,3,0,h.partyDirector.invitation(3)->id));
    assert(run(LocalPartyAction::Promote,1,2));assert(run(LocalPartyAction::Invite,2,4));h.syncParty(true);
    g.commitParty(h.partyViewFor(2));g.gameplay.setPartyMembership(h.partyDirector.parties());const auto rev=g.partyRosterRevision;
    if(mode==0){g.sendDeparture();h.receive();}
    if(mode==1){Writer w;w.u64(2);w.u64(22);g.send(Message::AbortJoin,57,w,g.host);h.receive();}
    if(mode==2){h.peers[0].lastSeen=0;h.peers[0].loading=true;h.peers[0].loadingSince=100;h.expirePeers();assert(h.peers.size()==3);h.peers[0].loadingSince=0;h.now=LoadingTimeout+101;h.peers[1].lastSeen=h.peers[2].lastSeen=h.now;h.lastPartyRefresh=h.now;assert(guest.partyCommand(LocalPartyAction::Invite,4));guest.update(.01f);host.update(0);}
    assert(h.peers.size()==2 && !h.partyDirector.party(2) && !h.partyDirector.invitation(4));
    assert(h.localParty.members.size()==2 && h.localParty.members[0].guid==1 && h.localParty.members[1].guid==3);
    auto isolatedRewards=[&](LocalGameplay& game){auto a=rewardPlayer(1),b=rewardPlayer(2),c=rewardPlayer(3);rewardKill(game,b,{&a,&b,&c},rewardNpc(10,2));assert(a.xp==0 && b.xp==101 && c.xp==0);};
    isolatedRewards(h.gameplay); // Removed GUID has no cached group reward rights.
    assert(h.status.find("3/")!=std::string::npos);
    // Reconnecting the same identity does not inherit the ended party session.
    LocalRealm::Impl::Peer rejoined;rejoined.guid=2;rejoined.identity={2,22};h.peers.push_back(rejoined);h.syncParty(true);assert(!h.partyDirector.party(2));h.peers.pop_back();
    g.fail("Fixture host disconnected");assert(g.localParty.members.empty() && !g.localParty.inviteId && g.partyRosterRevision>rev);isolatedRewards(g.gameplay);
    host.stop();guest.stop();std::filesystem::remove_all(dir);
 }
 std::cout<<"PASS party session lifecycle: real UDP departure/abort, timeout with loading grace, immediate leader succession/invite invalidation/authority sync, no reconnect membership, error clears roster, human-only console counts\n";
}
int main(){
    lifecycleCases();
    char temp[]="/tmp/wowps-party-lan-XXXXXX";const auto* directory=mkdtemp(temp);assert(directory);
    LocalRealm hostRealm,guestRealm;auto& h=*hostRealm.impl_;auto& g=*guestRealm.impl_;
    h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=directory;
    h.self.guid=1;h.self.name="Host";h.self.x=h.self.y=h.self.z=0;g.self=h.self;g.self.guid=2;g.self.name="Guest";
    auto bystander=h.self;bystander.guid=3;bystander.name="Bystander";
    h.saved={{{1,11},h.self},{{2,22},g.self},{{3,33},bystander}};
    assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;
    LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);
    // An unrelated authenticated participant exists but must see no other party.
    auto unrelated=peer;unrelated.guid=3;unrelated.identity={3,33};unrelated.session=888;h.peers.push_back(unrelated);
    auto bot=bystander;bot.guid=4;bot.name="Bot";h.botPlayers={bot};h.refreshPlayers();g.players=h.players;
    assert(hostRealm.partyPlayerByName("gUeSt")==2 && hostRealm.partyPlayerByName("absent")==0);
    assert(!hostRealm.partyCommand(LocalPartyAction::Invite,4)); // Walking bots cannot accept invitations.
    auto snapshot=[&]{h.sendParty(h.peers[0]);auto p=drain(g.socket);assert(p.size()==1 && p[0].size()<=MaxPacket);return p[0];};
    assert(hostRealm.partyCommand(LocalPartyAction::Invite,2));deliver(g,snapshot());const auto withdrawn=g.localParty.inviteId;
    assert(hostRealm.partyCommand(LocalPartyAction::Remove,2));deliver(g,snapshot());assert(!g.localParty.inviteId);
    assert(guestRealm.partyCommand(LocalPartyAction::Accept,0,withdrawn));guestRealm.update(.01f);h.receive();for(const auto& b:drain(g.socket))deliver(g,b);assert(!h.peers[0].lastCommandSuccess && !h.partyDirector.party(2));
    std::cout<<"PASS party UDP withdrawal: uninvite clears recipient view, stale acceptance rejected without membership\n";
    assert(hostRealm.partyCommand(LocalPartyAction::Invite,2));const auto invitation=snapshot();
    deliver(g,invitation,1234);assert(!guestRealm.partyView().inviteId);deliver(g,invitation);const auto id=guestRealm.partyView().inviteId;
    assert(id && guestRealm.partyView().inviterName=="Host" && guestRealm.partyView().members.empty());
    assert(guestRealm.partyCommand(LocalPartyAction::Accept,0,id));guestRealm.update(.01f);h.receive();
    auto replies=drain(g.socket);for(const auto& p:replies)deliver(g,p);
    assert(g.pendingCommands.empty() && guestRealm.partyView().members.size()==2 && hostRealm.partyView().members.size()==2);
    assert(guestRealm.partyView().members.front().guid==1 && h.partyViewFor(3).members.empty());
    g.players.clear();assert(guestRealm.partyPlayerByName("HOST")==1); // roster survives distance/map culling
    g.players=h.players;assert(guestRealm.partyPlayerByName("Host")==1); // same GUID appears in both lists
    auto ambiguous=h.self;ambiguous.guid=9;g.players.push_back(ambiguous);
    assert(guestRealm.partyPlayerByName("Host")==0);g.players=h.players;
    const auto inventory=h.saved[1].player.inventory;const auto money=h.saved[1].player.money;
    // Once-only command ID replay cannot add the member again.
    Writer replay;replay.u32(h.peers[0].lastCommand);replay.u8(uint8_t(LocalAction::PartyAccept));replay.u64(0);replay.u32(id);
    replay.u32(0);replay.u32(0);replay.u32(0);replay.u64(0);g.send(Message::Command,g.session,replay,g.host);h.receive();drain(g.socket);
    assert(h.partyDirector.party(2)->members.size()==2);
    assert(!guestRealm.partyView().inviteId);deliver(g,invitation);assert(!guestRealm.partyView().inviteId); // stale request cannot reopen
    const auto before=snapshot();deliver(g,before);
    h.self.health=41;h.self.mana=23;h.syncParty(true);const auto after=snapshot();
    const auto rosterRevision=g.partyRosterRevision;deliver(g,after);assert(g.localParty.members[0].health==41 && g.localParty.members[0].power==23);
    assert(g.partyRosterRevision==rosterRevision);deliver(g,before);assert(g.localParty.members[0].health==41);
    // Parser rejects malformed full snapshots without committing even their revision.
    const auto unchanged=g.localParty;const auto sequence=g.partySequence;
    auto rejectPayload=[&](std::vector<uint8_t> p){Reader r(p.data(),p.size());g.receiveParty(r);assert(g.localParty==unchanged && g.partySequence==sequence);};
    Writer bad;bad.u32(sequence+1);bad.u32(1);bad.u32(0);bad.u64(0);bad.name("");bad.u8(6);rejectPayload(bad.bytes);
    auto fresh=snapshot();std::vector<uint8_t> payload(fresh.begin()+HeaderSize,fresh.end());
    auto truncated=payload;truncated.pop_back();rejectPayload(truncated);
    auto duplicate=payload;std::copy(duplicate.begin()+38,duplicate.begin()+46,duplicate.begin()+38+66);rejectPayload(duplicate);
    auto absent=payload;absent[38+66+7]=9;rejectPayload(absent); // replace self GUID with another valid GUID
    auto invalidVitals=payload;invalidVitals[38+33]=0xff;rejectPayload(invalidVitals); // invalid health > max
    assert(hostRealm.partyCommand(LocalPartyAction::Promote,2));deliver(g,snapshot());assert(g.localParty.members.front().guid==2);
    assert(!hostRealm.partyCommand(LocalPartyAction::Remove,2));
    assert(guestRealm.partyCommand(LocalPartyAction::Remove,1));guestRealm.update(.01f);h.receive();replies=drain(g.socket);for(const auto& p:replies)deliver(g,p);
    assert(g.localParty.members.empty() && h.localParty.members.empty());
    assert(h.saved[1].player.inventory==inventory && h.saved[1].player.money==money);
    // A 30-second invitation expires, then an old acceptance is refused.
    assert(hostRealm.partyCommand(LocalPartyAction::Invite,2));deliver(g,snapshot());const auto expired=g.localParty.inviteId;
    h.now+=31;h.syncParty(true);deliver(g,snapshot());assert(!g.localParty.inviteId);
    LocalRealmCommand old{LocalAction::PartyAccept,0,expired};std::string result;assert(!h.runCommand(h.saved[1].player,old,result));
    // Session membership is deliberately not written into save13.
    assert(h.saveRealm());LocalRealm::Impl loaded;assert(loaded.parseSave(h.directory+"/realm.wprs"));
    assert(loaded.partyDirector.parties().empty() && loaded.partyDirector.invitations().empty());
    assert(hostRealm.partyCommand(LocalPartyAction::Invite,2));const auto invite2=h.partyDirector.invitation(2)->id;
    LocalRealmCommand join{LocalAction::PartyAccept,0,invite2};assert(h.runCommand(h.saved[1].player,join,result));
    h.peers.erase(h.peers.begin());h.syncParty(true);assert(h.localParty.members.empty());
    hostRealm.stop();guestRealm.stop();std::filesystem::remove_all(directory);
    std::cout<<"PASS party LAN: authenticated invite/accept/roster; bots rejected; owner-only party views; real UDP command replay; stale snapshots; public health/resource updates; roster event gating; oversized/truncated/duplicate/self-absent/invalid-vitals rejection; leader permissions/removal; expiry; unchanged economic state; save13/session boundary; disconnect cleanup\n";
}
