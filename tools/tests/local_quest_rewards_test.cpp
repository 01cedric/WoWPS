#include "game/local_bots.hpp"
#include "game/local_world_catalog.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <iostream>
#include "local_group_rewards_fixture.hpp"
using namespace wowee::game;
namespace net=wowee::net;
static sockaddr_in loopback(uint16_t port){sockaddr_in a{};initAddress(a);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);return a;}
static std::vector<std::vector<uint8_t>> drain(socket_t socket){
    std::vector<std::vector<uint8_t>> packets;
    for(;;){std::array<uint8_t,MaxPacket+1> buffer{};const auto n=::recvfrom(socket,reinterpret_cast<char*>(buffer.data()),buffer.size(),net::datagramFlags(),nullptr,nullptr);
        if(n<0){assert(net::isWouldBlock(net::lastError()));break;}assert(size_t(n)<=MaxPacket);packets.emplace_back(buffer.begin(),buffer.begin()+n);}
    return packets;
}
static void deliver(LocalRealm::Impl& guest,const std::vector<uint8_t>& bytes,uint64_t wrongToken=0){
    Reader r(bytes.data(),bytes.size());assert(r.u32()==WireMagic && r.u8()==Version);const auto type=Message(r.u8());assert(r.u16()==bytes.size());
    const auto seq=r.u32();const auto token=r.u64();guest.handleClient(type,r,guest.host,wrongToken?wrongToken:token,seq);
}
static uint32_t countItem(const LocalRealmPlayer& p,uint32_t id) {
    uint32_t result=0;for(const auto& s:p.inventory)if(s.itemId==id)result+=s.count;return result;
}
static std::shared_ptr<LocalWorldContent> questContent() {
    auto c=rewardContent();for(uint32_t id=118;id<=128;++id){auto item=c->items[0];item.id=id;item.stack=1;c->items.push_back(item);}
    auto& q=c->quests[0];q.giverEntry=q.turnInEntry=50;q.money=10;q.xp=17;
    q.objectives={{LocalQuestObjective::Type::Collect,117,1}};
    q.rewardItem=118;q.rewardCount=1;q.additionalRewards={{119,1},{120,1},{121,1}};
    q.rewardChoices={{122,1},{123,1},{124,1},{125,1},{126,1},{127,1}};return c;
}
static LocalRealmPlayer questPlayer(uint64_t id) {
    auto p=rewardPlayer(id);p.money=100;p.inventory={{117,1}};
    p.quests={{1,LocalQuestStatus::Complete,{1}}};return p;
}
static LocalRealmCommand turnIn(uint64_t npc,uint32_t choice) {
    LocalRealmCommand cmd{LocalAction::TurnInQuest,npc,1};cmd.bid=choice;return cmd;
}
static void authority() {
    auto c=questContent();LocalGameplay game;game.useContent(c);
    auto npc=rewardNpc();npc.hostile=false;npc.questGiver=true;game.setRemoteNpcs({npc});
    auto p=questPlayer(1);std::string result;
    for(auto choice:{0u,7u,UINT32_MAX}) {
        assert(!game.execute(p,turnIn(npc.guid,choice),{&p},result));
        assert(p.money==100 && countItem(p,117)==1 && p.quests.size()==1 && p.completedQuestIds.empty());
    }
    p.money=999999995;assert(!game.execute(p,turnIn(npc.guid,2),{&p},result));p.money=100;
    p.inventory.assign(24,{128,1});p.inventory[0]={117,1};
    // Collecting the objective frees only one slot: the partial fixed bundle must roll back.
    assert(!game.execute(p,turnIn(npc.guid,2),{&p},result));
    assert(p.inventory.size()==24 && countItem(p,117)==1 && countItem(p,118)==0 && p.money==100 && p.xp==0);
    p.inventory={{117,1}};
    assert(game.execute(p,turnIn(npc.guid,2),{&p},result));
    for(auto id:{118u,119u,120u,121u,123u})assert(countItem(p,id)==1);
    assert(countItem(p,122)==0 && countItem(p,124)==0 && countItem(p,117)==0 && p.money==110 && p.xp==17);
    assert(!game.execute(p,turnIn(npc.guid,1),{&p},result));
    for(uint32_t choice=1;choice<=6;++choice){p=questPlayer(1);assert(game.execute(p,turnIn(npc.guid,choice),{&p},result));assert(countItem(p,121+choice)==1);}
    // Rewards sharing an item ID still add both authored quantities.
    c->quests[0].rewardChoices={{118,1}};p=questPlayer(1);assert(game.execute(p,turnIn(npc.guid,1),{&p},result));assert(countItem(p,118)==2);
    c->quests[0].rewardChoices.clear();p=questPlayer(1);assert(!game.execute(p,turnIn(npc.guid,1),{&p},result));assert(game.execute(p,turnIn(npc.guid,0),{&p},result));
    c->quests[0].additionalRewards.clear();p=questPlayer(1);assert(game.execute(p,turnIn(npc.guid,0),{&p},result));assert(p.inventory.size()==1 && p.inventory[0].itemId==118);
    c->quests[0].rewardChoices={{999999,1}};p=questPlayer(1);assert(!game.execute(p,turnIn(npc.guid,1),{&p},result));assert(p.money==100 && countItem(p,117)==1);
    c->quests[0].rewardChoices.assign(7,{122,1});assert(!game.execute(p,turnIn(npc.guid,1),{&p},result));
    std::cout<<"PASS quest reward authority: all six choices, four fixed items, invalid/missing choice, full-bag bundle rollback, money cap, XP, same-item totals, legacy reward and malformed bundle\n";
}
static void questLan() {
    char temp[]="/tmp/wowps-quest-reward-0172-XXXXXX";const auto* directory=mkdtemp(temp);assert(directory);
    LocalRealm hostRealm,guestRealm;auto& h=*hostRealm.impl_;auto& g=*guestRealm.impl_;
    auto content=questContent();h.gameplay.useContent(content);g.gameplay.useContent(content);
    h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=directory;
    h.self=questPlayer(1);g.self=questPlayer(2);h.saved={{{1,11},h.self},{{2,22},g.self}};
    assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;
    LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);
    auto npc=rewardNpc(0xf13000000000000aULL);npc.hostile=false;npc.questGiver=true;
    h.gameplay.setRemoteNpcs({npc});g.gameplay.setRemoteNpcs({npc});h.refreshPlayers();g.players=h.players;
    auto& member=h.saved[1].player;const auto savedDirectory=h.directory;
    auto receive=[&]{for(const auto& packet:drain(g.socket))deliver(g,packet);};h.history(h.peers[0]);receive();
    auto pump=[&]{guestRealm.update(.21f);h.receive();h.history(h.peers[0]);receive();assert(g.pendingCommands.empty());};
    assert(guestRealm.turnInQuest(1,npc.guid));pump();assert(member.quests.size()==1 && g.self.quests.size()==1);
    h.directory+="/missing/reward";assert(guestRealm.turnInQuest(1,npc.guid,6));pump();
    assert(member.money==100 && member.xp==0 && countItem(member,117)==1 && member.completedQuestIds.empty());
    assert(g.self.money==100 && g.self.xp==0 && countItem(g.self,117)==1 && g.self.completedQuestIds.empty());
    h.directory=savedDirectory;assert(guestRealm.turnInQuest(1,npc.guid,6));pump();
    assert(member.money==110 && member.xp==17 && countItem(member,127)==1 && countItem(member,122)==0);
    assert(g.self.money==110 && g.self.xp==17 && countItem(g.self,127)==1 && g.self.completedQuestIds==std::vector<uint32_t>{1});
    // Altered choice on a retransmitted command ID cannot exchange the already committed reward.
    Writer w;w.u32(h.peers[0].lastCommand);w.u8(uint8_t(LocalAction::TurnInQuest));w.u64(npc.guid);w.u32(1);w.u32(1);w.u32(0);w.u32(0);w.u64(0);
    g.send(Message::Command,g.session,w,g.host);h.receive();receive();assert(countItem(member,127)==1 && countItem(member,122)==0 && member.money==110);
    h.directory+="/missing/host";assert(!hostRealm.turnInQuest(1,npc.guid,3));assert(h.self.xp==0 && h.self.money==100 && countItem(h.self,117)==1);
    h.directory=savedDirectory;assert(hostRealm.turnInQuest(1,npc.guid,3));assert(countItem(h.self,124)==1);
    LocalRealm::Impl loaded;loaded.gameplay.useContent(content);assert(loaded.parseSave(h.directory+"/realm.wprs"));
    for(uint64_t id:{1,2}){const auto* p=loaded.findSaved(id);assert(p && p->player.xp==17 && p->player.money==110 && p->player.quests.empty() && p->player.completedQuestIds==std::vector<uint32_t>{1});}
    assert(countItem(loaded.findSaved(1)->player,124)==1 && countItem(loaded.findSaved(2)->player,127)==1);
    hostRealm.stop();guestRealm.stop();std::filesystem::remove_all(directory);
    std::cout<<"PASS quest reward LAN/save: explicit choices over real UDP, host/guest failed-write rollback, atomic items/money/XP/history, changed-choice replay and save12 reload\n";
}
static void catalog(const std::string& directory) {
    LocalWorldCatalog catalog;std::string result;assert(catalog.load(directory,result));
    std::ifstream ids(directory+"/quest-test-ids.txt");assert(ids);uint32_t id=0;size_t count=0,choices=0,multiple=0;
    while(ids>>id){LocalQuestDefinition q;assert(catalog.quest(id,q,result));assert(validLocalQuestRewards(q));++count;
        choices+=!q.rewardChoices.empty();multiple+=!q.additionalRewards.empty();
        for(size_t i=0;i<localQuestRewardCount(q);++i){LocalItemDefinition item;assert(catalog.item(localQuestRewardAt(q,i).itemId,item,result));}
        for(const auto& r:q.rewardChoices){LocalItemDefinition item;assert(catalog.item(r.itemId,item,result));}
    }
    assert(count==919 && choices==139 && multiple==18);
    std::cout<<"PASS quest reward catalog: all 919 parsed; 139 choice quests and 18 multi-fixed quests; every reward item resolves\n";
}
int main(int argc,char**argv){assert(argc==2);authority();questLan();catalog(argv[1]);}
