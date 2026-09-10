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
static std::shared_ptr<LocalWorldContent> fixture(){
    auto c=rewardContent();c->quests.clear();c->npcs[0].hostile=false;c->npcs[0].vendorItems={117,118,119,900000};
    c->items[0].value=1;
    for(uint32_t id:{118,119,900000}){LocalItemDefinition i;i.id=id;i.name="Bank fixture";i.stack=20;i.value=id==900000?200000001:3;c->items.push_back(i);}return c;
}
static LocalRealmPlayer player(uint64_t id){auto p=rewardPlayer(id);p.quests.clear();p.money=1000000000;p.inventory={{117,10},{118,4},{119,2}};p.bank[0]={117,18};p.bank[1]={118,3};return p;}
static LocalRealmNpc banker(){auto n=rewardNpc(0xf13000000000000aULL);n.hostile=false;n.banker=n.vendor=true;n.x=2;return n;}
static LocalRealmCommand deposit(const LocalRealmPlayer& p,uint32_t bag,uint32_t bank,uint16_t count){
    LocalRealmCommand c{LocalAction::BankDepositSlot,count,bag};c.buyout=bank;c.bid=p.inventory[bag-1].itemId;c.bankSourceCount=p.inventory[bag-1].count;c.durationMinutes=p.bank[bank-1].itemId;c.bankDestinationCount=p.bank[bank-1].count;c.serviceNpcGuid=banker().guid;return c;
}
static void authority(){
    LocalGameplay game;auto content=fixture();game.useContent(content);game.setRemoteNpcs({banker()});auto p=player(1);std::vector<LocalRealmPlayer*> ps{&p};std::string result;
    auto call=[&](LocalRealmCommand c){return game.execute(p,c,ps,result);};
    auto reject=[&](LocalRealmCommand c){auto before=p;assert(!call(c));assert(p.inventory==before.inventory && p.bank==before.bank && p.money==before.money);};
    assert(call(deposit(p,1,28,4)) && p.inventory[0].count==6 && p.bank[27].count==4 && p.bank[2].itemId==0);
    assert(call(deposit(p,1,1,2)) && p.bank[0].count==20 && p.inventory[0].count==4);reject(deposit(p,1,1,1));
    reject(deposit(p,1,2,1));assert(call(deposit(p,1,2,4)));assert((p.inventory[0]==LocalItemStack{118,3}) && (p.bank[1]==LocalItemStack{117,4}));
    assert(call(deposit(p,3,27,2)));assert(p.inventory.size()==2 && (p.bank[26]==LocalItemStack{119,2}));
    p=player(1);p.inventory={{117,4},{117,9},{119,2}};assert(call(deposit(p,2,28,5)));assert(p.inventory.size()==3 && p.inventory[0].count==4 && p.inventory[1].count==4);
    p=player(1);auto c=deposit(p,1,28,1);c.bid=119;reject(c);c=deposit(p,1,28,1);c.bankSourceCount--;reject(c);
    c=deposit(p,1,1,1);c.bankDestinationCount--;reject(c);c=deposit(p,1,1,1);c.durationMinutes=118;reject(c);
    c=deposit(p,1,28,1);c.id=0;reject(c);c.id=25;reject(c);c=deposit(p,1,28,1);c.buyout=29;reject(c);c.buyout=0;reject(c);
    c=deposit(p,1,28,1);c.target=65536;reject(c);c.target=0;reject(c);c.target=11;reject(c);
    c=deposit(p,1,28,1);c.serviceNpcGuid=0;reject(c);c.serviceNpcGuid++;reject(c);
    p.equipment[0]=117;reject(deposit(p,1,28,10));assert(call(deposit(p,1,28,9)) && p.inventory[0].count==1);p=player(1);
    for(int mode=0;mode<6;++mode){auto n=banker();p=player(1);if(mode==0)p.dead=true;if(mode==1)p.flight.active=true;if(mode==2)p.castingSpellId=1;if(mode==3)p.attackTarget=99;if(mode==4)n.x=100;if(mode==5)n.instanceId=1;game.setRemoteNpcs({n});reject(deposit(p,1,28,1));}
    game.setRemoteNpcs({banker()});p=player(1);p.inventory.assign(24,{117,20});p.bank.fill({118,20});assert(call(deposit(p,24,28,20)));assert(p.inventory.size()==24 && p.inventory[23].itemId==118 && p.bank[27].itemId==117);
    std::cout<<"PASS targeted bank authority: chosen empty/merge/swap slots, full bags/bank, exact source consumption, stale item/count guards, malformed arguments, equipped-copy and service/state protections\n";
    p=player(1);LocalRealmCommand buy{LocalAction::BuyFromVendor,1,900000};buy.serviceNpcGuid=banker().guid;reject(buy);assert(localVendorBuyTotal(*content->item(900000),1)==1000000005ULL);
    p.inventory={{900000,5}};p.money=0;LocalRealmCommand sell{LocalAction::SellToVendor,5,900000};sell.serviceNpcGuid=banker().guid;reject(sell);assert(p.buyback.empty());
    content->items.back().value=200000000;p.money=1000000000;buy.target=1;assert(call(buy) && p.money==0);sell.target=5;assert(call(sell) && p.money==1000000000);
    LocalItemDefinition extreme;extreme.id=900001;extreme.value=UINT32_MAX;assert(localVendorBuyTotal(extreme,UINT32_MAX)==UINT64_MAX);
    std::cout<<"PASS merchant totals: no clipped-price purchases or clipped sale proceeds, exact wallet boundary accepted, public wide-total overflow guarded\n";
}
static sockaddr_in loopback(uint16_t port){sockaddr_in a{};initAddress(a);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);return a;}
static void bankLan(){
    char temp[]="/tmp/wowps-bank-0174-XXXXXX";auto* dir=mkdtemp(temp);assert(dir);LocalRealm host,guest;auto& h=*host.impl_;auto& g=*guest.impl_;auto content=fixture();h.gameplay.useContent(content);g.gameplay.useContent(content);
    h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=dir;h.self=player(1);g.self=player(2);h.saved={{{1,11},h.self},{{2,22},g.self}};
    assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);
    h.gameplay.setRemoteNpcs({banker()});g.gameplay.setRemoteNpcs({banker()});h.refreshPlayers();g.players=h.players;auto& member=h.saved[1].player;const auto good=h.directory;
    auto receive=[&]{for(;;){std::array<uint8_t,MaxPacket+1> b{};auto n=::recvfrom(g.socket,reinterpret_cast<char*>(b.data()),b.size(),net::datagramFlags(),nullptr,nullptr);if(n<0){assert(net::isWouldBlock(net::lastError()));break;}assert(size_t(n)<=MaxPacket);Reader r(b.data(),n);assert(r.u32()==WireMagic && r.u8()==Version);auto type=Message(r.u8());assert(r.u16()==n);auto seq=r.u32();auto token=r.u64();g.handleClient(type,r,g.host,token,seq);}};
    auto command=[&](LocalRealmCommand c){assert(guest.command(c));guest.update(.21f);h.receive();h.history(h.peers[0]);receive();assert(g.pendingCommands.empty());};
    auto wire=[&](uint32_t id,LocalRealmCommand c,bool extension){Writer w;w.u32(id);w.u8(uint8_t(c.action));w.u64(c.target);w.u32(c.id);w.u32(c.bid);w.u32(c.buyout);w.u32(c.durationMinutes);if(extension){w.u16(c.bankSourceCount);w.u16(c.bankDestinationCount);}w.u64(c.serviceNpcGuid);assert(HeaderSize+w.bytes.size()<=MaxPacket);g.send(Message::Command,g.session,w,g.host);h.receive();receive();};
    auto c=deposit(member,1,28,4);wire(1,c,false);assert(h.peers[0].lastCommand==0 && member.inventory[0].count==10);
    h.directory+="/missing/deposit";command(c);assert(!h.peers[0].lastCommandSuccess && member.inventory[0].count==10 && member.bank[27].itemId==0 && g.self.bank[27].itemId==0);
    assert(!host.depositBankSlot(1,28,4,{117,10},{},banker().guid));assert(h.self.inventory[0].count==10);
    h.directory=good;command(c);assert(h.peers[0].lastCommandSuccess && member.inventory[0].count==6 && member.bank[27].count==4 && g.self.bank==member.bank);
    auto changed=c;changed.buyout=27;wire(h.peers[0].lastCommand,changed,true);assert(member.bank[26].itemId==0 && member.bank[27].count==4);
    command(c);assert(!h.peers[0].lastCommandSuccess && member.inventory[0].count==6); // stale source/destination snapshots
    assert(host.depositBankSlot(1,28,4,{117,10},{},banker().guid));
    c=deposit(member,1,2,6);auto before=member;h.directory+="/missing/swap";command(c);assert(member.inventory==before.inventory && member.bank==before.bank);h.directory=good;command(c);assert((member.inventory[0]==LocalItemStack{118,3}) && (member.bank[1]==LocalItemStack{117,6}) && g.self.inventory==member.inventory);
    assert(host.depositBankSlot(1,2,6,{117,6},{118,3},banker().guid));
    LocalRealm::Impl loaded,scanner;loaded.gameplay.useContent(content);assert(loaded.parseSave(h.directory+"/realm.wprs") && scanner.parseSave(h.directory+"/realm.wprs"));for(uint64_t id:{1,2}){const auto* p=loaded.findSaved(id);assert(p && (p->player.inventory[0]==LocalItemStack{118,3}) && (p->player.bank[1]==LocalItemStack{117,6}) && p->player.bank[27].count==4);assert(scanner.findSaved(id)->player.bank==p->player.bank);}
    host.stop();guest.stop();std::filesystem::remove_all(dir);
    std::cout<<"PASS targeted bank LAN/save: actual new command encoding, missing extension rejection, host/guest disk rollback, stale/replayed/changed-destination commands, slot swaps, save12 and catalog-free reload\n";
}
int main(){authority();bankLan();}
