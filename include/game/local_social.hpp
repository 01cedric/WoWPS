#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_inventory_layout.hpp"
#include "game/local_chat.hpp"
#include "game/local_auction_catalog.hpp"
#include <array>
#include <algorithm>
namespace wowee::game {
struct LocalReadyMember {uint64_t guid=0;std::string name;uint8_t answer=0;bool operator==(const LocalReadyMember&)const=default;};
struct LocalReadyCheck {
    uint32_t id=0,party=0;uint64_t initiator=0;double deadline=0;
    uint8_t state=0; // 1 active, 2 finished, 3 cancelled
    std::vector<LocalReadyMember> members;
    bool operator==(const LocalReadyCheck&)const=default;
};
struct LocalTrade {
    uint32_t id=0,revision=0;uint8_t state=0; // 1 request, 2 open, 3 complete, 4 cancelled
    std::array<uint64_t,2> players{};
    std::array<std::string,2> names;
    std::array<uint32_t,2> money{};
    std::array<bool,2> accepted{};
    std::array<std::array<LocalTradeItem,6>,2> items{};
    std::array<uint64_t,2> fingerprints{}; // authority only
    double deadline=0;
    int side(uint64_t guid)const{return players[0]==guid?0:players[1]==guid?1:-1;}
    bool live()const{return state==1 || state==2;}
    bool operator==(const LocalTrade&)const=default;
};
inline uint64_t localTradeFingerprint(const LocalRealmPlayer& p) {
    uint64_t h=1469598103934665603ULL;
    auto add=[&](uint64_t n){h^=n;h*=1099511628211ULL;};
    add(p.money);add(p.inventory.size());const auto slots=localInventoryLayout(p);size_t index=0;for(const auto& item:p.inventory){add(item.itemId);add(item.count);add(slots[index++]);}
    for(auto id:p.equipment)add(id);return h;
}
inline bool localTradeAvailable(const LocalRealmPlayer& p) {
    return !p.dead && !p.attackTarget && !p.castingSpellId && !p.flight.active && !p.transportEntry;
}
inline bool localTradeReach(const LocalRealmPlayer& a,const LocalRealmPlayer& b) {
    if(!localTradeAvailable(a) || !localTradeAvailable(b) || a.mapId!=b.mapId || a.instanceId!=b.instanceId ||
       !localChatTeam(a.race) || localChatTeam(a.race)!=localChatTeam(b.race))return false;
    const double x=double(a.x)-b.x,y=double(a.y)-b.y,z=double(a.z)-b.z,d=x*x+y*y+z*z;
    return std::isfinite(d) && d<=100.0;
}
inline bool localTradeItemValid(const LocalRealmPlayer& player,const LocalTradeItem& item,const LocalWorldContent& content) {
    if(!item.item)return !item.count && !item.sourceCount;
    const auto* meta=localAuctionMetadata(item.item);const auto* def=content.item(item.item);
    // Item instances do not yet track past binding. Only known unbound items
    // are safe to transfer; equipment, quests and unknown metadata stay put.
    const auto index=localInventoryIndex(player,item.bag);
    if(!def || !meta || meta->bonding!=0 || !meta->tradeable() || !item.count || index>=player.inventory.size())return false;
    const auto& stack=player.inventory[index];
    if(stack.itemId!=item.item || stack.count!=item.sourceCount || item.count>stack.count)return false;
    return std::find(player.equipment.begin(),player.equipment.end(),item.item)==player.equipment.end();
}
inline bool prepareLocalTrade(const LocalTrade& trade,const LocalRealmPlayer& a,const LocalRealmPlayer& b,
        const LocalWorldContent& content,LocalRealmPlayer& outA,LocalRealmPlayer& outB,std::string& error) {
    auto reject=[&](const char* text){error=text;return false;};
    if(trade.state!=2 || trade.players!=std::array<uint64_t,2>{a.guid,b.guid} || !localTradeReach(a,b))return reject("Trade partner is unavailable or too far away");
    if(localTradeFingerprint(a)!=trade.fingerprints[0] || localTradeFingerprint(b)!=trade.fingerprints[1])return reject("Inventory changed; review the trade again");
    const std::array<const LocalRealmPlayer*,2> originals{&a,&b};
    for(unsigned side=0;side<2;++side) {
        if(trade.money[side]>originals[side]->money)return reject("Not enough money for this offer");
        std::array<bool,LocalGameplay::MaxInventory> seen{};
        for(const auto& item:trade.items[side])if(item.item){
            if(item.bag>=seen.size() || !localTradeItemValid(*originals[side],item,content) || seen[item.bag])return reject("An offered stack is stale, bound or equipped");
            seen[item.bag]=true;
        }
    }
    outA=a;outB=b;const std::array<LocalRealmPlayer*,2> candidates{&outA,&outB};
    for(unsigned side=0;side<2;++side) {
        auto& p=*candidates[side];normalizeLocalInventory(p);
        const uint64_t money=uint64_t(p.money)-trade.money[side]+trade.money[1-side];
        if(money>1000000000ULL)return reject("Trade would exceed the wallet limit");
        p.money=uint32_t(money);
        for(const auto& item:trade.items[side])if(item.item)p.inventory[localInventoryIndex(p,item.bag)].count-=item.count;
        std::erase_if(p.inventory,[](const auto& item){return !item.count;});
    }
    for(unsigned side=0;side<2;++side)for(const auto& item:trade.items[1-side])if(item.item) {
        auto& bag=candidates[side]->inventory;const auto limit=std::max(uint16_t(1),content.item(item.item)->stack);unsigned left=item.count;
        for(auto& stack:bag)if(stack.itemId==item.item && stack.count<limit){const auto n=std::min(left,unsigned(limit-stack.count));stack.count+=n;left-=n;}
        while(left){if(bag.size()>=LocalGameplay::MaxInventory)return reject("Not enough backpack space for the complete trade");const auto n=std::min(left,unsigned(limit));bag.push_back({item.item,uint16_t(n)});left-=n;}
    }
    normalizeLocalInventory(outA);normalizeLocalInventory(outB);return true;
}
}
