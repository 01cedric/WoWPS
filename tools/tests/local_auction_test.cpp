#include "game/local_bots.hpp"
#include "game/local_auction_catalog.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace wowee::game;

// Host fixture for the indexed world catalog: item definitions retain the
// actual generated server fields. No renderer, PS4 syscall or MPQ is mocked as
// a successful operation; these tests exercise the production market engine.
namespace wowee::game {
const LocalItemDefinition* LocalWorldContent::item(uint32_t id) const {
    for (const auto& item : items) if (item.id == id) return &item;
    const auto* metadata = localAuctionMetadata(id);
    if (!metadata) return nullptr;
    auto& item = itemCache[id];
    item.id=id; item.name="Item "+std::to_string(id); item.value=metadata->sellPrice; item.stack=metadata->stack;
    return &item;
}
const LocalSpellDefinition* LocalWorldContent::spell(uint32_t id) const {
    for (const auto& spell:spells) if(spell.id==id)return &spell;
    return nullptr;
}
}

static uint32_t count(const LocalRealmPlayer& player, uint32_t id) {
    uint32_t result=0; for (const auto& stack:player.inventory) if(stack.itemId==id)result+=stack.count;
    return result;
}

int main() {
    LocalWorldContent world;
    LocalItemDefinition custom; custom.id=800001; custom.name="Fixture Material"; custom.value=100; custom.stack=20;
    world.items.push_back(custom);
    assert(LocalAuctionPricing::buyoutFor(custom,1,.5f,4)==400);
    assert(LocalAuctionPricing::buyoutFor(custom,1,.5f,10)==1000);
    assert(LocalAuctionPricing::buyoutFor(custom,1,0.f,10)==980);
    assert(LocalAuctionPricing::buyoutFor(custom,1,1.f,10)==1020);
    custom.value=1; assert(LocalAuctionPricing::buyoutFor(custom,1,.5f,10)==10);
    custom.value=0; assert(LocalAuctionPricing::buyoutFor(custom,1,.5f,10)==0);
    custom.value=UINT32_MAX;
    assert(LocalAuctionPricing::buyoutFor(custom,UINT16_MAX,1.f,10)==LocalAuctionPricing::MoneyCap);
    custom.value=100;
    assert(LocalAuctionPricing::buyoutFor(custom,1,std::numeric_limits<float>::quiet_NaN(),10)==1000);
    const auto* tiger=localAuctionMetadata(49284); assert(tiger && tiger->has(LocalAuctionItemMetadata::TcgMount) && tiger->mountSpell);
    const auto* tigerItem=world.item(tiger->id);
    assert(LocalAuctionPricing::buyoutFor(*tigerItem,1,0.f,4)==LocalAuctionPricing::MoneyCap);
    assert(LocalAuctionPricing::bidFor(LocalAuctionPricing::MoneyCap)==LocalAuctionPricing::MoneyCap);
    assert(LocalAuctionPricing::rarityPremiumBasisPoints(0)==10000);
    assert(LocalAuctionPricing::rarityPremiumBasisPoints(10000)==10000);
    assert(LocalAuctionPricing::rarityPremiumBasisPoints(100)>LocalAuctionPricing::rarityPremiumBasisPoints(1000));
    assert(LocalAuctionPricing::rarityPremiumBasisPoints(1000)>LocalAuctionPricing::rarityPremiumBasisPoints(5000));
    assert(LocalAuctionPricing::rarityPremiumBasisPoints(1)<=80000);
    std::cout << "PASS market pricing: 4..10 multiplier, quality metadata, bounded small spreads, TCG cap, overflow\n";

    LocalBotDirector market;
    std::vector<LocalRealmPlayer> bots;
    market.setEnabled(false); market.setSeed(77);
    market.setAuctionPriceMultiplier(1); assert(market.auctionPriceMultiplier()==4);
    market.setAuctionPriceMultiplier(15); assert(market.auctionPriceMultiplier()==10);
    for(int i=0;i<160;++i)market.tick(.25f,world,bots);
    assert(bots.empty() && market.bots().empty());
    assert(market.auctions().size()==LocalBotDirector::MaxMarketAuctions);
    unsigned materials=0;
    for(const auto& listing:market.auctions()) {
        assert(LocalBotDirector::isMarketSeller(listing.seller));
        const auto* meta=localAuctionMetadata(listing.itemId);assert(meta);
        if(meta->has(LocalAuctionItemMetadata::Material))++materials;
    }
    assert(materials>=40);
    LocalRealmPlayer buyer; buyer.guid=101; buyer.money=LocalAuctionPricing::MoneyCap;
    const auto initial=market.auctions().front();
    std::string message;
    assert(market.buyout(initial.id,buyer,world,message));
    assert(buyer.money==LocalAuctionPricing::MoneyCap-initial.buyout && count(buyer,initial.itemId)==initial.count);
    assert(market.deliveries().empty()); // virtual seller proceeds cannot fill human escrow forever
    assert(!market.buyout(initial.id,buyer,world,message));
    market.setEnabled(true);market.populate(world,buyer,bots);assert(!bots.empty());
    market.setEnabled(false);market.clear(bots);assert(bots.empty());
    assert(!market.auctions().empty());
    for(int i=0;i<4000;++i)market.tick(1.f,world,bots);
    assert(market.auctions().size()<=LocalBotDirector::MaxMarketAuctions && market.deliveries().empty());
    std::cout << "PASS independent market: no walking bots, bounded stock, material supply, purchase, expiry/restock\n";

    LocalBotDirector exchange;
    LocalRealmPlayer seller; seller.guid=201;seller.name="Seller";seller.inventory={{800001,20}};
    LocalRealmPlayer first;first.guid=202;first.money=10000;
    LocalRealmPlayer second;second.guid=203;second.money=10000;
    assert(exchange.listItemPriced(seller,800001,5,100,500,720,world,message));
    auto id=exchange.auctions().front().id;
    assert(count(seller,800001)==15);
    assert(!exchange.buyout(id,seller,world,message));
    assert(exchange.placeBid(id,first,100,message));assert(first.money==9900);
    assert(exchange.placeBid(id,first,105,message));assert(first.money==9895);
    assert(exchange.placeBid(id,second,111,message));assert(second.money==9889);
    assert(exchange.deliver(first,world));assert(first.money==10000);
    assert(!exchange.cancelAuction(id,seller,message));
    assert(exchange.buyout(id,second,world,message));assert(second.money==9500);
    assert(count(second,800001)==5);
    assert(exchange.deliver(seller,world));assert(seller.money==500);
    assert(!exchange.deliver(seller,world));assert(seller.money==500);
    assert(exchange.listItemPriced(seller,800001,5,100,500,720,world,message));
    id=exchange.auctions().front().id;
    assert(exchange.cancelAuction(id,seller,message));
    auto pending=exchange.deliveries();
    LocalBotDirector restored;
    assert(restored.restoreAuctions(exchange.auctions(),message));assert(restored.restoreDeliveries(pending));
    assert(restored.deliver(seller,world));assert(count(seller,800001)==15);
    assert(!restored.deliver(seller,world));
    // Expiration settles a bid even while playerbots remain disabled.
    assert(exchange.listItemPriced(seller,800001,1,100,500,720,world,message));
    id=exchange.auctions().front().id;
    assert(exchange.placeBid(id,first,100,message));
    exchange.tick(43201.f,world,bots);
    assert(exchange.deliver(first,world));assert(count(first,800001)==1);
    assert(exchange.deliver(seller,world));assert(seller.money==600);
    std::cout << "PASS transactions: escrow/refund, bid increment, own-auction guard, buyout delta, cancel/reload, expiry\n";

    LocalBotDirector full;
    seller.inventory={{800001,1}};
    assert(full.listItemPriced(seller,800001,1,100,500,720,world,message));
    id=full.auctions().front().id;
    buyer.inventory.assign(LocalGameplay::MaxInventory,{800001,20});
    const auto money=buyer.money;
    assert(!full.buyout(id,buyer,world,message));assert(buyer.money==money && full.auctions().size()==1);
    assert(!full.listItemPriced(seller,800001,1,100,500,1,world,message));
    auto bad=full.auctions();bad.push_back(bad.front());
    assert(!full.restoreAuctions(bad,message));assert(full.auctions().size()==1);
    assert(!full.restoreDeliveries({{0,800001,0,1}}));
    std::cout << "PASS rejected transactions leave money/items unchanged; malformed restored state rejected\n";

    LocalBotDirector bulk;
    seller.inventory={{800001,20}};
    assert(!bulk.listStacksPriced(seller,800001,5,5,100,500,720,world,message));
    assert(count(seller,800001)==20 && bulk.auctions().empty());
    assert(bulk.listStacksPriced(seller,800001,5,4,100,500,720,world,message));
    assert(count(seller,800001)==0 && bulk.auctions().size()==4);
    assert(!bulk.listStacksPriced(seller,800001,1,0,100,500,720,world,message));
    std::cout << "PASS bulk listings commit all stacks together or leave original bags/board unchanged\n";

    LocalBotDirector mounts;
    LocalAuction rare; rare.id=1;rare.itemId=tiger->id;rare.count=1;
    rare.bid=rare.buyout=LocalAuctionPricing::MoneyCap;rare.seller=0x0A11000000000001ULL;
    rare.sellerName="Aldren";rare.remainingSeconds=1000;
    assert(mounts.restoreAuctions({rare},message));
    buyer.inventory.clear();buyer.money=LocalAuctionPricing::MoneyCap;
    assert(!mounts.buyout(rare.id,buyer,world,message));
    assert(buyer.money==LocalAuctionPricing::MoneyCap && buyer.inventory.empty());
    LocalSpellDefinition groundMount;groundMount.id=tiger->mountSpell;groundMount.mountDisplayId=21974;
    world.spells.push_back(groundMount);
    assert(mounts.buyout(rare.id,buyer,world,message));
    assert(buyer.money==0 && count(buyer,tiger->id)==1 && mounts.deliveries().empty());
    std::cout << "PASS mount purchases require supported learned mount spell; TCG costs exactly local gold cap\n";
}
