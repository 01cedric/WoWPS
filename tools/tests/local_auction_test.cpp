#include "game/local_bots.hpp"
#include "game/local_auction_catalog.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace wowee::game;
static size_t itemReads = 0;

// Host fixture for the indexed world catalog: item definitions retain the
// actual generated server fields. No renderer, PS4 syscall or MPQ is mocked as
// a successful operation; these tests exercise the production market engine.
namespace wowee::game {
const LocalItemDefinition* LocalWorldContent::item(uint32_t id) const {
    ++itemReads;
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

static const LocalAuction& auction(const LocalBotDirector& market, uint32_t id) {
    const auto& entries=market.auctions();
    const auto it=std::find_if(entries.begin(),entries.end(),[id](const auto& a){return a.id==id;});
    assert(it!=entries.end()); return *it;
}

static void assertPostedPrices(const LocalBotDirector& market, const std::vector<LocalAuction>& posted) {
    for(const auto& before:posted) {
        const auto& after=auction(market,before.id);
        assert(after.buyout==before.buyout && after.bid==before.bid);
        assert(after.highestBid==before.highestBid && after.highestBidder==before.highestBidder);
    }
}

static void fillMarket(LocalBotDirector& market, const LocalWorldContent& world,
                       std::vector<LocalRealmPlayer>& players) {
    for(int i=0;i<200 && market.auctions().size()<LocalBotDirector::MaxMarketAuctions;++i) {
        const auto reads=itemReads;
        market.tick(1.f,world,players);
        assert(itemReads-reads<=12); // bounded supply work even while the board fills
    }
    assert(market.auctions().size()==LocalBotDirector::MaxMarketAuctions);
}

int main() {
    LocalWorldContent world;
    LocalItemDefinition custom; custom.id=800001; custom.name="Fixture Material"; custom.value=100; custom.stack=20;
    world.items.push_back(custom);
    assert(LocalAuctionPricing::buyoutFor(custom,1,.5f,4)==400);
    assert(LocalAuctionPricing::buyoutFor(custom,1,.5f,10)==1000);
    assert(LocalAuctionPricing::buyoutFor(custom,1,.5f,1)==400);
    assert(LocalAuctionPricing::buyoutFor(custom,1,.5f,15)==1000);
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
    fillMarket(market,world,bots);
    assert(bots.empty() && market.bots().empty());
    assert(market.auctions().size()==LocalBotDirector::MaxMarketAuctions);
    unsigned materials=0;
    for(const auto& listing:market.auctions()) {
        assert(LocalBotDirector::isMarketSeller(listing.seller));
        const auto* meta=localAuctionMetadata(listing.itemId);assert(meta);
        if(meta->has(LocalAuctionItemMetadata::Material))++materials;
    }
    assert(materials>=40);
    const auto posted=market.auctions();
    const auto idleReads=itemReads;
    for(int i=0;i<120;++i)market.tick(1.f,world,bots);
    assertPostedPrices(market,posted);
    assert(itemReads==idleReads); // a full board needs no item I/O or price rerolls
    LocalBotDirector savedMarket;
    std::string message;
    assert(savedMarket.restoreAuctions(posted,message));
    savedMarket.tick(1.f,world,bots);
    assertPostedPrices(savedMarket,posted);
    LocalRealmPlayer buyer; buyer.guid=101; buyer.money=LocalAuctionPricing::MoneyCap;
    const auto initial=market.auctions().front();
    assert(market.buyout(initial.id,buyer,world,message));
    assert(count(buyer,initial.itemId)==0 && market.deliveries().size()==1);
    assert(market.deliver(buyer,world)); // Unit-level escrow claim; the realm migrates this to mail.
    assert(buyer.money==LocalAuctionPricing::MoneyCap-initial.buyout && count(buyer,initial.itemId)==initial.count);
    assert(market.deliveries().empty()); // virtual seller proceeds cannot fill human escrow forever
    assert(!market.buyout(initial.id,buyer,world,message));
    const auto remaining=market.auctions();
    for(int i=0;i<65;++i)market.tick(1.f,world,bots);
    assert(market.auctions().size()==LocalBotDirector::MaxMarketAuctions);
    assertPostedPrices(market,remaining);
    market.setEnabled(true);market.populate(world,buyer,bots);assert(!bots.empty());
    market.setEnabled(false);market.clear(bots);assert(bots.empty());
    assert(!market.auctions().empty());
    for(int i=0;i<4000;++i)market.tick(1.f,world,bots);
    assert(market.auctions().size()<=LocalBotDirector::MaxMarketAuctions && market.deliveries().empty());
    std::cout << "PASS independent market: no walking bots, bounded stock, material supply, purchase, expiry/restock\n";

    // Distinguish a per-listing draw from one random choice shared by the whole
    // realm. Non-overlapping price bands also verify every generated quote.
    LocalWorldContent rareWorld=world;
    for(const uint32_t mountId:{13335u,19872u,19902u,30480u,32768u,35513u,
                               49282u,49283u,49284u,49290u,54068u}) {
        const auto* data=localAuctionMetadata(mountId);assert(data && data->mountSpell);
        LocalSpellDefinition supported;supported.id=data->mountSpell;supported.mountDisplayId=1;
        rareWorld.spells.push_back(supported);
    }
    std::array<unsigned,7> multiplierCounts{};
    bool competingQuotes=false;
    unsigned rareDrops=0,tcgMounts=0;
    for(uint32_t seed=1;seed<=64;++seed) {
        LocalBotDirector sample; sample.setSeed(seed);
        fillMarket(sample,rareWorld,bots);
        std::array<bool,7> seen{};
        unsigned materialCount=0,mountCount=0;
        for(const auto& listing:sample.auctions()) {
            const auto* item=rareWorld.item(listing.itemId); assert(item);
            const auto* data=localAuctionMetadata(listing.itemId); assert(data);
            materialCount+=data->has(LocalAuctionItemMetadata::Material);
            if(data->has(LocalAuctionItemMetadata::Mount)) {
                ++mountCount;
                assert(rareWorld.spell(data->mountSpell));
                rareDrops+=data->has(LocalAuctionItemMetadata::DropMount);
                if(data->has(LocalAuctionItemMetadata::TcgMount)) {
                    ++tcgMounts;
                    assert(listing.bid==LocalAuctionPricing::MoneyCap && listing.buyout==listing.bid);
                }
            }
            assert(listing.buyout>=LocalAuctionPricing::buyoutFor(*item,listing.count,0.f,4));
            assert(listing.buyout<=LocalAuctionPricing::buyoutFor(*item,listing.count,1.f,10));
            unsigned matches=0,matched=0;
            for(unsigned multiplier=4;multiplier<=10;++multiplier) {
                if(listing.buyout>=LocalAuctionPricing::buyoutFor(*item,listing.count,0.f,multiplier) &&
                   listing.buyout<=LocalAuctionPricing::buyoutFor(*item,listing.count,1.f,multiplier)) {
                    ++matches;matched=multiplier-4;
                }
            }
            assert(matches); // includes premiums and copper rounding
            if(matches==1) {++multiplierCounts[matched];seen[matched]=true;}
            for(const auto& rival:sample.auctions())
                if(rival.itemId==listing.itemId && rival.seller!=listing.seller &&
                   uint64_t(rival.buyout)*listing.count!=uint64_t(listing.buyout)*rival.count)
                    competingQuotes=true;
        }
        assert(materialCount>=40 && mountCount<=2);
        assert(std::count(seen.begin(),seen.end(),true)>=4);
    }
    for(const auto occurrences:multiplierCounts)assert(occurrences>200);
    assert(competingQuotes && rareDrops && tcgMounts);
    std::cout << "PASS autonomous seller prices: all seven 4..10 choices across 64 seeds, competing quotes, supported rare/TCG supply, stable refill/reload\n";

    // A full virtual market leaves 32 posting places. Player prices and bid
    // escrow remain exactly as posted through both a refresh and a reload.
    LocalRealmPlayer holder;holder.guid=401;holder.name="Holder";holder.inventory={{800001,20},{800001,13}};
    for(int batch=0;batch<2;++batch)
        assert(savedMarket.listStacksPriced(holder,800001,1,16,100,LocalAuctionPricing::MoneyCap,720,world,message));
    assert(savedMarket.auctions().size()==LocalBotDirector::MaxAuctions && count(holder,800001)==1);
    assert(!savedMarket.listItemPriced(holder,800001,1,100,LocalAuctionPricing::MoneyCap,720,world,message));
    LocalRealmPlayer bidder;bidder.guid=402;bidder.money=LocalAuctionPricing::MoneyCap;
    const auto marketId=posted.front().id;
    assert(savedMarket.placeBid(marketId,bidder,auction(savedMarket,marketId).bid,message));
    const auto playerId=savedMarket.auctions().back().id;
    assert(savedMarket.placeBid(playerId,bidder,100,message));
    const auto protectedPrices=savedMarket.auctions();
    savedMarket.tick(61.f,world,bots);
    assertPostedPrices(savedMarket,protectedPrices);
    LocalBotDirector reloadWithBids;
    assert(reloadWithBids.restoreAuctions(savedMarket.auctions(),message));
    reloadWithBids.tick(61.f,world,bots);
    assertPostedPrices(reloadWithBids,protectedPrices);
    std::cout << "PASS player capacity and authority: 32 reserved places, unchanged manual prices and active bids on refresh/reload\n";

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
    assert(count(second,800001)==0 && exchange.deliver(second,world));
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
    assert(buyer.money==money && full.auctions().size()==1);
    assert(!full.listItemPriced(seller,800001,1,100,500,1,world,message));
    auto bad=full.auctions();bad.push_back(bad.front());
    assert(!full.restoreAuctions(bad,message));assert(full.auctions().size()==1);
    assert(!full.restoreDeliveries({{0,800001,0,1}}));
    assert(full.buyout(id,buyer,world,message));assert(buyer.money==money-500 && full.auctions().empty());
    assert(!full.deliver(buyer,world)); // Full bags retain purchased goods in escrow.
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
    assert(mounts.deliver(buyer,world));
    assert(buyer.money==0 && count(buyer,tiger->id)==1 && mounts.deliveries().empty());
    std::cout << "PASS mount purchases require supported learned mount spell; TCG costs exactly local gold cap\n";
}
