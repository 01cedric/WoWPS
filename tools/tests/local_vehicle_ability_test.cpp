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
static LocalRealmNpc& actor(LocalGameplay& g,uint32_t entry) {
    for(const auto& n:g.npcs())if(n.entry==entry)return const_cast<LocalRealmNpc&>(n);
    std::abort();
}
static LocalRealmCommand shot(uint64_t hull,uint64_t target,uint32_t slot=0) {
    LocalRealmCommand c{LocalAction::VehicleAbility,target,slot};c.serviceNpcGuid=hull;return c;
}
static sockaddr_in loopback(uint16_t port){sockaddr_in a{};initAddress(a);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);return a;}
static std::vector<std::vector<uint8_t>> drain(socket_t socket){
    std::vector<std::vector<uint8_t>> packets;
    for(;;){std::array<uint8_t,MaxPacket+1> bytes{};const auto n=::recvfrom(socket,reinterpret_cast<char*>(bytes.data()),bytes.size(),net::datagramFlags(),nullptr,nullptr);
        if(n<0){assert(net::isWouldBlock(net::lastError()));break;}assert(size_t(n)<=MaxPacket);packets.emplace_back(bytes.begin(),bytes.begin()+n);}
    return packets;
}
static void deliver(LocalRealm::Impl& g,const std::vector<uint8_t>& bytes){
    Reader r(bytes.data(),bytes.size());assert(r.u32()==WireMagic && r.u8()==Version);const auto type=Message(r.u8());assert(r.u16()==bytes.size());
    const auto seq=r.u32();const auto token=r.u64();g.handleClient(type,r,g.host,token,seq);
}
int main(int argc,char** argv) {
    assert(argc==2);std::string error;LocalGameplay game;assert(game.loadContent(argv[1],error));
    LocalRealmPlayer p;p.guid=1;p.name="Driver";game.initializePlayer(p,true);auto gunner=p;gunner.guid=2;gunner.name="Gunner";
    std::vector<LocalRealmPlayer*> players{&p,&gunner};game.tick(0,players);
    const auto hull=actor(game,50).guid,enemy=actor(game,51).guid;
    assert(game.execute(p,{LocalAction::AcceptQuest,hull,1},players,error));
    assert(game.execute(p,{LocalAction::EnterVehicle,hull,0},players,error));
    assert(game.execute(gunner,{LocalAction::EnterVehicle,hull,1},players,error));
    const auto base=p,baseGunner=gunner;const auto baseHull=actor(game,50),baseEnemy=actor(game,51);
    assert(baseHull.vehiclePower==100);
    for(int scenario=0;scenario<17;++scenario){
        p=base;actor(game,50)=baseHull;actor(game,51)=baseEnemy;auto command=shot(hull,enemy);
        if(scenario==0)command.id=6;
        if(scenario==1)command.id=3;
        if(scenario==2)command.serviceNpcGuid=hull+10;
        if(scenario==3)p.vehicleSeat=2;
        if(scenario==4)p.vehicleSeat=8;
        if(scenario==5)p.vehicleId=701;
        if(scenario==6)p.phaseMask=2;
        if(scenario==7)p.mapId=1;
        if(scenario==8)p.instanceId=1;
        if(scenario==9){p.dead=true;p.health=0;}
        if(scenario==10)actor(game,50).vehiclePower=29;
        if(scenario==11)actor(game,50).vehicleCooldownMs[0]=1;
        if(scenario==12)actor(game,50).vehicleGlobalCooldownMs=1;
        if(scenario==13)actor(game,51).z=31;
        if(scenario==14)actor(game,51).requiredPhaseMask=2;
        if(scenario==15)command.target=hull;
        if(scenario==16){actor(game,51).dead=true;actor(game,51).health=0;}
        const auto before=actor(game,50);const auto targetHealth=actor(game,51).health;
        assert(!game.execute(p,command,players,error));
        assert(actor(game,50).vehiclePower==before.vehiclePower && actor(game,50).vehicleCooldownMs==before.vehicleCooldownMs && actor(game,51).health==targetHealth);
    }
    p=base;actor(game,50)=baseHull;actor(game,51)=baseEnemy;
    p.level=80;p.mana=0;const auto health=p.health;
    assert(game.execute(p,shot(hull,enemy),players,error));
    assert(actor(game,51).health==910 && actor(game,51).targetGuid==hull && actor(game,51).lootOwner==p.guid);
    assert(actor(game,50).vehiclePower==70 && actor(game,50).vehicleCooldownMs[0]==2000 && p.mana==0);
    auto events=game.combatEvents();const auto& hit=events.back();
    assert(p.meleeViews.back().source==hull && gunner.meleeViews.back().source==hull);
    assert(hit.source==hull && hit.target==enemy && hit.effective==90 && !hit.actorIsPlayer);
    p.level=base.level;
    assert(!game.execute(gunner,shot(hull,enemy),players,error)); // Shared cooldown, not per seat.
    assert(!game.execute(gunner,shot(hull,hull,1),players,error)); // Driver-only repair.
    game.tick(.25f,players);game.tick(.25f,players);
    assert(p.health==health && actor(game,50).health<1000 && actor(game,50).vehiclePower==75);
    events=game.combatEvents();assert(std::any_of(events.begin(),events.end(),[&](const auto& e){return e.source==enemy && e.target==hull && e.kind==LocalCombatEventKind::NpcMelee;}));
    std::cout<<"PASS authority guards/no partial spend, vehicle damage identity, shared cooldown and hull retaliation without driver damage\n";
    // No extra energy or fresh cooldown from handoff/reboarding.
    const auto remaining=actor(game,50).vehicleCooldownMs;
    assert(game.execute(p,{LocalAction::ExitVehicle},players,error));
    assert(game.execute(gunner,{LocalAction::SwitchVehicleSeat,hull,0},players,error));
    assert(game.execute(p,{LocalAction::EnterVehicle,hull,1},players,error));
    assert(actor(game,50).vehicleCooldownMs==remaining && actor(game,50).vehiclePower==75);
    // Repair the hull; it must not heal either rider or accept another target.
    for(int i=0;i<6;++i)game.tick(.25f,players);
    const auto hp=actor(game,50).health;assert(hp<1000);
    const auto energy=actor(game,50).vehiclePower;
    assert(!game.execute(gunner,shot(hull,p.guid,1),players,error));
    assert(game.execute(gunner,shot(hull,hull,1),players,error));
    assert(actor(game,50).health==std::min(1000u,hp+60) && actor(game,50).vehiclePower==energy-20);
    for(int i=0;i<4;++i)game.tick(.25f,players);
    assert(game.execute(p,shot(hull,enemy,5),players,error));
    assert(p.xp==80);
    assert(actor(game,51).dead && p.quests[0].status==LocalQuestStatus::Complete && p.quests[0].progress[0]==1);
    events=game.combatEvents();assert(std::any_of(events.begin(),events.end(),[&](const auto& e){return e.kind==LocalCombatEventKind::Kill && e.source==hull && !e.actorIsPlayer;}));
    std::cout<<"PASS gunner handoff/reboard preserve resources, repair target validation, driver-independent quest/kill credit\n";
    // Death ejects all seats, emits the vehicle death, and respawn starts a new resource pool.
    p=base;gunner=baseGunner;actor(game,50)=baseHull;actor(game,51)=baseEnemy;
    assert(game.execute(p,shot(hull,enemy),players,error));actor(game,50).health=1;
    for(int i=0;i<4 && p.vehicleGuid;++i)game.tick(.25f,players);
    assert(actor(game,50).dead && !p.vehicleGuid && !gunner.vehicleGuid && p.health==base.health && gunner.health==baseGunner.health);
    for(int i=0;i<9;++i)game.tick(.25f,players);
    assert(!actor(game,50).dead && actor(game,50).vehiclePower==100 && actor(game,50).vehicleCooldownMs[0]==0);
    std::cout<<"PASS hull death, forced seat cleanup and fresh respawn resources\n";
    // Fractional regeneration: one second yields exactly ten, no per-frame rounding.
    actor(game,50)=baseHull;actor(game,51)=baseEnemy;actor(game,50).vehiclePower=0;
    p=base;gunner=baseGunner;
    for(int i=0;i<100;++i)game.tick(.01f,players);
    assert(actor(game,50).vehiclePower>=9 && actor(game,50).vehiclePower<=10);
    std::cout<<"PASS bounded fractional energy regeneration\n";

    // The installed collision gate is evaluated before spending resources.
    {
        LocalGameplay blocked;blocked.useContent(game.sharedContent());auto driver=base;
        blocked.tick(0,{&driver});LocalCollisionTile wall;wall.mapId=0;wall.tileX=wall.tileY=32;
        wall.vertices={{2,-10,-10},{2,10,-10},{2,10,10},{2,-10,10}};
        wall.triangles={{0,1,2,0},{0,2,3,0}};wall.buildTree();blocked.adoptCollisionTile(std::move(wall));
        assert(!blocked.execute(driver,shot(hull,enemy),{&driver},error));
        assert(error.find("sight")!=std::string::npos && actor(blocked,50).vehiclePower==100 && actor(blocked,51).health==1000);
    }
    // Aggressive acquisition also selects the hull before the first shot.
    {
        LocalGameplay aggro;auto c=std::make_shared<LocalWorldContent>(*game.sharedContent());c->npcs[1].aggroRadius=20;
        aggro.useContent(c);auto driver=base;aggro.tick(0,{&driver});
        assert(actor(aggro,51).targetGuid==hull);
    }
    std::cout<<"PASS collision rejects before spend and aggressive acquisition targets the hull\n";

    char temp[]="/tmp/wowps-vehicle-ability-XXXXXX";assert(mkdtemp(temp));
    const auto original=nlohmann::json::parse(std::ifstream(argv[1]));const auto path=std::string(temp)+"/world.json";
    for(int scenario=0;scenario<13;++scenario) {
        auto j=original;auto& kit=j["vehicleKits"][0];auto& a=kit["abilities"][0];
        if(scenario==0)a["slot"]=0;
        if(scenario==1)a["slot"]=7;
        if(scenario==2)kit["abilities"].push_back(a);
        if(scenario==3)a["spellId"]=999;
        if(scenario==4)a["seatMask"]=8;
        if(scenario==5)a["seatMask"]=0;
        if(scenario==6)a["repair"]=1;
        if(scenario==7)a["damage"]=0;
        if(scenario==8)a["powerCost"]=101;
        if(scenario==9)a["cooldownMs"]=0;
        if(scenario==10)a["range"]=61;
        if(scenario==11)kit["id"]=999;
        if(scenario==12)kit["abilities"]=nlohmann::json::array();
        std::ofstream(path)<<j.dump();LocalGameplay rejected;assert(!rejected.loadContent(path,error));
    }
    std::cout<<"PASS authored kit, slot, seat, effect, metadata and resource bounds\n";

    // A kill action can be temporarily refused after the NPC death has already
    // been published. Its immutable fact survives owner disconnect and Save43,
    // then resumes in order when that saved owner reconnects.
    {
        char pendingTemp[]="/tmp/wowps-pending-script-XXXXXX";assert(mkdtemp(pendingTemp));
        const auto scriptFixture=std::filesystem::path(argv[1]).parent_path()/"script_actions_world.json";
        auto scriptJson=nlohmann::json::parse(std::ifstream(scriptFixture));
        scriptJson["scriptActions"].push_back({{"id",9},{"action","spawn"},{"actorId",300},{"npcEntry",60},
            {"mapId",0},{"x",6},{"y",0},{"z",0},{"lifetimeMs",600000}});
        for(auto& trigger:scriptJson["scriptTriggers"])if(trigger["trigger"]=="npcKill")trigger["actionIds"]={9};
        const auto scriptWorld=std::string(pendingTemp)+"/world.json";std::ofstream(scriptWorld)<<scriptJson.dump();

        LocalRealm pendingRealm;auto& authority=*pendingRealm.impl_;
        authority.state=LocalRealmState::Hosting;authority.realmId=456;authority.directory=pendingTemp;
        assert(authority.gameplay.loadContent(scriptWorld,error));
        LocalRealmPlayer host;host.guid=10;host.name="Host";authority.gameplay.initializePlayer(host,true);
        LocalRealmPlayer owner;owner.guid=11;owner.name="Offline";authority.gameplay.initializePlayer(owner,true);
        authority.self=host;authority.saved={{{10,10},host},{{11,11},owner}};
        auto& savedOwner=authority.saved[1].player;
        LocalRealmPlayer bot;bot.guid=kLocalBotGuidPrefix|1;bot.name="Transient";authority.gameplay.initializePlayer(bot,true);
        authority.botPlayers.push_back(bot);auto& transientBot=authority.botPlayers.back();
        std::vector<LocalRealmPlayer*> roster{&authority.self,&savedOwner,&transientBot};authority.gameplay.tick(0,roster);
        const auto guide=actor(authority.gameplay,50).guid;
        assert(authority.gameplay.execute(savedOwner,{LocalAction::AcceptQuest,guide,1},roster,error));

        auto blocked=authority.gameplay.scriptActionCheckpoint();
        for(auto& npc:blocked.npcs)if(npc.entry==61){npc.health=0;npc.dead=true;}
        while(blocked.npcs.size()<LocalGameplay::MaxNpcs) {
            auto dummy=blocked.npcs.front();dummy.guid=0xe000000000000000ULL+blocked.npcs.size();
            dummy.health=0;dummy.dead=true;dummy.scriptActorId=0;dummy.scriptActorRetired=false;
            blocked.npcs.push_back(std::move(dummy));
        }
        // Both owners are blocked by the full NPC roster. Save43 must retain the
        // durable human fact while omitting the equally valid transient bot fact.
        blocked.pendingKills.push_back({transientBot.guid,61,50,1});
        blocked.pendingKills.push_back({savedOwner.guid,61,50,1});
        authority.gameplay.restoreScriptActionCheckpoint(std::move(blocked));
        authority.gameplay.tick(0,roster); // Spawn action 9 is refused at NPC capacity.
        assert(authority.gameplay.pendingScriptKills().size()==2 && transientBot.xp==0 && savedOwner.xp==0 &&
               savedOwner.quests[0].status==LocalQuestStatus::Active);
        authority.gameplay.tick(0,{&authority.self}); // Owner disconnected: fact remains.
        assert(authority.gameplay.pendingScriptKills().size()==2);

        const auto validPending=authority.gameplay.scriptActionCheckpoint();auto unknown=validPending;
        unknown.pendingKills[1].playerGuid=99999;
        authority.gameplay.restoreScriptActionCheckpoint(std::move(unknown));
        assert(!authority.saveRealm()); // Unknown saved owner cannot enter a durable queue.
        authority.gameplay.restoreScriptActionCheckpoint(validPending);assert(authority.saveRealm());

        // Free one slot and retry the still-live bot fact. It commits in memory;
        // the human fact remains queued and must still be present in the save.
        auto available=authority.gameplay.scriptActionCheckpoint();available.npcs.pop_back();
        authority.gameplay.restoreScriptActionCheckpoint(std::move(available));authority.gameplay.tick(0,roster);
        assert(transientBot.xp==50 && authority.gameplay.pendingScriptKills().size()==1 &&
               authority.gameplay.pendingScriptKills()[0].playerGuid==savedOwner.guid);

        std::vector<uint8_t> damaged;assert(readFile(std::string(pendingTemp)+"/realm.wprs",damaged,MaxSaveSize));
        assert(damaged.size()>28);const size_t pendingGuid=damaged.size()-4-24;
        const uint64_t foreign=99999;for(unsigned i=0;i<8;++i)damaged[pendingGuid+i]=uint8_t(foreign>>(i*8));
        const auto badSum=checksum(damaged.data(),damaged.size()-4);
        for(unsigned i=0;i<4;++i)damaged[damaged.size()-4+i]=uint8_t(badSum>>(i*8));
        const auto badSave=std::string(pendingTemp)+"/unknown-player.wprs";
        std::ofstream badOut(badSave,std::ios::binary);badOut.write(reinterpret_cast<const char*>(damaged.data()),damaged.size());badOut.close();
        LocalRealm rejectedRealm;auto& rejected=*rejectedRealm.impl_;assert(rejected.gameplay.loadContent(scriptWorld,error));
        assert(!rejected.parseSave(badSave));

        LocalRealm resumedRealm;auto& resumed=*resumedRealm.impl_;resumed.state=LocalRealmState::Hosting;
        resumed.directory=pendingTemp;assert(resumed.gameplay.loadContent(scriptWorld,error));assert(resumed.loadRealm());
        resumed.self=resumed.saved[0].player;
        assert(resumed.gameplay.pendingScriptKills().size()==1);
        resumed.gameplay.tick(0,resumed.activePlayers()); // Still offline after reload.
        assert(resumed.gameplay.pendingScriptKills().size()==1);
        LocalRealm::Impl::Peer resumedPeer;resumedPeer.guid=11;resumedPeer.identity={11,11};resumedPeer.loading=false;
        resumed.peers.push_back(resumedPeer);resumed.gameplay.tick(0,resumed.activePlayers());
        const auto* credited=resumed.findSaved(11);assert(credited);
        assert(resumed.gameplay.pendingScriptKills().empty() && credited->player.xp==50 &&
               credited->player.quests[0].status==LocalQuestStatus::Complete && credited->player.quests[0].progress[0]==1);
        assert(std::any_of(resumed.gameplay.npcs().begin(),resumed.gameplay.npcs().end(),[](const auto& npc){return npc.scriptActorId==300;}));
        pendingRealm.stop();resumedRealm.stop();rejectedRealm.stop();
        std::filesystem::remove_all(pendingTemp);
    }
    std::cout<<"PASS refused human kill survives reload; transient bot fact saves safely, retries live, and unknown owners remain rejected\n";

    LocalRealm hostRealm,guestRealm;auto& h=*hostRealm.impl_;auto& g=*guestRealm.impl_;
    h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=temp;
    auto content=game.sharedContent();h.gameplay.useContent(content);g.gameplay.useContent(content);
    h.self=base;h.self.vehicleGuid=0;h.self.vehicleId=0;h.self.vehicleSeat=0;h.self.vehicleControl=false;
    g.self=baseGunner;h.saved={{{1,11},h.self},{{2,22},baseGunner}};
    auto peerPlayer=baseGunner;h.gameplay.tick(0,{&h.self,&peerPlayer});
    assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;
    LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);
    h.refreshPlayers();g.players=h.players;
    assert(guestRealm.useVehicleAbility(0,enemy));guestRealm.update(.01f);h.receive();
    for(const auto& packet:drain(g.socket))deliver(g,packet);
    assert(g.pendingCommands.empty() && h.peers[0].lastCommandSuccess && actor(h.gameplay,50).vehiclePower==70 && actor(h.gameplay,51).health==910);
    h.progress(h.peers[0]);h.history(h.peers[0]);h.world(h.peers[0],1);
    for(const auto& packet:drain(g.socket))deliver(g,packet);
    assert(actor(g.gameplay,50).vehiclePower==70 && actor(g.gameplay,50).vehicleCooldownMs[0]==2000 && actor(g.gameplay,51).targetGuid==hull);
    assert(g.self.meleeViews.back().source==hull);
    {
        const auto* source=h.findSaved(2);assert(source);Writer cast;writeCast(cast,source->player);
        auto passenger=baseGunner;Reader valid(cast.bytes.data(),cast.bytes.size());assert(readCast(valid,passenger) && valid.done());
        passenger.vehicleGuid=hull+99;Reader foreign(cast.bytes.data(),cast.bytes.size());assert(!readCast(foreign,passenger));
        passenger.vehicleGuid=0;Reader detached(cast.bytes.data(),cast.bytes.size());assert(!readCast(detached,passenger));
    }

    // Duplicate command, gap, truncated datagram and wrong session never spend again.
    const auto send=[&](uint32_t id,bool truncated=false,uint64_t token=987){
        Writer w;w.u32(id);w.u8(uint8_t(LocalAction::VehicleAbility));w.u64(enemy);w.u32(0);
        w.u32(0);w.u32(0);w.u32(0);w.u64(hull);if(truncated)w.bytes.pop_back();g.send(Message::Command,token,w,g.host);h.receive();drain(g.socket);
    };
    actor(h.gameplay,50).vehicleCooldownMs={};actor(h.gameplay,50).vehicleGlobalCooldownMs=0;
    send(1);send(3);send(2,true);send(2,false,444);
    assert(h.peers[0].lastCommand==1 && actor(h.gameplay,50).vehiclePower==70 && actor(h.gameplay,51).health==910);
    send(2);assert(h.peers[0].lastCommandSuccess && actor(h.gameplay,50).vehiclePower==40);
    std::cout<<"PASS real UDP ability command, NPC energy/cooldown/threat snapshot, replay/gap/truncation/session rejection\n";
    // Instant command kills settle their deferred immutable fact before the
    // authority can take a save snapshot. The queue itself is deliberately
    // transient, so saving between execute and the next tick must not lose XP
    // or quest credit.
    {
        auto& owner=h.saved[1].player;owner.xp=0;owner.quests=base.quests;
        actor(h.gameplay,50)=baseHull;actor(h.gameplay,51)=baseEnemy;
        std::string result;assert(h.runCommand(owner,shot(hull,enemy,5),result));
        assert(owner.xp==80 && owner.quests[0].status==LocalQuestStatus::Complete && owner.quests[0].progress[0]==1);
        assert(h.saveRealm());
        LocalRealm restoredRealm;auto& restored=*restoredRealm.impl_;
        restored.directory=temp;restored.gameplay.useContent(content);assert(restored.loadRealm());
        const auto* savedOwner=restored.findSaved(owner.guid);assert(savedOwner);
        assert(savedOwner->player.xp==80 && savedOwner->player.quests[0].status==LocalQuestStatus::Complete &&
               savedOwner->player.quests[0].progress[0]==1);
    }
    std::cout<<"PASS instant kill credit is committed before runCommand returns and survives an immediate realm save\n";
    // The decoder rejects out-of-bounds resource/cooldown fields and truncation.
    auto snapshot=baseHull;snapshot.playerThreat.viewerGuid=2;
    for(int scenario=0;scenario<4;++scenario){auto n=snapshot;
        if(scenario==0)n.vehiclePower=101;
        if(scenario==1)n.vehicleCooldownMs[0]=2001;
        if(scenario==2)n.vehicleGlobalCooldownMs=1001;
        Writer w;writeNpc(w,n);if(scenario==3)w.bytes.pop_back();Reader rd(w.bytes.data(),w.bytes.size());readNpc(rd,*content);assert(!rd.valid);
    }
    Writer w;writeNpc(w,snapshot);Reader rd(w.bytes.data(),w.bytes.size());auto decoded=readNpc(rd,*content);
    assert(rd.done() && decoded.vehiclePower==100 && HeaderSize+19+NpcsPerPage*NpcWireBytes<=MaxPacket);
    guestRealm.stop();hostRealm.stop();
    std::filesystem::remove_all(temp);
    std::cout<<"PASS strict NPC decoder and packet bound; Save"<<int(SaveVersion)<<" unchanged, LAN"<<int(Version)<<"\n";
}
