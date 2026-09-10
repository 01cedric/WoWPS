#include "game/local_bots.hpp"
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
int main() {
    char temp[]="/tmp/wowps-warning-lan-0171-XXXXXX";auto* directory=mkdtemp(temp);assert(directory);
    LocalRealm hostRealm,guestRealm;auto& h=*hostRealm.impl_;auto& g=*guestRealm.impl_;
    auto content=rewardContent();content->spells.clear();
    for(uint32_t id=1;id<=50;++id) {
        LocalSpellDefinition spell;spell.id=id;spell.name="Rank fixture";spell.clientSpell=true;
        spell.allowableClasses=1;spell.heal=1;spell.mana=10;spell.cooldownMs=10000;spell.baseLevel=id==3?3:id==2?2:1;
        content->spells.push_back(spell);
    }
    content->spells[0].supercededBySpell=2;content->spells[1].supercededBySpell=3;
    auto& q=content->quests[0];q.turnInEntry=50;q.money=10;q.rewardItem=117;q.rewardCount=2;
    q.objectives={{LocalQuestObjective::Type::Collect,117,1}};
    h.gameplay.useContent(content);g.gameplay.useContent(content);
    h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=directory;
    h.self=rewardPlayer(1);h.self.money=1000000;h.self.level=80;
    h.self.knownSpells={1};for(uint32_t id=4;id<=50;++id)h.self.knownSpells.push_back(id);
    h.self.cooldowns={{1,7000}};h.self.inventory={{117,1}};h.self.quests={{1,LocalQuestStatus::Complete,{1}}};
    g.self=h.self;g.self.guid=2;g.self.name="Guest";
    h.saved={{{1,11},h.self},{{2,22},g.self}};
    assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;
    LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);
    auto npc=rewardNpc(0xf13000000000000aULL);npc.hostile=false;npc.classTrainer=true;npc.trainerClass=1;npc.questGiver=true;npc.innkeeper=true;npc.x=2;
    auto wrong=npc;wrong.guid++;wrong.trainerClass=2;wrong.x=1;
    h.gameplay.setRemoteNpcs({npc,wrong});g.gameplay.setRemoteNpcs({npc,wrong});
    h.refreshPlayers();g.players=h.players;auto& member=h.saved[1].player;const auto savedDirectory=h.directory;
    auto receive=[&]{for(const auto& packet:drain(g.socket))deliver(g,packet);};
    h.history(h.peers[0]);receive();
    auto command=[&](LocalRealmCommand cmd){assert(guestRealm.command(cmd));guestRealm.update(.21f);h.receive();h.history(h.peers[0]);receive();assert(g.pendingCommands.empty());};
    auto replay=[&](LocalRealmCommand cmd){
        Writer w;w.u32(h.peers[0].lastCommand);w.u8(uint8_t(cmd.action));w.u64(cmd.target);w.u32(cmd.id);
        w.u32(0);w.u32(0);w.u32(0);w.u64(cmd.serviceNpcGuid);
        g.send(Message::Command,g.session,w,g.host);h.receive();receive();
    };
    LocalRealmCommand learn{LocalAction::LearnSpell,0,3};learn.serviceNpcGuid=npc.guid;
    const auto money=member.money;
    auto wrongLearn=learn;wrongLearn.serviceNpcGuid=wrong.guid;command(wrongLearn);assert(member.money==money && member.knownSpells.front()==1);
    h.directory+="/missing/train";command(learn);
    assert(member.money==money && member.knownSpells.front()==1 && member.cooldowns[0].spellId==1 && member.cooldowns[0].remainingMs==7000);
    assert(g.self.knownSpells==member.knownSpells && g.self.money==money);
    h.directory=savedDirectory;command(learn);const auto trainedMoney=member.money;
    assert(trainedMoney<money && member.knownSpells.size()==48 && member.knownSpells.back()==3 && member.cooldowns[0].spellId==3 && member.cooldowns[0].remainingMs==7000);
    assert(g.self.knownSpells==member.knownSpells && g.self.money==member.money && g.self.cooldowns[0].spellId==3);
    replay(learn);assert(member.money==trainedMoney && member.cooldowns[0].remainingMs==7000);
    h.directory+="/missing/host-train";assert(!hostRealm.learnSpell(3,npc.guid));assert(h.self.money==money && h.self.knownSpells.front()==1);
    h.directory=savedDirectory;assert(hostRealm.learnSpell(3,npc.guid));assert(h.self.knownSpells.back()==3);
    std::cout<<"PASS warning LAN training: selected NPC, full spellbook skipped-rank upgrade, cooldown inheritance, host/guest save rollback and replay\n";
    LocalRealmCommand turnIn{LocalAction::TurnInQuest,npc.guid,1};
    member.money=999999995;g.self.money=member.money;command(turnIn);
    assert(member.money==999999995 && member.inventory[0].count==1 && member.quests.size()==1);
    member.money=999999990;g.self.money=member.money;
    h.directory+="/missing/quest";command(turnIn);
    assert(member.money==999999990 && member.inventory[0].count==1 && member.quests.size()==1 && member.completedQuestIds.empty());
    assert(g.self.money==member.money && g.self.inventory[0].count==1 && g.self.quests.size()==1);
    h.directory=savedDirectory;command(turnIn);
    assert(member.money==1000000000 && member.inventory[0].count==2 && member.quests.empty() && member.completedQuestIds==std::vector<uint32_t>{1});
    assert(g.self.money==member.money && g.self.inventory[0].count==2 && g.self.quests.empty() && g.self.completedQuestIds==member.completedQuestIds);
    replay(turnIn);assert(member.money==1000000000 && member.inventory[0].count==2);
    h.directory+="/missing/host-quest";const auto hostMoney=h.self.money;
    assert(!hostRealm.command(turnIn));assert(h.self.money==hostMoney && h.self.inventory[0].count==1 && h.self.completedQuestIds.empty());
    h.directory=savedDirectory;assert(hostRealm.command(turnIn));assert(h.self.money==hostMoney+10 && h.self.inventory[0].count==2);
    std::cout<<"PASS warning LAN quests: full reward cap refusal, host/guest save rollback, successful commit and duplicate-command protection\n";
    LocalRealmCommand bind{LocalAction::SetHome};bind.serviceNpcGuid=npc.guid;command(bind);
    assert(member.homeX==2 && g.self.homeX==2);assert(hostRealm.setHome(npc.guid) && h.self.homeX==2);
    LocalRealm::Impl loaded;loaded.gameplay.useContent(content);assert(loaded.parseSave(h.directory+"/realm.wprs"));
    for(auto id:{1,2}) {
        const auto* saved=loaded.findSaved(id);assert(saved);
        assert(saved->player.knownSpells.size()==48 && saved->player.knownSpells.back()==3 && saved->player.cooldowns[0].spellId==3);
        assert(saved->player.completedQuestIds==std::vector<uint32_t>{1} && saved->player.inventory[0].count==2 && saved->player.homeX==2);
    }
    assert(loaded.findSaved(2)->player.money==1000000000);
    hostRealm.stop();guestRealm.stop();std::filesystem::remove_all(directory);
    std::cout<<"PASS warning LAN persistence: selected inn roundtrip and save12 reload of trained rank, inherited cooldown, quest history, items and complete money\n";
}
