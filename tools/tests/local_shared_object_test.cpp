#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <nlohmann/json.hpp>
using namespace wowee::game;
namespace net=wowee::net;
static uint32_t items(const LocalRealmPlayer& p,uint32_t id){uint32_t count=0;for(const auto& s:p.inventory)if(s.itemId==id)count+=s.count;return count;}
static bool same(const LocalGameObjectState& a,const LocalGameObjectState& b){return a.id==b.id && a.revision==b.revision && a.status==b.status && a.remainingMs==b.remainingMs;}
static LocalRealmCommand use(uint32_t id,uint32_t revision=1,uint32_t desired=0){LocalRealmCommand c{LocalAction::UseGameObject,localGameObjectGuid(id),id};c.bid=revision;c.buyout=desired;return c;}
struct Fixture {
    LocalGameplay game;LocalRealmPlayer a,b;std::string error;
    explicit Fixture(const char* path){
        assert(game.loadContent(path,error));a.guid=1;a.name="First";game.initializePlayer(a,true);a.inventory.clear();a.money=0;
        b=a;b.guid=2;b.name="Second";game.tick(0,{&a,&b});
    }
    const LocalGameObjectState& state(uint32_t id)const{const auto* p=game.gameObjectState(id);assert(p);return *p;}
    bool execute(LocalRealmPlayer& p,const LocalRealmCommand& c){return game.execute(p,c,{&a,&b},error);}
    void tick(float dt){game.tick(dt,{&a,&b});}
    void accept(LocalRealmPlayer& p,uint32_t quest){assert(execute(p,{LocalAction::AcceptQuest,game.npcs()[0].guid,quest}));}
    void wall(){LocalCollisionTile wall;wall.mapId=0;wall.tileX=wall.tileY=32;wall.vertices={{2,-10,-10},{2,10,-10},{2,10,10},{2,-10,10}};
        wall.triangles={{0,1,2,0},{0,2,3,0}};wall.buildTree();game.adoptCollisionTile(std::move(wall));}
};
static sockaddr_in loopback(uint16_t port){sockaddr_in a{};initAddress(a);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);return a;}
static std::vector<std::vector<uint8_t>> drain(socket_t socket){std::vector<std::vector<uint8_t>> packets;
    for(;;){std::array<uint8_t,MaxPacket+1> bytes{};const auto n=::recvfrom(socket,reinterpret_cast<char*>(bytes.data()),bytes.size(),net::datagramFlags(),nullptr,nullptr);
        if(n<0){assert(net::isWouldBlock(net::lastError()));break;}assert(size_t(n)<=MaxPacket);packets.emplace_back(bytes.begin(),bytes.begin()+n);}return packets;}
static void deliver(LocalRealm::Impl& guest,const std::vector<uint8_t>& bytes){Reader r(bytes.data(),bytes.size());assert(r.u32()==WireMagic && r.u8()==Version);
    const auto kind=Message(r.u8());assert(r.u16()==bytes.size());const auto seq=r.u32();const auto token=r.u64();guest.handleClient(kind,r,guest.host,token,seq);}
int main(int argc,char** argv){
    assert(argc==2);const auto path=argv[1];
    {
        Fixture f(path);f.accept(f.a,1);f.accept(f.b,1);const auto command=use(800,f.state(800).revision);
        assert(f.execute(f.a,command));assert(items(f.a,101)==2 && items(f.a,102)==1 && f.a.money==37);
        assert(f.a.quests[0].status==LocalQuestStatus::Complete && f.a.quests[0].progress[0]==2 && localScriptState(f.a,610)==1 && localScriptState(f.a,611)==1);
        assert(f.state(800).status==2 && f.state(800).revision==2 && f.state(800).remainingMs==1000);
        assert(!f.execute(f.b,command) && f.b.inventory.empty() && !f.b.money && f.b.quests[0].progress[0]==0);
        assert(!f.execute(f.a,command) && items(f.a,101)==2 && f.a.money==37 && localScriptState(f.a,610)==1);
        assert(f.execute(f.b,use(805,f.state(805).revision)) && f.b.money==37 && items(f.b,101)==2);
        f.tick(.25f);f.tick(.25f);f.tick(.25f);assert(f.state(800).status==2);f.tick(.25f);f.tick(.01f);
        assert(f.state(800).status==0 && f.state(800).revision>2 && !f.state(800).remainingMs);
        assert(!f.execute(f.b,command) && f.b.money==37);assert(f.execute(f.b,use(800,f.state(800).revision)) && f.b.money==74);
    }
    std::cout<<"PASS shared single-winner chest claim, complete reward/collect quest/script credit, independent spawn and stale-generation refusal after respawn\n";

    for(int scenario=0;scenario<4;++scenario){
        Fixture f(path);f.accept(f.a,1);
        if(scenario==0){f.a.inventory={{101,19}};for(size_t i=1;i<LocalGameplay::MaxInventory;++i)f.a.inventory.push_back({105,1});}
        if(scenario==1){f.a.inventory={{101,18}};for(size_t i=1;i<LocalGameplay::MaxInventory;++i)f.a.inventory.push_back({105,1});}
        if(scenario==2)f.a.money=1000000000-36;
        if(scenario==3)localSetScriptState(f.a,611,INT32_MAX);
        const auto before=f.a;const auto state=f.state(800);assert(!f.execute(f.a,use(800,state.revision)));
        assert(f.a.inventory==before.inventory && f.a.money==before.money && f.a.scriptStates==before.scriptStates && f.a.quests[0].status==LocalQuestStatus::Active && same(f.state(800),state));
    }
    {
        Fixture f(path);localSetScriptState(f.a,610,INT32_MAX);const auto before=f.state(800);assert(!f.execute(f.a,use(800,before.revision)));
        assert(f.a.inventory.empty() && !f.a.money && same(f.state(800),before));
    }
    std::cout<<"PASS full/partially fitting bags, money overflow and late object/quest script failure preserve whole player and shared state\n";

    for(int scenario=0;scenario<15;++scenario){
        Fixture f(path);auto command=use(800);const auto state=f.state(800);
        if(scenario==0)f.a.x=-1.01f;if(scenario==1)f.a.z=4;if(scenario==2)f.a.mapId=1;if(scenario==3)f.a.instanceId=1;
        if(scenario==4)f.a.phaseMask=2;if(scenario==5){f.a.dead=true;f.a.health=0;}if(scenario==6)f.a.ghost=true;
        if(scenario==7)f.a.flight.active=true;if(scenario==8)f.a.transportEntry=1;if(scenario==9)f.a.castingSpellId=1;
        if(scenario==10)f.a.attackTarget=1;if(scenario==11)f.a.vehicleGuid=1;if(scenario==12)command.target^=uint64_t(1)<<32;
        if(scenario==13)command.bid=0;if(scenario==14)command.buyout=1;
        assert(!f.execute(f.a,command) && f.a.inventory.empty() && !f.a.money && same(f.state(800),state));
    }
    {Fixture f(path);f.a.x=-1;assert(f.execute(f.a,use(800)) && f.a.money==37);}
    {Fixture f(path);f.wall();const auto before=f.state(800);assert(!f.execute(f.a,use(800)) && f.a.inventory.empty() && same(f.state(800),before));}
    {
        Fixture f(path);assert(!f.execute(f.a,use(804)));f.accept(f.a,2);localSetScriptState(f.a,600,1);assert(!f.execute(f.a,use(804)));
        localSetScriptState(f.a,600,0);assert(f.execute(f.a,use(804)) && items(f.a,101)==1 && f.a.quests[0].status==LocalQuestStatus::Complete);
    }
    {
        Fixture f(path);assert(!f.execute(f.a,use(802)));f.a.professions={{186,24,75,0}};f.a.inventory={{104,1}};assert(!f.execute(f.a,use(802)));
        f.a.professions[0].current=25;f.a.inventory.clear();assert(!f.execute(f.a,use(802)));
        f.a.inventory={{104,1}};assert(f.execute(f.a,use(802)) && items(f.a,103)==3 && items(f.a,104)==1 && f.state(802).status==2);
        f.b.professions=f.a.professions;f.b.inventory={{104,1}};assert(!f.execute(f.b,use(802,1)) && !items(f.b,103));
    }
    std::cout<<"PASS authority range/vertical/phase/life/travel/combat/LOS gates, exact radius, quest/state gates and mining skill/tool enforcement\n";

    {
        Fixture f(path);assert(!f.execute(f.a,use(801,1,0)) && f.state(801).revision==1 && !localScriptState(f.a,620));
        assert(f.execute(f.a,use(801,1,1)) && f.state(801).status==1 && f.state(801).remainingMs==500 && localScriptState(f.a,620)==1);
        assert(!f.execute(f.b,use(801,1,0)) && f.state(801).status==1);
        assert(!f.execute(f.a,use(801,2,1)) && f.state(801).revision==2 && localScriptState(f.a,620)==1);
        assert(f.execute(f.b,use(801,2,0)) && f.state(801).status==0 && !f.state(801).remainingMs && localScriptState(f.b,620)==1);
        assert(f.execute(f.a,use(801,3,1)));f.tick(.25f);f.tick(.25f);f.tick(.01f);
        assert(f.state(801).status==0 && !f.state(801).remainingMs && f.state(801).revision==5 && localScriptState(f.a,620)==2);
        assert(f.execute(f.a,use(806,0)) && localScriptState(f.a,630)==1);assert(!f.execute(f.a,use(806,1)));
    }
    {
        Fixture f(path);assert(f.execute(f.a,use(800)));assert(f.execute(f.a,use(803)));f.tick(.25f);
        const auto saved=f.game.gameObjectStates();LocalGameplay restored;restored.useContent(f.game.sharedContent());assert(restored.restoreGameObjectStates(saved));
        assert(restored.gameObjectState(800)->remainingMs==f.state(800).remainingMs && restored.gameObjectState(803)->status==2 && !restored.gameObjectState(803)->remainingMs);
        auto p=f.a;p.x=5000;for(int i=0;i<2;++i)restored.tick(.25f,{&p});assert(restored.gameObjectState(800)->status==2);
        restored.tick(.25f,{&p});restored.tick(.01f,{&p});assert(restored.gameObjectState(800)->status==0 && restored.gameObjectState(803)->status==2);
        p.x=0;std::string error;assert(!restored.execute(p,use(803,restored.gameObjectState(803)->revision),{&p},error));
        auto broken=restored.gameObjectStates();broken.push_back(broken[0]);const auto before=*restored.gameObjectState(800);assert(!restored.restoreGameObjectStates(broken) && same(*restored.gameObjectState(800),before));
    }
    std::cout<<"PASS explicit shared door open/close/no-op/auto-close, legacy script compatibility, saved remaining time and permanent depletion outside streaming range\n";

    char temp[]="/tmp/wowps-shared-object-XXXXXX";assert(mkdtemp(temp));
    {
        Fixture f(path);f.accept(f.a,1);LocalRealm realm;auto& h=*realm.impl_;h.state=LocalRealmState::Hosting;h.realmId=123;h.directory=temp;
        h.gameplay.useContent(f.game.sharedContent());h.self=f.a;h.saved={{{1,11},h.self},{{2,22},f.b}};std::string error;
        const auto before=*h.gameplay.gameObjectState(800);h.directory=std::string(temp)+"/not-a-directory";std::ofstream(h.directory)<<"x";
        assert(!h.runCommand(h.self,use(800,before.revision),error));assert(error.find("not saved")!=std::string::npos);
        assert(h.self.inventory.empty() && !h.self.money && h.self.quests[0].status==LocalQuestStatus::Active && same(*h.gameplay.gameObjectState(800),before));
        h.directory=temp;assert(h.runCommand(h.self,use(800,before.revision),error));assert(h.self.money==37 && items(h.self,101)==2);
        LocalRealm::Impl loaded;loaded.gameplay.useContent(f.game.sharedContent());assert(loaded.parseSave(h.directory+"/realm.wprs"));
        assert(loaded.findSaved(1) && loaded.findSaved(1)->player.money==37 && items(loaded.findSaved(1)->player,101)==2 && loaded.gameplay.gameObjectState(800)->status==2);
        auto second=loaded.findSaved(2)->player;assert(!loaded.gameplay.execute(second,use(800,loaded.gameplay.gameObjectState(800)->revision),{&second},error) && second.inventory.empty());
        LocalRealm::Impl browsing;assert(browsing.parseSave(h.directory+"/realm.wprs") && browsing.findSaved(1));
        // A structurally valid checksum must not make an invalid world row partially replace a live realm.
        std::vector<uint8_t> bytes;assert(readFile(h.directory+"/realm.wprs",bytes,MaxSaveSize));
        const auto recordOffset=bytes.size()-4-GameObjectWireBytes;for(size_t i=0;i<4;++i)bytes[recordOffset+4+i]=0;
        Writer corrupted;corrupted.bytes=std::move(bytes);corrupted.bytes.resize(corrupted.bytes.size()-4);corrupted.u32(checksum(corrupted.bytes.data(),corrupted.bytes.size()));
        const auto badPath=std::string(temp)+"/bad.wprs";assert(atomicWrite(badPath,corrupted.bytes,false));
        const auto preserved=*loaded.gameplay.gameObjectState(800);assert(!loaded.parseSave(badPath));
        assert(same(*loaded.gameplay.gameObjectState(800),preserved) && loaded.findSaved(1)->player.money==37);
        // Produce the actual previous format: character progress40 and no world-object tail.
        Writer legacy;legacy.u32(SaveMagic);legacy.u8(40);legacy.u64(456);legacy.u16(1);legacy.u64(1);legacy.u64(11);
        writePlayer(legacy,f.a);writeProgress(legacy,f.a,40);writeAppearance(legacy,f.a);legacy.u32(uint32_t(f.a.completedQuestIds.size()));
        for(auto id:f.a.completedQuestIds)legacy.u32(id);legacy.u8(f.a.introSeen?1:0);writeBuyback(legacy,f.a.buybackSerial,f.a.buyback);
        legacy.u8(0);legacy.u64(0);legacy.u16(0);legacy.u16(0);legacy.u16(0);legacy.u32(1);legacy.u32(1);legacy.u16(0);legacy.u8(0);
        legacy.u32(checksum(legacy.bytes.data(),legacy.bytes.size()));const auto legacyPath=std::string(temp)+"/legacy40.wprs";assert(atomicWrite(legacyPath,legacy.bytes,false));
        LocalRealm::Impl migrated;migrated.gameplay.useContent(f.game.sharedContent());assert(migrated.parseSave(legacyPath));
        assert(migrated.gameplay.gameObjectState(800)->status==0 && migrated.gameplay.gameObjectState(800)->revision==1 && migrated.findSaved(1)->player.money==0);
        {   // Save44 (2.26) rows for objects the installed content no longer keeps
            // shared state for are dropped by migration; Save45 stays strict.
            auto changed=std::make_shared<LocalWorldContent>(*f.game.sharedContent());
            for(auto& object:changed->gameObjects)if(object.id==800){object.kind=LocalGameObjectKind::Decorative;object.loot.clear();object.money=0;object.respawnMs=0;}
            LocalRealm::Impl strict;strict.gameplay.useContent(changed);assert(!strict.parseSave(h.directory+"/realm.wprs"));
            std::vector<uint8_t> current;assert(readFile(h.directory+"/realm.wprs",current,MaxSaveSize));assert(current[4]==SaveVersion);
            Writer older;older.bytes=std::move(current);older.bytes[4]=44;older.bytes.resize(older.bytes.size()-4);older.u32(checksum(older.bytes.data(),older.bytes.size()));
            const auto olderPath=std::string(temp)+"/save44.wprs";assert(atomicWrite(olderPath,older.bytes,false));
            LocalRealm::Impl old;old.gameplay.useContent(changed);assert(old.parseSave(olderPath));
            assert(!old.gameplay.gameObjectState(800) && old.findSaved(1)->player.money==37);
        }
        realm.stop();
    }
    std::cout<<"PASS failed durable transaction restores shared state and reward/quest state; successful retry and full/content-free save reload preserve single claim; Save44 stateless-row migration\n";

    const auto original=nlohmann::json::parse(std::ifstream(path));const auto invalidPath=std::string(temp)+"/invalid.json";
    for(int scenario=0;scenario<11;++scenario){auto j=original;auto& chest=j["gameObjects"][0];auto& resource=j["gameObjects"][2];
        if(scenario==0)chest["kind"]="unknown";if(scenario==1)chest["respawnMs"]=86400001;if(scenario==2)chest["loot"][0]["itemId"]=999;
        if(scenario==3)chest["loot"][0]["count"]=0;if(scenario==4)chest["loot"][0]["count"]=65536;
        if(scenario==5)j["gameObjects"][1]["loot"]=chest["loot"];if(scenario==6)resource["toolItemId"]=999;
        if(scenario==7)resource["requiredSkillId"]=0;if(scenario==8)chest["requiredSkillId"]=186;
        if(scenario==9)chest["money"]=1000000001;if(scenario==10)chest["loot"][0]["count"]=-1;
        std::ofstream(invalidPath)<<j.dump();LocalGameplay invalid;std::string error;assert(!invalid.loadContent(invalidPath,error));
    }
    std::cout<<"PASS strict shared kind/loot/respawn/reward and resource requirement references\n";
    {
        Fixture f(path);f.accept(f.a,1);f.accept(f.b,1);auto content=f.game.sharedContent();
        const auto sample=*content->gameObject(805);
        for(uint32_t id=900;id<1028;++id){auto object=sample;object.id=id;content->gameObjects.push_back(std::move(object));}
        LocalRealm hostRealm,firstRealm,secondRealm;auto& h=*hostRealm.impl_;auto& g=*firstRealm.impl_;auto& j=*secondRealm.impl_;
        h.state=LocalRealmState::Hosting;g.state=j.state=LocalRealmState::Connected;h.realmId=g.realmId=j.realmId=123;h.directory=temp;
        h.gameplay.useContent(content);g.gameplay.useContent(content);j.gameplay.useContent(content);h.self=f.a;g.self=f.b;j.self=f.b;j.self.guid=3;j.self.name="Third";
        h.saved={{{1,11},h.self},{{2,22},g.self},{{3,33},j.self}};
        h.gameplay.tick(0,{&h.self,&h.saved[1].player,&h.saved[2].player});assert(h.openSocket(0) && g.openSocket(0) && j.openSocket(0));
        g.host=j.host=loopback(h.port);g.session=987;j.session=654;
        for(auto* guest:{&g,&j}){LocalRealm::Impl::Peer peer;peer.guid=guest->self.guid;peer.identity=guest==&g?LocalRealm::Impl::Identity{2,22}:LocalRealm::Impl::Identity{3,33};
            peer.address=loopback(guest->port);peer.session=guest->session;peer.loading=false;h.peers.push_back(peer);}
        h.refreshPlayers();g.players=j.players=h.players;
        for(auto& peer:h.peers){h.progress(peer);h.history(peer);}
        for(const auto& packet:drain(g.socket))deliver(g,packet);for(const auto& packet:drain(j.socket))deliver(j,packet);
        assert(!firstRealm.useGameObject(800));
        h.gameObjectDeck(h.peers[0],1);auto pages=drain(g.socket);assert(pages.size()==3);
        deliver(g,pages.back());deliver(g,pages.back());assert(!g.gameObjectsReady && !firstRealm.gameObjectState(800));
        for(size_t i=0;i+1<pages.size();++i)deliver(g,pages[i]);
        h.gameObjectDeck(h.peers[1],1);pages=drain(j.socket);for(auto it=pages.rbegin();it!=pages.rend();++it)deliver(j,*it);
        h.gameObjectTick=1;assert(firstRealm.gameObjectState(800)->revision==1 && secondRealm.gameObjectState(800)->revision==1);
        // Both clients observed the same Ready revision before either command was received.
        assert(firstRealm.useGameObject(800) && secondRealm.useGameObject(800));firstRealm.update(.01f);secondRealm.update(.01f);h.receive();
        for(const auto& packet:drain(g.socket))deliver(g,packet);for(const auto& packet:drain(j.socket))deliver(j,packet);
        const auto* first=h.findSaved(2);const auto* second=h.findSaved(3);assert(first && second);
        assert(first->player.money+second->player.money==37 && items(first->player,101)+items(second->player,101)==2);
        assert(h.peers[0].lastCommandSuccess!=h.peers[1].lastCommandSuccess && g.pendingCommands.empty() && j.pendingCommands.empty());
        assert(firstRealm.gameObjectState(800)->status==2 && secondRealm.gameObjectState(800)->status==2);
        const auto moneyFirst=first->player.money,moneySecond=second->player.money;
        for(int i=0;i<4;++i)h.gameplay.tick(.25f,{&h.self,&h.saved[1].player,&h.saved[2].player});h.gameplay.tick(.01f,{&h.self,&h.saved[1].player,&h.saved[2].player});
        assert(h.gameplay.gameObjectState(800)->status==0);
        const auto rawUse=[&](uint32_t id,bool truncated=false,uint64_t session=987){Writer w;w.u32(id);w.u8(uint8_t(LocalAction::UseGameObject));w.u64(localGameObjectGuid(800));w.u32(800);
            w.u32(h.gameplay.gameObjectState(800)->revision);w.u32(0);w.u32(0);w.u64(0);if(truncated)w.bytes.pop_back();g.send(Message::Command,session,w,g.host);h.receive();drain(g.socket);drain(j.socket);};
        rawUse(1);rawUse(3);rawUse(2,true);rawUse(2,false,444);
        assert(h.peers[0].lastCommand==1 && h.findSaved(2)->player.money==moneyFirst && h.findSaved(3)->player.money==moneySecond && h.gameplay.gameObjectState(800)->status==0);
        const auto previous=g.gameObjectSequence;const auto old=*firstRealm.gameObjectState(800);const auto states=h.gameplay.gameObjectStates();
        const size_t parts=(states.size()+GameObjectsPerPage-1)/GameObjectsPerPage;
        for(int scenario=0;scenario<10;++scenario){
            for(size_t part=0;part<parts;++part){
                const size_t begin=part*GameObjectsPerPage;size_t end=std::min(begin+GameObjectsPerPage,states.size());
                if(scenario==8 && part+1==parts)--end;
                Writer w;w.u32(uint32_t(100+2*scenario+(scenario==9 && part>0)));w.u32(g.self.mapId);w.u32(g.self.instanceId);
                w.u32(g.self.positionRevision+(scenario==1));w.u32(g.self.phaseMask);w.u8(uint8_t(part));w.u8(uint8_t(parts));w.u8(uint8_t(end-begin));
                for(size_t row=begin;row<end;++row){auto state=states[row];if(part==0 && row==0){
                    if(scenario==2)state.revision=0;if(scenario==4)state.id=999999;if(scenario==6){state.status=2;state.remainingMs=1001;}}
                    if(scenario==3 && row==1)state=states[0];if(scenario==5 && state.id==801)state.status=2;
                    writeGameObjectState(w,state);}
                if(scenario==0 && part==0)w.bytes.pop_back();if(scenario==7 && part==0)w.u8(0);
                h.send(Message::GameObjects,g.session,w,h.peers[0].address);
            }
            for(const auto& packet:drain(g.socket))deliver(g,packet);
            assert(g.gameObjectSequence==previous && same(*firstRealm.gameObjectState(800),old));
        }
        h.gameObjectDeck(h.peers[0],1000);pages=drain(g.socket);for(auto it=pages.rbegin();it!=pages.rend();++it)deliver(g,*it);
        assert(g.gameObjectSequence==1000 && firstRealm.gameObjectState(800)->status==0 && firstRealm.gameObjectStates().size()==states.size());
        h.gameObjectDeck(h.peers[0],999);for(const auto& packet:drain(g.socket))deliver(g,packet);assert(g.gameObjectSequence==1000);
        h.gameObjectTick=1000;assert(firstRealm.useGameObject(801));firstRealm.update(.01f);h.receive();
        for(const auto& packet:drain(g.socket))deliver(g,packet);for(const auto& packet:drain(j.socket))deliver(j,packet);
        assert(h.gameplay.gameObjectState(801)->status==1 && firstRealm.gameObjectState(801)->status==1 && secondRealm.gameObjectState(801)->status==1);
        ++g.self.positionRevision;assert(!firstRealm.gameObjectState(800) && !firstRealm.useGameObject(800));
        assert(HeaderSize+23+GameObjectsPerPage*GameObjectWireBytes<=MaxPacket);
        firstRealm.stop();secondRealm.stop();hostRealm.stop();
    }
    std::cout<<"PASS real two-guest UDP race, durable single reward/broadcast door state, command replay/session guards and atomic reordered/invalid/mixed-generation object decks\n";
    std::filesystem::remove_all(temp);
}
