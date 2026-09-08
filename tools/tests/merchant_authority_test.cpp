#include "game/local_gameplay.hpp"
#include "game/local_services.hpp"
#include <cassert>
#include <algorithm>
#include <iostream>

using namespace wowee::game;
int main() {
    auto content=std::make_shared<LocalWorldContent>();
    for(uint32_t id:{2488u,2392u,6270u,117u}) {
        LocalItemDefinition item;item.id=id;item.name="Fixture item";item.stack=1000;
        const auto* price=localVendorPrice(id);assert(price);item.value=price->sellPrice;
        content->items.push_back(item);
    }
    for(uint32_t id:{54u,2046u,66u}) {
        LocalNpcDefinition npc;npc.id=id;npc.name="Fixture merchant";npc.npcFlags=kLocalNpcFlagVendor;
        content->npcs.push_back(npc);
    }
    std::sort(content->items.begin(),content->items.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    std::sort(content->npcs.begin(),content->npcs.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    LocalGameplay game;game.useContent(content);
    LocalRealmNpc near,selected;
    near.guid=10;near.entry=54;near.mapId=0;near.x=1;near.vendor=true;near.vendorCategories=1;
    selected=near;selected.guid=20;selected.entry=2046;selected.x=3;
    game.setRemoteNpcs({near,selected});
    LocalRealmPlayer player;player.guid=1;player.name="Human";player.x=player.y=player.z=0;player.money=1000000;
    std::vector<LocalRealmPlayer*> players{&player};std::string result;
    assert(game.vendorStock(player)==std::vector<uint32_t>{2488});
    assert(game.vendorStock(player,20)==std::vector<uint32_t>{2392});
    LocalRealmCommand buy{LocalAction::BuyFromVendor,1,2392};buy.serviceNpcGuid=20;
    const auto money=player.money;
    assert(game.execute(player,buy,players,result));
    assert(player.inventory.size()==1 && player.inventory[0].itemId==2392 && player.inventory[0].count==1);
    assert(player.money==money-localVendorBuyPrice(*content->item(2392),1));
    auto reject=[&](LocalRealmCommand cmd) {
        const auto before=player.money;const auto count=player.inventory.size();
        assert(!game.execute(player,cmd,players,result));
        assert(player.money==before && player.inventory.size()==count);
    };
    buy.serviceNpcGuid=10;reject(buy); // Selected merchant identity is not substituted.
    buy.serviceNpcGuid=999;reject(buy);
    buy.serviceNpcGuid=20;buy.target=65536;reject(buy);buy.target=1;
    selected.x=20;game.setRemoteNpcs({near,selected});reject(buy);
    selected.x=3;selected.mapId=1;game.setRemoteNpcs({near,selected});reject(buy);
    selected.mapId=0;selected.instanceId=1;game.setRemoteNpcs({near,selected});reject(buy);
    selected.instanceId=0;selected.dead=true;game.setRemoteNpcs({near,selected});reject(buy);
    selected.dead=false;game.setRemoteNpcs({near,selected});
    LocalRealmCommand sell{LocalAction::SellToVendor,1,2392};sell.serviceNpcGuid=20;
    assert(game.execute(player,sell,players,result) && player.inventory.empty());
    selected.entry=66;game.setRemoteNpcs({selected});buy.id=6270;
    player.inventory.assign(LocalGameplay::MaxInventory,{117,1000});
    reject(buy); // Failed inventory-capacity transaction does not consume stock.
    assert(game.vendorRemaining(player,6270,20)==1);
    player.inventory.clear();assert(game.execute(player,buy,players,result));
    assert(game.vendorRemaining(player,6270,20)==0);reject(buy);
    game.setRemoteNpcs({});game.setRemoteNpcs({selected});
    assert(game.vendorRemaining(player,6270,20)==0); // Streaming can't refill the shop.
    auto guest=player;guest.guid=2;guest.inventory.clear();players.push_back(&guest);
    assert(!game.execute(guest,buy,players,result) && guest.inventory.empty());
    std::cout<<"PASS merchant authority: exact selected NPC, map/instance/range/alive gates, quantity, money/item atomicity, shared limited stock across unload\n";
}
