#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace wowee::game;
namespace net=wowee::net;

static sockaddr_in loopback(uint16_t port) {
    sockaddr_in address{};initAddress(address);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(port);return address;
}
static std::vector<std::vector<uint8_t>> drain(socket_t socket) {
    std::vector<std::vector<uint8_t>> packets;
    for(;;) {
        std::array<uint8_t,MaxPacket+1> bytes{};
        const auto size=::recvfrom(socket,reinterpret_cast<char*>(bytes.data()),bytes.size(),net::datagramFlags(),nullptr,nullptr);
        if(size<0){assert(net::isWouldBlock(net::lastError()));break;}
        assert(size_t(size)<=MaxPacket);packets.emplace_back(bytes.begin(),bytes.begin()+size);
    }
    return packets;
}
static Message deliver(LocalRealm::Impl& guest,const std::vector<uint8_t>& bytes) {
    Reader r(bytes.data(),bytes.size());assert(r.u32()==WireMagic&&r.u8()==Version);const auto type=Message(r.u8());
    assert(r.u16()==bytes.size());const auto seq=r.u32();const auto token=r.u64();guest.handleClient(type,r,guest.host,token,seq);return type;
}
static LocalRealmPlayer player(LocalGameplay& game,uint64_t guid,const char* name) {
    LocalRealmPlayer p;p.guid=guid;p.name=name;game.initializePlayer(p,true);return p;
}
static bool scriptedActor(const LocalGameplay& game,uint32_t id) {
    return std::any_of(game.npcs().begin(),game.npcs().end(),[&](const auto& npc){return npc.scriptActorId==id;});
}
static void rewriteChecksum(std::vector<uint8_t>& bytes) {
    bytes.resize(bytes.size()-4);Writer sum;sum.bytes=std::move(bytes);sum.u32(checksum(sum.bytes.data(),sum.bytes.size()));bytes=std::move(sum.bytes);
}

int main(int argc,char** argv) {
    assert(argc==2);std::string error;
    LocalHolidayDefinition pilgrim;pilgrim.id=404;pilgrim.durationHours[0]=168;
    LocalWorldEventSchedule pilgrimEvent;pilgrimEvent.id=2;pilgrimEvent.name="Pilgrim";pilgrimEvent.clock=LocalWorldEventClock::Holiday;
    pilgrimEvent.holidayId=404;pilgrimEvent.holidayStage=1;pilgrimEvent.activePhaseMask=2;
    assert(validLocalWorldEventSchedule(pilgrimEvent));bool resolved=false;
    assert(localHolidayEventActive(pilgrimEvent,{pilgrim},{2026,11,22,12,0,0},resolved)&&resolved);
    assert(!localHolidayEventActive(pilgrimEvent,{pilgrim},{2026,11,29,12,0,0},resolved)&&resolved);
    LocalHolidayDefinition darkmoon;darkmoon.id=374;darkmoon.durationHours[0]=72;darkmoon.durationHours[1]=168;
    LocalWorldEventSchedule darkmoonEvent;darkmoonEvent.id=3;darkmoonEvent.name="Darkmoon";darkmoonEvent.clock=LocalWorldEventClock::Holiday;
    darkmoonEvent.holidayId=374;darkmoonEvent.holidayStage=2;darkmoonEvent.activePhaseMask=4;
    assert(localHolidayEventActive(darkmoonEvent,{darkmoon},{2026,6,8,12,0,0},resolved)&&resolved);
    assert(!localHolidayEventActive(darkmoonEvent,{darkmoon},{2026,6,7,12,0,0},resolved)&&resolved);
    std::cout<<"PASS recurring holiday calendar and Holiday.dbc stage offsets resolve deterministically\n";

    LocalWorldEventSchedule unit;unit.id=1;unit.name="unit";unit.activeDurationMs=100;unit.cooldownMs=100;unit.repeat=true;
    assert(validLocalWorldEventSchedule(unit));auto state=localInitialWorldEventState(unit);bool changed=false;
    unsigned calls=0;
    assert(localAdvanceWorldEvent(state,unit,0,[&](auto,auto&,auto&){++calls;return false;},changed));
    assert(changed&&calls==1&&state.enabled&&!state.active&&!state.remainingMs&&!state.cycle&&state.revision==1);
    assert(localAdvanceWorldEvent(state,unit,0,[&](auto,auto&,auto&){++calls;return true;},changed));
    assert(state.active&&state.remainingMs==100&&state.cycle==1&&state.revision==2);
    assert(localAdvanceWorldEvent(state,unit,250,[&](auto,auto&,auto&){++calls;return true;},changed));
    assert(state.active&&state.remainingMs==50&&state.cycle==2&&state.revision==4);
    state={1,UINT32_MAX,1,UINT32_MAX,true,true};
    assert(localAdvanceWorldEvent(state,unit,101,[&](auto,auto&,auto&){return true;},changed));
    assert(state.active&&state.revision==2&&state.cycle==1&&state.remainingMs==100);
    std::cout<<"PASS boundary retry, sub-tick active/cooldown crossings and nonzero revision/cycle wrap\n";

    LocalGameplay source;assert(source.loadContent(argv[1],error));auto content=source.sharedContent();
    char temp[]="/tmp/wowps-world-event-XXXXXX";assert(mkdtemp(temp));
    LocalRealm authority;auto& h=*authority.impl_;h.gameplay.useContent(content);h.state=LocalRealmState::Hosting;h.realmId=123;h.directory=temp;
    h.self=player(h.gameplay,1,"Host");h.saved={{{1,11},h.self}};h.resetWorldEventsFromContent();
    assert(h.worldEvents.size()==1&&h.worldEvents[0].remainingMs==50);
    assert(h.tickWorldEvents(0)&&h.self.phaseMask==5&&h.self.positionRevision==1);
    assert(h.tickAuthoritySimulation(.05f)&&h.worldEvents[0].active&&h.worldEvents[0].cycle==1&&h.worldEvents[0].revision==2);
    assert(h.self.phaseMask==3&&h.self.positionRevision==2&&scriptedActor(h.gameplay,100));
    assert(h.gameplay.scriptDialogues().size()==1&&h.gameplay.scriptDialogues()[0].text=="The shared event has begun.");
    assert(h.tickAuthoritySimulation(.20f)&&!h.worldEvents[0].active&&h.worldEvents[0].remainingMs==100);
    assert(h.tickAuthoritySimulation(.101f));
    assert(h.worldEvents[0].active&&h.worldEvents[0].cycle==2&&h.worldEvents[0].remainingMs==199);
    assert(h.self.phaseMask==3&&scriptedActor(h.gameplay,100));
    assert(h.tickAuthoritySimulation(.074f)&&h.worldEvents[0].remainingMs==125);
    const auto savedState=h.worldEvents[0];assert(h.saveRealm());

    LocalRealm::Impl reloaded;reloaded.gameplay.useContent(content);assert(reloaded.parseSave(std::string(temp)+"/realm.wprs"));
    assert(reloaded.worldEvents.size()==1&&reloaded.worldEvents[0]==savedState);
    reloaded.state=LocalRealmState::SinglePlayer;reloaded.self=reloaded.saved[0].player;
    assert(reloaded.restoreWorldEventActions()&&scriptedActor(reloaded.gameplay,100));
    assert(reloaded.worldEvents[0].remainingMs==125); // stopped/offline time never advanced it.
    LocalRealm::Impl inactive;inactive.gameplay.useContent(content);inactive.state=LocalRealmState::SinglePlayer;
    inactive.self=player(inactive.gameplay,30,"Inactive");inactive.saved={{{30,30},inactive.self}};
    inactive.worldEvents={savedState};inactive.worldEvents[0].active=false;inactive.worldEvents[0].remainingMs=50;
    assert(inactive.restoreWorldEventActions()&&!scriptedActor(inactive.gameplay,100));
    LocalRealm::Impl offMap;offMap.gameplay.useContent(content);offMap.state=LocalRealmState::SinglePlayer;
    offMap.self=player(offMap.gameplay,32,"OffMap");offMap.self.mapId=1;offMap.saved={{{32,32},offMap.self}};
    offMap.worldEvents={savedState};assert(offMap.restoreWorldEventActions()&&scriptedActor(offMap.gameplay,100));
    assert(offMap.gameplay.scriptDialogues().empty());
    std::cout<<"PASS active restore, inactive cooldown restore and off-map restore without ephemeral dialogue blockage\n";

    // A boundary at the exact frame edge creates a full-lifetime actor. A
    // render frame spanning the whole short event simulates only its active
    // interval, then lets the equal-lifetime despawn be idempotent.
    LocalRealm::Impl timed;timed.gameplay.useContent(content);timed.state=LocalRealmState::SinglePlayer;timed.realmId=31;
    timed.self=player(timed.gameplay,31,"Timed");timed.saved={{{31,31},timed.self}};timed.resetWorldEventsFromContent();
    assert(timed.tickAuthoritySimulation(.05f));
    auto timedActor=std::find_if(timed.gameplay.npcs().begin(),timed.gameplay.npcs().end(),[](const auto& npc){return npc.scriptActorId==100;});
    assert(timedActor!=timed.gameplay.npcs().end()&&timedActor->scriptLifetimeMs==200&&timed.worldEvents[0].remainingMs==200);
    assert(timed.tickAuthoritySimulation(.20f)&&!timed.worldEvents[0].active&&!scriptedActor(timed.gameplay,100));
    assert(timed.worldEvents[0].remainingMs==100);
    assert(timed.tickAuthoritySimulation(.10f)&&timed.worldEvents[0].active&&timed.worldEvents[0].cycle==2);
    timedActor=std::find_if(timed.gameplay.npcs().begin(),timed.gameplay.npcs().end(),[](const auto& npc){return npc.scriptActorId==100;});
    assert(timedActor!=timed.gameplay.npcs().end()&&timedActor->scriptLifetimeMs==200);
    std::cout<<"PASS exact-edge start and segmented short-event lifetime\n";

    std::vector<uint8_t> good;assert(readFile(std::string(temp)+"/realm.wprs",good,MaxSaveSize));assert(good[4]==SaveVersion);
    const size_t eventTail=1+h.worldEvents.size()*WorldEventWireBytes;
    constexpr size_t PendingKillWireBytes=8+4+8+4;
    const size_t pendingTail=2+h.gameplay.pendingScriptKills().size()*PendingKillWireBytes;
    auto legacy=good;legacy.erase(legacy.end()-4-eventTail-pendingTail,legacy.end()-4);legacy[4]=41;rewriteChecksum(legacy);
    const auto legacyPath=std::string(temp)+"/legacy41.wprs";assert(atomicWrite(legacyPath,legacy,false));
    LocalRealm::Impl migrated;migrated.gameplay.useContent(content);assert(migrated.parseSave(legacyPath));
    assert(migrated.worldEvents.size()==1&&migrated.worldEvents[0].id==700&&migrated.worldEvents[0].revision==1&&
           !migrated.worldEvents[0].active&&!migrated.worldEvents[0].cycle&&migrated.worldEvents[0].remainingMs==50);
    migrated.state=LocalRealmState::SinglePlayer;migrated.self=migrated.saved[0].player;
    assert(migrated.reconcileWorldEventPhases()&&migrated.self.phaseMask==5);
    LocalRealm::Impl browser;assert(browser.parseSave(std::string(temp)+"/realm.wprs")&&browser.worldEvents==h.worldEvents);
    auto invalid=good;const size_t tailStart=invalid.size()-4-pendingTail-eventTail;for(size_t i=1;i<5;++i)invalid[tailStart+i]=0;rewriteChecksum(invalid);
    const auto invalidSave=std::string(temp)+"/invalid.wprs";assert(atomicWrite(invalidSave,invalid,false));
    const auto preserved=reloaded.worldEvents;assert(!reloaded.parseSave(invalidSave)&&reloaded.worldEvents==preserved);
    auto badFlags=good;badFlags[tailStart+17]=4;rewriteChecksum(badFlags);
    const auto badFlagsPath=std::string(temp)+"/bad-flags.wprs";assert(atomicWrite(badFlagsPath,badFlags,false));
    assert(!reloaded.parseSave(badFlagsPath)&&reloaded.worldEvents==preserved);
    auto badRemaining=good;badRemaining[tailStart+9]=0xff;badRemaining[tailStart+10]=0xff;
    badRemaining[tailStart+11]=0xff;badRemaining[tailStart+12]=0xff;rewriteChecksum(badRemaining);
    const auto badRemainingPath=std::string(temp)+"/bad-remaining.wprs";assert(atomicWrite(badRemainingPath,badRemaining,false));
    assert(!reloaded.parseSave(badRemainingPath)&&reloaded.worldEvents==preserved);
    std::cout<<"PASS Save41 migration, content-free roster scan and malformed reject-before-mutation Save45 validation\n";

    // A pre-existing actor makes the whole start batch fail. The boundary and
    // its dialogue remain uncommitted until the actor is removed and tick(0)
    // retries the exact same generation.
    LocalRealm::Impl retry;retry.gameplay.useContent(content);retry.state=LocalRealmState::SinglePlayer;retry.realmId=9;
    retry.self=player(retry.gameplay,9,"Retry");retry.saved={{{9,9},retry.self}};retry.resetWorldEventsFromContent();
    std::vector<LocalRealmPlayer*> retryScope{&retry.self};assert(retry.gameplay.executeScriptActions({100},retryScope,error));
    assert(retry.tickAuthoritySimulation(.25f)&&!retry.worldEvents[0].active&&!retry.worldEvents[0].cycle&&!retry.worldEvents[0].remainingMs);
    const auto pendingActor=std::find_if(retry.gameplay.npcs().begin(),retry.gameplay.npcs().end(),[](const auto& npc){return npc.scriptActorId==100;});
    assert(pendingActor!=retry.gameplay.npcs().end()&&pendingActor->scriptLifetimeMs==150);
    assert(retry.gameplay.scriptDialogues().empty());assert(retry.gameplay.executeScriptActions({102},retryScope,error));
    retry.gameplay.tick(0,retryScope);
    assert(retry.tickAuthoritySimulation(0)&&retry.worldEvents[0].active&&retry.worldEvents[0].cycle==1&&retry.gameplay.scriptDialogues().size()==1);
    std::cout<<"PASS failed boundary freezes the long-frame remainder, keeps the batch pending and retries deterministically\n";

    // A quest-triggered dialogue is part of the durable command transaction.
    const auto dialogueBefore=h.gameplay.scriptDialogues();h.self.quests.clear();h.saved[0].player=h.self;
    h.directory=std::string(temp)+"/not-a-directory";std::ofstream(h.directory)<<"x";
    const auto guide=std::find_if(h.gameplay.npcs().begin(),h.gameplay.npcs().end(),[](const auto& npc){return npc.entry==51;});assert(guide!=h.gameplay.npcs().end());
    assert(!h.runCommand(h.self,{LocalAction::AcceptQuest,guide->guid,1},error));
    assert(h.self.quests.empty()&&h.gameplay.scriptDialogues()==dialogueBefore&&error.find("not saved")!=std::string::npos);h.directory=temp;
    std::cout<<"PASS failed realm write restores scripted actors/dialogue history and player transaction\n";

    // Real UDP: delay an old NPC deck across the event phase/revision change,
    // then atomically install the current event and a reordered dialogue deck.
    LocalRealm hostRealm,guestRealm;auto& host=*hostRealm.impl_;auto& guest=*guestRealm.impl_;
    host.gameplay.useContent(content);guest.gameplay.useContent(content);host.state=LocalRealmState::Hosting;guest.state=LocalRealmState::Connected;
    host.realmId=guest.realmId=777;host.directory=temp;host.self=player(host.gameplay,11,"Host2");guest.self=player(guest.gameplay,22,"Guest");
    host.saved={{{11,11},host.self},{{22,22},guest.self}};host.resetWorldEventsFromContent();assert(host.tickWorldEvents(0));
    guest.self=host.saved[1].player;guest.gameplay.setRemoteNpcs({});guest.gameplay.setRemoteScriptDialogues({});
    assert(host.openSocket(0)&&guest.openSocket(0));guest.host=loopback(host.port);guest.session=987;
    LocalRealm::Impl::Peer peer;peer.guid=22;peer.identity={22,22};peer.address=loopback(guest.port);peer.session=987;host.peers.push_back(peer);
    host.world(host.peers[0],1);auto oldNpc=drain(guest.socket);assert(!oldNpc.empty());
    assert(host.tickWorldEvents(.05f));guest.self=host.saved[1].player;guest.gameplay.setRemoteNpcs({});
    for(const auto& packet:oldNpc)assert(deliver(guest,packet)==Message::Npcs);
    assert(guest.gameplay.npcs().empty());
    host.worldEventDeck(host.peers[0],1);auto packets=drain(guest.socket);assert(packets.size()==1&&deliver(guest,packets[0])==Message::WorldEvents);
    assert(guest.worldEventsReady&&guest.worldEvents.size()==1&&guest.worldEvents[0].active&&guest.worldEventSequence==1);
    auto& remote=host.saved[1].player;std::vector<LocalRealmPlayer*> remoteScope{&remote};
    for(unsigned i=0;i<5;++i)assert(host.gameplay.executeScriptActions({103},remoteScope,error));
    host.dialogueDeck(host.peers[0],2);packets=drain(guest.socket);assert(packets.size()==2);
    assert(deliver(guest,packets[1])==Message::ScriptDialogues&&guest.gameplay.scriptDialogues().empty());
    assert(deliver(guest,packets[0])==Message::ScriptDialogues&&guest.gameplay.scriptDialogues().size()==6&&guest.dialogueSequence==2);
    assert(guestRealm.scriptDialogues().size()==6);

    // The first page fixes every multipart invariant. A later page from the
    // same tick cannot silently change total while keeping the same part count.
    guest.gameplay.setRemoteScriptDialogues({});
    auto dialoguePage=[&](uint8_t part,uint8_t total,uint8_t count,uint64_t firstRevision) {
        Writer w;w.u32(3);w.u32(guest.self.mapId);w.u32(guest.self.instanceId);w.u32(guest.self.positionRevision);
        w.u8(part);w.u8(2);w.u8(total);w.u8(count);
        for(unsigned i=0;i<count;++i){w.u64(firstRevision+i);w.u64(host.self.guid);w.u8(0);w.text255("mixed");}
        host.send(Message::ScriptDialogues,peer.session,w,peer.address);
    };
    dialoguePage(0,5,4,20);packets=drain(guest.socket);assert(packets.size()==1);deliver(guest,packets[0]);
    assert(guest.gameplay.scriptDialogues().empty());
    dialoguePage(1,6,2,24);packets=drain(guest.socket);assert(packets.size()==1);deliver(guest,packets[0]);
    assert(guest.gameplay.scriptDialogues().empty()&&guest.dialogueSequence==2);
    dialoguePage(1,5,1,24);packets=drain(guest.socket);assert(packets.size()==1);deliver(guest,packets[0]);
    assert(guestRealm.scriptDialogues().size()==5&&guest.dialogueSequence==3);
    // LAN102: creature speech carries its ChatMsg type; unknown types are refused.
    auto typedPage=[&](uint32_t tick,uint8_t type) {
        Writer w;w.u32(tick);w.u32(guest.self.mapId);w.u32(guest.self.instanceId);w.u32(guest.self.positionRevision);
        w.u8(0);w.u8(1);w.u8(1);w.u8(1);w.u64(40);w.u64(host.self.guid);w.u8(type);w.text255("You no take candle!");
        host.send(Message::ScriptDialogues,peer.session,w,peer.address);
    };
    typedPage(4,13);packets=drain(guest.socket);assert(packets.size()==1);deliver(guest,packets[0]);
    assert(guest.dialogueSequence==3);
    typedPage(5,kLocalChatMonsterSay);packets=drain(guest.socket);assert(packets.size()==1);deliver(guest,packets[0]);
    assert(guest.dialogueSequence==5&&guestRealm.scriptDialogues().size()==1&&guestRealm.scriptDialogues()[0].chatType==kLocalChatMonsterSay);

    // LAN100's single cast deck is 841 bytes at capacity. Exercise a real UDP
    // cast against vehicle content, then prove a local revision change hides it.
    LocalGameplay vehicleContentLoader;const auto vehiclePath=(std::filesystem::path(argv[1]).parent_path()/"vehicle_effects_world.json").string();
    assert(vehicleContentLoader.loadContent(vehiclePath,error));const auto vehicleContent=vehicleContentLoader.sharedContent();
    host.gameplay.useContent(vehicleContent);guest.gameplay.useContent(vehicleContent);
    LocalVehicleCast cast;cast.sourceGuid=0xf130000000000001ULL;cast.ownerGuid=host.saved[1].player.guid;
    cast.targetGuid=0xf130000000000002ULL;cast.spellId=9201;cast.remainingMs=cast.totalMs=1000;
    cast.mapId=cast.instanceId=0;cast.phaseMask=1;cast.slot=0;cast.seat=0;
    assert(validLocalVehicleCastView(cast,host.gameplay.content()));host.gameplay.setRemoteVehicleCasts({cast});
    guest.self.mapId=host.saved[1].player.mapId=0;guest.self.instanceId=host.saved[1].player.instanceId=0;
    guest.self.phaseMask=host.saved[1].player.phaseMask=1;
    host.vehicleCastDeck(host.peers[0],1);packets=drain(guest.socket);
    assert(packets.size()==1&&packets[0].size()==HeaderSize+21+VehicleCastWireBytes);
    assert(deliver(guest,packets[0])==Message::VehicleCasts&&guestRealm.vehicleCasts().size()==1);
    ++guest.self.positionRevision;assert(guestRealm.vehicleCasts().empty()&&guestRealm.scriptDialogues().empty());
    Writer stale;stale.u32(1);stale.u32(guest.self.mapId);stale.u32(guest.self.instanceId);stale.u32(guest.self.positionRevision);stale.u8(0);
    Reader staleReader(stale.bytes.data(),stale.bytes.size());guest.receiveWorldEvents(staleReader);assert(guest.worldEventSequence==1);
    host.worldEvents.clear();
    hostRealm.stop();guestRealm.stop();
    std::cout<<"PASS real UDP stale NPC rejection, partial/reordered/mixed dialogue pages, event snapshot and cast revision gate\n";

    const auto original=nlohmann::json::parse(std::ifstream(argv[1]));const auto invalidJson=std::string(temp)+"/world.json";
    for(int scenario=0;scenario<20;++scenario) {
        auto json=original;auto& event=json["worldEvents"][0];
        if(scenario==0)event["clock"]="utc";
        if(scenario==1)event["activeDurationMs"]=0;
        if(scenario==2)event["activePhaseMask"]=1;
        if(scenario==3)event["inactivePhaseMask"]=2;
        if(scenario==4)event["cooldownMs"]=0;
        if(scenario==5)json["worldEvents"].push_back(event);
        if(scenario==6)event["startActionIds"][0]=999;
        if(scenario==7)json["scriptActions"][0]["mapId"]=1;
        if(scenario==8)json["scriptTriggers"][0]["addPhaseMask"]=2;
        if(scenario==9)event["activeDurationMs"]=201;
        if(scenario==10)event["startActionIds"].push_back(100);
        if(scenario==11)for(unsigned i=0;i<9;++i)event["endActionIds"].push_back(102);
        if(scenario==12){json["scriptActions"].push_back({{"id",104},{"action","combat"},{"actorId",100},{"mapId",0}});event["startActionIds"]={100,104};}
        if(scenario==13)event["endActionIds"]=nlohmann::json::array();
        if(scenario==14)event["startActionIds"]={101,100};
        if(scenario==15){json["scriptActions"].push_back({{"id",105},{"action","spawn"},{"actorId",200},{"npcEntry",50},
            {"mapId",0},{"x",3},{"y",0},{"z",0},{"orientation",0},{"lifetimeMs",200}});event["endActionIds"]={105,102};}
        if(scenario==16){json["scriptActions"].push_back({{"id",107},{"action","despawn"},{"actorId",200},{"mapId",0}});event["endActionIds"]={107,102};}
        if(scenario==17)json["scriptActions"].push_back({{"id",108},{"action","dialogue"},{"actorId",100},
            {"mapId",0},{"text","Invalid external event actor reference."}});
        if(scenario==18)json["scriptTriggers"][0]["actionIds"].push_back(100);
        if(scenario==19)event["endActionIds"]={101,102};
        std::ofstream(invalidJson)<<json.dump();LocalGameplay rejected;assert(!rejected.loadContent(invalidJson,error));
    }
    authority.stop();std::filesystem::remove_all(temp);
    std::cout<<"PASS strict clock/duration/action/scope/exclusive-phase/reference bounds; Save"<<int(SaveVersion)<<"/LAN"<<int(Version)<<"\n";
}
