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
static LocalRealmPlayer player(uint64_t id){auto p=rewardPlayer(id);p.quests.clear();p.inventory={{117,20}};p.money=10000;return p;}
static LocalRealmNpc auctioneer(){auto n=rewardNpc(0xf13000000000000aULL);n.hostile=false;n.auctioneer=true;n.x=2;return n;}
static sockaddr_in loopback(uint16_t port){sockaddr_in a{};initAddress(a);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);return a;}
int main(){
 char temp[]="/tmp/wowps-auction-0175-XXXXXX";auto* dir=mkdtemp(temp);assert(dir);LocalRealm host,guest;auto& h=*host.impl_;auto& g=*guest.impl_;auto content=rewardContent();content->quests.clear();content->npcs[0].hostile=false;content->items[0].value=1;h.gameplay.useContent(content);g.gameplay.useContent(content);
 h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=dir;h.self=player(1);g.self=player(2);h.saved={{{1,11},h.self},{{2,22},g.self}};
 assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);
 h.gameplay.setRemoteNpcs({auctioneer()});g.gameplay.setRemoteNpcs({auctioneer()});h.refreshPlayers();g.players=h.players;auto& member=h.saved[1].player;const auto good=h.directory;std::string result;
 size_t maxDatagram=0;
 auto receive=[&]{for(;;){std::array<uint8_t,MaxPacket+1> b{};auto n=::recvfrom(g.socket,reinterpret_cast<char*>(b.data()),b.size(),net::datagramFlags(),nullptr,nullptr);if(n<0){assert(net::isWouldBlock(net::lastError()));break;}maxDatagram=std::max(maxDatagram,size_t(n));assert(size_t(n)<=MaxPacket);Reader r(b.data(),n);assert(r.u32()==WireMagic && r.u8()==Version);auto type=Message(r.u8());assert(r.u16()==n);auto seq=r.u32();auto token=r.u64();g.handleClient(type,r,g.host,token,seq);}};
 auto command=[&](LocalRealmCommand c){assert(guest.command(c));guest.update(.21f);h.receive();h.history(h.peers[0]);receive();assert(g.pendingCommands.empty());};
 auto replay=[&](uint32_t id,LocalRealmCommand c){Writer w;w.u32(id);w.u8(uint8_t(c.action));w.u64(c.target);w.u32(c.id);w.u32(c.bid);w.u32(c.buyout);w.u32(c.durationMinutes);if(c.action==LocalAction::ListAuction)w.u16(c.auctionCount);w.u64(c.serviceNpcGuid);g.send(Message::Command,g.session,w,g.host);h.receive();receive();};
 assert(h.botDirector.restoreAuctionSequence(100,result));
 LocalRealmCommand post{LocalAction::ListAuction,1,117,100,500,720};post.serviceNpcGuid=auctioneer().guid;
 h.directory+="/missing/post";assert(!host.command(post));assert(h.botDirector.nextAuctionId()==100 && h.botDirector.auctions().empty() && h.self.inventory[0].count==20);h.directory=good;
 assert(host.command(post));assert(h.botDirector.auctions()[0].id==100 && h.botDirector.nextAuctionId()==101);
 LocalRealmCommand bid{LocalAction::BidAuction,100,100};bid.serviceNpcGuid=auctioneer().guid;
 auto bad=bid;bad.serviceNpcGuid++;command(bad);assert(!h.peers[0].lastCommandSuccess && member.money==10000);
 h.directory+="/missing/bid";command(bid);assert(!h.peers[0].lastCommandSuccess && member.money==10000 && h.botDirector.auctions()[0].highestBid==0);h.directory=good;
 command(bid);if(!h.peers[0].lastCommandSuccess || member.money!=9900 || g.self.money!=9900)std::cerr<<"bid status="<<h.peers[0].lastCommandStatus<<" host="<<member.money<<" guest="<<g.self.money<<"\n";assert(h.peers[0].lastCommandSuccess && member.money==9900 && g.self.money==9900);
 auto altered=bid;altered.target=200;replay(h.peers[0].lastCommand,altered);assert(member.money==9900 && h.botDirector.auctions()[0].highestBid==100);
 LocalRealmCommand buy{LocalAction::BuyoutAuction,0,100};buy.serviceNpcGuid=auctioneer().guid;
 h.directory+="/missing/buy";command(buy);assert(!h.peers[0].lastCommandSuccess && member.money==9900 && member.inventory[0].count==20 && h.botDirector.auctions().size()==1 && h.botDirector.deliveries().empty());h.directory=good;
 command(buy);assert(h.peers[0].lastCommandSuccess && member.money==9500 && member.inventory.size()==1 && h.botDirector.auctions().empty() && h.botDirector.deliveries().size()==2);
 replay(h.peers[0].lastCommand,buy);assert(member.money==9500 && h.botDirector.deliveries().size()==2);
 assert(h.botDirector.deliver(member,*content));assert(member.inventory.size()==2 && h.botDirector.deliveries().size()==1);
 std::cout<<"PASS auction LAN transactions: production UDP post/bid/buyout, selected service rejection, host/guest disk rollback, changed replay payload inert, bidder credit and seller escrow\n";
 LocalRealm::Impl loaded,scanner;loaded.gameplay.useContent(content);assert(loaded.parseSave(good+"/realm.wprs") && scanner.parseSave(good+"/realm.wprs"));assert(loaded.botDirector.auctions().empty() && loaded.botDirector.nextAuctionId()==101 && scanner.botDirector.nextAuctionId()==101);auto& seller=loaded.findSaved(1)->player;
 assert(loaded.botDirector.listItemPriced(seller,117,1,100,500,720,*content,result));assert(loaded.botDirector.auctions()[0].id==101);
 // Verify an exact partial escrow balance survives the real save codec.
 h.self.money=LocalAuctionPricing::MoneyCap-30;assert(h.botDirector.deliver(h.self,*content));assert(h.botDirector.deliveries()[0].money==470);assert(h.saveRealm());assert(loaded.parseSave(good+"/realm.wprs"));assert(loaded.botDirector.deliveries()[0].money==470 && loaded.findSaved(1)->player.money==LocalAuctionPricing::MoneyCap);
 // This realm keeps no mail and no owned creatures, so the realm-level payload
 // after the vendor stock is exactly u32 sequence, u32 mail id, u16 mail count,
 // u8 pet count and the u32 checksum.
 assert(h.mailbox.messages.empty() && h.gameplay.pets().empty());
 constexpr size_t SequenceTail=4+4+2+1+4,PetTail=1+4;
 std::vector<uint8_t> bytes;assert(readFile(good+"/realm.wprs",bytes,MaxSaveSize));assert(bytes[4]==SaveVersion);assert(*(bytes.end()-PetTail)==0);
 // A previous-release file can no longer be fabricated by truncating a tail:
 // save 29 grew the PER-PLAYER block, not the realm tail, and save 30 grew the
 // realm-level PET block rather than either. Pin both facts at their own
 // boundaries instead of against a moving SaveVersion-1, and keep the
 // realm-level identities checked against the current file above.
 {
  auto legacyPlayer=h.self;
  Writer atTwentyEight;writeProgress(atTwentyEight,legacyPlayer,28);
  Writer atTwentyNine;writeProgress(atTwentyNine,legacyPlayer,29);
  Writer currentProgress;writeProgress(currentProgress,legacyPlayer);
  // 29 added the area emitters to the per-player block ...
  assert(atTwentyEight.bytes.size()<atTwentyNine.bytes.size());
  // ... and 30 left that block byte-identical, because what it added is the
  // pet's command state, react state and stay point, which live in the
  // realm-level roster the pet-count tail above already identifies.
  assert(atTwentyNine.bytes==currentProgress.bytes);
  LocalRealmPlayer restored=player(1);Reader r(atTwentyEight.bytes.data(),atTwentyEight.bytes.size());
  assert(readProgress(r,restored,28)&&r.done());
  assert(restored.areaEmitters.empty()&&restored.money==legacyPlayer.money&&
         restored.statAuras==legacyPlayer.statAuras&&restored.knownSpells==legacyPlayer.knownSpells);
 }
 // The block save 30 did grow, round-tripped through the one layout the save
 // and the LAN pet deck share.
 {
  LocalRealmPet summon;summon.guid=kLocalPetGuidPrefix|3;summon.ownerGuid=1;summon.entry=416;
  summon.displayId=4449;summon.summonSpellId=688;summon.kind=LocalPetKind::Controlled;summon.level=40;
  summon.health=summon.maxHealth=904;summon.resourceType=0;summon.power=summon.maxPower=1053;
  summon.attackPeriodMs=2000;summon.name="Jakyal";summon.x=3;summon.y=4;summon.z=0;
  summon.command=LocalPetCommand::Stay;summon.react=LocalPetReact::Defensive;
  summon.stayX=3;summon.stayY=4;summon.stayZ=0;
  assert(validLocalPet(summon));
  Writer petBytes;writePet(petBytes,summon);
  Reader petReader(petBytes.bytes.data(),petBytes.bytes.size());
  const auto restoredPet=readPet(petReader);
  assert(petReader.valid&&restoredPet==summon);
 }
 auto corrupt=bytes;std::fill(corrupt.end()-SequenceTail,corrupt.end()-SequenceTail+4,0);Writer checksumWriter;checksumWriter.u32(checksum(corrupt.data(),corrupt.size()-4));std::copy(checksumWriter.bytes.begin(),checksumWriter.bytes.end(),corrupt.end()-4);assert(atomicWrite(good+"/bad-sequence.wprs",corrupt,false));const auto savedSequence=loaded.botDirector.nextAuctionId();assert(!loaded.parseSave(good+"/bad-sequence.wprs") && loaded.botDirector.nextAuctionId()==savedSequence);
 std::cout<<"PASS auction save"<<int(SaveVersion)<<": empty-board sequence retained, next posting distinct, "
            "partial escrow reload, catalog-free roster scan, previous-format per-player migration, "
            "invalid sequence rejected without mutation\n";
 std::vector<LocalAuction> board;for(unsigned i=0;i<96;++i){LocalAuction a;a.id=200+i;a.itemId=117;a.count=1;a.bid=100;a.buyout=500;a.seller=1;a.sellerName="Seller";a.remainingSeconds=1000;board.push_back(a);}assert(h.botDirector.restoreAuctions(board,result));h.auctionBoard(h.peers[0],10);receive();assert(g.remoteAuctions.size()==96 && g.remoteAuctions.front().id==200 && g.remoteAuctions.back().id==295);
 assert(h.botDirector.restoreAuctions({},result));h.auctionBoard(h.peers[0],9);receive();assert(g.remoteAuctions.size()==96);h.auctionBoard(h.peers[0],11);receive();assert(g.remoteAuctions.empty());
 std::cout<<"PASS auction LAN board: 96 listings across bounded datagrams, stale snapshot rejected, empty-board refresh; maximum observed bytes="<<maxDatagram<<"\n";
 host.stop();guest.stop();std::filesystem::remove_all(dir);
}
