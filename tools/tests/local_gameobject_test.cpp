#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace wowee::game;
namespace net=wowee::net;
static sockaddr_in loopback(uint16_t port){sockaddr_in a{};initAddress(a);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);return a;}
static std::vector<std::vector<uint8_t>> drain(socket_t socket){
    std::vector<std::vector<uint8_t>> packets;
    for(;;){std::array<uint8_t,MaxPacket+1> buffer{};const auto n=::recvfrom(socket,reinterpret_cast<char*>(buffer.data()),buffer.size(),net::datagramFlags(),nullptr,nullptr);
        if(n<0){assert(net::isWouldBlock(net::lastError()));break;}assert(size_t(n)<=MaxPacket);packets.emplace_back(buffer.begin(),buffer.begin()+n);}
    return packets;
}
static void deliver(LocalRealm::Impl& guest,const std::vector<uint8_t>& bytes){
    Reader r(bytes.data(),bytes.size());assert(r.u32()==WireMagic && r.u8()==Version);const auto type=Message(r.u8());assert(r.u16()==bytes.size());
    const auto seq=r.u32();const auto token=r.u64();guest.handleClient(type,r,guest.host,token,seq);
}
int main(int argc,char** argv) {
    assert(argc==2);std::string error;LocalGameplay game;assert(game.loadContent(argv[1],error));
    LocalRealmPlayer p;p.guid=2;p.name="Objectuser";game.initializePlayer(p,true);game.tick(0,{&p});
    const LocalRealmCommand use{LocalAction::UseGameObject,localGameObjectGuid(800),800};
    assert(!game.execute(p,use,{&p},error)); // Quest gate before accepting.
    assert(game.execute(p,{LocalAction::AcceptQuest,game.npcs()[0].guid,1},{&p},error));
    const auto base=p;const auto* object=game.content().gameObject(800);assert(object);
    assert(game.content().nearbyGameObject(p)==object && localGameObjectVisible(*object,p));
    for(int scenario=0;scenario<14;++scenario) {
        auto q=base;auto cmd=use;
        if(scenario==0)q.x=-1.01f;
        if(scenario==1)q.z=5;
        if(scenario==2)q.mapId=1;
        if(scenario==3)q.instanceId=1;
        if(scenario==4)q.phaseMask=2;
        if(scenario==5){q.dead=true;q.health=0;}
        if(scenario==6)q.flight.active=true;
        if(scenario==7)q.transportEntry=1;
        if(scenario==8)q.castingSpellId=1;
        if(scenario==9)q.attackTarget=1;
        if(scenario==10)q.vehicleGuid=1;
        if(scenario==11)localSetScriptState(q,610,1);
        if(scenario==12)cmd.target^=1ULL<<32;
        if(scenario==13)cmd.id=999;
        const auto states=q.scriptStates;
        assert(!game.execute(q,cmd,{&q},error));assert(q.scriptStates==states && q.quests[0].progress[0]==0 && q.scriptTimers.empty());
    }
    p.x=-1;assert(game.execute(p,use,{&p},error)); // Inclusive 5-yard boundary.
    assert(p.quests[0].status==LocalQuestStatus::Complete && p.quests[0].progress[0]==1);
    assert(localScriptState(p,610)==1 && localScriptState(p,611)==1 && p.scriptTimers.size()==1);
    assert(!game.execute(p,use,{&p},error) && !game.content().nearbyGameObject(p));
    Writer saved;writeProgress(saved,p);auto restored=base;Reader sr(saved.bytes.data(),saved.bytes.size());
    assert(readProgress(sr,restored) && sr.done());game.initializePlayer(restored,false);
    assert(!game.execute(restored,use,{&restored},error));game.tick(.1f,{&restored});assert(localScriptState(restored,612)==1);
    assert(game.execute(restored,{LocalAction::AbandonQuest,0,1},{&restored},error));
    restored.x=0;assert(game.execute(restored,{LocalAction::AcceptQuest,game.npcs()[0].guid,1},{&restored},error));
    assert(game.execute(restored,use,{&restored},error));
    std::cout<<"PASS object authority guards, radius boundary, quest credit, saved use gate/timer and abandon/reaccept\n";

    // Later object rows and quest-complete actions must roll back the whole use.
    auto content=game.sharedContent();LocalScriptTrigger fail;fail.kind=LocalScriptTriggerKind::ObjectUse;
    fail.sourceId=800;fail.scriptId=620;fail.valueOp=LocalScriptValueOp::Add;fail.value=1;content->scriptTriggers.push_back(fail);
    p=base;localSetScriptState(p,620,INT32_MAX);const auto before=p.scriptStates;
    assert(!game.execute(p,use,{&p},error) && p.scriptStates==before && p.scriptTimers.empty() && p.quests[0].status==LocalQuestStatus::Active);
    content->scriptTriggers.pop_back();p=base;localSetScriptState(p,611,INT32_MAX);const auto overflow=p.scriptStates;
    assert(!game.execute(p,use,{&p},error) && p.scriptStates==overflow && p.scriptTimers.empty() && p.quests[0].progress[0]==0);
    const auto triggers=content->scriptTriggers;
    for(auto& t:content->scriptTriggers)if(t.kind==LocalScriptTriggerKind::ObjectUse){t.requiredScriptId=610;t.requiredValue=9;}
    p=base;assert(!game.execute(p,use,{&p},error) && p.scriptStates==base.scriptStates);content->scriptTriggers=triggers;
    std::cout<<"PASS multi-row and quest-complete atomic rollback; unavailable action refusal\n";

    char temp[]="/tmp/wowps-object-XXXXXX";assert(mkdtemp(temp));
    LocalRealm hostRealm,guestRealm;auto& h=*hostRealm.impl_;auto& g=*guestRealm.impl_;
    h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=temp;
    h.gameplay.useContent(content);g.gameplay.useContent(content);h.self=base;h.self.guid=1;h.self.name="Host";g.self=base;
    h.saved={{{1,11},h.self},{{2,22},base}};
    assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;
    LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);
    h.refreshPlayers();g.players=h.players;
    assert(guestRealm.useGameObject(800));guestRealm.update(.01f);h.receive();
    auto replies=drain(g.socket);assert(!replies.empty());for(const auto& packet:replies)deliver(g,packet);
    // The initial owner snapshot waits for the matching completed-quest history.
    h.history(h.peers[0]);for(const auto& packet:drain(g.socket))deliver(g,packet);
    assert(g.pendingCommands.empty() && localScriptState(g.self,610)==1 && g.self.quests[0].status==LocalQuestStatus::Complete);
    assert(localScriptState(h.saved[1].player,611)==1 && !localScriptState(h.self,611));
    // Reset the gate on the host; an identical command ID must still not run.
    h.saved[1].player=base;
    const auto send=[&](uint32_t id,bool truncated=false,uint64_t token=987){
        Writer w;w.u32(id);w.u8(uint8_t(LocalAction::UseGameObject));w.u64(use.target);w.u32(use.id);
        w.u32(0);w.u32(0);w.u32(0);w.u64(0);if(truncated)w.bytes.pop_back();
        g.send(Message::Command,token,w,g.host);h.receive();drain(g.socket);
    };
    send(1);assert(h.saved[1].player.scriptStates==base.scriptStates);
    send(3);send(2,true);send(2,false,555);assert(h.peers[0].lastCommand==1 && h.saved[1].player.scriptStates==base.scriptStates);
    send(2);assert(h.peers[0].lastCommandSuccess && localScriptState(h.saved[1].player,611)==1);
    LocalRealm::Impl loaded;loaded.gameplay.useContent(content);assert(loaded.parseSave(h.directory+"/realm.wprs"));
    assert(loaded.findSaved(2) && localScriptState(loaded.findSaved(2)->player,610)==1);
    std::cout<<"PASS real UDP command/owner-progress roundtrip, replay/gap/truncation/session rejection and durable realm reload\n";

    // The successful ack depends on the atomic disk write. Use a regular file
    // as directory to force a deterministic I/O failure even when run as root.
    h.saved[1].player=base;h.directory=std::string(temp)+"/not-a-directory";std::ofstream(h.directory)<<"x";
    assert(!h.runCommand(h.saved[1].player,use,error));
    assert(h.saved[1].player.scriptStates==base.scriptStates && h.saved[1].player.scriptTimers.empty() && h.saved[1].player.quests[0].progress[0]==0);
    assert(error.find("not saved")!=std::string::npos);
    h.directory=temp;
    std::cout<<"PASS save failure restores object state, timers and quest progress\n";

    const auto original=nlohmann::json::parse(std::ifstream(argv[1]));const auto path=std::string(temp)+"/world.json";
    for(int scenario=0;scenario<10;++scenario){
        auto json=original;
        if(scenario==0)json["gameObjects"][0]["useRadius"]=6;
        if(scenario==1)json["gameObjects"][0]["displayId"]=0;
        if(scenario==2)json["gameObjects"].push_back(json["gameObjects"][0]);
        if(scenario==3)json["gameObjects"][0]["requiredQuestId"]=999;
        if(scenario==4)json["gameObjects"][0]["requiredScriptId"]=999;
        if(scenario==5)json["gameObjects"][0]["excludedPhaseMask"]=1;
        if(scenario==6)json["scriptTriggers"][2]["sourceId"]=999;
        if(scenario==7){json["scriptTriggers"].erase(3);json["scriptTriggers"].erase(2);}
        if(scenario==8)json["gameObjects"][0]["scale"]=0;
        if(scenario==9)for(unsigned i=801;i<1057;++i){auto object=json["gameObjects"][0];object["id"]=i;json["gameObjects"].push_back(object);}
        std::ofstream(path)<<json.dump();LocalGameplay invalid;assert(!invalid.loadContent(path,error));
    }
    std::filesystem::remove_all(temp);
    std::cout<<"PASS bounded authored content, geometry, phase, quest/state/event references; Save"<<int(SaveVersion)<<"/LAN"<<int(Version)<<"\n";
}
