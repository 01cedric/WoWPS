#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
using namespace wowee::game;

static LocalRealmPlayer player(LocalGameplay& game,uint64_t guid) {
    LocalRealmPlayer p;p.guid=guid;p.name="Test"+std::to_string(guid);
    game.initializePlayer(p,true);return p;
}
static const LocalRealmNpc& vehicle(const LocalGameplay& game) {
    for(const auto& n:game.npcs())if(n.vehicleId==700)return n;
    assert(false);std::abort();
}
int main(int argc,char** argv) {
    assert(argc==2);
    LocalGameplay game;std::string result;
    assert(game.loadContent(argv[1],result)); // Vehicle 700 is not NPC entry 50.
    // Invalid authored references must fail in the production loader.
    const auto source=nlohmann::json::parse(std::ifstream(argv[1]));
    char directory[]="/tmp/wowps-vehicle-content-XXXXXX";assert(mkdtemp(directory));
    const auto invalidPath=std::string(directory)+"/world.json";
    for(int scenario=0;scenario<4;++scenario) {
        auto content=source;
        if(scenario==0)content["scriptTriggers"][1]["sourceId"]=701;
        if(scenario==1)content["spawns"][0]["vehicleControllerSeat"]=2;
        if(scenario==2)content["quests"][0]["objectives"][0]["entry"]=999;
        if(scenario==3)content["scriptTriggers"][1]["scheduleTimerId"]=999;
        std::ofstream(invalidPath)<<content.dump();LocalGameplay rejected;
        assert(!rejected.loadContent(invalidPath,result));
    }
    std::filesystem::remove_all(directory);
    std::cout<<"PASS content validation: vehicle IDs, seat bounds, quest script references, timer references\n";
    auto driver=player(game,1),passenger=player(game,2),third=player(game,3);
    std::vector<LocalRealmPlayer*> roster{&driver,&passenger,&third};
    game.tick(0,roster);const auto guid=vehicle(game).guid;
    assert(game.execute(driver,{LocalAction::AcceptQuest,guid,1},roster,result));
    assert(game.execute(driver,{LocalAction::EnterVehicle,guid,0},roster,result));
    assert(driver.vehicleControl && driver.vehicleGuid==guid && localScriptState(driver,501)==1);
    assert(driver.quests[0].progress[0]==1 && driver.quests[0].status==LocalQuestStatus::Active);
    assert(!game.execute(passenger,{LocalAction::EnterVehicle,guid,0},roster,result));
    assert(!passenger.vehicleGuid && passenger.scriptStates.empty());
    assert(game.execute(passenger,{LocalAction::EnterVehicle,guid,1},roster,result));
    assert(!passenger.vehicleControl && passenger.vehicleSeat==1);
    assert(!game.execute(third,{LocalAction::EnterVehicle,guid,2},roster,result));
    third.x=99;assert(!game.execute(third,{LocalAction::EnterVehicle,guid,0},roster,result));third.x=0;
    assert(!game.execute(driver,{LocalAction::ReturnHome},roster,result));
    assert(!game.execute(driver,{LocalAction::CastSpell,guid,1},roster,result));
    assert(!game.moveVehicle(passenger,0,1,0,0,0,0));
    assert(!game.moveVehicle(driver,1,1,0,0,0,0));
    assert(!game.moveVehicle(driver,0,std::numeric_limits<float>::quiet_NaN(),0,0,0,0));
    assert(game.moveVehicle(driver,0,.75f,0,0,.5f,0));
    assert(!game.moveVehicle(driver,0,1.5f,0,0,.5f,0)); // No free allowance per packet.
    game.tick(.15f,roster);
    assert(passenger.x==driver.x && passenger.orientation==driver.orientation);
    assert(driver.quests[0].status==LocalQuestStatus::Complete && driver.quests[0].progress[0]==2);
    assert(localScriptState(driver,502)==1);
    for(int i=0;i<160;++i) {
        game.tick(.25f,roster);
        assert(game.moveVehicle(driver,0,driver.x+1.5f,0,0,.5f,0));
    }
    game.tick(.01f,roster);
    assert(vehicle(game).guid==guid && driver.x>200 && passenger.x==driver.x);
    assert(localScriptState(driver,502)==1); // Completion is not replayed on refresh.
    std::cout<<"PASS seats, movement authority/rate limit, passenger follow, streaming retention, timer-to-quest credit\n";

    // Save migration and restart deliberately restore exit scripts, not a seat.
    Writer saved;writeProgress(saved,driver);
    auto restored=driver;restored.vehicleGuid=0;restored.vehicleId=0;restored.vehicleSeat=0;restored.vehicleControl=false;
    Reader sr(saved.bytes.data(),saved.bytes.size());assert(readProgress(sr,restored) && sr.done());
    assert(restored.vehicleRecoveryId==700 && !restored.vehicleGuid);
    game.initializePlayer(restored,false);
    assert(!restored.vehicleRecoveryId && !restored.vehicleGuid && !localScriptState(restored,501));
    assert(restored.quests[0].status==LocalQuestStatus::Complete && restored.quests[0].progress[0]==2);
    Writer legacy;writeProgress(legacy,driver,37);
    restored.vehicleRecoveryId=999;Reader lr(legacy.bytes.data(),legacy.bytes.size());
    assert(readProgress(lr,restored,37) && lr.done() && !restored.vehicleRecoveryId);
    std::cout<<"PASS Save38 recovery, Save37 migration, retained quest credit\n";

    // Wire round trips include both owner/public occupancy and NPC seat metadata.
    Writer owner;writeNetworkVitals(owner,driver);LocalRealmPlayer decoded;
    Reader nr(owner.bytes.data(),owner.bytes.size());readNetworkVitals(nr,decoded);
    assert(nr.done() && decoded.vehicleGuid==guid && decoded.vehicleControl);
    auto npc=vehicle(game);npc.playerThreat.viewerGuid=driver.guid;
    Writer nw;writeNpc(nw,npc);Reader nn(nw.bytes.data(),nw.bytes.size());auto n=readNpc(nn,game.content());
    assert(nn.done() && n.vehicleId==700 && n.vehicleSeatCount==2 && n.vehicleControllerSeat==0);
    auto invalid=driver;invalid.vehicleSeat=8;Writer malformed;writeVehicle(malformed,invalid);
    Reader bad(malformed.bytes.data(),malformed.bytes.size());assert(!readVehicle(bad,decoded));
    auto truncated=owner.bytes;truncated.pop_back();Reader shortRead(truncated.data(),truncated.size());
    readNetworkVitals(shortRead,decoded);assert(!shortRead.valid);

    LocalRealm guest;auto& client=*guest.impl_;client.self=passenger;client.gameplay.useContent(game.sharedContent());
    client.self.x=-5;auto update=passenger;update.x=111;
    client.applyProgress(update,10);assert(client.self.x==111 && client.self.vehicleSeat==1);
    auto exited=update;exited.vehicleGuid=0;exited.vehicleId=0;exited.vehicleSeat=0;exited.vehicleControl=false;
    ++exited.positionRevision;client.commitPlayers({exited},20);assert(!client.self.vehicleGuid);
    client.applyProgress(update,21);assert(!client.self.vehicleGuid && client.self.positionRevision==exited.positionRevision);
    std::cout<<"PASS LAN vehicle/NPC codecs, malformed/truncated rejection, passenger positions and stale-progress ordering\n";

    // Explicit exit is all-or-nothing; forced safety release cannot be trapped.
    auto shared=game.sharedContent();LocalScriptTrigger overflow;
    overflow.kind=LocalScriptTriggerKind::VehicleExit;overflow.sourceId=700;
    overflow.scriptId=503;overflow.valueOp=LocalScriptValueOp::Add;overflow.value=1;
    shared->scriptTriggers.push_back(overflow);assert(localSetScriptState(driver,503,INT32_MAX));
    const auto before=driver.scriptStates;
    assert(!game.execute(driver,{LocalAction::ExitVehicle},roster,result));
    assert(driver.vehicleGuid==guid && driver.scriptStates==before);
    assert(game.detachVehicle(driver));assert(!driver.vehicleGuid && driver.vehicleRecoveryId==700);
    shared->scriptTriggers.pop_back();game.initializePlayer(driver,false);
    assert(!driver.vehicleRecoveryId && !localScriptState(driver,501));
    assert(game.execute(passenger,{LocalAction::ExitVehicle},roster,result));
    driver.x=passenger.x=vehicle(game).x;
    assert(game.execute(driver,{LocalAction::EnterVehicle,guid,0},roster,result));
    driver.phaseMask=2;game.tick(.01f,roster);assert(!driver.vehicleGuid);
    passenger.x=vehicle(game).x;
    assert(game.execute(passenger,{LocalAction::EnterVehicle,guid,1},roster,result));
    passenger.health=0;passenger.dead=true;game.tick(.01f,roster);assert(!passenger.vehicleGuid);

    driver.phaseMask=1;driver.x=vehicle(game).x;
    assert(game.execute(driver,{LocalAction::EnterVehicle,guid,0},roster,result));
    assert(!driver.scriptTimers.empty());
    assert(game.execute(driver,{LocalAction::AbandonQuest,0,1},roster,result));
    assert(driver.quests.empty() && driver.scriptTimers.empty() && !localScriptState(driver,500));
    assert(game.execute(driver,{LocalAction::ExitVehicle},roster,result));
    std::cout<<"PASS exit rollback/recovery, phase/death detach, quest-abandon timer cleanup\n";

    // Session removal must release the host-side seat and its script state.
    LocalRealm host;auto& authority=*host.impl_;authority.gameplay.useContent(game.sharedContent());
    authority.saved={{{1,1},driver}};auto& stored=authority.saved[0].player;
    stored.vehicleGuid=guid;stored.vehicleId=700;stored.vehicleControl=true;localSetScriptState(stored,501,1);
    authority.resetSavedSession(stored.guid);assert(!stored.vehicleGuid && !localScriptState(stored,501));
    std::cout<<"PASS disconnect cleanup; Save"<<int(SaveVersion)<<"/LAN"<<int(Version)<<"\n";

    // Exercise real host position handlers: permission, rate rejection and
    // stale-revision protection use the same packets as a LAN console.
    authority.gameplay.setRemoteNpcs({vehicle(game)});
    stored.vehicleGuid=guid;stored.vehicleId=700;stored.vehicleControl=true;stored.vehicleMoveAllowance=3.5f;
    LocalRealm::Impl::Peer peer;peer.guid=stored.guid;peer.session=123;
    initAddress(peer.address);peer.address.sin_port=htons(34567);peer.address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    authority.peers.push_back(peer);
    const auto report=[&](const LocalRealmPlayer& position,uint32_t sequence) {
        Writer packet;writePosition(packet,position);packet.u32(position.positionRevision);packet.u32(position.instanceId);
        packet.u8(0);packet.u8(0);packet.u32(0);packet.f32(0);packet.f32(0);packet.f32(0);
        Reader input(packet.bytes.data(),packet.bytes.size());
        authority.handleHost(Message::Position,input,peer.address,peer.session,sequence);
    };
    auto position=stored;position.x+=1;report(position,1);assert(stored.x==position.x);
    position.x+=1000;const auto validX=stored.x;const auto revision=stored.positionRevision;
    report(position,2);assert(stored.x==validX && stored.positionRevision==revision+1);
    position.x=validX+1;report(position,3);assert(stored.x==validX); // Old revision cannot bypass correction.
    position=stored;stored.vehicleSeat=1;stored.vehicleControl=false;position.x+=1;
    report(position,4);assert(stored.x==validX);
    assert(authority.gameplay.npcs()[0].x==validX);
    std::cout<<"PASS production LAN position handler: driver movement, correction, replay and passenger rejection\n";
}
