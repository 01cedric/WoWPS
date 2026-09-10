#include "game/local_services.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

// Content resolver fixture only. The stock/price/restock implementation below
// is the production C++ code, with its actual generated source database rows.
namespace wowee::game {
const LocalItemDefinition* LocalWorldContent::item(uint32_t id) const {
    for (const auto& item : items) if (item.id == id) return &item;
    return nullptr;
}
}

using namespace wowee::game;

int main() {
    LocalWorldContent content;
    const std::set<uint32_t> corina = {2488,2489,2490,2491,2492,2493,2494,2495};
    const std::set<uint32_t> godric = {2129,2379,2380,2381,2383,2384,2385,17184};
    const std::set<uint32_t> andrew = {1201,2392,2393,2394,2395,2396,2397,17186};
    std::set<uint32_t> items = corina;
    items.insert(godric.begin(), godric.end()); items.insert(andrew.begin(), andrew.end());
    items.insert(117); items.insert(2516); items.insert(6270);
    for (uint32_t id : items) {
        LocalItemDefinition item; item.id = id; item.name = "Fixture";
        if (const auto* price = localVendorPrice(id)) item.value = price->sellPrice;
        content.items.push_back(item);
    }
    auto stock = [&](uint32_t npc) {
        const auto rows = localVendorStockForNpc(npc, {}, content);
        return std::set<uint32_t>(rows.begin(), rows.end());
    };
    assert(stock(54) == corina);       // Corina Steele, Goldshire weaponsmith.
    assert(stock(1213) == godric);    // Godric Rothgar, Northshire armor.
    assert(stock(2046) == andrew);    // Andrew Krighton, Goldshire armor.
    assert(stock(999999).empty());    // Never invent a general vendor shop.
    assert(!stock(54).contains(117)); // Weaponsmith does not sell food.
    assert(!stock(1213).contains(2516));
    const auto override = localVendorStockForNpc(54, {117,117,999999}, content);
    assert((override == std::vector<uint32_t>{117}));
    content.items.erase(std::remove_if(content.items.begin(), content.items.end(),
        [](const auto& item) { return item.id == 2488; }), content.items.end());
    assert(stock(54).size() == corina.size() - 1); // Missing assets not fabricated.

    const auto* arrowPrice = localVendorPrice(2516);
    assert(arrowPrice && arrowPrice->buyCount == 200 && arrowPrice->buyPrice == 10);
    LocalItemDefinition arrow; arrow.id = 2516; arrow.value = arrowPrice->sellPrice;
    assert(localVendorBuyPrice(arrow, 200) == 10);
    assert(localVendorBuyPrice(arrow, 1) == 1); // No free sub-copper quantities.
    assert(localVendorBuyPrice(arrow, 0) == 0);
    arrow.value = 100; // An explicit repriced custom catalog retains its price.
    assert(localVendorBuyPrice(arrow, 1) == 500);
    LocalItemDefinition expensive; expensive.value = UINT32_MAX;
    assert(localVendorBuyPrice(expensive, UINT32_MAX) == 1000000000);
    assert(localVendorSellPrice(expensive, UINT32_MAX) == 1000000000);

    const auto* limited = localVendorOffer(66,6270); // Tharynn's rare linen vest pattern.
    assert(limited && limited->maxCount == 1 && limited->restockSeconds == 9000);
    LocalVendorInventory inventory;
    assert(inventory.consume(1,*limited,1,1,0));
    assert(inventory.available(1,*limited,1,8999) == 0);
    assert(!inventory.consume(1,*limited,1,1,8999));
    assert(inventory.available(2,*limited,1,8999) == 1); // Independent physical shop.
    assert(inventory.consume(1,*limited,1,1,9000));
    assert(inventory.available(1,*limited,1,17999) == 0);

    // A restart rebases the remaining fraction onto the new simulation clock.
    const auto saved = inventory.snapshot(10000.125);
    assert(saved.size() == 1 && saved[0].remaining == 0 && saved[0].elapsedMs == 1000125);
    LocalVendorInventory restarted;
    assert(restarted.restore(saved, 0));
    assert(restarted.available(1,*limited,1,7999.874) == 0);
    assert(restarted.available(1,*limited,1,7999.875) == 1);
    assert(restarted.snapshot(7999.875).empty()); // Full stock takes no save space.
    const auto unchanged = restarted.snapshot(0);
    auto invalid = saved;
    invalid.push_back(saved.front()); assert(!restarted.restore(invalid, 0));
    invalid = saved; invalid[0].npcGuid = 0; assert(!restarted.restore(invalid, 0));
    invalid = saved; invalid[0].entry = 999999; assert(!restarted.restore(invalid, 0));
    invalid = saved; invalid[0].remaining = 1; assert(!restarted.restore(invalid, 0));
    invalid = saved; invalid[0].elapsedMs = 9000000; assert(!restarted.restore(invalid, 0));
    assert(!restarted.restore(saved, std::numeric_limits<double>::infinity()));
    assert(restarted.snapshot(0) == unchanged); // Rejection never partially installs.
    assert(restarted.restore(restarted.snapshot(100), 300));
    assert(restarted.available(1,*limited,1,8199.874) == 0);
    assert(restarted.available(1,*limited,1,8199.875) == 1);

    const LocalVendorOffer batch{1,1,10,10};
    LocalVendorInventory bundles;
    assert(bundles.consume(10,batch,10,2,0));
    assert(bundles.available(10,batch,2,9) == 0);
    assert(bundles.available(10,batch,2,15) == 2);
    assert(bundles.consume(10,batch,1,2,15));
    assert(bundles.available(10,batch,2,19) == 1);
    assert(bundles.available(10,batch,2,20) == 3); // Residual interval preserved.
    assert(bundles.available(10,batch,2,1e300) == 10);
    assert(!bundles.consume(10,batch,1,2,std::numeric_limits<double>::quiet_NaN()));

    LocalVendorInventory bounded;
    for (uint64_t id=1; id<=LocalVendorInventory::MaxDepletedOffers; ++id)
        assert(bounded.consume(id,*limited,1,1,0));
    assert(!bounded.consume(999999,*limited,1,1,1));
    assert(bounded.retainedOffers() == LocalVendorInventory::MaxDepletedOffers);
    const auto fullSave = bounded.snapshot(1);
    assert(restarted.restore(fullSave, 0));
    assert(restarted.retainedOffers() == LocalVendorInventory::MaxDepletedOffers);
    auto tooMany = fullSave; tooMany.push_back(fullSave.front());
    assert(!restarted.restore(tooMany, 0));
    assert(bounded.consume(999999,*limited,1,1,9000));
    assert(bounded.retainedOffers() == 1); // Fully replenished entries reclaimed.
    bounded.clear(); assert(bounded.retainedOffers() == 0);
    std::puts("PASS original vendor stocks, exact bundle prices, finite stock/restock, restart phase, malformed/duplicate/oversized persistence rejection and bounded memory");
}
