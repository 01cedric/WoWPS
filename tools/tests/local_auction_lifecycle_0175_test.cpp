#include "game/local_bots.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace wowee::game;
namespace wowee::game {
const LocalItemDefinition* LocalWorldContent::item(uint32_t id) const {for(const auto& i:items)if(i.id==id)return &i;return nullptr;}
const LocalSpellDefinition* LocalWorldContent::spell(uint32_t id) const {for(const auto& s:spells)if(s.id==id)return &s;return nullptr;}
}
static LocalAuction listing(uint32_t id,uint64_t seller){LocalAuction a;a.id=id;a.itemId=800001;a.count=1;a.bid=100;a.buyout=500;a.seller=seller;a.sellerName="Seller";a.remainingSeconds=100;return a;}
int main(){
 LocalWorldContent world;LocalItemDefinition item;item.id=800001;item.name="Fixture";item.stack=20;item.value=100;world.items={item};std::string error;std::vector<LocalRealmPlayer> players;
 LocalRealmPlayer buyer;buyer.guid=1;buyer.name="Buyer";buyer.money=10000;
 const uint64_t virtualSeller=0x0A11000000000001ULL;
 std::vector<LocalAuctionDelivery> full(LocalBotDirector::MaxDeliveries,{99,0,1,0});
 LocalBotDirector d;assert(d.restoreDeliveries(full));assert(d.restoreAuctions({listing(1,virtualSeller)},error));
 assert(d.placeBid(1,buyer,100,error));assert(d.placeBid(1,buyer,105,error));assert(d.deliveries().size()==1024 && buyer.money==9895);
 auto second=buyer;second.guid=2;assert(!d.placeBid(1,second,111,error) && second.money==9895);
 assert(d.buyout(1,buyer,world,error));assert(buyer.money==9500 && buyer.inventory.size()==1 && d.deliveries().size()==1024);
 auto a=listing(2,virtualSeller);a.remainingSeconds=0;assert(d.restoreAuctions({a},error));assert(d.tick(0,world,players) && d.auctions().empty());
 a=listing(3,virtualSeller);a.highestBid=100;a.highestBidder=1;a.remainingSeconds=0;assert(d.restoreAuctions({a},error));assert(!d.tick(0,world,players));
 full.pop_back();assert(d.restoreDeliveries(full));assert(d.tick(0,world,players));assert(d.auctions().empty() && d.deliveries().size()==1024 && d.deliveries().back().recipient==1);
 a=listing(4,2);a.highestBid=100;a.highestBidder=1;a.remainingSeconds=0;assert(d.restoreAuctions({a},error));assert(d.restoreDeliveries(full));assert(!d.tick(0,world,players) && d.auctions().size()==1);
 full.pop_back();assert(d.restoreDeliveries(full));assert(d.tick(0,world,players) && d.deliveries().size()==1024);
 // Human buyout with exactly one free proceeds slot.
 assert(d.restoreAuctions({listing(5,2)},error));full.push_back({99,0,1,0});assert(d.restoreDeliveries(full));assert(d.buyout(5,buyer,world,error) && d.deliveries().size()==1024);
 // A virtual buyer needs only one slot when there is no prior bidder.
 assert(d.restoreAuctions({listing(6,2)},error));assert(d.restoreDeliveries(full));
 for(int i=0;i<80 && !d.auctions().empty();++i)d.tick(1,world,players);
 assert(d.auctions().empty() && d.deliveries().size()==1024 && d.deliveries().back().money==500);
 // An expired bid with two required deliveries cannot be bought virtually
 // while only one entry is free; its bid escrow stays intact.
 a=listing(7,2);a.remainingSeconds=0;a.highestBid=100;a.highestBidder=1;
 assert(d.restoreAuctions({a},error));assert(d.restoreDeliveries(full));
 for(int i=0;i<80;++i)d.tick(1,world,players);
 assert(d.auctions().size()==1 && d.auctions()[0].highestBid==100 && d.deliveries().size()==1023);
 std::cout<<"PASS auction queue boundaries: zero/one/two deliveries, full-queue self bids and virtual buyouts, blocked outbid/expiry, exact proceeds space\n";
 buyer.money=LocalAuctionPricing::MoneyCap-30;assert(d.restoreDeliveries({{1,0,100,0}}));assert(d.deliver(buyer,world));assert(buyer.money==LocalAuctionPricing::MoneyCap && d.deliveries()[0].money==70);assert(!d.deliver(buyer,world));
 LocalBotDirector reload;assert(reload.restoreDeliveries(d.deliveries()));buyer.money-=50;assert(reload.deliver(buyer,world) && reload.deliveries()[0].money==20);buyer.money-=20;assert(reload.deliver(buyer,world) && reload.deliveries().empty());
 assert(reload.restoreDeliveries({{1,800001,100,1}}));buyer.money-=30;const auto before=buyer.inventory;assert(!reload.deliver(buyer,world) && buyer.inventory==before && reload.deliveries()[0].money==100);
 std::cout<<"PASS escrow wallet limit: exact partial money remainder, repeat/reload, final drain and atomic mixed delivery\n";
 assert(d.restoreAuctions({},error));assert(d.restoreAuctionSequence(123,error));assert(!d.restoreAuctionSequence(122,error));assert(!d.restoreAuctionSequence(0,error));buyer.inventory={{800001,20}};assert(d.listItemPriced(buyer,800001,1,100,0,720,world,error));assert(d.auctions().back().id==123 && d.nextAuctionId()==124);
 assert(d.restoreAuctionSequence(UINT32_MAX,error));assert(!d.listItemPriced(buyer,800001,1,100,0,720,world,error));
 std::cout<<"PASS auction sequence: monotonic restore, invalid/retrograde rejection and exhausted ID guard\n";
 LocalBotDirector bots;bots.setEnabled(true);bots.setBotCount(1);buyer.instanceId=42;bots.populate(world,buyer,players);assert(players.empty());buyer.instanceId=0;bots.populate(world,buyer,players);assert(players.size()==1);
 players[0].inventory={{800001,20}};
 for(int mode=0;mode<5;++mode){auto& p=players[0];p.dead=mode==0;p.attackTarget=mode==1?42:0;p.castingSpellId=mode==2?1:0;p.flight.active=mode==3;p.instanceId=mode==4?42:0;auto old=p;bots.tick(300,world,players);assert(p.inventory==old.inventory && p.x==old.x && p.y==old.y && bots.auctions().empty());}
 auto& p=players[0];p.instanceId=0;p.inventory={{800001,20}};bots.tick(300,world,players);assert(bots.auctions().size()==1);
 std::vector<LocalAuction> board;for(unsigned i=1;i<=64;++i){auto v=listing(i,i%2?virtualSeller:p.guid);v.remainingSeconds=170000;board.push_back(v);}assert(bots.restoreAuctions(board,error));p.inventory={{800001,20}};bots.tick(300,world,players);assert(bots.auctions().size()==64 && p.inventory[0].count==20);
 buyer.inventory.assign(2,{800001,20});assert(bots.listStacksPriced(buyer,800001,1,16,100,0,720,world,error));assert(bots.listStacksPriced(buyer,800001,1,16,100,0,720,world,error));assert(bots.auctions().size()==96);
 bots.setEnabled(false);bots.clear(players);assert(players.empty() && bots.auctions().size()==96);
 std::cout<<"PASS walking bots: no dungeon spawn, no movement/listing during dead/combat/cast/flight/instance states, shared 64 simulated listings preserve 32 human slots, disable retains market\n";
}
