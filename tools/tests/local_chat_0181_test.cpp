#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include "game/local_chat_client.hpp"
#include "local_group_rewards_fixture.hpp"
#include <cassert>
#include <iostream>
using namespace wowee::game;
namespace net=wowee::net;
static sockaddr_in loopback(uint16_t port){sockaddr_in a{};initAddress(a);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);return a;}
static void discard(socket_t socket){std::array<uint8_t,MaxPacket+1> b;while(::recvfrom(socket,reinterpret_cast<char*>(b.data()),b.size(),net::datagramFlags(),nullptr,nullptr)>=0){};assert(net::isWouldBlock(net::lastError()));}
struct Fixture {
 LocalRealm host,guest,other;std::string directory;
 Fixture(){
  char temp[]="/tmp/wowps-chat-0181-XXXXXX";directory=mkdtemp(temp);
  auto& h=*host.impl_;h.gameplay.useContent(rewardContent());h.state=LocalRealmState::Hosting;h.realmId=123;h.directory=directory;h.now=5;
  h.self=rewardPlayer(1);h.self.name="Host";h.self.race=1;h.self.mapId=h.self.instanceId=0;h.self.x=h.self.y=h.self.z=0;
  assert(h.openSocket(0));
  for(auto* realm:{&guest,&other}){
   auto& g=*realm->impl_;g.gameplay.useContent(rewardContent());g.self=h.self;g.self.guid=realm==&guest?2:3;g.self.name=realm==&guest?"Guest":"Other";
   g.state=LocalRealmState::Connected;g.now=5;g.session=900+g.self.guid;assert(g.openSocket(0));g.host=loopback(h.port);
   h.saved.push_back({{g.self.guid,g.self.guid+10},g.self});
   LocalRealm::Impl::Peer peer;peer.guid=g.self.guid;peer.identity={peer.guid,peer.guid+10};peer.session=g.session;peer.address=loopback(g.port);peer.loading=false;peer.lastSeen=5;h.peers.push_back(peer);
  }
 }
 ~Fixture(){guest.stop();other.stop();host.stop();std::filesystem::remove_all(directory);}
 void pump(){auto& h=*host.impl_;auto& g=*guest.impl_;auto& o=*other.impl_;g.pumpChat();o.pumpChat();h.receive();h.pumpChat();g.receive();o.receive();h.receive();h.now+=.6;g.now+=.6;o.now+=.6;}
};
static void rules(){
 std::vector<LocalChatActor> a={{1,"Host",0,0,10,1,0,0,0,false,false},{2,"Guest",0,0,10,1,25,0,0,false,false},{3,"Other",0,1,0,1,0,0,0,false,false},{4,"Far",1,0,10,1,0,0,0,false,false},{5,"Enemy",0,0,0,2,0,0,0,false,false}};
 auto route=[&](LocalChatChannel c){return routeLocalChat(a,1,c,"Hello","");};
 assert(route(LocalChatChannel::Say).recipients==std::vector<uint64_t>({1,2}));a[1].x=25.01f;assert(route(LocalChatChannel::Say).recipients.size()==1);
 a[1].x=300;assert(route(LocalChatChannel::Yell).recipients.size()==2);a[1].x=300.01f;assert(route(LocalChatChannel::Yell).recipients.size()==1);
 assert(route(LocalChatChannel::Party).recipients==std::vector<uint64_t>({1,2,4}));
 assert(routeLocalChat(a,1,LocalChatChannel::Whisper,"Hello","fAr").recipients==std::vector<uint64_t>({1,4}));
 assert(!routeLocalChat(a,1,LocalChatChannel::Whisper,"Hello","Enemy").error.empty());
 a[1].x=0;a[1].dead=true;assert(route(LocalChatChannel::Say).recipients.size()==1);a[1].dead=false;a[1].loading=true;assert(route(LocalChatChannel::Say).recipients.size()==1);
 a[1].loading=false;a[1].x=std::numeric_limits<float>::quiet_NaN();assert(route(LocalChatChannel::Say).recipients.size()==1);
 a[0].party=0;assert(!route(LocalChatChannel::Party).error.empty());assert(!route(LocalChatChannel::WhisperInform).error.empty());
 assert(!routeLocalChat(a,999,LocalChatChannel::Say,"Hello","").error.empty());
 a.push_back(a[3]);assert(!routeLocalChat(a,1,LocalChatChannel::Whisper,"Hello","Far").error.empty());
 std::cout<<"PASS chat routing: range boundaries, map/instance/faction/life/loading, party across maps, ambiguous/missing names, unsupported channels\n";
 for(const auto& s:std::vector<std::string>{"hello",std::string(160,'a'),"Grüße 😀"})assert(validLocalChatText(s));
 for(const auto& s:std::vector<std::string>{"","   ",std::string(161,'a'),"a\nb",std::string("a\0b",3),"\xc0\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xc3"})assert(!validLocalChatText(s));
 LocalChatRate rate;for(int i=0;i<4;++i)assert(rate.consume(10));assert(!rate.consume(10));assert(!rate.consume(10.99));assert(rate.consume(11));assert(!rate.consume(9));
 const LocalChatLine line{LocalChatChannel::WhisperInform,1,"Host","Guest","|Hitem:1|hHi|h"};const auto message=localChatClientMessage(line);
 assert(message.type==ChatType::WHISPER_INFORM && message.senderGuid==1 && message.receiverName=="Guest" && message.message=="||Hitem:1||hHi||h");
 std::cout<<"PASS UTF-8/length/control validation, bounded token bucket and plain-text chat presentation\n";
}
static void reliability(){
 Fixture f;auto& h=*f.host.impl_;auto& g=*f.guest.impl_;auto& o=*f.other.impl_;
 assert(f.guest.sendChat(LocalChatChannel::Say,"Hello"));assert(f.guest.takeChatMessages().empty());
 g.pumpChat();h.receive();assert(h.chatInbox.size()==1 && h.peers[0].chatOut.size()==1 && h.peers[1].chatOut.size()==1);
 discard(g.socket); // Lose request acceptance; sender must retry the same ID.
 g.now+=.6;g.pumpChat();h.receive();assert(h.chatInbox.size()==1 && h.peers[0].chatOut.size()==1);
 h.pumpChat();g.receive();o.receive();assert(g.chatInbox.size()==1 && o.chatInbox.size()==1 && g.chatPending.empty());
 discard(h.socket); // Lose delivery acknowledgments.
 h.now+=.6;h.pumpChat();g.receive();o.receive();assert(g.chatInbox.size()==1 && o.chatInbox.size()==1);h.receive();assert(h.peers[0].chatOut.empty() && h.peers[1].chatOut.empty());
 Writer repeat;repeat.u32(1);repeat.u8(uint8_t(LocalChatChannel::Yell));repeat.name("");repeat.text("Changed replay");g.send(Message::ChatRequest,g.session,repeat,g.host);h.receive();assert(h.chatInbox.size()==1);
 Writer forged;forged.u32(2);forged.u8(uint8_t(LocalChatChannel::Say));forged.name("");forged.text("Forged session");g.send(Message::ChatRequest,g.session+1,forged,g.host);h.receive();assert(h.peers[0].lastChat==1 && h.chatInbox.size()==1);
 forged.bytes.push_back(0);g.send(Message::ChatRequest,g.session,forged,g.host);h.receive();assert(h.peers[0].lastChat==1);
 std::cout<<"PASS UDP lost request/delivery ACKs, duplicate/replayed mutation suppression, session identity and trailing-byte rejection\n";
}
static void privacy(){
 Fixture f;auto& h=*f.host.impl_;auto& g=*f.guest.impl_;
 assert(f.guest.sendChat(LocalChatChannel::Whisper,"Private","hOsT"));f.pump();
 auto host=f.host.takeChatMessages(),guest=f.guest.takeChatMessages();assert(host.size()==1 && guest.size()==1 && f.other.takeChatMessages().empty());
 assert(host[0].sender==2 && host[0].senderName=="Guest" && host[0].receiverName=="Host" && guest[0].channel==LocalChatChannel::WhisperInform);
 std::string result;assert(h.partyDirector.execute(LocalPartyAction::Invite,1,2,0,h.now,h.partyActors(),result));
 assert(h.partyDirector.execute(LocalPartyAction::Accept,2,0,h.partyDirector.invitation(2)->id,h.now,h.partyActors(),result));h.syncParty(true);
 h.findSaved(2)->player.mapId=1;assert(f.host.sendChat(LocalChatChannel::Party,"Group"));f.pump();
 assert(f.host.takeChatMessages().size()==1 && f.guest.takeChatMessages().size()==1 && f.other.takeChatMessages().empty());
 assert(h.partyDirector.execute(LocalPartyAction::Leave,2,0,0,h.now,h.partyActors(),result));h.syncParty(true);
 assert(f.guest.sendChat(LocalChatChannel::Party,"No group"));f.pump();assert(g.chatPending.empty() && !h.peers[0].lastChatSuccess && f.host.takeChatMessages().empty());
 assert(!f.host.sendChat(static_cast<LocalChatChannel>(4),"No guild"));
 std::cout<<"PASS private whisper/party delivery, canonical names, membership removal, rejection without false local echo\n";
}
static void bounds(){
 Fixture f;auto& h=*f.host.impl_;auto& g=*f.guest.impl_;
 h.peers[1].chatOut.resize(32);assert(!f.host.sendChat(LocalChatChannel::Say,"Blocked"));assert(h.chatInbox.empty() && h.peers[0].chatOut.empty() && h.peers[0].chatSerial==0);
 h.peers[1].chatOut.clear();assert(f.host.sendChat(LocalChatChannel::Say,"Accepted"));assert(h.peers[0].chatSerial==1);
 for(unsigned i=0;i<8;++i)assert(f.guest.sendChat(LocalChatChannel::Say,"Queued"));assert(!f.guest.sendChat(LocalChatChannel::Say,"Overflow"));
 // Out-of-order and malformed deliveries cannot consume an ID or acknowledge it.
 Writer delivery;delivery.u32(2);delivery.u8(1);delivery.u64(1);delivery.name("Host");delivery.name("");delivery.text("Late");
 Reader input(delivery.bytes.data(),delivery.bytes.size());g.receiveChatDelivery(input);assert(g.chatReceived==0 && g.chatInbox.empty());
 g.fail("Session ended");assert(g.chatInbox.empty() && g.chatPending.empty() && g.chatReceived==0);
 f.host.stop();assert(h.chatInbox.empty() && h.chatPending.empty() && h.peers.empty());
 std::cout<<"PASS all-recipient queue backpressure, eight-request limit, ordered input and session cleanup\n";
}
int main(){rules();reliability();privacy();bounds();}
