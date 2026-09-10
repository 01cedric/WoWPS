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
static LocalRealmPlayer player(uint64_t id){auto p=rewardPlayer(id);p.race=1;p.classId=1;p.quests.clear();p.inventory={{117,20}};p.money=10000;return p;}
static LocalRealmNpc inn(){auto n=rewardNpc(10);n.hostile=false;n.lootOwner=0;n.innkeeper=true;n.auctioneer=true;n.x=2;return n;}
static sockaddr_in loopback(uint16_t port){sockaddr_in a{};initAddress(a);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);return a;}
int main(){
 char temp[]="/tmp/wowps-mail-0183-XXXXXX";auto* dir=mkdtemp(temp);assert(dir);LocalRealm host,guest;auto& h=*host.impl_;auto& g=*guest.impl_;
 auto content=rewardContent();content->quests.clear();content->npcs[0].hostile=false;h.gameplay.useContent(content);g.gameplay.useContent(content);
 h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=dir;h.self=player(1);g.self=player(2);
 h.saved={{{1,11},h.self},{{2,22},g.self},{{3,33},player(3)}};h.gameplay.setRemoteNpcs({inn()});g.gameplay.setRemoteNpcs({inn()});h.refreshPlayers();g.players=h.players;
 auto& member=h.saved[1].player;const auto good=h.directory;std::string result;
 assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;
 LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);
 size_t maxDatagram=0;
 auto receive=[&]{for(;;){std::array<uint8_t,MaxPacket+1> bytes{};auto n=::recvfrom(g.socket,reinterpret_cast<char*>(bytes.data()),bytes.size(),net::datagramFlags(),nullptr,nullptr);
   if(n<0){assert(net::isWouldBlock(net::lastError()));break;}maxDatagram=std::max(maxDatagram,size_t(n));assert(size_t(n)<=1400);
   Reader r(bytes.data(),n);assert(r.u32()==WireMagic && r.u8()==Version);auto type=Message(r.u8());assert(r.u16()==n);auto seq=r.u32();auto token=r.u64();g.handleClient(type,r,g.host,token,seq);}};
 auto command=[&](LocalRealmCommand c){assert(guest.command(c));guest.update(.21f);h.receive();h.history(h.peers[0]);receive();assert(g.pendingCommands.empty());};
 auto action=[&](LocalAction a,uint32_t id,uint32_t slot=0){LocalRealmCommand c{a,slot,id};c.serviceNpcGuid=10;command(c);return h.peers[0].lastCommandSuccess;};
 auto query=[&]{h.now+=1;g.now+=1;guest.requestMail(10);h.receive();receive();};
 LocalRealmPlayer candidate;LocalMail letter;const auto initial=h.self;
 assert(prepareLocalMail(initial,member,"Test","Body",100,0,{{117,4,20,0}},*content,candidate,letter,result));
 assert(candidate.money==9870 && candidate.inventory[0].count==16 && letter.items[0].count==4 && initial.inventory[0].count==20);
 assert(!prepareLocalMail(initial,member,"","",0,0,{{117,4,19,0}},*content,candidate,letter,result));
 assert(!prepareLocalMail(initial,member,"","",0,0,{{117,4,20,0},{117,4,20,0}},*content,candidate,letter,result));
 auto enemy=member;enemy.race=2;assert(!prepareLocalMail(initial,enemy,"","",0,0,{},*content,candidate,letter,result));
 assert(!prepareLocalMail(initial,initial,"","",0,0,{},*content,candidate,letter,result));
 assert(!prepareLocalMail(initial,member,std::string(65,'s'),"",0,0,{},*content,candidate,letter,result));
 assert(!prepareLocalMail(initial,member,"","",1,2,{{117,1,20,0}},*content,candidate,letter,result));
 auto twelve=initial;twelve.inventory.assign(12,{117,20});std::vector<LocalTradeItem> all;
 for(uint8_t i=0;i<12;++i)all.push_back({117,20,20,i});assert(prepareLocalMail(twelve,member,"12","",0,0,all,*content,candidate,letter,result));
 assert(candidate.inventory.empty() && candidate.money==9640 && letter.items[11].count==20);
 std::cout<<"PASS mail rules: postage, exact stack fingerprints, duplicate rejection, faction/self/text/COD guards\n";
 h.directory+="/missing/send";assert(!host.sendMail(10,"Player2","One","Body",100,0,{{117,4,20,0}}));h.directory=good;
 assert(h.mailbox.messages.empty() && h.self.money==10000 && h.mailbox.nextId==1);
 assert(!host.sendMail(11,"Player2","One","Body",100,0,{}));
 assert(!host.sendMail(10,"Missing","One","Body",100,0,{}));
 assert(host.sendMail(10,"pLaYeR2","One","Body",100,0,{{117,4,20,0}}));
 assert(h.self.money==9870 && h.self.inventory[0].count==16 && h.mailbox.messages.size()==1);
 LocalRealm::Impl loaded,scanner;loaded.gameplay.useContent(content);assert(loaded.parseSave(good+"/realm.wprs") && scanner.parseSave(good+"/realm.wprs"));
 assert(loaded.mailbox.messages==h.mailbox.messages && loaded.mailbox.nextId==2);
 assert(host.sendMail(10,"Player3","Offline","Saved character",0,0,{}));assert(h.hasMailAssets(1) && h.hasMailAssets(2));
 std::cout<<"PASS mail authority: offline/case-insensitive recipient, service/unknown rejection, save failure rollback, Save14 and catalog-free reload\n";
 query();assert(g.remoteMail.size()==1 && g.remoteMail[0].recipient==2 && g.remoteMail[0].body=="Body");
 assert(!action(LocalAction::MailDelete,1));
 const auto before=member.money;h.directory+="/missing/take";assert(!action(LocalAction::MailTakeMoney,1));h.directory=good;
 assert(member.money==before && h.mailbox.find(2,1)->money==100);
 member.money=999999950;assert(action(LocalAction::MailTakeMoney,1));assert(member.money==1000000000 && h.mailbox.find(2,1)->money==50);
 assert(!action(LocalAction::MailTakeMoney,1));member.money=10000;assert(action(LocalAction::MailTakeMoney,1));assert(member.money==10050);
 auto inventory=member.inventory;member.inventory.assign(24,{117,20});assert(!action(LocalAction::MailTakeItem,1));assert(h.mailbox.find(2,1)->items[0].count==4);
 member.inventory=inventory;assert(action(LocalAction::MailTakeItem,1));assert(h.mailbox.find(2,1)->items[0].itemId==0);
 assert(!action(LocalAction::MailTakeItem,1));assert(action(LocalAction::MailDelete,1));assert(!h.mailbox.find(2,1));
 std::cout<<"PASS private UDP inbox and collection: owner isolation, partial wallet payout, full bags, write failure, empty-only delete, duplicate claim rejected\n";
 member.inventory={{117,20}};member.money=10000;
 assert(host.sendMail(10,"Player2","COD","",0,500,{{117,2,16,0}}));const auto codId=h.mailbox.messages.back().id;
 h.directory+="/missing/cod";assert(!action(LocalAction::MailTakeItem,codId));h.directory=good;assert(member.money==10000 && h.mailbox.find(2,codId)->cod==500);
 assert(action(LocalAction::MailTakeItem,codId));assert(member.money==9500 && h.mailbox.find(2,codId)->cod==0);
 auto payment=std::find_if(h.mailbox.messages.begin(),h.mailbox.messages.end(),[](const auto& m){return m.subject=="COD payment";});assert(payment!=h.mailbox.messages.end() && payment->money==500 && payment->recipient==1);
 LocalRealmCommand changed{LocalAction::MailTakeMoney,0,payment->id};changed.serviceNpcGuid=10;
 Writer replay;replay.u32(h.peers[0].lastCommand);replay.u8(uint8_t(changed.action));replay.u64(changed.target);replay.u32(changed.id);replay.u32(0);replay.u32(0);replay.u32(0);replay.u64(10);
 g.send(Message::Command,g.session,replay,g.host);h.receive();receive();assert(member.money==9500 && payment->money==500);
 assert(!action(LocalAction::MailTakeMoney,payment->id));
 assert(host.sendMail(10,"Player2","Return","",20,0,{}));const auto returnId=h.mailbox.messages.back().id;
 assert(action(LocalAction::MailReturn,returnId));assert(h.mailbox.find(1,returnId)->returned && h.mailbox.find(1,returnId)->money==20);
 assert(!host.mailAction(LocalAction::MailReturn,10,returnId));
 std::cout<<"PASS COD and returns: atomic one-time payment, changed replay inert, foreign ID refused, returned value retained, return loop refused\n";
 LocalRealmCommand send{LocalAction::MailSend};send.serviceNpcGuid=10;send.mailRecipient="Player3";send.mailSubject="LAN letter";send.mailBody=std::string(160,'b');send.mailAttachments={{117,1,20,0}};
 // Collection merged into the existing stack, so use the current exact source.
 send.mailAttachments[0].sourceCount=member.inventory[0].count;
 command(send);assert(h.peers[0].lastCommandSuccess && h.mailbox.messages.back().recipient==3 && h.mailbox.messages.back().body.size()==160);
 assert(guest.mailResultSuccess() && guest.mailResultRevision()>0);
 std::cout<<"PASS outgoing UDP letter: full text, checked attachment, host acknowledgment and offline recipient\n";
 // A full inbox retains old auction escrow; making room migrates once.
 while(h.mailbox.count(2)<LocalMailbox::MaxInbox){LocalMail m;m.recipient=2;m.sender=1;m.senderName="Player1";m.subject=std::string(64,'s');m.body=std::string(160,'b');assert(h.mailbox.append(m));}
 assert(h.botDirector.restoreDeliveries({{2,117,0,3}}));assert(!h.migrateAuctionMail());assert(h.botDirector.deliveries().size()==1);
 query();assert(g.remoteMail.size()==64);
 const auto fullMoney=h.self.money;assert(!host.sendMail(10,"Player2","Full","",0,0,{}));assert(h.self.money==fullMoney);
 const auto emptyId=std::find_if(h.mailbox.messages.begin(),h.mailbox.messages.end(),[](const auto& m){return m.recipient==2 && !m.money && !m.hasItems();})->id;
 assert(action(LocalAction::MailDelete,emptyId));assert(h.migrateAuctionMail());assert(h.botDirector.deliveries().empty());assert(!h.migrateAuctionMail());
 assert(h.saveRealm());assert(loaded.parseSave(good+"/realm.wprs"));assert(loaded.mailbox.messages==h.mailbox.messages && loaded.botDirector.deliveries().empty());
 std::cout<<"PASS escrow migration: full inbox backpressure, exact item delivery, no repeat migration, atomic Save14 reload, 64-letter private pages\n";
 // Save13 migration preserves pending escrow without interpreting mail bytes.
 h.mailbox={};assert(h.botDirector.restoreDeliveries({{2,117,0,3}}));assert(h.saveRealm());
 std::vector<uint8_t> bytes;assert(readFile(good+"/realm.wprs",bytes,MaxSaveSize));assert(bytes[4]==19);
 Writer old;old.u32(SaveMagic);old.u8(13);old.u64(h.realmId);old.u16(uint16_t(h.saved.size()));
 for(const auto& row:h.saved){old.u64(row.identity.a);old.u64(row.identity.b);writePlayer(old,row.player);writeProgress(old,row.player,13);writeAppearance(old,row.player);old.u32(0);old.u8(row.player.introSeen);writeBuyback(old,row.player.buybackSerial,row.player.buyback);}
 old.u8(0);old.u64(0);old.u16(0);old.u16(1);old.u64(2);old.u32(117);old.u32(0);old.u16(3);old.u16(0);old.u32(h.botDirector.nextAuctionId());old.u32(checksum(old.bytes.data(),old.bytes.size()));auto legacy=old.bytes;
 assert(atomicWrite(good+"/legacy13.wprs",legacy,false));assert(loaded.parseSave(good+"/legacy13.wprs"));assert(loaded.mailbox.messages.empty() && loaded.botDirector.deliveries().size()==1);
 std::fill(bytes.end()-10,bytes.end()-6,0);Writer badsum;badsum.u32(checksum(bytes.data(),bytes.size()-4));std::copy(badsum.bytes.begin(),badsum.bytes.end(),bytes.end()-4);
 assert(atomicWrite(good+"/bad-mail.wprs",bytes,false));assert(!loaded.parseSave(good+"/bad-mail.wprs"));assert(loaded.botDirector.deliveries().size()==1);
 std::cout<<"PASS save migration: legacy Save13 escrow retained; invalid mail sequence rejected before state mutation\n";
 // Purchases succeed with full bags and enqueue both buyer goods and seller money.
 LocalBotDirector market;auto seller=player(3),buyer=player(2);buyer.inventory.assign(24,{117,20});
 assert(market.listItemPriced(seller,117,1,100,500,720,*content,result));auto auctionId=market.auctions()[0].id;
 assert(market.buyout(auctionId,buyer,*content,result));assert(buyer.money==9500 && buyer.inventory.size()==24 && market.deliveries().size()==2);
 assert(!market.buyout(auctionId,buyer,*content,result));
 std::cout<<"PASS auction mail settlement: full-bag buyout, durable buyer goods/seller proceeds, repeat purchase rejected\n";
 LocalRealm::Impl receiver;receiver.self=player(2);LocalMail a;a.id=1;a.sender=1;a.recipient=2;a.senderName="Player1";auto b=a;b.id=2;auto c=a;c.id=3;
 auto page=[&](uint32_t tick,uint8_t part,uint64_t owner,std::vector<LocalMail> rows){Writer w;w.u32(tick);w.u64(owner);w.u8(part);w.u8(2);w.u8(uint8_t(rows.size()));for(const auto& m:rows)writeMail(w,m);Reader r(w.bytes.data(),w.bytes.size());receiver.receiveMailState(r);};
 page(1,1,2,{c});assert(receiver.remoteMail.empty());page(1,0,2,{a,b});assert(receiver.remoteMail.size()==3);
 page(2,0,3,{a,b});page(2,1,2,{c});assert(receiver.mailSequence==1);
 page(3,0,2,{a,a});page(3,1,2,{c});assert(receiver.mailSequence==1);
 page(4,0,2,{a,b});page(4,1,2,{c});assert(receiver.mailSequence==4);page(3,0,2,{a,b});assert(receiver.mailSequence==4);
 std::cout<<"PASS mail snapshot assembly: out-of-order pages commit together, foreign owner/duplicate IDs/stale revisions rejected\n";
 g.clearMail();assert(g.remoteMail.empty() && !guest.mailResultSuccess());
 std::cout<<"PASS disconnect clears private inbox; largest observed datagram="<<maxDatagram<<" bytes\n";
 host.stop();guest.stop();std::filesystem::remove_all(dir);
}
