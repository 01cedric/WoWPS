#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include "local_group_rewards_fixture.hpp"
#include <iostream>
#include <cassert>
using namespace wowee::game;
namespace net=wowee::net;
static LocalRealmPlayer player(uint64_t id){auto p=rewardPlayer(id);p.quests.clear();p.race=1;p.classId=1;p.money=10000;p.inventory={{117,10,0},{118,4,5},{900001,1,23}};p.bank[0]={117,18};p.bank[1]={118,3};return p;}
static LocalRealmNpc banker(){auto n=rewardNpc(10);n.hostile=false;n.lootOwner=0;n.banker=n.innkeeper=n.vendor=true;n.x=2;return n;}
static std::shared_ptr<LocalWorldContent> fixture(){auto c=rewardContent();c->quests.clear();c->npcs[0].hostile=false;c->items[0].value=1;LocalItemDefinition potion;potion.id=118;potion.name="Potion";potion.stack=20;potion.value=1;c->items.push_back(potion);auto gear=potion;gear.id=900001;gear.inventoryType=1;gear.stack=1;c->items.push_back(gear);return c;}
static LocalRealmCommand move(const LocalRealmPlayer& p,unsigned from,unsigned to,unsigned n,bool bank=false){
 auto i=localInventoryIndex(p,from-1),j=localInventoryIndex(p,to-1);auto a=bank?p.bank[from-1]:(i<p.inventory.size()?p.inventory[i]:LocalItemStack{});auto b=j<p.inventory.size()?p.inventory[j]:LocalItemStack{};
 LocalRealmCommand cmd{bank?LocalAction::BankWithdrawSlot:LocalAction::BackpackMove,n,from};cmd.buyout=to;cmd.bid=a.itemId;cmd.durationMinutes=b.itemId;cmd.bankSourceCount=a.count;cmd.bankDestinationCount=b.count;cmd.serviceNpcGuid=bank?10:0;return cmd;
}
static sockaddr_in loopback(uint16_t port){sockaddr_in a{};initAddress(a);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);return a;}
int main(){
 auto content=fixture();LocalGameplay game;game.useContent(content);game.setRemoteNpcs({banker()});auto p=player(1);std::string result;std::vector<LocalRealmPlayer*> players{&p};
 auto call=[&](LocalRealmCommand c){return game.execute(p,c,players,result);};auto reject=[&](LocalRealmCommand c){auto prior=p;assert(!call(c));assert(p.inventory==prior.inventory && p.bank==prior.bank && p.equipment==prior.equipment);};
 assert(call(move(p,1,24,10)));assert(p.inventory[0].itemId==900001 && p.inventory[0].bagSlot==0 && p.inventory[2].itemId==117 && p.inventory[2].bagSlot==23);
 assert(call(move(p,24,12,4)));assert(p.inventory[localInventoryIndex(p,11)].count==4 && p.inventory[localInventoryIndex(p,23)].count==6);
 assert(call(move(p,24,12,6)));assert(localInventoryIndex(p,23)==p.inventory.size() && p.inventory[localInventoryIndex(p,11)].count==10);
 assert(p.inventory[localInventoryIndex(p,5)].itemId==118 && p.inventory[localInventoryIndex(p,0)].itemId==900001);
 auto stale=move(p,12,13,1);--stale.bankSourceCount;reject(stale);reject(move(p,12,6,1));reject(move(p,12,12,1));auto invalid=move(p,12,13,1);invalid.buyout=25;reject(invalid);
 std::cout<<"PASS backpack cells: whole swap, split, merge, unrelated positions stable, stale/same-cell/partial-swap/invalid rejection\n";
 p=player(1);assert(call(move(p,1,13,5,true)));assert(p.bank[0].count==13 && p.inventory[localInventoryIndex(p,12)].count==5);
 assert(call(move(p,2,6,3,true)));assert(p.bank[1].itemId==0 && p.inventory[localInventoryIndex(p,5)].count==7);
 p=player(1);assert(call(move(p,1,6,18,true)));assert(p.bank[0].itemId==118 && p.bank[0].count==4 && p.inventory[localInventoryIndex(p,5)].itemId==117);
 p=player(1);p.equipment[0]=900001;reject(move(p,1,24,18,true));p.equipment[0]=0;
 for(int mode=0;mode<5;++mode){p=player(1);auto c=move(p,1,13,1,true);if(mode==0)c.serviceNpcGuid=11;if(mode==1)p.dead=true;if(mode==2)p.attackTarget=99;if(mode==3)p.flight.active=true;if(mode==4)p.castingSpellId=1;reject(c);}
 p=player(1);p.inventory.assign(24,{117,20});normalizeLocalInventory(p);p.bank[1]={118,20};assert(call(move(p,2,24,20,true)));assert(p.inventory[23].itemId==118 && p.bank[1].itemId==117);
 std::cout<<"PASS exact bank withdrawal: chosen empty/merge/swap cells, full backpack, worn-copy and selected-service/state protection\n";
 p=player(1);p.inventory={{117,4,0},{117,9,8},{118,1,23}};LocalRealmCommand deposit{LocalAction::BankDepositFromSlot,5,9};deposit.bid=117;deposit.buyout=9;deposit.serviceNpcGuid=10;
 assert(call(deposit));assert(p.inventory[localInventoryIndex(p,0)].count==4 && p.inventory[localInventoryIndex(p,8)].count==4 && p.inventory[localInventoryIndex(p,23)].count==1);reject(deposit);
 p.bank.fill({118,20});deposit.target=1;deposit.buyout=4;reject(deposit);
 p=player(1);LocalRealmCommand targeted{LocalAction::BankDepositSlot,1,24};targeted.bid=900001;targeted.buyout=28;targeted.bankSourceCount=1;targeted.serviceNpcGuid=10;assert(call(targeted));assert(p.bank[27].itemId==900001 && localInventoryIndex(p,23)==p.inventory.size() && p.inventory[localInventoryIndex(p,5)].itemId==118);
 std::cout<<"PASS physical bank deposits: chosen duplicate stack only, multi-bank-slot fill, stale/full-bank rollback and sparse source placement\n";
 // Normal removal, trade and mail retain remaining physical cells.
 p=player(1);LocalRealmPlayer recipient=player(2);LocalRealmPlayer sent;LocalMail letter;
 assert(prepareLocalMail(p,recipient,"Cells","",0,0,{{118,4,4,5}},*content,sent,letter,result));assert(localInventoryIndex(sent,5)==sent.inventory.size() && sent.inventory[localInventoryIndex(sent,23)].itemId==900001);
 assert(giveLocalMailItem(sent,{118,2},*content));assert(sent.inventory[localInventoryIndex(sent,23)].itemId==900001 && validLocalInventoryLayout(sent));
 LocalTrade trade;trade.state=2;trade.players={p.guid,recipient.guid};trade.items[0][0]={118,4,4,5};trade.fingerprints={localTradeFingerprint(p),localTradeFingerprint(recipient)};LocalRealmPlayer a,b;
 assert(prepareLocalTrade(trade,p,recipient,*content,a,b,result));assert(localInventoryIndex(a,5)==a.inventory.size() && a.inventory[localInventoryIndex(a,23)].itemId==900001);
 LocalBotDirector market;assert(market.listItemPriced(p,118,4,100,500,720,*content,result));assert(localInventoryIndex(p,5)==p.inventory.size() && p.inventory[localInventoryIndex(p,23)].itemId==900001);
 std::cout<<"PASS cross-system positions: noncontiguous mail/trade sources, incoming mail capacity and auction removal preserve other cells\n";
 // Exercise actual metadata masks, not a duplicate requirement implementation.
 p=player(1);const LocalAuctionItemMetadata* restricted=nullptr;
 for(const auto& m:kLocalAuctionItems)if(m.allowableClasses && m.allowableClasses!=UINT32_MAX && (m.allowableClasses&1) && !(m.allowableClasses&128) && m.requiredLevel>1 && m.requiredLevel<=80){restricted=&m;break;}
 assert(restricted);LocalItemDefinition item;item.id=restricted->id;item.name="Restricted gear";item.inventoryType=1;content->items.push_back(item);std::sort(content->items.begin(),content->items.end(),[](auto& a,auto& b){return a.id<b.id;});p.inventory={{item.id,1,3}};
 LocalRealmCommand equip{LocalAction::EquipItem,1,item.id};reject(equip);p.level=80;p.classId=8;reject(equip);p.classId=1;
 for(uint8_t race=1;race<=11;++race)if(!restricted->allowableRaces || (restricted->allowableRaces&(1u<<(race-1)))){p.race=race;break;}
 assert(call(equip));assert(p.equipment[0]==item.id);p.attackTarget=99;reject({LocalAction::UnequipItem,0,0});p.attackTarget=0;assert(call({LocalAction::UnequipItem,0,0}));
 std::cout<<"PASS equipment requirements: production level/class/race metadata, allowed equip and combat refusal\n";
 char temp[]="/tmp/wowps-layout-0184-XXXXXX";auto* dir=mkdtemp(temp);assert(dir);LocalRealm host,guest;auto& h=*host.impl_;auto& g=*guest.impl_;h.gameplay.useContent(content);g.gameplay.useContent(content);
 h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=dir;h.self=player(1);g.self=player(2);h.saved={{{1,11},h.self},{{2,22},g.self}};
 h.gameplay.setRemoteNpcs({banker()});g.gameplay.setRemoteNpcs({banker()});h.refreshPlayers();g.players=h.players;auto& member=h.saved[1].player;
 assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);
 auto receive=[&]{for(;;){std::array<uint8_t,MaxPacket+1> bytes{};auto n=::recvfrom(g.socket,reinterpret_cast<char*>(bytes.data()),bytes.size(),net::datagramFlags(),nullptr,nullptr);if(n<0){assert(net::isWouldBlock(net::lastError()));break;}Reader r(bytes.data(),n);assert(r.u32()==WireMagic && r.u8()==Version);auto type=Message(r.u8());assert(r.u16()==n);auto seq=r.u32();auto token=r.u64();g.handleClient(type,r,g.host,token,seq);}};
 auto command=[&](LocalRealmCommand cmd){assert(guest.command(cmd));guest.update(.21f);h.receive();h.history(h.peers[0]);receive();assert(g.pendingCommands.empty());};
 const auto good=h.directory;auto cmd=move(member,1,20,10);h.directory+="/missing";command(cmd);assert(!h.peers[0].lastCommandSuccess && member.inventory==player(2).inventory);h.directory=good;
 command(cmd);assert(h.peers[0].lastCommandSuccess && localInventoryIndex(member,0)==member.inventory.size() && g.self.inventory==member.inventory);
 Writer replay;replay.u32(h.peers[0].lastCommand);replay.u8(uint8_t(cmd.action));replay.u64(cmd.target);replay.u32(cmd.id);replay.u32(cmd.bid);replay.u32(21);replay.u32(cmd.durationMinutes);replay.u16(cmd.bankSourceCount);replay.u16(cmd.bankDestinationCount);replay.u64(0);
 g.send(Message::Command,g.session,replay,g.host);h.receive();receive();assert(localInventoryIndex(member,19)<member.inventory.size() && localInventoryIndex(member,20)==member.inventory.size());
 command(move(member,1,13,2,true));assert(h.peers[0].lastCommandSuccess && g.self.inventory==member.inventory);
 auto depositSource=member.inventory[localInventoryIndex(member,19)];
 assert(guest.depositBankFromSlot(20,3,depositSource,10));guest.update(.21f);h.receive();h.history(h.peers[0]);receive();
 assert(g.pendingCommands.empty() && h.peers[0].lastCommandSuccess && g.self.inventory==member.inventory);
 assert(member.inventory[localInventoryIndex(member,19)].count==7 && member.inventory[localInventoryIndex(member,12)].count==2);
 std::cout<<"PASS production UDP layout: saved host/guest physical cells, disk rollback, exact bank destination and altered replay ignored\n";
 auto hostPrior=h.self;h.directory+="/missing/equip";assert(!host.equipItem(900001,0));assert(h.self.equipment==hostPrior.equipment);h.directory=good;assert(host.equipItem(900001,0));
 LocalRealm::Impl loaded;loaded.gameplay.useContent(content);assert(loaded.parseSave(good+"/realm.wprs"));assert(loaded.findSaved(1)->player.equipment[0]==900001 && loaded.findSaved(2)->player.inventory==member.inventory);
 h.directory+="/missing/unequip";assert(!host.unequipItem(0));assert(h.self.equipment[0]==900001);h.directory=good;assert(host.unequipItem(0));
 std::cout<<"PASS equipment transactions: equip/unequip disk rollback, confirmed save and real realm reload\n";
 Writer progress;writeProgress(progress,member,15);Reader pr(progress.bytes.data(),progress.bytes.size());LocalRealmPlayer copy;assert(readProgress(pr,copy,15) && pr.done() && copy.inventory==member.inventory);
 auto malformed=progress.bytes;assert(member.inventory.size()>1);malformed.back()=malformed[malformed.size()-2];Reader bad(malformed.data(),malformed.size());assert(!readProgress(bad,copy,15));
 // Minimal genuine Save14 realm, using the old progress layout and unchanged tail.
 Writer legacy;legacy.u32(SaveMagic);legacy.u8(14);legacy.u64(123);legacy.u16(1);legacy.u64(1);legacy.u64(11);auto old=player(1);writePlayer(legacy,old);writeProgress(legacy,old,14);writeAppearance(legacy,old);legacy.u32(0);legacy.u8(0);writeBuyback(legacy,0,{});legacy.u8(0);legacy.u64(0);legacy.u16(0);legacy.u16(0);legacy.u16(0);legacy.u32(1);legacy.u32(1);legacy.u16(0);legacy.u32(checksum(legacy.bytes.data(),legacy.bytes.size()));
 assert(atomicWrite(good+"/legacy14.wprs",legacy.bytes,false));assert(loaded.parseSave(good+"/legacy14.wprs"));auto& migrated=loaded.saved[0].player;assert(migrated.inventory.size()==3 && migrated.inventory[0].bagSlot==0 && migrated.inventory[1].bagSlot==1 && migrated.inventory[2].bagSlot==2);
 std::cout<<"PASS Save15 codec: sparse layout roundtrip, duplicate physical cells rejected, genuine Save14 migration keeps old display order\n";
 host.stop();guest.stop();std::filesystem::remove_all(dir);
}
