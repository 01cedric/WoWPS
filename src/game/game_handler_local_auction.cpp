#include "game/game_handler.hpp"
#include "game/local_realm.hpp"
#include "game/local_auction_catalog.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_loader.hpp"
#include "core/logger.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace wowee::game {
const ItemSlot* GameHandler::localBagSlot(int index, uint64_t* guid) {
    if (guid) *guid=0;
    auto* realm=localAuctionRealm_?localAuctionRealm_():nullptr;
    const auto* player=realm?realm->localPlayer():nullptr;
    if (!localExploration_ || !player || index<0 || index>=24) return nullptr;
    const auto storageIndex=localInventoryIndex(*player,uint32_t(index));if(storageIndex>=player->inventory.size())return nullptr;
    const auto& stack=player->inventory[storageIndex];
    const auto* definition=realm->content().item(stack.itemId);
    if (!definition || !stack.count) return nullptr;
    cacheLocalAuctionItem(stack.itemId);
    auto& item=localBagSlots_[size_t(index)].item;
    item.itemId=stack.itemId;item.stackCount=stack.count;
    item.name=definition->name;item.maxStack=definition->stack;
    item.inventoryType=definition->inventoryType;item.displayInfoId=definition->displayId;
    item.sellPrice=definition->value;item.armor=definition->armor;
    item.damageMin=item.damageMax=float(definition->attack);
    if (const auto* info=getItemInfo(stack.itemId)) {
        item.displayInfoId=info->displayInfoId;item.quality=ItemQuality(info->quality);
        item.requiredLevel=info->requiredLevel;
    }
    // The inventory is indexed, not a network object store. Include the entry in
    // the handle so a compacted bag cannot silently sell a different item.
    if (guid) *guid=(uint64_t(stack.itemId)<<16)|uint64_t(index+1);
    return &localBagSlots_[size_t(index)];
}

void GameHandler::cacheLocalAuctionItem(uint32_t id) {
    if (itemInfoCache_.count(id)) return;
    auto* realm=localAuctionRealm_?localAuctionRealm_():nullptr;
    const auto* item=realm?realm->content().item(id):nullptr;
    if (!item) return;
    ItemQueryResponseData data;
    data.entry=id; data.name=item->name; data.displayInfoId=item->displayId;
    data.inventoryType=item->inventoryType; data.maxStack=item->stack; data.sellPrice=item->value;
    data.armor=item->armor; data.damageMin=data.damageMax=float(item->attack);
    data.valid=true; data.quality=1;
    if (const auto* metadata = localAuctionMetadata(id)) {
        data.itemClass=metadata->itemClass; data.subClass=metadata->subClass;
        data.quality=metadata->quality; data.requiredLevel=metadata->requiredLevel;
        data.sellPrice=metadata->sellPrice;
        data.allowableClass=metadata->allowableClasses; data.allowableRace=metadata->allowableRaces;
        data.bindType=metadata->bonding;
    }
    itemInfoCache_[id]=std::move(data);
}

bool GameHandler::submitLocalAuction(uint32_t itemId,uint16_t count,uint32_t bid,uint32_t buyout,uint32_t minutes,uint16_t stacks) {
    auto* realm=localAuctionRealm_?localAuctionRealm_():nullptr;
    if (!localExploration_ || !auctionOpen_ || !realm) return false;
    const bool ok=realm->listAuctionStacks(itemId,count,stacks,bid,buyout,minutes,auctioneerGuid_);
    if (!ok && addonEventCallback_) addonEventCallback_("UI_ERROR_MESSAGE",{realm->actionStatus()});
    refreshLocalAuctions(true); return ok;
}

bool GameHandler::submitLocalAuctionStacks(uint64_t guid, uint16_t count, uint16_t stacks,
        uint32_t bid,uint32_t buyout,uint32_t minutes) {
    uint64_t current=0;
    const auto* item=localBagSlot(int(guid & 0xffffu)-1,&current);
    return item && current==guid && submitLocalAuction(item->item.itemId,count,bid,buyout,minutes,stacks);
}

void GameHandler::refreshLocalAuctions(bool force) {
    if(localAuctionRefreshing_)return;
    struct RefreshGuard {bool& active;RefreshGuard(bool& v):active(v){active=true;}~RefreshGuard(){active=false;}} guard(localAuctionRefreshing_);
    auto* realm=localAuctionRealm_?localAuctionRealm_():nullptr;
    if (!localExploration_ || !auctionOpen_ || !realm || !realm->localPlayer()) return;
    const auto& p=*realm->localPlayer();
    bool near=false;
    for (const auto& n:realm->npcs()) {
        const float dx=n.x-p.x,dy=n.y-p.y,dz=n.z-p.z;
        if (n.guid==auctioneerGuid_ && n.auctioneer && !n.dead && !n.hostile &&
            n.mapId==p.mapId && n.instanceId==p.instanceId && dx*dx+dy*dy+dz*dz<=64) near=true;
    }
    if (!near || p.dead) {closeAuctionHouse(); return;}
    uint64_t hash=1469598103934665603ULL;
    const auto mix=[&](uint64_t v){hash=(hash^v)*1099511628211ULL;};
    for (const auto& a:realm->auctions()) {mix(a.id);mix(a.highestBid);mix(a.bid);mix(a.buyout);mix(uint32_t(std::max(0.f,a.remainingSeconds)/60));}
    mix(realm->actionStatusRevision());
    if (!force && hash==localAuctionFingerprint_) return;
    localAuctionFingerprint_=hash;
    AuctionListResult browse,owner,bidder;
    localAuctionNames_.clear(); // bound name cache to the current board and roster
    for (const auto& peer:realm->players()) localAuctionNames_[peer.guid]=peer.name;
    localAuctionNames_[p.guid]=p.name;
    const auto lower=[](std::string text) {for(char& c:text)c=char(std::tolower(static_cast<unsigned char>(c)));return text;};
    const auto needle=lower(localAuctionQuery_.name);
    for (const auto& a:realm->auctions()) {
        if (a.remainingSeconds<=0) continue;
        localAuctionNames_[a.seller]=a.sellerName;
        cacheLocalAuctionItem(a.itemId);
        const auto* item=getItemInfo(a.itemId); if (!item) continue;
        AuctionEntry entry;
        entry.auctionId=a.id;entry.itemEntry=a.itemId;entry.stackCount=a.count;entry.ownerGuid=a.seller;
        entry.startBid=a.bid;entry.minBidIncrement=std::max(1u,a.highestBid/20);
        entry.buyoutPrice=a.buyout;entry.currentBid=a.highestBid;entry.bidderGuid=a.highestBidder;
        entry.timeLeftMs=uint32_t(std::clamp(double(a.remainingSeconds)*1000.0,0.0,172800000.0));
        if(a.seller==p.guid)owner.auctions.push_back(entry);
        if(a.highestBidder==p.guid)bidder.auctions.push_back(entry);
        const auto& q=localAuctionQuery_;
        if(!needle.empty() && lower(item->name).find(needle)==std::string::npos)continue;
        if(q.quality!=UINT32_MAX && item->quality<q.quality)continue;
        if(q.itemClass!=UINT32_MAX && item->itemClass!=q.itemClass)continue;
        if(q.itemSubClass!=UINT32_MAX && item->subClass!=q.itemSubClass)continue;
        if(q.invTypeMask && q.invTypeMask!=UINT32_MAX && item->inventoryType!=q.invTypeMask)continue;
        if(q.levelMin && item->requiredLevel<q.levelMin)continue;
        if(q.levelMax && item->requiredLevel>q.levelMax)continue;
        if(q.usableOnly) {
            if(item->requiredLevel>p.level)continue;
            if(const auto* metadata=localAuctionMetadata(a.itemId)) {
                if(metadata->allowableClasses && p.classId>0 && p.classId<=32 && !(metadata->allowableClasses&(1u<<(p.classId-1))))continue;
                if(metadata->allowableRaces && p.race>0 && p.race<=32 && !(metadata->allowableRaces&(1u<<(p.race-1))))continue;
            }
        }
        browse.auctions.push_back(entry);
    }
    // Apply sorting to the complete matching board before pagination. Sorting
    // just the first50 rows misses cheaper matches that happen to be on page2.
    std::sort(browse.auctions.begin(),browse.auctions.end(),[&](const auto& a,const auto& b){
        const auto compare=[](const auto& left,const auto& right){return left<right?-1:(right<left?1:0);};
        for(const auto& key:localAuctionSort_) {
            int order=0;
            const auto* left=getItemInfo(a.itemEntry); const auto* right=getItemInfo(b.itemEntry);
            switch(key.column) {
            case 0: order=compare(left?left->requiredLevel:0u,right?right->requiredLevel:0u);break;
            case 1: order=compare(left?left->quality:1u,right?right->quality:1u);break;
            case 2: order=compare(a.buyoutPrice,b.buyoutPrice);break;
            case 3: order=compare(a.timeLeftMs,b.timeLeftMs);break;
            case 5: order=compare(left?left->name:std::string{},right?right->name:std::string{});break;
            case 6: order=compare(a.buyoutPrice,b.buyoutPrice);break;
            case 7: order=compare(localAuctionNames_[a.ownerGuid],localAuctionNames_[b.ownerGuid]);break;
            case 8: order=compare(a.currentBid?a.currentBid:a.startBid,b.currentBid?b.currentBid:b.startBid);break;
            case 9: order=compare(a.stackCount,b.stackCount);break;
            default:break;
            }
            if(order)return key.descending?order>0:order<0;
        }
        return a.auctionId<b.auctionId;
    });
    browse.totalCount=uint32_t(browse.auctions.size());owner.totalCount=uint32_t(owner.auctions.size());bidder.totalCount=uint32_t(bidder.auctions.size());
    const size_t begin=std::min<size_t>(localAuctionQuery_.offset,browse.auctions.size());
    const size_t end=std::min(begin+50,browse.auctions.size());
    browse.auctions=std::vector<AuctionEntry>(browse.auctions.begin()+begin,browse.auctions.begin()+end);
    auctionBrowseResults_=std::move(browse);auctionOwnerResults_=std::move(owner);auctionBidderResults_=std::move(bidder);
    if(addonEventCallback_) for(const auto* event:{"AUCTION_ITEM_LIST_UPDATE","AUCTION_OWNED_LIST_UPDATE","AUCTION_BIDDER_LIST_UPDATE"}) addonEventCallback_(event,{});
}
} // namespace wowee::game
