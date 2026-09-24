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
    auto c=game.sharedContent();c->spawns[0].vehicleSeatCount=3;c->spawns[0].vehicleSeatOffsets[1]={2,0,1};c->spawns[0].vehicleSeatOffsets[2]={0,2,1};
    game.useContent(c);LocalRealmPlayer driver;driver.guid=1;driver.name="Driver";game.initializePlayer(driver,true);
    auto rider=driver;rider.guid=2;rider.name="Rider";game.tick(0,{&driver,&rider});const auto guid=game.npcs()[0].guid;
    assert(game.execute(driver,{LocalAction::EnterVehicle,guid,0},{&driver,&rider},error));
    assert(game.execute(rider,{LocalAction::EnterVehicle,guid,1},{&driver,&rider},error));assert(rider.x==2 && rider.z==1);
    assert(!game.execute(rider,{LocalAction::SwitchVehicleSeat,guid,0},{&driver,&rider},error));
    const auto states=rider.scriptStates;const auto revision=rider.positionRevision;
    assert(game.execute(rider,{LocalAction::SwitchVehicleSeat,guid,2},{&driver,&rider},error));
    assert(rider.vehicleSeat==2 && rider.y==2 && rider.z==1 && rider.positionRevision==revision+1 && rider.scriptStates==states);
    assert(!rider.vehicleMoveAllowance && !game.moveVehicle(rider,0,1,0,0,0,0));
    assert(game.moveVehicle(driver,0,0,0,0,1.57079632679f,0));game.tick(0,{&driver,&rider});
    assert(std::abs(rider.x+2)<.0001f && std::abs(rider.y)<.0001f && rider.z==1);
    assert(game.execute(driver,{LocalAction::ExitVehicle},{&driver,&rider},error));
    assert(game.execute(rider,{LocalAction::SwitchVehicleSeat,guid,0},{&driver,&rider},error));
    assert(rider.vehicleControl && rider.x==0 && rider.z==0 && !rider.vehicleMoveAllowance);
    assert(!game.moveVehicle(rider,0,1,0,0,0,0));game.tick(.25f,{&driver,&rider});assert(game.moveVehicle(rider,0,1,0,0,0,0));
    for(unsigned seat:{0u,3u,256u})assert(!game.execute(rider,{LocalAction::SwitchVehicleSeat,guid,seat},{&driver,&rider},error));
    assert(!game.execute(rider,{LocalAction::SwitchVehicleSeat,guid+1,1},{&driver,&rider},error));
    std::cout<<"PASS occupied/invalid/stale seat guards, rotated offsets, controller handoff and no movement-budget gain\n";
    // Production command wrapper and owner-progress codecs retain the authority revision.
    LocalRealm realm;auto& h=*realm.impl_;h.state=LocalRealmState::Hosting;h.self=rider;
    h.gameplay.useContent(c);h.gameplay.setRemoteNpcs(game.npcs());h.players={driver,rider};
    assert(realm.cycleVehicleSeat(1));assert(h.self.vehicleSeat==1 && !h.self.vehicleControl);
    Writer w;writeNetworkVitals(w,h.self);auto decoded=h.self;decoded.vehicleSeat=0;Reader rd(w.bytes.data(),w.bytes.size());
    readNetworkVitals(rd,decoded);assert(rd.done() && decoded.vehicleSeat==1 && !decoded.vehicleControl && decoded.positionRevision==h.self.positionRevision);
    std::cout<<"PASS realm seat selection and public occupancy/revision codec\n";
    char temp[]="/tmp/wowps-seat-XXXXXX";assert(mkdtemp(temp));const auto path=std::string(temp)+"/world.json";
    const auto original=nlohmann::json::parse(std::ifstream(argv[1]));
    for(int scenario=0;scenario<4;++scenario){auto json=original;
        json["spawns"][0]["vehicleSeatOffsets"]={{{"seat",1},{"x",2},{"z",1}}};
        if(scenario==0)json["spawns"][0]["vehicleSeatOffsets"][0]["seat"]=2;
        if(scenario==1)json["spawns"][0]["vehicleSeatOffsets"][0]["seat"]=0;
        if(scenario==2)json["spawns"][0]["vehicleSeatOffsets"][0]["x"]=21;
        if(scenario==3)json["spawns"][0]["vehicleSeatOffsets"].push_back(json["spawns"][0]["vehicleSeatOffsets"][0]);
        std::ofstream(path)<<json.dump();LocalGameplay rejected;assert(!rejected.loadContent(path,error));
    }
    auto valid=original;valid["spawns"][0]["vehicleSeatOffsets"]={{{"seat",1},{"x",2},{"z",1}}};std::ofstream(path)<<valid.dump();
    LocalGameplay loaded;assert(loaded.loadContent(path,error) && loaded.content().spawns[0].vehicleSeatOffsets[1][2]==1);
    std::filesystem::remove_all(temp);std::cout<<"PASS authored offset bounds, driver-origin constraint and duplicate/absent seat validation\n";
}
