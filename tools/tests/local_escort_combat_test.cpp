#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace wowee::game;
static LocalRealmNpc& actor(LocalGameplay& g,uint32_t entry) {
    for(const auto& n:g.npcs())if(n.entry==entry)return const_cast<LocalRealmNpc&>(n);
    std::abort();
}
static LocalRealmPlayer begin(LocalGameplay& g,const char* path) {
    std::string error;assert(g.loadContent(path,error));LocalRealmPlayer p;p.guid=1;p.name="Escort";
    g.initializePlayer(p,true);g.tick(0,{&p});const auto guide=actor(g,50).guid;
    assert(g.execute(p,{LocalAction::AcceptQuest,guide,2},{&p},error));
    assert(g.execute(p,{LocalAction::AcceptQuest,guide,1},{&p},error));g.tick(0,{&p});return p;
}
static void nearEnemy(LocalGameplay& g,float x=4) {auto& n=actor(g,51);n.x=n.homeX=x;n.y=n.homeY=n.z=n.homeZ=0;}
static LocalCollisionTile wall() {
    LocalCollisionTile t;t.mapId=0;t.tileX=t.tileY=32;t.vertices={{2,-10,-10},{2,10,-10},{2,10,10},{2,-10,10}};
    t.triangles={{0,1,2,0},{0,2,3,0}};t.buildTree();return t;
}
int main(int argc,char** argv) {
    assert(argc==2);std::string error;
    {
        LocalGameplay g;auto p=begin(g,argv[1]);const auto guide=actor(g,50).guid,enemy=actor(g,51).guid;
        nearEnemy(g);p.x=-4;p.escort.waitMs=500;const auto ownerHealth=p.health;
        g.tick(.25f,{&p});
        assert(actor(g,51).targetGuid==guide && actor(g,50).health==90 && actor(g,51).health==20);
        assert(p.health==ownerHealth && p.escort.guideHealth==90 && p.escort.waitMs==500 && p.escort.nextPoint==0);
        for(int i=0;i<9 && !actor(g,51).dead;++i)g.tick(.25f,{&p});
        assert(actor(g,51).dead && p.xp==80 && p.quests[0].id==2 && p.quests[0].status==LocalQuestStatus::Complete);
        assert(p.escort.routeId==900 && p.escort.waitMs<500);
        bool damage=false,kill=false;for(const auto& e:g.combatEvents()) {
            if(e.source==guide && e.target==enemy && e.kind==LocalCombatEventKind::NpcMelee){damage=true;assert(!e.actorIsPlayer);}
            if(e.source==guide && e.target==enemy && e.kind==LocalCombatEventKind::Kill){kill=true;assert(!e.actorIsPlayer);}
        }
        assert(damage && kill);
        p.x=4;for(int i=0;i<30 && p.escort.routeId;++i)g.tick(.25f,{&p});
        assert(!p.escort.routeId && localScriptState(p,600)==1 && localScriptState(p,602)==2);
        std::cout<<"PASS guide acquisition, NPC identity/armor damage, owner rewards, combat wait pause and route completion\n";
    }
    {
        LocalGameplay g;auto p=begin(g,argv[1]);nearEnemy(g);p.x=-4;g.tick(.25f,{&p});const auto hp=p.escort.guideHealth;
        assert(hp<100);Writer w;writeProgress(w,p);auto restored=p;Reader r(w.bytes.data(),w.bytes.size());
        assert(readProgress(r,restored) && r.done() && restored.escort.guideHealth==hp);
        LocalGameplay reload;assert(reload.loadContent(argv[1],error));reload.initializePlayer(restored,false);reload.tick(0,{&restored});
        assert(actor(reload,50).health==hp && restored.escort.guideHealth==hp);
        g.tick(0,{});assert(!actor(g,50).escortOwner && actor(g,50).health==100);
        g.tick(0,{&p});assert(actor(g,50).health==hp);
        Writer old;writeProgress(old,p,40);Reader oldReader(old.bytes.data(),old.bytes.size());auto legacy=p;
        assert(readProgress(oldReader,legacy,40) && oldReader.done() && !legacy.escort.guideHealth);
        reload.initializePlayer(legacy,false);assert(legacy.escort.guideHealth==100);
        std::cout<<"PASS exact guide HP codec/reload and disconnect resume; legacy health migration\n";
    }
    {
        LocalGameplay g;auto p=begin(g,argv[1]);nearEnemy(g);p.x=-4;actor(g,50).health=1;p.escort.guideHealth=1;
        g.tick(.25f,{&p});assert(actor(g,50).dead && !p.escort.routeId && localScriptState(p,601)==1 && !localScriptState(p,600));
        g.tick(.25f,{&p});assert(localScriptState(p,601)==1);
        for(int i=0;i<13;++i)g.tick(.25f,{&p});assert(!actor(g,50).dead && !actor(g,50).escortOwner && actor(g,50).health==100);
        std::cout<<"PASS same-tick guide death failure, no completion/repeated failure, normal guide respawn\n";
    }
    {
        LocalGameplay g;auto p=begin(g,argv[1]);g.adoptCollisionTile(wall());
        for(int i=0;i<8;++i)g.tick(.25f,{&p});assert(actor(g,50).x<=2 && p.escort.nextPoint==0);
        const auto guide=actor(g,50).guid;nearEnemy(g,4);actor(g,51).targetGuid=guide;actor(g,51).threat[0]={guide,100};
        actor(g,50).x=p.escort.x=0;const auto hp=actor(g,50).health,enemyHp=actor(g,51).health;
        for(int i=0;i<8;++i)g.tick(.25f,{&p});
        assert(actor(g,50).x<2 && actor(g,51).x>2 && actor(g,50).health==hp && actor(g,51).health==enemyHp);
        p.phaseMask=2;g.tick(.25f,{&p});assert(!p.escort.routeId && localScriptState(p,601)==1 && !actor(g,50).escortOwner);
        std::cout<<"PASS installed-geometry route/chase/melee gates and phase-loss release\n";
    }
    {
        LocalGameplay g;auto p=begin(g,argv[1]);const auto guide=actor(g,50).guid;nearEnemy(g,20);p.x=-4;
        actor(g,51).targetGuid=p.guid;actor(g,51).threat[0]={p.guid,100};g.tick(.25f,{&p});
        assert(actor(g,50).x==0 && !actor(g,50).targetGuid); // Enemy outside defense leash; route still pauses for owner combat.
        auto inactive=p;inactive.guid=2;inactive.name="Other";inactive.escort={};inactive.quests.clear();
        assert(!g.execute(inactive,{LocalAction::AcceptQuest,guide,1},{&p,&inactive},error));
        std::cout<<"PASS bounded defense radius and shared-guide reservation during combat\n";
    }
    char directory[]="/tmp/wowps-escort-combat-XXXXXX";assert(mkdtemp(directory));
    const auto original=nlohmann::json::parse(std::ifstream(argv[1]));const auto path=std::string(directory)+"/world.json";
    for(int scenario=0;scenario<4;++scenario) {
        auto j=original;if(scenario==0)j["escortRoutes"][0]["combatChaseRadius"]=31;
        if(scenario==1)j["escortRoutes"][0]["combatChaseRadius"]=0;
        if(scenario==2)j["escortRoutes"][0]["combat"]=false;
        if(scenario==3)j["npcs"][0]["damage"]=0;
        std::ofstream(path)<<j.dump();LocalGameplay bad;assert(!bad.loadContent(path,error));
    }
    std::filesystem::remove_all(directory);
    std::cout<<"PASS combat content bounds; Save"<<int(SaveVersion)<<"/LAN"<<int(Version)<<"\n";
}
