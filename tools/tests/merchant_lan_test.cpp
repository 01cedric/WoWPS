// Exercise the actual realm transport, save and transaction implementation.
// Access is widened only in this test TU; no production test API is needed.
#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <iostream>

using namespace wowee::game;
namespace net=wowee::net;
static sockaddr_in loopback(uint16_t port) {
    sockaddr_in a{};initAddress(a);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);return a;
}
static std::vector<std::vector<uint8_t>> drain(socket_t socket) {
    std::vector<std::vector<uint8_t>> packets;
    for(;;) {
        std::array<uint8_t,MaxPacket+1> buffer{};
        const auto n=::recvfrom(socket,reinterpret_cast<char*>(buffer.data()),buffer.size(),net::datagramFlags(),nullptr,nullptr);
        if(n<0){assert(net::isWouldBlock(net::lastError()));break;}
        assert(size_t(n)<=MaxPacket);packets.emplace_back(buffer.begin(),buffer.begin()+n);
    }
    return packets;
}
static void deliver(LocalRealm::Impl& guest,const std::vector<uint8_t>& bytes) {
    Reader r(bytes.data(),bytes.size());assert(r.u32()==WireMagic && r.u8()==Version);
    const auto type=Message(r.u8());assert(r.u16()==bytes.size());
    const auto seq=r.u32();const auto token=r.u64();
    guest.handleClient(type,r,guest.host,token,seq);
}
static std::shared_ptr<LocalWorldContent> fixture() {
    auto c=std::make_shared<LocalWorldContent>();
    for(uint32_t id:{117u,2392u,6270u}) {
        LocalItemDefinition d;d.id=id;d.name="Merchant fixture";d.stack=1000;d.value=localVendorPrice(id)->sellPrice;c->items.push_back(d);
    }
    LocalNpcDefinition limited;limited.id=66;limited.name="Limited shop";limited.npcFlags=kLocalNpcFlagVendor;c->npcs.push_back(limited);
    LocalNpcDefinition large;large.id=9000;large.name="Paged shop";large.npcFlags=kLocalNpcFlagVendor;
    for(unsigned i=0;i<256;++i) {
        LocalItemDefinition d;d.id=100000+i;d.name="Paged item";d.stack=20;d.value=1;
        c->items.push_back(d);large.vendorItems.push_back(d.id);
    }
    c->npcs.push_back(large);return c;
}
static void saveLegacy(const std::string& path,const LocalRealm::Impl& host,uint8_t version) {
    Writer w;w.u32(SaveMagic);w.u8(version);w.u64(host.realmId);w.u16(uint16_t(host.saved.size()));
    for(const auto& row:host.saved) {
        w.u64(row.identity.a);w.u64(row.identity.b);writePlayer(w,row.player);writeProgress(w,row.player,version);
        writeAppearance(w,row.player);w.u32(0);w.u8(1);
        if (version >= 10) writeBuyback(w,row.player.buybackSerial,row.player.buyback);
    }
    w.u8(0);w.u64(0);w.u16(0);w.u16(0);if(version>=11)w.u16(0);w.u32(checksum(w.bytes.data(),w.bytes.size()));
    assert(atomicWrite(path,w.bytes,false));
}
int main() {
    char temp[]="/tmp/wowps-merchant-lan-XXXXXX";const auto* directory=mkdtemp(temp);assert(directory);
    LocalRealm hostRealm,guestRealm;
    auto& h=*hostRealm.impl_;auto& g=*guestRealm.impl_;auto content=fixture();
    h.gameplay.useContent(content);g.gameplay.useContent(content);
    h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;
    h.directory=directory;h.self.guid=1;h.self.name="Host";h.self.x=h.self.y=h.self.z=0;h.self.money=100000;
    g.self=h.self;g.self.guid=2;g.self.name="Guest";
    h.saved={{{1,11},h.self},{{2,22},g.self}};
    h.saved[1].player.buybackSerial=12;
    for(uint32_t i=12;i>0;--i)h.saved[1].player.buyback.push_back({i,2392,1,1});
    assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;
    LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;
    h.peers.push_back(peer);
    LocalRealmNpc limited;limited.guid=20;limited.entry=66;limited.x=2;limited.vendor=true;
    auto large=limited;large.guid=30;large.entry=9000;
    h.gameplay.setRemoteNpcs({limited,large});g.gameplay.setRemoteNpcs({limited,large});
    auto query=[&](uint64_t guid) {
        h.now+=1;g.now+=1;guestRealm.refreshMerchant(guid);h.receive();return drain(g.socket);
    };
    auto packets=query(20);assert(packets.size()==1);deliver(g,packets[0]);
    assert(guestRealm.vendorRemaining(6270,20)==1 && guestRealm.vendorStock(20)==std::vector<uint32_t>{6270});
    LocalRealmCommand purchase{LocalAction::BuyFromVendor,1,6270};purchase.serviceNpcGuid=20;std::string result;
    assert(h.runCommand(h.self,purchase,result)); // Another owner depletes this same physical shop.
    {
        // Restart and the catalog-free character scanner both retain stock.
        LocalRealm::Impl restarted, scanner;
        restarted.gameplay.useContent(content);
        assert(restarted.parseSave(h.directory+"/realm.wprs"));
        assert(scanner.parseSave(h.directory+"/realm.wprs"));
        restarted.gameplay.setRemoteNpcs({limited,large});
        assert(restarted.gameplay.vendorRemaining(h.self,6270,20)==0);
        assert(scanner.saved.size()==2 && scanner.saved[0].player.guid==h.self.guid);
        assert(scanner.gameplay.savedVendorStock()==h.gameplay.savedVendorStock());
        std::string travelError;
        assert(restarted.gameplay.setTravelNetwork({{1},{2}}, {{1,1,2,0}}, {}, travelError));
        assert(restarted.gameplay.savedVendorStock()==h.gameplay.savedVendorStock());
        // Save a partial timer and verify production load/tick across its edge.
        auto stock = restarted.gameplay.savedVendorStock();stock[0].elapsedMs=8999875;
        assert(restarted.gameplay.restoreVendorStock(stock));
        restarted.directory=h.directory;restarted.self=restarted.saved[0].player;
        restarted.state=LocalRealmState::Hosting;
        assert(restarted.saveRealm());
        assert(scanner.parseSave(h.directory+"/realm.wprs"));
        scanner.gameplay.useContent(content); // Rebuild clears state; reload after content.
        assert(scanner.parseSave(h.directory+"/realm.wprs"));
        scanner.gameplay.setRemoteNpcs({limited,large});
        assert(scanner.gameplay.vendorRemaining(h.self,6270,20)==0);
        scanner.gameplay.tick(0.125f,{});
        scanner.gameplay.setRemoteNpcs({limited,large}); // Synthetic actors have no catalog spawns.
        assert(scanner.gameplay.vendorRemaining(h.self,6270,20)==1);
        // Valid checksum, invalid stock: reject without changing loaded state.
        std::vector<uint8_t> bytes;assert(readFile(h.directory+"/realm.wprs",bytes,MaxSaveSize));
        const auto rowStart=bytes.size()-4-28;
        std::fill(bytes.begin()+rowStart,bytes.begin()+rowStart+8,0); // zero physical GUID
        Writer sum;sum.u32(checksum(bytes.data(),bytes.size()-4));
        std::copy(sum.bytes.begin(),sum.bytes.end(),bytes.end()-4);
        const auto before=scanner.gameplay.savedVendorStock();
        assert(atomicWrite(h.directory+"/invalid.wprs",bytes,false));
        assert(!scanner.parseSave(h.directory+"/invalid.wprs"));
        assert(scanner.gameplay.savedVendorStock()==before && scanner.saved.size()==2);
        assert(h.saveRealm());
    }
    h.merchantState(h.peers[0]);packets=drain(g.socket);assert(packets.size()==1);deliver(g,packets[0]);
    assert(guestRealm.vendorRemaining(6270,20)==0);
    auto oldLimited=packets[0];
    // Maximum custom shop: both datagrams are bounded; out-of-order pages
    // remain invisible until complete and a stale selected-shop reply is inert.
    packets=query(30);assert(packets.size()==2 && packets[0].size()==1237 && packets[1].size()==1237);
    deliver(g,packets[1]);assert(guestRealm.vendorStock(30).empty());deliver(g,oldLimited);assert(guestRealm.vendorStock(30).empty());
    deliver(g,packets[0]);assert(guestRealm.vendorStock(30).size()==256);
    auto oldLarge=packets;
    packets=query(20);deliver(g,packets[0]);deliver(g,oldLarge[0]);deliver(g,oldLarge[1]);
    assert(guestRealm.vendorStock(20).size()==1 && guestRealm.vendorRemaining(6270,20)==0);
    // Authority uses the guest's position, map, instance, life and faction.
    auto& owner=h.saved[1].player;
    auto denied=[&](){const auto p=query(20);assert(p.size()==1);deliver(g,p[0]);assert(guestRealm.vendorStock(20).empty() && guestRealm.vendorBuyback(20).empty());};
    owner.x=25;denied();owner.x=0;owner.mapId=1;denied();owner.mapId=0;owner.instanceId=1;denied();owner.instanceId=0;
    owner.dead=true;denied();owner.dead=false;owner.flight.active=true;denied();owner.flight.active=false;
    limited.dead=true;h.gameplay.setRemoteNpcs({limited,large});denied();limited.dead=false;
    limited.hostile=true;h.gameplay.setRemoteNpcs({limited,large});denied();limited.hostile=false;
    h.gameplay.setRemoteNpcs({limited,large});
    LocalFactionTemplate alliance,horde;alliance.id=101;alliance.factionGroup=1;alliance.friendGroup=1;alliance.enemyGroup=2;
    horde.id=102;horde.factionGroup=2;horde.friendGroup=2;horde.enemyGroup=1;
    std::array<uint32_t,12> races{};races[1]=101;races[2]=102;
    assert(h.gameplay.setFactionTemplates({alliance,horde},races,result));
    content->npcs[0].faction=101;content->npcs[0].unitFlags=0x2;owner.race=2;
    assert(h.gameplay.serviceNpc(h.self,kLocalNpcFlagAnyVendor,20));denied();
    owner.race=1;content->npcs[0].unitFlags=0;
    packets=query(999);deliver(g,packets[0]);assert(g.remoteMerchant.empty());
    // A sale is saved with gold/bags and the owner-only ledger before ack.
    owner.buyback.clear();owner.inventory={{2392,1}};const auto money=owner.money;
    LocalRealmCommand sale{LocalAction::SellToVendor,1,2392};sale.serviceNpcGuid=20;
    assert(h.runCommand(owner,sale,result));assert(owner.buyback.size()==1 && owner.inventory.empty());
    assert(owner.money==money+owner.buyback[0].price);
    LocalRealm::Impl loaded;loaded.gameplay.useContent(content);assert(loaded.parseSave(h.directory+"/realm.wprs"));
    assert(loaded.saved[1].player.buyback==owner.buyback && loaded.saved[1].player.money==owner.money && loaded.saved[1].player.inventory.empty());
    packets=query(20);deliver(g,packets[0]);assert(guestRealm.vendorBuyback(20)==owner.buyback);
    // Generic Progress never overwrites the separately replicated buyback.
    h.progress(h.peers[0]);for(const auto& p:drain(g.socket))deliver(g,p);
    assert(guestRealm.vendorBuyback(20)==owner.buyback);
    const auto row=owner.buyback.front();LocalRealmCommand back{LocalAction::BuybackItem,0,row.id};back.serviceNpcGuid=20;
    assert(guestRealm.buybackItem(row.id,20));guestRealm.update(0.01f);h.receive();
    auto responses=drain(g.socket);assert(!responses.empty());for(const auto& p:responses)deliver(g,p);
    assert(h.peers[0].lastCommandSuccess && h.peers[0].lastCommand==1 && g.pendingCommands.empty());
    assert(owner.buyback.empty() && owner.money==money);
    // Retried command ID receives the cached result and cannot charge twice.
    Writer retry;retry.u32(1);retry.u8(uint8_t(LocalAction::BuybackItem));retry.u64(0);retry.u32(row.id);
    retry.u32(0);retry.u32(0);retry.u32(0);retry.u64(20);g.send(Message::Command,g.session,retry,g.host);h.receive();
    drain(g.socket);assert(owner.buyback.empty() && owner.money==money);
    assert(loaded.parseSave(h.directory+"/realm.wprs") && loaded.saved[1].player.buyback.empty() && loaded.saved[1].player.inventory[0].itemId==2392);
    // Failed persistence rolls back the item, money and limited stock too.
    auto secondLimited=limited;secondLimited.guid=40;h.gameplay.setRemoteNpcs({limited,large,secondLimited});
    const auto savedDirectory=h.directory;h.directory+="/missing/child";
    purchase.serviceNpcGuid=40;const auto beforeMoney=owner.money;const auto beforeItems=owner.inventory.size();
    assert(!h.runCommand(owner,purchase,result));assert(owner.money==beforeMoney && owner.inventory.size()==beforeItems);
    assert(h.gameplay.vendorRemaining(owner,6270,40)==1);
    assert(!h.runCommand(owner,sale,result));assert(owner.buyback.empty() && owner.inventory[0].itemId==2392 && owner.money==beforeMoney);
    h.directory=savedDirectory;assert(h.runCommand(owner,sale,result));h.directory+="/missing/child";
    back.id=owner.buyback.front().id;const auto ledger=owner.buyback;const auto saleMoney=owner.money;
    assert(!h.runCommand(owner,back,result));assert(owner.buyback==ledger && owner.inventory.empty() && owner.money==saleMoney);
    h.directory=savedDirectory;
    saveLegacy(h.directory+"/legacy11.wprs",h,11);assert(loaded.parseSave(h.directory+"/legacy11.wprs") && loaded.saved[1].player.bank[0].itemId==0);
    saveLegacy(h.directory+"/legacy10.wprs",h,10);assert(loaded.parseSave(h.directory+"/legacy10.wprs"));
    assert(loaded.saved[1].player.buyback==owner.buyback && loaded.gameplay.savedVendorStock().empty());
    saveLegacy(h.directory+"/legacy.wprs",h,9);assert(loaded.parseSave(h.directory+"/legacy.wprs"));
    assert(loaded.saved[1].player.buyback.empty() && loaded.saved[1].player.money==owner.money);
    // Malformed ledgers never allocate beyond twelve or accept duplicate IDs.
    Writer bad;writeBuyback(bad,1,{{1,2392,1,1},{1,2392,1,1}});Reader br(bad.bytes.data(),bad.bytes.size());
    uint32_t serial;std::vector<LocalMerchantBuyback> rows;assert(!readBuyback(br,serial,rows));
    bad.bytes={0,0,0,1,13};Reader oversized(bad.bytes.data(),bad.bytes.size());assert(!readBuyback(oversized,serial,rows));
    const auto request=g.merchantRequest;guestRealm.refreshMerchant(0);deliver(g,oldLimited);
    assert(g.merchantRequest!=request && g.remoteMerchant.empty() && g.remoteBuyback.empty());
    h.now+=4;h.merchantState(h.peers[0]);assert(drain(g.socket).empty()); // Subscription lease expires.
    std::cout<<"PASS merchant LAN/save: authenticated selected-vendor UDP, 256 offers under MTU, reordered/stale pages, host depletion, owner service gates, separate buyback state, save11/load9/load10, catalog-free character scan, restart restock timing, malformed stock rejection, disk-failure item/gold/stock rollback\n";
    // 01.64: bank and profession ownership is saved atomically with inventory.
    {
        LocalRealmNpc banker=limited;banker.guid=70;banker.vendor=false;banker.banker=true;
        LocalRealmNpc trainer=limited;trainer.guid=80;trainer.vendor=false;trainer.professionTrainer=true;trainer.trainerSkill=164;
        h.gameplay.setRemoteNpcs({banker,trainer});g.gameplay.setRemoteNpcs({banker,trainer});
        owner.dead=false;owner.flight={};owner.attackTarget=0;owner.castingSpellId=0;owner.mapId=0;owner.instanceId=0;owner.x=owner.y=owner.z=0;
        owner.inventory={{117,10}};owner.bank.fill({});owner.equipment.fill(0);
        LocalRealmCommand deposit{LocalAction::BankDeposit,6,117};deposit.serviceNpcGuid=70;
        assert(h.runCommand(owner,deposit,result));assert(owner.inventory[0].count==4 && owner.bank[0].count==6);
        LocalRealm::Impl reload;reload.gameplay.useContent(content);assert(reload.parseSave(h.directory+"/realm.wprs"));
        assert(reload.saved[1].player.bank==owner.bank && reload.saved[1].player.inventory==owner.inventory);
        // Character menu reads save12 without importing any world/recipe data.
        LocalRealm::Impl scan;assert(scan.parseSave(h.directory+"/realm.wprs") && scan.saved.size()==2);
        LocalRealmCommand withdraw{LocalAction::BankWithdraw,2,1};withdraw.serviceNpcGuid=70;withdraw.bid=117;
        assert(h.runCommand(owner,withdraw,result));assert(owner.inventory[0].count==6 && owner.bank[0].count==4);
        withdraw.bid=2392;assert(!h.runCommand(owner,withdraw,result));withdraw.bid=117;
        auto unchanged=owner;
        auto attacker=limited;attacker.guid=90;attacker.targetGuid=owner.guid;attacker.mapId=owner.mapId;attacker.instanceId=owner.instanceId;
        h.gameplay.setRemoteNpcs({banker,trainer,attacker});assert(!h.runCommand(owner,withdraw,result));h.gameplay.setRemoteNpcs({banker,trainer});
        owner.x=100;assert(!h.runCommand(owner,withdraw,result));owner=unchanged;
        owner.dead=true;assert(!h.runCommand(owner,withdraw,result));owner=unchanged;
        owner.flight.active=true;assert(!h.runCommand(owner,withdraw,result));owner=unchanged;
        owner.instanceId=1;assert(!h.runCommand(owner,withdraw,result));owner=unchanged;
        deposit.serviceNpcGuid=80;assert(!h.runCommand(owner,deposit,result));deposit.serviceNpcGuid=70;
        owner.bank.fill({2392,1000});assert(!h.runCommand(owner,deposit,result));assert(owner.inventory==unchanged.inventory);owner=unchanged;
        owner.inventory.clear();for(unsigned i=0;i<LocalGameplay::MaxInventory;++i)owner.inventory.push_back({100000+i,20});
        assert(!h.runCommand(owner,withdraw,result));assert(owner.bank==unchanged.bank);owner=unchanged;
        owner.equipment[0]=117;deposit.target=6;assert(!h.runCommand(owner,deposit,result));owner=unchanged;
        const auto directory=h.directory;h.directory+="/missing/bank";
        deposit.target=1;assert(!h.runCommand(owner,deposit,result));assert(owner.inventory==unchanged.inventory && owner.bank==unchanged.bank);
        assert(!h.runCommand(owner,withdraw,result));assert(owner.inventory==unchanged.inventory && owner.bank==unchanged.bank);h.directory=directory;
        // 01.65: slot-specific moves preserve both stacks and save atomically.
        owner.bank.fill({});owner.bank[0]={117,10};owner.bank[1]={117,995};owner.bank[2]={2392,4};
        auto move=[&](uint32_t from,uint32_t to,uint16_t quantity){
            LocalRealmCommand cmd{LocalAction::BankMove,quantity,from};cmd.buyout=to;cmd.serviceNpcGuid=70;
            cmd.bid=owner.bank[from-1].itemId;cmd.durationMinutes=owner.bank[to-1].itemId;
            cmd.bankSourceCount=owner.bank[from-1].count;cmd.bankDestinationCount=owner.bank[to-1].count;return cmd;
        };
        auto split=move(1,28,3);assert(h.runCommand(owner,split,result));
        assert(owner.bank[0].count==7 && owner.bank[27].itemId==117 && owner.bank[27].count==3);
        unchanged=owner;assert(!h.runCommand(owner,split,result));assert(owner.bank==unchanged.bank); // stale retry, no duplication
        auto merge=move(1,2,6);assert(!h.runCommand(owner,merge,result));assert(owner.bank==unchanged.bank);
        merge.target=5;assert(h.runCommand(owner,merge,result));assert(owner.bank[0].count==2 && owner.bank[1].count==1000);
        auto swap=move(1,3,1);unchanged=owner;assert(!h.runCommand(owner,swap,result));assert(owner.bank==unchanged.bank);
        swap.target=2;assert(h.runCommand(owner,swap,result));assert(owner.bank[0].itemId==2392 && owner.bank[0].count==4 && owner.bank[2].itemId==117 && owner.bank[2].count==2);
        assert(reload.parseSave(h.directory+"/realm.wprs") && reload.saved[1].player.bank==owner.bank);
        assert(reload.saved[1].player.inventory==owner.inventory); // arranging never changes bags
        auto stale=move(1,4,4);unchanged=owner;stale.bankSourceCount++;assert(!h.runCommand(owner,stale,result));
        stale=move(1,4,4);stale.durationMinutes=117;assert(!h.runCommand(owner,stale,result));
        stale=move(1,4,4);stale.bankDestinationCount=1;assert(!h.runCommand(owner,stale,result));
        stale=move(1,4,4);stale.id=0;assert(!h.runCommand(owner,stale,result));
        stale=move(1,4,4);stale.buyout=29;assert(!h.runCommand(owner,stale,result));
        stale=move(1,1,4);assert(!h.runCommand(owner,stale,result));
        stale=move(1,4,4);stale.target=65536;assert(!h.runCommand(owner,stale,result));assert(owner.bank==unchanged.bank);
        auto relocate=move(1,4,4);
        owner.x=100;assert(!h.runCommand(owner,relocate,result));owner=unchanged;
        owner.dead=true;assert(!h.runCommand(owner,relocate,result));owner=unchanged;
        owner.castingSpellId=1;assert(!h.runCommand(owner,relocate,result));owner=unchanged;
        owner.flight.active=true;assert(!h.runCommand(owner,relocate,result));owner=unchanged;
        owner.attackTarget=90;assert(!h.runCommand(owner,relocate,result));owner=unchanged;
        relocate.serviceNpcGuid=80;assert(!h.runCommand(owner,relocate,result));relocate.serviceNpcGuid=70;
        h.directory+="/missing/arrange";assert(!h.runCommand(owner,relocate,result));assert(owner.bank==unchanged.bank && owner.inventory==unchanged.inventory);h.directory=directory;
        assert(h.runCommand(owner,relocate,result));assert(owner.bank[0].itemId==0 && owner.bank[3].itemId==2392);
        // Withdraw validates the source count captured at pickup too.
        LocalRealmCommand staleWithdrawal{LocalAction::BankWithdraw,1,4};staleWithdrawal.bid=2392;staleWithdrawal.buyout=3;staleWithdrawal.serviceNpcGuid=70;
        unchanged=owner;assert(!h.runCommand(owner,staleWithdrawal,result));assert(owner.bank==unchanged.bank && owner.inventory==unchanged.inventory);
        // A malformed destination cannot underflow the bank deposit capacity.
        owner.bank[2]={117,1001};deposit.target=1;unchanged=owner;
        assert(!h.runCommand(owner,deposit,result));assert(owner.bank==unchanged.bank && owner.inventory==unchanged.inventory);
        // Fractional skill gains survive saving; a failed batch consumes nothing.
        content->recipes.clear();
        for(uint32_t i=0;i<96;++i){LocalRecipe recipe;recipe.spellId=900000+i;recipe.skillId=164;recipe.requiredSkill=1;recipe.name="Test recipe";
            recipe.createdItemId=2392;recipe.createdCount=1;recipe.trivialLow=1;recipe.trivialHigh=101;recipe.reagents={{117,2}};content->recipes.push_back(recipe);}
        owner.professions={{164,1,75,0},{171,1,75,0},{129,1,75,0}};owner.knownRecipes.clear();
        for(const auto& recipe:content->recipes)owner.knownRecipes.push_back(recipe.spellId);
        owner.inventory={{117,10}};owner.bank.fill({});
        LocalRealmCommand craft{LocalAction::CraftItem,3,900000};
        assert(h.runCommand(owner,craft,result));
        assert(owner.inventory.size()==2 && owner.inventory[0].count==4 && owner.inventory[1].count==3);
        assert(owner.professions[0].current>1 && owner.professions[0].progress>0);
        assert(reload.parseSave(h.directory+"/realm.wprs"));assert(reload.saved[1].player.professions[0].progress==owner.professions[0].progress);
        assert(reload.saved[1].player.knownRecipes.size()==96);
        unchanged=owner;craft.target=3;assert(!h.runCommand(owner,craft,result));
        assert(owner.inventory==unchanged.inventory && owner.professions[0].current==unchanged.professions[0].current && owner.professions[0].progress==unchanged.professions[0].progress);
        craft.target=21;assert(!h.runCommand(owner,craft,result));craft.target=1;
        h.directory+="/missing/craft";assert(!h.runCommand(owner,craft,result));assert(owner.inventory==unchanged.inventory && owner.professions[0].progress==unchanged.professions[0].progress);h.directory=directory;
        // Force the complete owner snapshot above the datagram limit. Bags,
        // bank, recipe book and skill progress commit only once every page arrives.
        owner.bank.fill({117,3});owner.inventory.clear();
        for(unsigned i=0;i<LocalGameplay::MaxInventory;++i)owner.inventory.push_back({100000+i,20});
        owner.knownSpells.clear();owner.cooldowns.clear();
        for(unsigned i=1;i<=LocalGameplay::MaxSpells;++i)owner.knownSpells.push_back(i);
        for(unsigned i=1;i<=LocalGameplay::MaxCooldowns;++i)owner.cooldowns.push_back({i,100});
        owner.quests.clear();for(unsigned i=1;i<=LocalGameplay::MaxQuests;++i)owner.quests.push_back({i,LocalQuestStatus::Active,{0,0,0,0}});
        g.self.bank.fill({});g.self.inventory.clear();g.historyRevision=h.peers[0].historyRevision;g.self.completedQuestIds=owner.completedQuestIds;
        drain(g.socket);h.progress(h.peers[0]);auto fragments=drain(g.socket);assert(fragments.size()>=2);
        for(const auto& f:fragments)assert(f.size()<=MaxPacket);
        deliver(g,fragments.back());assert(g.self.inventory.empty() && g.self.bank[0].itemId==0);
        deliver(g,fragments.back());assert(g.self.inventory.empty());
        for(size_t i=0;i+1<fragments.size();++i)deliver(g,fragments[i]);
        assert(g.self.inventory==owner.inventory && g.self.bank==owner.bank && g.self.knownRecipes.size()==96 && g.self.professions[0].progress==owner.professions[0].progress);
        owner.bank[0].count=4;h.progress(h.peers[0]);auto newerFragments=drain(g.socket);
        deliver(g,newerFragments[0]);for(const auto& f:fragments)deliver(g,f);assert(g.self.bank[0].count==3);
        for(size_t i=1;i<newerFragments.size();++i)deliver(g,newerFragments[i]);assert(g.self.bank[0].count==4);
        // Actual guest command, then retry the exact command ID: once-only transfer.
        owner.inventory={{117,10}};owner.bank.fill({});owner.quests.clear();owner.cooldowns.clear();owner.knownSpells.clear();
        LocalRealmCommand lanDeposit{LocalAction::BankDeposit,2,117};lanDeposit.serviceNpcGuid=70;
        assert(guestRealm.depositBankItem(117,2,70));guestRealm.update(.01f);h.receive();
        auto replies=drain(g.socket);for(const auto& f:replies)deliver(g,f);
        assert(owner.inventory[0].count==8 && owner.bank[0].count==2 && g.pendingCommands.empty());
        Writer replay;replay.u32(h.peers[0].lastCommand);replay.u8(uint8_t(LocalAction::BankDeposit));replay.u64(2);replay.u32(117);
        replay.u32(0);replay.u32(0);replay.u32(0);replay.u64(70);g.send(Message::Command,g.session,replay,g.host);h.receive();drain(g.socket);
        assert(owner.inventory[0].count==8 && owner.bank[0].count==2);
        // Move over the actual authenticated guest wire, with source/target
        // expectations encoded in the new action extension; exact replay is inert.
        const auto hostBank=h.self.bank;
        assert(guestRealm.moveBankItem(1,28,1,{117,2},{},70));guestRealm.update(.01f);h.receive();
        replies=drain(g.socket);for(const auto& f:replies)deliver(g,f);
        assert(owner.bank[0].count==1 && owner.bank[27].count==1 && g.pendingCommands.empty());
        assert(g.self.bank==owner.bank && h.self.bank==hostBank);
        Writer replayMove;replayMove.u32(h.peers[0].lastCommand);replayMove.u8(uint8_t(LocalAction::BankMove));replayMove.u64(1);replayMove.u32(1);
        replayMove.u32(117);replayMove.u32(28);replayMove.u32(0);replayMove.u16(2);replayMove.u16(0);replayMove.u64(70);
        g.send(Message::Command,g.session,replayMove,g.host);h.receive();drain(g.socket);
        assert(owner.bank[0].count==1 && owner.bank[27].count==1);
        assert(reload.parseSave(h.directory+"/realm.wprs") && reload.saved[1].player.bank==owner.bank);
        // Missing/truncated count extension must not consume the next command ID.
        const auto lastMove=h.peers[0].lastCommand;
        Writer truncated;truncated.u32(lastMove+1);truncated.u8(uint8_t(LocalAction::BankMove));truncated.u64(1);truncated.u32(1);
        truncated.u32(117);truncated.u32(28);truncated.u32(117);truncated.u64(70);
        g.send(Message::Command,g.session,truncated,g.host);h.receive();assert(h.peers[0].lastCommand==lastMove);
        assert(owner.bank[0].count==1 && owner.bank[27].count==1);
        // A fresh command with outdated counts is rejected and acknowledged.
        assert(guestRealm.moveBankItem(1,28,1,{117,2},{},70));guestRealm.update(.01f);h.receive();
        replies=drain(g.socket);for(const auto& f:replies)deliver(g,f);
        assert(owner.bank[0].count==1 && owner.bank[27].count==1 && g.pendingCommands.empty());
        std::cout<<"PASS 01.65 bank arrangement: split/merge/swap/relocate; both slot snapshots; source-count withdrawal; malformed bounds; service/life/combat/cast/flight gates; item conservation; save12 restart; disk rollback; real UDP move/replay/stale/truncated rejection; host/guest isolation\n";
        // Unlearning removes only that profession and its own recipe list.
        LocalRealmCommand unlearn{LocalAction::UnlearnProfession,0,129};assert(!h.runCommand(owner,unlearn,result));
        unlearn.id=164;h.directory+="/missing/unlearn";assert(!h.runCommand(owner,unlearn,result));assert(owner.professions.size()==3 && owner.knownRecipes.size()==96);h.directory=directory;
        assert(h.runCommand(owner,unlearn,result));assert(owner.professions.size()==2 && owner.knownRecipes.empty());
        assert(reload.parseSave(h.directory+"/realm.wprs") && reload.saved[1].player.professions.size()==2);
        LocalRealmCommand learn{LocalAction::LearnProfession,0,164};learn.serviceNpcGuid=80;owner.level=10;
        assert(h.runCommand(owner,learn,result));assert(owner.professions.back().current==1 && owner.knownRecipes.empty());
        std::cout<<"PASS 01.64 economy: personal bank ownership/capacity/access/stale-item/disk rollback; save12 catalog-free scan; atomic batches and persisted skill fraction; 96 recipes; reordered/duplicate/stale multipart owner snapshots; guest bank retry idempotency; unlearn/relearn primary and recipe removal\n";
    }
    hostRealm.stop();guestRealm.stop();std::filesystem::remove_all(directory);
}
