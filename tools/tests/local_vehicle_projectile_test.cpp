#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
using namespace wowee::game;
namespace net=wowee::net;

static LocalRealmNpc& actor(LocalGameplay& g,uint32_t entry) {
    for(const auto& n:g.npcs())if(n.entry==entry)return const_cast<LocalRealmNpc&>(n);
    std::abort();
}
static LocalRealmCommand shot(uint64_t hull,uint32_t slot=0,uint64_t target=0) {
    LocalRealmCommand c{LocalAction::VehicleAbility,target,slot};c.serviceNpcGuid=hull;return c;
}
static LocalRealmCommand aim(uint64_t hull,uint32_t seat,float yaw,float pitch) {
    LocalRealmCommand c{LocalAction::VehicleAim,hull,seat};c.serviceNpcGuid=hull;c.vehicleAimYaw=yaw;c.vehicleAimPitch=pitch;return c;
}
static bool near(float a,float b,float tolerance=.002f){return std::abs(a-b)<=tolerance;}
struct Fixture {
    LocalGameplay game;
    LocalRealmPlayer driver;
    std::string error;
    uint64_t hull=0,enemy=0;
    explicit Fixture(const char* path) {
        assert(game.loadContent(path,error));driver.guid=1;driver.name="Driver";game.initializePlayer(driver,true);
        game.tick(0,{&driver});hull=actor(game,50).guid;enemy=actor(game,51).guid;
        assert(game.execute(driver,{LocalAction::EnterVehicle,hull,0},{&driver},error));
    }
    bool execute(const LocalRealmCommand& c){return game.execute(driver,c,{&driver},error);}
    void tick(float dt){game.tick(dt,{&driver});}
    void clearCooldown(){actor(game,50).vehicleCooldownMs={};actor(game,50).vehicleGlobalCooldownMs=0;}
    void wall(float x) {
        LocalCollisionTile wall;wall.mapId=0;wall.tileX=wall.tileY=32;
        wall.vertices={{x,-10,-10},{x,10,-10},{x,10,10},{x,-10,10}};
        wall.triangles={{0,1,2,0},{0,2,3,0}};wall.buildTree();game.adoptCollisionTile(std::move(wall));
    }
};
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
    assert(argc==2);const char* path=argv[1];
    // Per-seat authority, finite/bounded angles, and no effect from stale seat commands.
    for(int scenario=0;scenario<12;++scenario) {
        Fixture f(path);auto command=aim(f.hull,0,.2f,.3f);const auto before=actor(f.game,50).vehicleAim;
        if(scenario==0)command.vehicleAimYaw=std::numeric_limits<float>::quiet_NaN();
        if(scenario==1)command.vehicleAimPitch=std::numeric_limits<float>::infinity();
        if(scenario==2)command.vehicleAimYaw=3.2f;
        if(scenario==3)command.vehicleAimPitch=1.1f;
        if(scenario==4)command.target=f.hull+99;
        if(scenario==5)command.serviceNpcGuid=f.hull+99;
        if(scenario==6)command.id=1;
        if(scenario==7){f.driver.vehicleGuid=0;f.driver.vehicleId=0;}
        if(scenario==8){f.driver.dead=true;f.driver.health=0;}
        if(scenario==9)f.driver.phaseMask=2;
        if(scenario==10)f.driver.mapId=1;
        if(scenario==11)f.driver.instanceId=1;
        assert(!f.execute(command));assert(actor(f.game,50).vehicleAim==before && actor(f.game,50).vehiclePower==100);
    }
    {
        Fixture f(path);assert(f.execute(aim(f.hull,0,.25f,.5f)));
        assert(f.execute({LocalAction::SwitchVehicleSeat,f.hull,1}));
        assert(!f.execute(aim(f.hull,0,-.4f,-.2f)));assert(f.execute(aim(f.hull,1,-.4f,-.2f)));
        const auto& angles=actor(f.game,50).vehicleAim;
        assert(near(angles[0][0],.25f) && near(angles[0][1],.5f) && near(angles[1][0],-.4f) && near(angles[1][1],-.2f));
        assert(!f.execute(shot(f.hull,0,f.enemy)));assert(f.game.vehicleProjectiles().empty() && actor(f.game,50).vehiclePower==100);
        assert(f.execute(shot(f.hull,4,f.enemy)));assert(actor(f.game,51).health==910 && f.game.vehicleProjectiles().empty());
    }
    std::cout<<"PASS finite aim/seat/phase authority, per-seat aim, stale handoff rejection and projectile versus targeted command contract\n";

    {
        Fixture f(path);assert(f.execute(shot(f.hull)));
        assert(actor(f.game,51).health==1000 && actor(f.game,50).vehiclePower==90 && actor(f.game,50).vehicleCooldownMs[0]==2000);
        assert(f.game.vehicleProjectiles().size()==1);const auto launched=f.game.vehicleProjectiles().front();
        assert(launched.sourceGuid==f.hull && launched.ownerGuid==f.driver.guid && launched.spellId==9101);
        f.tick(.25f);assert(actor(f.game,51).health==1000 && f.game.vehicleProjectiles().size()==1);
        f.tick(.25f);assert(actor(f.game,51).health==1000 && f.game.vehicleProjectiles().size()==1);
        f.tick(.1f);assert(actor(f.game,51).health==910 && actor(f.game,52).health==1000 && f.game.vehicleProjectiles().empty());
        assert(actor(f.game,51).lootOwner==f.driver.guid && actor(f.game,51).targetGuid==f.hull);
        const auto events=f.game.combatEvents();assert(std::any_of(events.begin(),events.end(),[&](const auto& e){return e.source==f.hull && e.target==f.enemy && e.spell==9101 && e.effective==90 && !e.actorIsPlayer;}));
    }
    {
        Fixture f(path);actor(f.game,50).orientation=.5f;assert(f.execute(aim(f.hull,0,.25f,.5f)));assert(f.execute(shot(f.hull,1)));
        const auto initial=f.game.vehicleProjectiles().front();assert(near(initial.vx,20*std::cos(.5f)*std::cos(.75f)) && near(initial.vy,20*std::cos(.5f)*std::sin(.75f)));
        f.tick(.1f);f.tick(.1f);assert(f.game.vehicleProjectiles().size()==1);const auto& moved=f.game.vehicleProjectiles().front();
        assert(near(moved.x,initial.x+initial.vx*.2f) && near(moved.y,initial.y+initial.vy*.2f));
        assert(near(moved.z,initial.z+initial.vz*.2f-.5f*10*.2f*.2f) && near(moved.vz,initial.vz-2));
        assert(moved.remainingMs>=3799 && moved.remainingMs<=3801);
    }
    {
        Fixture f(path);actor(f.game,51).x=12.6f;actor(f.game,51).y=.55f;
        assert(f.execute(shot(f.hull,2)));f.tick(.25f);
        assert(actor(f.game,51).health==910 && actor(f.game,52).health==1000 && f.game.vehicleProjectiles().empty());
    }
    std::cout<<"PASS launch-only resource spend, delayed nearest impact, hull caster/threat identity, exact gravity and swept fast-shell collision\n";

    {
        Fixture f(path);assert(f.execute(shot(f.hull)));f.tick(.25f);
        actor(f.game,51).y=10;actor(f.game,52).y=10;
        f.tick(.25f);assert(f.game.vehicleProjectiles().size()==1 && near(f.game.vehicleProjectiles().front().y,0));
        for(int i=0;i<5;++i)f.tick(.25f);
        assert(f.game.vehicleProjectiles().empty() && actor(f.game,51).health==1000 && actor(f.game,52).health==1000 && actor(f.game,50).vehiclePower==90);
    }
    {
        Fixture f(path);f.wall(6);assert(f.execute(shot(f.hull)));f.tick(.25f);f.tick(.25f);
        assert(f.game.vehicleProjectiles().empty() && actor(f.game,51).health==1000 && actor(f.game,50).vehiclePower==90);
    }
    {
        Fixture f(path);f.wall(18);assert(f.execute(shot(f.hull,2)));f.tick(.25f);
        assert(f.game.vehicleProjectiles().empty() && actor(f.game,51).health==910 && actor(f.game,52).health==1000);
    }
    {
        Fixture f(path);assert(f.execute(shot(f.hull,5)));f.tick(.25f);
        assert(f.game.vehicleProjectiles().empty() && actor(f.game,51).health==1000 && actor(f.game,50).vehiclePower==90);
    }
    {
        Fixture f(path);actor(f.game,51).x=30.8f;actor(f.game,52).y=10;
        assert(f.execute(shot(f.hull)));for(int i=0;i<8;++i)f.tick(.25f);
        assert(f.game.vehicleProjectiles().empty() && actor(f.game,51).health==1000 && actor(f.game,52).health==1000);
    }
    {
        Fixture f(path);actor(f.game,51).requiredPhaseMask=2;
        assert(f.execute(shot(f.hull)));for(int i=0;i<5;++i)f.tick(.25f);
        assert(f.game.vehicleProjectiles().empty() && actor(f.game,51).health==1000 && actor(f.game,52).health==910);
    }
    {
        Fixture f(path);f.game.sharedContent()->vehicleKits[0].abilities[2].projectileSpeed=100;
        f.wall(2);assert(f.execute(shot(f.hull,2)));f.tick(.25f);
        assert(f.game.vehicleProjectiles().empty() && actor(f.game,51).health==1000);
    }
    std::cout<<"PASS dodge without homing, finite range and TTL misses, phase-filtered impact, wall-before-target blocking, exact substep wall boundary and target-before-wall impact\n";

    // An in-flight shot belongs to a living occupant and one hull life, not a saved character or reused GUID.
    for(int scenario=0;scenario<10;++scenario) {
        Fixture f(path);assert(f.execute(shot(f.hull)));
        if(scenario==0){f.driver.dead=true;f.driver.health=0;}
        if(scenario==1)assert(f.execute({LocalAction::ExitVehicle}));
        if(scenario==2)f.driver.vehicleGuid=f.hull+99;
        if(scenario==3){actor(f.game,50).dead=true;actor(f.game,50).health=0;actor(f.game,50).respawnTimer=2;}
        if(scenario==4)++actor(f.game,50).combatEpoch;
        if(scenario==5)f.driver.phaseMask=2;
        if(scenario==6)f.driver.mapId=1;
        if(scenario==7)f.driver.instanceId=1;
        if(scenario==8)actor(f.game,50).requiredPhaseMask=2;
        if(scenario==9)f.game.tick(.01f,{});else f.tick(.01f);
        assert(f.game.vehicleProjectiles().empty() && actor(f.game,51).health==1000 && actor(f.game,50).vehiclePower==90);
    }
    {
        Fixture f(path);assert(f.execute(shot(f.hull)));assert(f.execute({LocalAction::SwitchVehicleSeat,f.hull,1}));
        f.tick(.25f);f.tick(.25f);f.tick(.1f);assert(actor(f.game,51).health==910 && f.game.vehicleProjectiles().empty());
    }
    {
        Fixture f(path);assert(f.execute(shot(f.hull)));assert(f.execute({LocalAction::ExitVehicle}));
        assert(f.game.vehicleProjectiles().empty());assert(f.execute({LocalAction::EnterVehicle,f.hull,0}));
        assert(f.game.vehicleProjectiles().empty() && actor(f.game,50).vehiclePower==90 && actor(f.game,50).vehicleCooldownMs[0]==2000);
        f.tick(.25f);f.tick(.25f);f.tick(.1f);assert(actor(f.game,51).health==1000);
    }
    {
        Fixture f(path);assert(f.execute({LocalAction::ExitVehicle}));
        assert(f.execute({LocalAction::AcceptQuest,f.hull,1}));assert(f.execute({LocalAction::EnterVehicle,f.hull,0}));
        actor(f.game,51).health=80;assert(f.execute(shot(f.hull)));f.tick(.25f);f.tick(.25f);f.tick(.1f);
        assert(actor(f.game,51).dead && f.driver.xp==80 && f.driver.quests[0].status==LocalQuestStatus::Complete && f.driver.quests[0].progress[0]==1);
    }
    std::cout<<"PASS exit/death/disconnect/phase/travel/hull-epoch cancellation, seat change preserves flight and projectile kill quest credit\n";

    {
        Fixture f(path);assert(f.execute(aim(f.hull,0,1.5f,0)));
        for(size_t i=0;i<kLocalMaxVehicleProjectiles;++i){f.clearCooldown();assert(f.execute(shot(f.hull,3)));}
        assert(f.game.vehicleProjectiles().size()==16 && actor(f.game,50).vehiclePower==84);
        f.clearCooldown();const auto before=actor(f.game,50);assert(!f.execute(shot(f.hull,3)));
        assert(actor(f.game,50).vehiclePower==before.vehiclePower && actor(f.game,50).vehicleCooldownMs==before.vehicleCooldownMs && !actor(f.game,50).vehicleGlobalCooldownMs);
        std::set<uint32_t> ids;for(const auto& p:f.game.vehicleProjectiles())assert(p.id && ids.insert(p.id).second);
    }
    {
        Fixture f(path);actor(f.game,50).x=1000;f.driver.x=1000;assert(f.execute({LocalAction::ExitVehicle}));
        f.tick(.25f);f.tick(.25f);f.tick(.25f);assert(actor(f.game,50).x==1000);
        assert(f.execute({LocalAction::EnterVehicle,f.hull,0}));
    }
    {
        Fixture f(path);actor(f.game,51).x=2;assert(f.execute(shot(f.hull,4,f.enemy)));const auto before=actor(f.game,50).health;
        f.driver.phaseMask=2;for(int i=0;i<4;++i)f.tick(.25f);
        assert(actor(f.game,50).health==before && !actor(f.game,51).targetGuid);
    }
    for(int axis=0;axis<3;++axis){
        Fixture f(path);auto& hull=actor(f.game,50);f.driver.vehicleSeat=1;f.driver.vehicleControl=false;
        hull.vehicleSeatOffsets[1][axis]=20;
        if(axis==0)hull.x=f.driver.x=99999;
        if(axis==1)hull.y=f.driver.y=99999;
        if(axis==2)hull.z=f.driver.z=19999;
        assert(!f.execute(shot(f.hull)) && f.game.vehicleProjectiles().empty() && hull.vehiclePower==100 && !hull.vehicleCooldownMs[0] && !hull.vehicleGlobalCooldownMs);
    }
    std::cout<<"PASS bounded global projectile pool with no failed-launch spend, XYZ muzzle bounds, empty moved hull retention and phase-loss retaliation cleanup\n";

    char temp[]="/tmp/wowps-vehicle-projectile-XXXXXX";assert(mkdtemp(temp));
    const auto original=nlohmann::json::parse(std::ifstream(path));const auto invalidPath=std::string(temp)+"/world.json";
    for(int scenario=0;scenario<18;++scenario) {
        auto j=original;auto& kit=j["vehicleKits"][0];auto& a=kit["abilities"][0];
        if(scenario==0)a["projectileSpeed"]=-1;
        if(scenario==1)a["projectileSpeed"]=.5;
        if(scenario==2)a["projectileSpeed"]=121;
        if(scenario==3)a["projectileGravity"]=-1;
        if(scenario==4)a["projectileGravity"]=31;
        if(scenario==5)a["projectileRadius"]=.09;
        if(scenario==6)a["projectileRadius"]=5.1;
        if(scenario==7)a["projectileLifetimeMs"]=99;
        if(scenario==8)a["projectileLifetimeMs"]=10001;
        if(scenario==9){a["damage"]=0;a["repair"]=90;}
        if(scenario==10)kit["minPitch"]=-1.41;
        if(scenario==11)kit["maxPitch"]=1.41;
        if(scenario==12){kit["minPitch"]=.6;kit["maxPitch"]=.5;}
        if(scenario==13)kit["muzzleHeight"]=-.1;
        if(scenario==14)kit["muzzleHeight"]=20.1;
        if(scenario==15)a["range"]=61;
        if(scenario==16)a["projectileSpeed"]="fast";
        if(scenario==17)a["projectileGravity"]=nullptr;
        std::ofstream(invalidPath)<<j.dump();LocalGameplay rejected;std::string error;assert(!rejected.loadContent(invalidPath,error));
    }
    std::cout<<"PASS authored projectile speed/gravity/radius/lifetime/effect and kit aim/muzzle bounds\n";

    // Exercise the public command wrappers, actual sockets and production client decoders.
    Fixture network(path);LocalRealm hostRealm,guestRealm;auto& h=*hostRealm.impl_;auto& g=*guestRealm.impl_;
    h.state=LocalRealmState::Hosting;g.state=LocalRealmState::Connected;h.realmId=g.realmId=123;h.directory=temp;
    const auto content=network.game.sharedContent();h.gameplay.useContent(content);g.gameplay.useContent(content);
    h.self=network.driver;g.self=network.driver;g.self.guid=2;g.self.name="Gunner";g.self.vehicleSeat=1;g.self.vehicleControl=false;
    h.saved={{{1,11},h.self},{{2,22},g.self}};auto peerPlayer=g.self;h.gameplay.tick(0,{&h.self,&peerPlayer});
    assert(h.openSocket(0) && g.openSocket(0));g.host=loopback(h.port);g.session=987;
    LocalRealm::Impl::Peer peer;peer.guid=2;peer.identity={2,22};peer.address=loopback(g.port);peer.session=g.session;peer.loading=false;h.peers.push_back(peer);
    h.refreshPlayers();g.players=h.players;
    assert(guestRealm.aimVehicle(.2f,.3f));guestRealm.update(.01f);h.receive();
    for(const auto& packet:drain(g.socket))deliver(g,packet);
    assert(g.pendingCommands.empty() && h.peers[0].lastCommandSuccess && near(actor(h.gameplay,50).vehicleAim[1][0],.2f) && near(actor(h.gameplay,50).vehicleAim[1][1],.3f));
    assert(guestRealm.useVehicleAbility(0));guestRealm.update(.01f);h.receive();
    for(const auto& packet:drain(g.socket))deliver(g,packet);
    assert(g.pendingCommands.empty() && h.peers[0].lastCommandSuccess && h.gameplay.vehicleProjectiles().size()==1 && actor(h.gameplay,50).vehiclePower==90 && actor(h.gameplay,51).health==1000);
    h.progress(h.peers[0]);h.history(h.peers[0]);h.world(h.peers[0],1);h.projectileDeck(h.peers[0],1);
    for(const auto& packet:drain(g.socket))deliver(g,packet);
    const auto initial=guestRealm.vehicleProjectiles();assert(initial.size()==1 && initial[0].ownerGuid==2 && initial[0].sourceGuid==network.hull && initial[0].spellId==9101);
    assert(near(actor(g.gameplay,50).vehicleAim[1][0],.2f) && near(actor(g.gameplay,50).vehicleAim[1][1],.3f) && actor(g.gameplay,50).vehiclePower==90);
    assert(!initial[0].sourceEpoch && !initial[0].damage && !initial[0].radius && !initial[0].maxRange);

    // Failed frames leave the complete previous deck and its sequence intact.
    const auto sendFrame=[&](int scenario){
        Writer w;w.u32(scenario==0?1:2);w.u32(g.self.mapId);w.u32(g.self.instanceId);
        w.u32(g.self.positionRevision+(scenario==1));w.u32(scenario==2?2:g.self.phaseMask);w.u8(scenario==3?2:scenario==4?17:1);
        auto p=initial[0];if(scenario==5)p.spellId=999999;if(scenario==6)p.phaseMask=2;if(scenario==7)p.vx=std::numeric_limits<float>::quiet_NaN();
        writeVehicleProjectile(w,p);if(scenario==3)writeVehicleProjectile(w,p);if(scenario==8)w.bytes.pop_back();if(scenario==9)w.u8(0);
        h.send(Message::VehicleProjectiles,g.session,w,peer.address);
        for(const auto& packet:drain(g.socket))deliver(g,packet);
        assert(g.projectileSequence==1 && guestRealm.vehicleProjectiles().size()==1 && guestRealm.vehicleProjectiles()[0].id==initial[0].id);
    };
    for(int scenario=0;scenario<10;++scenario)sendFrame(scenario);
    h.gameplay.tick(.1f,{&h.self,&h.findSaved(2)->player});h.projectileDeck(h.peers[0],2);
    for(const auto& packet:drain(g.socket))deliver(g,packet);
    assert(g.projectileSequence==2 && guestRealm.vehicleProjectiles().size()==1 && guestRealm.vehicleProjectiles()[0].x>initial[0].x);

    // Replayed/gapped/truncated/wrong-session aim packets cannot overwrite authority aim.
    const auto sendAim=[&](uint32_t id,bool truncated=false,uint64_t token=987){
        Writer w;w.u32(id);w.u8(uint8_t(LocalAction::VehicleAim));w.u64(network.hull);w.u32(1);
        w.u32(0);w.u32(0);w.u32(0);w.u64(network.hull);w.f32(-.2f);w.f32(-.3f);
        if(truncated)w.bytes.pop_back();g.send(Message::Command,token,w,g.host);h.receive();drain(g.socket);
    };
    sendAim(2);sendAim(4);sendAim(3,true);sendAim(3,false,444);
    assert(h.peers[0].lastCommand==2 && near(actor(h.gameplay,50).vehicleAim[1][0],.2f) && actor(h.gameplay,50).vehiclePower==90);
    assert(guestRealm.exitVehicle());guestRealm.update(.01f);h.receive();
    for(const auto& packet:drain(g.socket))deliver(g,packet);
    assert(h.peers[0].lastCommand==3 && h.peers[0].lastCommandSuccess);
    h.gameplay.tick(.01f,{&h.self,&h.findSaved(2)->player});assert(h.gameplay.vehicleProjectiles().empty());
    h.progress(h.peers[0]);h.history(h.peers[0]);h.projectileDeck(h.peers[0],3);
    for(const auto& packet:drain(g.socket))deliver(g,packet);
    assert(g.projectileSequence==3 && guestRealm.vehicleProjectiles().empty());
    std::cout<<"PASS real UDP aim/fire/exit commands, per-seat aim and projectile snapshots, launch removal, atomic bad-frame rejection and command replay guards\n";

    for(int scenario=0;scenario<10;++scenario){
        auto p=initial[0];if(scenario==0)p.id=0;if(scenario==1)p.remainingMs=10001;if(scenario==2)p.sourceGuid=1;
        if(scenario==3)p.sourceGuid|=uint64_t(1)<<32;if(scenario==4)p.ownerGuid=0;if(scenario==5)p.x=100001;
        if(scenario==6)p.vz=421;if(scenario==7)p.gravity=31;if(scenario==8)p.spellId=999999;
        Writer w;writeVehicleProjectile(w,p);if(scenario==9)w.bytes.pop_back();Reader r(w.bytes.data(),w.bytes.size());readVehicleProjectile(r,0,0,*content);assert(!r.valid);
    }
    Writer encoded;writeVehicleProjectile(encoded,initial[0]);Reader reader(encoded.bytes.data(),encoded.bytes.size());const auto decoded=readVehicleProjectile(reader,0,0,*content);
    assert(reader.done() && encoded.bytes.size()==VehicleProjectileWireBytes && decoded.id==initial[0].id && HeaderSize+21+16*VehicleProjectileWireBytes<=MaxPacket);
    std::cout<<"PASS strict projectile decoder and maximum single-datagram bound; Save"<<int(SaveVersion)<<", LAN"<<int(Version)<<"\n";
    guestRealm.stop();hostRealm.stop();
    std::filesystem::remove_all(temp);
}
