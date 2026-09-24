#include "game/local_bots.hpp"
#include "game/local_mail.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>

using namespace wowee::game;

namespace wowee::game {
const LocalItemDefinition* LocalWorldContent::item(uint32_t id) const {
    for (const auto& item : items) if (item.id == id) return &item;
    return nullptr;
}
const LocalSpellDefinition* LocalWorldContent::spell(uint32_t id) const {
    for (const auto& spell : spells) if (spell.id == id) return &spell;
    return nullptr;
}
}

static LocalItemInstanceState marked() {
    LocalItemInstanceState i;
    i.instanceFlags = 0x10;
    i.permanentEnchantId = 3820;
    i.temporaryEnchantId = 2673;
    i.socketEnchantIds = {3520, 3521, 3522};
    i.curDurability = 37;
    i.maxDurability = 80;
    i.randomPropertyId = -123;
    i.suffixFactor = 9876;
    return i;
}
static LocalRealmPlayer actor(uint64_t guid, uint32_t item, uint16_t count, const LocalItemInstanceState& instance={}) {
    LocalRealmPlayer p;
    p.guid=guid;p.name="Actor"+std::to_string(guid);p.race=1;p.classId=1;p.level=80;p.health=p.maxHealth=100;p.money=10000;
    p.inventory={{item,count,0,instance}};
    return p;
}
static const LocalItemStack* findInstance(const LocalRealmPlayer& p,uint32_t item,const LocalItemInstanceState& instance) {
    const auto it=std::find_if(p.inventory.begin(),p.inventory.end(),[&](const auto& s){return s.itemId==item && s.instance==instance;});
    return it==p.inventory.end()?nullptr:&*it;
}

int main() {
    LocalWorldContent world;
    LocalItemDefinition a;a.id=117;a.name="Marked fixture";a.stack=20;a.value=10;world.items.push_back(a);
    LocalItemDefinition b=a;b.id=118;b.name="Other fixture";world.items.push_back(b);
    const auto state=marked();

    // Bank/inventory primitive: split and swap retain instance snapshots, and
    // differently rolled copies of the same template never merge by accident.
    LocalItemStack source{117,5,0,state}, empty{};empty.bagSlot=255;
    assert(moveLocalInventoryStack(source,empty,3,world));
    assert(source.count==2 && source.instance==state && empty.count==3 && empty.instance==state);
    LocalItemStack plain{117,1,255,{}};
    const auto beforeSource=source,beforePlain=plain;
    assert(!moveLocalInventoryStack(source,plain,1,world));
    assert(source==beforeSource && plain==beforePlain);
    LocalItemStack other{118,4,255,{}};
    assert(moveLocalInventoryStack(source,other,2,world));
    assert(source.itemId==118 && source.count==4 && source.instance==LocalItemInstanceState{});
    assert(other.itemId==117 && other.count==2 && other.instance==state);

    // Mail snapshots the concrete source and claims it as the same instance.
    auto sender=actor(1,117,5,state),recipient=actor(2,118,1);
    LocalRealmPlayer afterSend;LocalMail letter;std::string error;
    assert(prepareLocalMail(sender,recipient,"Instance","",0,0,{{117,3,5,0}},world,afterSend,letter,error));
    assert(letter.items[0].itemId==117 && letter.items[0].count==3 && letter.items[0].instance==state);
    assert(afterSend.inventory[0].count==2 && afterSend.inventory[0].instance==state);
    auto claimed=recipient;
    assert(giveLocalMailItem(claimed,letter.items[0],world));
    const auto* mailCopy=findInstance(claimed,117,state);assert(mailCopy && mailCopy->count==3);

    // Trade fingerprints include instance fields, and the received stack keeps
    // the original roll rather than merging into a plain same-template stack.
    auto traderA=actor(10,117,5,state),traderB=actor(11,118,2);
    traderA.x=traderB.x=traderA.y=traderB.y=traderA.z=traderB.z=0;
    LocalTrade trade;trade.id=1;trade.revision=1;trade.state=2;trade.players={traderA.guid,traderB.guid};
    trade.fingerprints={localTradeFingerprint(traderA),localTradeFingerprint(traderB)};
    trade.items[0][0]={117,3,5,0};
    LocalRealmPlayer outA,outB;
    assert(prepareLocalTrade(trade,traderA,traderB,world,outA,outB,error));
    const auto* tradeCopy=findInstance(outB,117,state);assert(tradeCopy && tradeCopy->count==3);
    auto changed=traderA;changed.inventory[0].instance.permanentEnchantId++;
    assert(localTradeFingerprint(changed)!=trade.fingerprints[0]);

    // AH escrow/listing/delivery all carry the same snapshot.
    LocalBotDirector market;auto seller=actor(20,117,5,state);auto buyer=actor(21,118,1);buyer.money=10000;
    assert(market.listItemPriced(seller,117,3,100,500,720,world,error));
    assert(market.auctions().size()==1 && market.auctions()[0].instance==state && seller.inventory[0].count==2);
    const auto auctionId=market.auctions()[0].id;
    assert(market.buyout(auctionId,buyer,world,error));
    assert(market.deliveries().size()>=1 && market.deliveries()[0].instance==state);
    assert(market.deliver(buyer,world));
    const auto* auctionCopy=findInstance(buyer,117,state);assert(auctionCopy && auctionCopy->count==3);

    // A concrete soulbound copy may never enter trade or AH escrow.
    auto bound=actor(30,117,1,state);bound.inventory[0].instance.soulbound=true;bound.inventory[0].instance.instanceFlags|=1u;
    LocalTradeItem offered{117,1,1,0};assert(!localTradeItemValid(bound,offered,world));
    assert(!market.listItemPriced(bound,117,1,100,500,720,world,error));

    std::cout << "PASS 4.2 item-instance transfer: bank/mail/trade/AH preserve enchant/socket/durability/random-property state and reject bound escrow\n";
}
