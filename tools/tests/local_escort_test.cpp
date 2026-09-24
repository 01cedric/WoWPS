#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace wowee::game;
int main(int argc,char** argv) {
    assert(argc==2);std::string error;LocalGameplay game;assert(game.loadContent(argv[1],error));
    LocalRealmPlayer p;p.guid=1;p.name="Escort";game.initializePlayer(p,true);game.tick(0,{&p});
    const auto guid=game.npcs()[0].guid;LocalRealmCommand accept{LocalAction::AcceptQuest,guid,1};
    auto guest=p;guest.guid=2;guest.name="Guest";
    assert(game.execute(p,accept,{&p,&guest},error));const auto started=p;
    assert(!game.execute(guest,accept,{&p,&guest},error) && guest.quests.empty());
    assert(p.escort.routeId==900 && localScriptState(p,610)==1);
    game.tick(.25f,{&p});assert(p.escort.x==1 && game.npcs()[0].x==1 && p.escort.nextPoint==0);
    p.x=-6;game.tick(.25f,{&p});assert(p.escort.x==1 && p.escort.remainingMs==9500);
    p.x=0;game.tick(.25f,{&p});assert(p.escort.x==2 && p.escort.waitMs==500 && localScriptState(p,611)==1);
    game.tick(.25f,{&p});assert(p.escort.waitMs==250 && p.escort.x==2);
    Writer save;writeProgress(save,p);auto restored=started;Reader r(save.bytes.data(),save.bytes.size());
    assert(readProgress(r,restored)&&r.done() && restored.escort==p.escort);
    LocalGameplay resumed;assert(resumed.loadContent(argv[1],error));resumed.initializePlayer(restored,false);
    resumed.tick(0,{&restored});assert(resumed.npcs()[0].x==2 && localScriptState(restored,611)==1);
    resumed.tick(.25f,{&restored});assert(!restored.escort.waitMs && restored.escort.x==2);
    resumed.tick(.25f,{&restored});resumed.tick(.25f,{&restored});
    assert(restored.escort.nextPoint==2 && localScriptState(restored,611)==2);
    resumed.tick(0,{&restored});assert(!restored.escort.routeId && restored.quests[0].status==LocalQuestStatus::Complete && localScriptState(restored,613)==1);
    resumed.tick(.25f,{&restored});assert(localScriptState(restored,613)==1);
    std::cout<<"PASS shared guide reservation, movement/wait/range pause, exact save/resume, waypoint once and completion\n";

    for(int scenario=0;scenario<9;++scenario) {
        LocalGameplay test;assert(test.loadContent(argv[1],error));auto q=started;test.tick(0,{&q});
        if(scenario==0){q.dead=true;q.health=0;}
        if(scenario==1)q.x=30;
        if(scenario==2)q.mapId=1;
        if(scenario==3)q.instanceId=1;
        if(scenario==4)q.phaseMask=2;
        if(scenario==5)q.escort.remainingMs=1;
        if(scenario==6)assert(test.execute(q,{LocalAction::AbandonQuest,0,1},{&q},error));
        if(scenario==7){auto actors=test.npcs();actors[0].dead=true;actors[0].health=0;actors[0].respawnTimer=10;test.setRemoteNpcs(actors);}
        if(scenario==8)q.flight.active=true;
        test.tick(.01f,{&q});assert(!q.escort.routeId && localScriptState(q,612)==1 && !localScriptState(q,600));
        assert(!test.npcs()[0].escortOwner && test.npcs()[0].x==0);
    }
    // Session absence releases presentation; the saved route can resume later.
    game.tick(0,{});assert(!game.npcs()[0].escortOwner && game.npcs()[0].x==0);
    game.tick(0,{&p});assert(game.npcs()[0].escortOwner==p.guid && game.npcs()[0].x==p.escort.x);
    auto conflict=p;conflict.guid=3;game.tick(0,{&conflict,&p});assert(!conflict.escort.routeId && p.escort.routeId);
    std::cout<<"PASS death/distance/map/instance/phase/timeout/abandon cleanup, session resume and deterministic ownership conflict\n";

    // A refused waypoint/completion must not consume that edge or award credit.
    LocalGameplay atomic;assert(atomic.loadContent(argv[1],error));auto q=started;localSetScriptState(q,611,INT32_MAX);
    atomic.tick(.25f,{&q});atomic.tick(.25f,{&q});assert(q.escort.nextPoint==0 && q.escort.x==1);
    localSetScriptState(q,611,0);atomic.tick(.25f,{&q});assert(q.escort.nextPoint==1 && localScriptState(q,611)==1);
    q.escort.nextPoint=2;q.escort.waitMs=0;localSetScriptState(q,613,INT32_MAX);atomic.tick(0,{&q});
    assert(q.escort.routeId && !localScriptState(q,600) && q.quests[0].progress[0]==0);
    localSetScriptState(q,613,0);atomic.tick(0,{&q});assert(!q.escort.routeId && localScriptState(q,600)==1);
    std::cout<<"PASS atomic waypoint and quest-completion failure/retry without duplicate credit\n";

    Writer old;writeProgress(old,p,39);auto migrated=p;Reader oldRead(old.bytes.data(),old.bytes.size());
    assert(readProgress(oldRead,migrated,39) && oldRead.done() && !migrated.escort.routeId);
    for(int scenario=0;scenario<4;++scenario){auto bad=p;
        if(scenario==0)bad.escort.nextPoint=65;
        if(scenario==1)bad.escort.remainingMs=0;
        if(scenario==2)bad.escort.x=std::numeric_limits<float>::quiet_NaN();
        if(scenario==3)bad.escort.routeId=0;
        Writer w;writeProgress(w,bad);Reader read(w.bytes.data(),w.bytes.size());auto decoded=p;assert(!readProgress(read,decoded));
    }
    char directory[]="/tmp/wowps-escort-XXXXXX";assert(mkdtemp(directory));
    LocalRealm realm;auto& host=*realm.impl_;host.state=LocalRealmState::Hosting;host.directory=directory;host.realmId=1;
    host.gameplay.useContent(game.sharedContent());host.self=p;host.saved={{{1,1},p}};assert(host.saveRealm());
    LocalRealm::Impl loaded;loaded.gameplay.useContent(game.sharedContent());assert(loaded.parseSave(std::string(directory)+"/realm.wprs"));
    assert(loaded.findSaved(1)->player.escort==p.escort);
    host.directory=std::string(directory)+"/file";std::ofstream(host.directory)<<"x";
    assert(!host.runCommand(host.self,{LocalAction::AbandonQuest,0,1},error));
    assert(host.self.escort==p.escort && host.self.quests.size()==1 && !localScriptState(host.self,612));
    std::cout<<"PASS Save39 migration, malformed Save40 rejection, full realm roundtrip and abandon-save rollback\n";
    // Actual owner progress/history and moving actor datagrams, without bypassing
    // the client's snapshot assembly or history prerequisite.
    host.directory=directory;assert(host.openSocket(0));LocalRealm clientRealm;auto& client=*clientRealm.impl_;
    client.state=LocalRealmState::Connected;client.realmId=host.realmId;client.self=started;client.gameplay.useContent(game.sharedContent());assert(client.openSocket(0));
    initAddress(client.host);client.host.sin_addr.s_addr=htonl(INADDR_LOOPBACK);client.host.sin_port=htons(host.port);client.session=321;
    LocalRealm::Impl::Peer peer;peer.guid=p.guid;peer.identity={1,1};peer.session=client.session;peer.loading=false;
    initAddress(peer.address);peer.address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);peer.address.sin_port=htons(client.port);host.peers.push_back(peer);
    host.gameplay.tick(0,{&host.self});host.saved[0].player=host.self;
    host.progress(host.peers[0]);host.history(host.peers[0]);host.world(host.peers[0],1);client.receive();
    assert(client.self.escort==host.self.escort && client.self.escort.nextPoint==1);
    assert(client.gameplay.npcs().size()==1 && client.gameplay.npcs()[0].x==host.self.escort.x);
    std::cout<<"PASS real UDP escort owner-progress/history and guide position replication\n";


    const auto original=nlohmann::json::parse(std::ifstream(argv[1]));const auto path=std::string(directory)+"/world.json";
    for(int scenario=0;scenario<8;++scenario){auto json=original;
        if(scenario==0)json["escortRoutes"][0]["points"]=nlohmann::json::array();
        if(scenario==1)json["escortRoutes"][0]["spawnId"]=99;
        if(scenario==2)json["escortRoutes"][0]["questId"]=99;
        if(scenario==3)json["escortRoutes"][0]["speed"]=8;
        if(scenario==4)json["escortRoutes"][0]["failRadius"]=1;
        if(scenario==5)json["escortRoutes"].push_back(json["escortRoutes"][0]);
        if(scenario==6)json["scriptTriggers"][1]["sourceId"]=99;
        if(scenario==7)json["npcs"][0]["hostile"]=true;
        std::ofstream(path)<<json.dump();LocalGameplay bad;assert(!bad.loadContent(path,error));
    }
    std::filesystem::remove_all(directory);
    std::cout<<"PASS bounded route content and actor/quest/event references; Save"<<int(SaveVersion)<<"/LAN"<<int(Version)<<"\n";
}
