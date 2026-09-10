#pragma once
#include "game/local_gameplay.hpp"
#include <array>
#include <algorithm>
namespace wowee::game {
// The vector remains dense for existing game rules. Physical backpack cells
// are independent, so erasing vector entries does not relocate other stacks.
inline std::array<uint8_t,24> localInventoryLayout(const LocalRealmPlayer& p) {
    std::array<uint8_t,24> slots;slots.fill(255);std::array<bool,24> used{};
    for(size_t i=0;i<std::min(p.inventory.size(),size_t(24));++i){
        const auto slot=p.inventory[i].bagSlot;if(slot<24 && !used[slot]){slots[i]=slot;used[slot]=true;}
    }
    for(size_t i=0;i<std::min(p.inventory.size(),size_t(24));++i)if(slots[i]==255)
        for(uint8_t slot=0;slot<24;++slot)if(!used[slot]){slots[i]=slot;used[slot]=true;break;}
    return slots;
}
inline void normalizeLocalInventory(LocalRealmPlayer& p) {
    const auto slots=localInventoryLayout(p);
    for(size_t i=0;i<std::min(p.inventory.size(),slots.size());++i)p.inventory[i].bagSlot=slots[i];
}
inline size_t localInventoryIndex(const LocalRealmPlayer& p,uint32_t slot) {
    const auto slots=localInventoryLayout(p);
    for(size_t i=0;i<std::min(p.inventory.size(),slots.size());++i)if(slots[i]==slot)return i;
    return p.inventory.size();
}
inline bool validLocalInventoryLayout(const LocalRealmPlayer& p) {
    std::array<bool,24> seen{};if(p.inventory.size()>seen.size())return false;
    for(const auto& s:p.inventory){if(s.bagSlot>=24 || seen[s.bagSlot])return false;seen[s.bagSlot]=true;}
    return true;
}
// Move a selected quantity between two concrete cells. Caller owns candidates
// and applies gameplay/service/equipment permissions before committing them.
inline bool moveLocalInventoryStack(LocalItemStack& source,LocalItemStack& destination,
        uint16_t amount,const LocalWorldContent& content) {
    const auto* a=content.item(source.itemId);if(!a || !amount || amount>source.count || source.count>a->stack)return false;
    if(!destination.itemId){if(destination.count)return false;destination.itemId=source.itemId;destination.count=amount;source.count-=amount;}
    else if(destination.itemId==source.itemId){
        if(!destination.count || destination.count>a->stack || amount>a->stack-destination.count)return false;
        destination.count+=amount;source.count-=amount;
    }else{
        const auto* b=content.item(destination.itemId);if(amount!=source.count || !b || !destination.count || destination.count>b->stack)return false;
        std::swap(source.itemId,destination.itemId);std::swap(source.count,destination.count);
    }
    if(!source.count)source.itemId=0;return true;
}
}
