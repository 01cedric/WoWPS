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
static std::shared_ptr<LocalWorldContent> content(){auto c=rewardContent();auto item=c->items[0];item.id=159;item.name="Water";c->items.push_back(item);item.id=6948;c->items.push_back(item);c->quests[0].objectives={{LocalQuestObjective::Type::Collect,117,6}};return c;}
struct Fixture {
 LocalRealm host,guest,other;std::string directory;std::shared_ptr<LocalWorldContent> data=content();
 Fixture(){char temp[]="/tmp/wowps-social-0182-XXXXXX";directory=mkdtemp(temp);auto& h=*host.impl_;
  h.gameplay.useContent(data);h.state=LocalRealmState::Hosting;h.realmId=123;h.directory=directory;h.now=5;h.identity={1,11};
  h.self=rewardPlayer(1);h.self.name="Host";h.self.race=1;h.self.mapId=h.self.instanceId=0;h.self.money=100;h.self.inventory={{117,10}};
  h.saved.push_back({h.identity,h.self});assert(h.openSocket(0));
  for(auto* realm:{&guest,&other}){auto& g=*realm->impl_;g.gameplay.useContent(data);g.self=h.self;g.self.guid=realm==&guest?2:3;g.self.name=realm==&guest?"Guest":"Other";g.self.inventory={{159,8}};
   g.state=LocalRealmState::Connected;g.now=5;g.session=900+g.self.guid;g.identity={g.self.guid,g.self.guid+10};g.directory=directory+"/"+g.self.name;std::filesystem::create_directory(g.directory);
   assert(g.openSocket(0));g.host=loopback(h.port);g.realmId=h.realmId;h.saved.push_back({g.identity,g.self});
   LocalRealm::Impl::Peer peer;peer.guid=g.self.guid;peer.identity=g.identity;peer.session=g.session;peer.address=loopback(g.port);peer.loading=false;peer.lastSeen=5;h.peers.push_back(peer);
  }h.refreshPlayers();guest.impl_->players=h.players;other.impl_->players=h.players;
 }
 ~Fixture(){guest.stop();other.stop();host.stop();std::filesystem::remove_all(directory);}
 void snapshots(){auto& h=*host.impl_;h.maintainSocial();for(auto& peer:h.peers){h.sendSocial(peer);h.history(peer);}guest.impl_->receive();other.impl_->receive();}
 bool remote(LocalRealm& realm,LocalAction action,uint32_t id=0,uint32_t rev=0,uint64_t value=0,uint32_t bag=0,uint32_t slot=0,uint32_t item=0,uint16_t count=0){
  auto& h=*host.impl_;auto& g=*realm.impl_;LocalRealmCommand c{action,value,id,rev,bag,slot};c.serviceNpcGuid=(uint64_t(item)<<32)|count;
  assert(realm.command(c));realm.update(.21f);h.receive();snapshots();assert(g.pendingCommands.empty());
  for(const auto& peer:h.peers)if(peer.guid==g.self.guid)return peer.lastCommandSuccess;assert(false);return false;
 }
 void group(){auto& h=*host.impl_;std::string result;for(uint64_t id:{2,3}){assert(h.partyDirector.execute(LocalPartyAction::Invite,1,id,0,h.now,h.partyActors(),result));assert(h.partyDirector.execute(LocalPartyAction::Accept,id,0,h.partyDirector.invitation(id)->id,h.now,h.partyActors(),result));}h.syncParty(true);}
 LocalTrade begin(){assert(host.tradeAction(LocalAction::TradeRequest,0,0,2));snapshots();auto t=guest.tradeView();assert(t.state==1);assert(remote(guest,LocalAction::TradeOpen,t.id,t.revision));return host.tradeView();}
 bool hostAction(LocalAction action,uint64_t value=0,uint32_t bag=0,uint32_t slot=0,uint32_t item=0,uint16_t count=0){auto t=host.tradeView();const bool ok=host.tradeAction(action,t.id,t.revision,value,bag,slot,item,count);snapshots();return ok;}
 bool guestAction(LocalAction action,uint64_t value=0,uint32_t bag=0,uint32_t slot=0,uint32_t item=0,uint16_t count=0){auto t=guest.tradeView();return remote(guest,action,t.id,t.revision,value,bag,slot,item,count);}
};
static void ready(){Fixture f;auto& h=*f.host.impl_;f.group();assert(!f.remote(f.guest,LocalAction::ReadyStart));assert(f.host.startReadyCheck());f.snapshots();auto id=f.host.readyCheck().id;
 assert(f.guest.readyCheck().members.size()==3 && f.other.readyCheck().members.size()==3);assert(f.host.readyTimeLeft()==30);
 assert(!f.remote(f.guest,LocalAction::ReadyAnswer,id+1,0,1));assert(!f.remote(f.guest,LocalAction::ReadyAnswer,id,0,2));
 assert(f.remote(f.guest,LocalAction::ReadyAnswer,id,0,1));assert(!f.remote(f.guest,LocalAction::ReadyAnswer,id,0,0));
 assert(f.remote(f.other,LocalAction::ReadyAnswer,id,0,0));assert(f.host.readyCheck().state==2 && f.host.readyCheck().members[1].answer==1 && f.host.readyCheck().members[2].answer==2);
 h.now+=11;h.maintainSocial();assert(f.host.startReadyCheck());h.now+=31;f.snapshots();assert(f.host.readyCheck().state==2 && f.host.readyCheck().members[1].answer==2);
 h.now+=11;h.maintainSocial();assert(f.host.startReadyCheck());h.peers[0].loading=true;f.snapshots();assert(f.host.readyCheck().state==3);h.peers[0].loading=false;
 h.now+=11;h.maintainSocial();assert(f.host.startReadyCheck());std::string result;assert(h.partyDirector.execute(LocalPartyAction::Promote,1,2,0,h.now,h.partyActors(),result));h.syncParty(true);f.snapshots();assert(f.host.readyCheck().state==3);
 std::cout<<"PASS ready checks: leader authority, immutable answers and IDs, per-member snapshots, deadline, loading and leadership cancellation\n";
}
static void tradeRules(){auto c=content();auto a=rewardPlayer(1),b=rewardPlayer(2);a.race=b.race=1;a.money=b.money=100;a.inventory={{117,10}};b.inventory={{159,8}};
 LocalTrade t;t.id=1;t.revision=1;t.state=2;t.players={1,2};t.fingerprints={localTradeFingerprint(a),localTradeFingerprint(b)};t.items[0][0]={117,6,10,0};t.items[1][0]={159,3,8,0};t.money={40,20};LocalRealmPlayer x,y;std::string error;
 assert(prepareLocalTrade(t,a,b,*c,x,y,error));assert(x.money==80 && y.money==120 && x.inventory[0].count==4 && y.inventory[0].count==5 && a.inventory[0].count==10);
 auto bad=t;bad.items[0][1]=bad.items[0][0];assert(!prepareLocalTrade(bad,a,b,*c,x,y,error));bad=t;bad.items[0][0].sourceCount=9;assert(!prepareLocalTrade(bad,a,b,*c,x,y,error));
 a.equipment[0]=117;t.fingerprints[0]=localTradeFingerprint(a);assert(!prepareLocalTrade(t,a,b,*c,x,y,error));a.equipment[0]=0;
 a.inventory={{6948,1}};t.fingerprints[0]=localTradeFingerprint(a);t.items[0][0]={6948,1,1,0};assert(!prepareLocalTrade(t,a,b,*c,x,y,error));
 a.inventory.assign(24,{117,20});b.inventory.assign(24,{159,20});t.items[0][0]={117,20,20,0};t.items[1][0]={159,20,20,0};t.fingerprints={localTradeFingerprint(a),localTradeFingerprint(b)};
 assert(prepareLocalTrade(t,a,b,*c,x,y,error) && x.inventory.size()==24 && y.inventory.size()==24);t.items[0][0].count=19;assert(!prepareLocalTrade(t,a,b,*c,x,y,error));
 t.items[0][0].count=20;a.money=1000000000;t.fingerprints[0]=localTradeFingerprint(a);t.money={0,1};assert(!prepareLocalTrade(t,a,b,*c,x,y,error));
 b.x=10;assert(localTradeReach(a,b));b.x=10.01f;assert(!localTradeReach(a,b));b.x=0;b.instanceId=1;assert(!localTradeReach(a,b));b.instanceId=0;b.race=2;assert(!localTradeReach(a,b));b.race=1;b.dead=true;assert(!localTradeReach(a,b));
 std::cout<<"PASS trade planning: exact stack quantities, equipped/bound/stale/duplicate rejection, full-backpack reciprocal exchange, wallet overflow and spatial/faction/life rules\n";
}
static void tradeLan(){Fixture f;auto& h=*f.host.impl_;auto t=f.begin();assert(t.state==2 && f.other.tradeView().state==0);
 assert(!f.remote(f.other,LocalAction::TradeAccept,t.id,t.revision));assert(!f.host.tradeAction(LocalAction::TradeOffer,t.id,t.revision-1,6,0,0,117,10));
 assert(f.hostAction(LocalAction::TradeOffer,6,0,0,117,10));assert(f.guestAction(LocalAction::TradeOffer,3,0,0,159,8));
 assert(f.hostAction(LocalAction::TradeAccept));assert(f.guest.tradeView().accepted[0]);const auto old=f.host.tradeView();
 assert(f.guestAction(LocalAction::TradeMoney,20));assert(!f.host.tradeView().accepted[0]);assert(!f.host.tradeAction(LocalAction::TradeAccept,old.id,old.revision));
 assert(f.hostAction(LocalAction::TradeMoney,40));assert(!f.host.command({LocalAction::UseItem,0,117}));
 assert(f.hostAction(LocalAction::TradeAccept));const auto beforeHost=h.self;const auto beforeGuest=h.findSaved(2)->player;const auto beforeSaved=h.findSaved(1)->player;
 h.directory+="/missing/trade";assert(!f.guestAction(LocalAction::TradeAccept));assert(h.self.inventory==beforeHost.inventory && h.self.money==beforeHost.money && h.findSaved(2)->player.inventory==beforeGuest.inventory && h.findSaved(2)->player.money==beforeGuest.money && h.findSaved(1)->player.inventory==beforeSaved.inventory);
 h.directory=f.directory;assert(f.hostAction(LocalAction::TradeAccept));assert(f.guestAction(LocalAction::TradeAccept));assert(f.host.tradeView().state==3 && f.guest.tradeView().state==3);
 assert(h.self.money==80 && h.findSaved(2)->player.money==120 && h.self.inventory[0].count==4 && h.findSaved(2)->player.inventory[0].count==5);
 assert(h.findSaved(2)->player.quests[0].status==LocalQuestStatus::Complete && h.self.quests[0].status==LocalQuestStatus::Active);
 assert(f.guest.impl_->self.money==120 && f.guest.impl_->self.inventory==h.findSaved(2)->player.inventory);
 const auto done=f.guest.tradeView();assert(!f.remote(f.guest,LocalAction::TradeAccept,done.id,done.revision));assert(h.self.money==80);
 LocalRealm::Impl loaded;loaded.gameplay.useContent(f.data);assert(loaded.parseSave(f.directory+"/realm.wprs"));assert(loaded.findSaved(1)->player.money==80 && loaded.findSaved(2)->player.money==120 && loaded.findSaved(2)->player.inventory==h.findSaved(2)->player.inventory);
 // Replaying an already consumed command with another action cannot reopen.
 Writer w;w.u32(h.peers[0].lastCommand);w.u8(uint8_t(LocalAction::TradeRequest));w.u64(1);for(int i=0;i<4;++i)w.u32(0);w.u64(0);f.guest.impl_->send(Message::Command,f.guest.impl_->session,w,f.guest.impl_->host);h.receive();assert(h.trades.size()==1 && h.trades[0].state==3);
 std::cout<<"PASS private UDP trade: requests, offer revisions, bilateral acceptance reset, simultaneous two-owner disk rollback, exact successful save/reload, quest refresh and replay suppression\n";
}
static void cancellation(){Fixture f;auto& h=*f.host.impl_;f.begin();assert(f.hostAction(LocalAction::TradeAccept));h.self.money++;f.snapshots();assert(!f.host.tradeView().accepted[0]);
 h.self.inventory[0].count--;f.snapshots();assert(f.host.tradeView().items[0][0].item==0);
 h.findSaved(2)->player.x=11;f.snapshots();assert(f.host.tradeView().state==4);h.findSaved(2)->player.x=0;
 f.begin();auto enemy=rewardNpc();enemy.targetGuid=2;h.gameplay.setRemoteNpcs({enemy});f.snapshots();assert(f.host.tradeView().state==4);h.gameplay.setRemoteNpcs({});
 f.begin();h.peers.erase(h.peers.begin());f.snapshots();assert(f.host.tradeView().state==4);assert(h.self.inventory[0].count==9);
 f.guest.impl_->fail("Disconnected");assert(f.guest.tradeView().state==0 && f.guest.readyCheck().state==0);
 std::cout<<"PASS trade lifecycle: inventory/wallet changes revoke acceptance, range/combat/disconnect cancellation and client session cleanup\n";
}
static void ignore(){Fixture f;auto& h=*f.host.impl_;auto& g=*f.guest.impl_;assert(f.host.changeIgnore("gUeSt",true));assert(f.host.changeIgnore("Guest",true) && f.host.ignoredNames().size()==1);assert(!f.host.changeIgnore("Host",true));
 assert(f.guest.sendChat(LocalChatChannel::Say,"Hidden"));g.pumpChat();h.receive();assert(f.host.takeChatMessages().empty());h.pumpChat();g.receive();f.other.impl_->receive();h.receive();assert(f.guest.takeChatMessages().size()==1);
 h.loadIgnores();assert(f.host.isIgnored("GUEST"));auto identity=h.identity;h.identity={88,99};h.loadIgnores();assert(h.ignoreNames.empty());h.identity=identity;h.loadIgnores();assert(f.host.isIgnored("Guest"));
 const auto good=h.directory;h.directory+="/missing/list";assert(!f.host.changeIgnore("Other",true) && !f.host.isIgnored("Other"));assert(!f.host.changeIgnore("Guest",false) && f.host.isIgnored("Guest"));h.directory=good;
 assert(f.guest.changeIgnore("Host",true));g.chatInbox.resize(128);assert(f.host.sendChat(LocalChatChannel::Say,"Guest ignores this"));h.pumpChat();g.receive();h.receive();assert(g.chatReceived==2 && h.peers[0].chatOut.empty() && g.chatInbox.size()==128);g.chatInbox.clear();
 assert(f.host.sendChat(LocalChatChannel::Whisper,"Own echo","Guest"));assert(f.host.takeChatMessages().size()==2); // own Say plus own whisper inform
 assert(f.host.changeIgnore("Guest",false));assert(!f.host.isIgnored("Guest"));
 {std::ofstream out(h.ignorePath(),std::ios::binary|std::ios::trunc);out<<"bad";}h.loadIgnores();assert(!h.ignoreWritable && !f.host.changeIgnore("Other",true));assert(std::filesystem::file_size(h.ignorePath())==3);
 std::cout<<"PASS ignore settings: case-insensitive duplicates, identity isolation, atomic failure, corrupt-file preservation, host filtering, guest ACK under full inbox and sender echo\n";
}
static void malformed(){Fixture f;auto& g=*f.guest.impl_;f.group();assert(f.host.startReadyCheck());f.begin();f.snapshots();const auto trade=g.localTrade;const auto check=g.localReady;const auto revision=g.socialRevision;
 for(const auto bytes:std::vector<std::vector<uint8_t>>{{},{1,0,0,0},{1,0,0,0,4,0},{255,255,255,127,0,0,1}}){Reader r(bytes.data(),bytes.size());g.receiveSocial(r);assert(g.localTrade==trade && g.localReady==check && g.socialRevision==revision);}
 Writer w;w.u32(g.socialReceived+1);w.u8(0);w.u8(2);w.u32(7);w.u32(1);for(uint64_t id:{5,6}){w.u64(id);w.name("Stranger");w.u32(0);w.u8(0);for(int i=0;i<6;++i){w.u32(0);w.u16(0);w.u16(0);w.u8(0);}}
 Reader r(w.bytes.data(),w.bytes.size());g.receiveSocial(r);assert(g.localTrade==trade && g.localReady==check);
 std::cout<<"PASS social snapshots: truncation, stale IDs, invalid states, extra bytes and other-owner trade rejected without replacing the visible state\n";
}
static void boundsAndGuests(){Fixture f;auto& h=*f.host.impl_;f.group();
 for(uint64_t id:{4,5}){auto player=h.self;player.guid=id;player.name="Extra"+std::to_string(id);h.saved.push_back({{id,id+10},player});LocalRealm::Impl::Peer peer;peer.guid=id;peer.loading=false;peer.lastSeen=h.now;h.peers.push_back(peer);std::string result;
  assert(h.partyDirector.execute(LocalPartyAction::Invite,1,id,0,h.now,h.partyActors(),result));assert(h.partyDirector.execute(LocalPartyAction::Accept,id,0,h.partyDirector.invitation(id)->id,h.now,h.partyActors(),result));}
 h.syncParty(true);assert(f.host.startReadyCheck() && f.host.readyCheck().members.size()==5);
 h.readyChecks.clear();h.peers.resize(2);h.maintainSocial();
 for(int i=0;i<50;++i)assert(f.host.changeIgnore("Ignored"+std::to_string(i),true));assert(!f.host.changeIgnore("Overflow",true));h.loadIgnores();assert(h.ignoreNames.size()==50);
 // All six offer positions, including index 5, operate over the real wire.
 h.self.inventory.assign(6,{117,1});f.begin();for(unsigned slot=0;slot<6;++slot)assert(f.hostAction(LocalAction::TradeOffer,1,slot,slot,117,1));
 assert(!f.hostAction(LocalAction::TradeOffer,1,0,6,117,1));assert(f.hostAction(LocalAction::TradeAccept));assert(f.guestAction(LocalAction::TradeAccept));assert(h.self.inventory.empty());
 assert(f.remote(f.other,LocalAction::TradeRequest,0,0,2));f.snapshots();auto t=f.guest.tradeView();assert(t.players[0]==3 && t.players[1]==2);assert(f.remote(f.guest,LocalAction::TradeOpen,t.id,t.revision));
 t=f.other.tradeView();assert(f.remote(f.other,LocalAction::TradeMoney,t.id,t.revision,10));t=f.other.tradeView();assert(f.remote(f.other,LocalAction::TradeAccept,t.id,t.revision));
 const auto one=h.findSaved(2)->player,two=h.findSaved(3)->player;h.directory+="/missing/two-guests";t=f.guest.tradeView();assert(!f.remote(f.guest,LocalAction::TradeAccept,t.id,t.revision));
 assert(h.findSaved(2)->player.money==one.money && h.findSaved(3)->player.money==two.money && h.self.money==100);h.directory=f.directory;
 t=f.other.tradeView();assert(f.remote(f.other,LocalAction::TradeAccept,t.id,t.revision));t=f.guest.tradeView();assert(f.remote(f.guest,LocalAction::TradeAccept,t.id,t.revision));
 assert(h.findSaved(2)->player.money==110 && h.findSaved(3)->player.money==90 && h.self.money==100);
 LocalRealm::Impl loaded;loaded.gameplay.useContent(f.data);assert(loaded.parseSave(f.directory+"/realm.wprs") && loaded.findSaved(2)->player.money==110 && loaded.findSaved(3)->player.money==90);
 std::cout<<"PASS capacity and non-host owners: five-member check, persisted 50-name ignore cap, six wire offer slots and atomic guest-to-guest failure/commit/reload\n";
}
int main(){ready();tradeRules();tradeLan();cancellation();ignore();malformed();boundsAndGuests();}
