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
inline bool validLocalItemInstance(const LocalItemStack& s) {
    if(bool(s.itemId)!=bool(s.count))return false;
    if(!s.itemId)return s.instance==LocalItemInstanceState{};
    const auto& i=s.instance;
    if(i.maxDurability && i.curDurability>i.maxDurability)return false;
    if(!i.maxDurability && i.curDurability)return false;
    return true;
}
inline bool sameLocalItemInstance(const LocalItemStack& a,const LocalItemStack& b) {
    return a.itemId==b.itemId && a.instance==b.instance;
}
inline void clearLocalItemKeepSlot(LocalItemStack& s) {
    const auto slot=s.bagSlot;s={};s.bagSlot=slot;
}
inline bool addLocalInventoryStack(LocalRealmPlayer& player,LocalItemStack incoming,const LocalWorldContent& content) {
    if(!validLocalItemInstance(incoming) || !incoming.itemId || !incoming.count)return false;
    const auto* def=content.item(incoming.itemId);if(!def)return false;
    const uint16_t limit=std::max<uint16_t>(1,def->stack);if(incoming.count>limit)return false;
    auto candidate=player;normalizeLocalInventory(candidate);
    uint16_t left=incoming.count;
    for(auto& stack:candidate.inventory){
        if(!sameLocalItemInstance(stack,incoming) || stack.count>=limit)continue;
        const uint16_t n=std::min<uint16_t>(left,uint16_t(limit-stack.count));stack.count+=n;left-=n;if(!left)break;
    }
    while(left){
        if(candidate.inventory.size()>=LocalGameplay::MaxInventory)return false;
        LocalItemStack copy=incoming;copy.count=std::min<uint16_t>(left,limit);copy.bagSlot=255;candidate.inventory.push_back(copy);left-=copy.count;
    }
    normalizeLocalInventory(candidate);player=std::move(candidate);return true;
}
// Move a selected quantity between two concrete cells. Caller owns candidates
// and applies gameplay/service/equipment permissions before committing them.
inline bool moveLocalInventoryStack(LocalItemStack& source,LocalItemStack& destination,
        uint16_t amount,const LocalWorldContent& content) {
    const auto* a=content.item(source.itemId);if(!a || !amount || amount>source.count || source.count>a->stack || !validLocalItemInstance(source) || !validLocalItemInstance(destination))return false;
    if(!destination.itemId){
        if(destination.count)return false;const auto destinationSlot=destination.bagSlot;destination=source;destination.bagSlot=destinationSlot;destination.count=amount;source.count-=amount;
    } else if(sameLocalItemInstance(destination,source)){
        if(!destination.count || destination.count>a->stack || amount>a->stack-destination.count)return false;
        destination.count+=amount;source.count-=amount;
    } else {
        const auto* b=content.item(destination.itemId);if(amount!=source.count || !b || !destination.count || destination.count>b->stack)return false;
        const auto sourceSlot=source.bagSlot,destinationSlot=destination.bagSlot;std::swap(source,destination);source.bagSlot=sourceSlot;destination.bagSlot=destinationSlot;
        return true;
    }
    if(!source.count)clearLocalItemKeepSlot(source);return true;
}
}
