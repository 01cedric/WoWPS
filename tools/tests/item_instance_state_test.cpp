#include "game/inventory.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using wowee::game::ItemDef;

int main() {
    ItemDef item;
    item.itemId = 12345;
    item.guid = 0xABCDEF1234ULL;
    item.bindType = 2; // BoE
    item.instanceFlags = 0;
    item.soulbound = false;
    item.curDurability = 37;
    item.maxDurability = 50;
    item.permanentEnchantId = 3820;
    item.temporaryEnchantId = 28420;
    item.socketEnchantIds = {3520, 3521, 3522};
    item.randomPropertyId = -123;
    item.suffixFactor = 9876;

    assert(item.wouldBindOnEquip());
    assert(item.hasDurability());
    assert(!item.isBroken());
    assert(std::fabs(item.durabilityFraction() - 0.74f) < 0.0001f);
    assert(item.hasEnchantments());
    assert(item.randomPropertyId == -123 && item.suffixFactor == 9876);

    ItemDef copy = item;
    assert(copy.guid == item.guid);
    assert(copy.instanceFlags == item.instanceFlags);
    assert(copy.permanentEnchantId == item.permanentEnchantId);
    assert(copy.temporaryEnchantId == item.temporaryEnchantId);
    assert(copy.socketEnchantIds == item.socketEnchantIds);
    assert(copy.randomPropertyId == item.randomPropertyId);
    assert(copy.suffixFactor == item.suffixFactor);

    copy.soulbound = true;
    copy.instanceFlags |= 0x1u;
    assert(!copy.wouldBindOnEquip());

    copy.curDurability = 0;
    assert(copy.isBroken());
    assert(copy.durabilityFraction() == 0.0f);

    ItemDef noDurability;
    assert(!noDurability.hasDurability());
    assert(!noDurability.isBroken());
    assert(noDurability.durabilityFraction() == 1.0f);

    std::cout << "PASS item instance state: binding, durability, enchants, sockets, random properties, copy stability\n";
    return 0;
}
