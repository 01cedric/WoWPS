// Original SmartAI creature speech: rule selection, chance/once/evade
// semantics, listen range, placeholders, guid-script precedence and the
// production companion.
#define private public
#include "game/local_gameplay.hpp"
#undef private
#include "../../src/game/local_gameplay.cpp"
#include <cassert>
#include <filesystem>
#include <iostream>

using namespace wowee::game;

static LocalRealmNpc* spawnNear(LocalGameplay& game,LocalRealmPlayer& p,uint32_t entry,float x,float y,float z) {
    p.mapId=0;p.x=x;p.y=y;p.z=z;for(int i=0;i<6;++i)game.tick(.25f,{&p}); // region streaming runs once a second
    for(auto& npc:game.impl_->npcs)if(npc.entry==entry&&!npc.dead)return &npc;
    return nullptr;
}

int main(int argc,char** argv) {
    assert(argc==2);std::string error;LocalGameplay game;
    if(!game.loadContent((std::filesystem::path(argv[1])/"world.json").string(),error)){std::cerr<<error<<"\n";return 1;}
    game.seedGameObjectRandom(335);
    auto& g=*game.impl_;const auto& c=game.content();
    assert(c.creatureTalk.size()==1678 && !c.creatureGuidScripts.empty());
    assert(std::is_sorted(c.creatureTalk.begin(),c.creatureTalk.end(),[](const auto& a,const auto& b){return a.owner<b.owner;}));
    std::cout<<"PASS production companion: 1678 reviewed TALK/FLEE rules, guid-script roster\n";

    LocalRealmPlayer p;p.guid=5;p.name="Arthas";game.initializePlayer(p,true);p.race=1;p.classId=2;p.gender=0;
    assert(localExpandCreatureText("Hail, $n the $r $c! $gBrother:Sister;!",&p)=="Hail, Arthas the Human Paladin! Brother!");
    p.gender=1;assert(localExpandCreatureText("All hail, $n, the new $G King : Queen;",&p)=="All hail, Arthas, the new Queen");
    p.gender=0;assert(localExpandCreatureText("$n",nullptr)=="$n");
    std::cout<<"PASS $n/$r/$c/$g placeholder expansion\n";

    // Kobold Vermin (6): 30 % NOT_REPEATABLE aggro line.
    const auto* rule=&*std::find_if(c.creatureTalk.begin(),c.creatureTalk.end(),[](const auto& r){return r.owner==6;});
    assert(rule->event==LocalCreatureTalkEvent::Aggro && rule->chance==30 && rule->once && rule->lines.size()>=2);
    LocalRealmNpc vermin;vermin.guid=0xF130000000000001ULL;vermin.entry=6;vermin.name="Kobold Vermin";vermin.mapId=0;vermin.x=1;vermin.y=0;vermin.z=0;
    p.mapId=0;p.x=p.y=p.z=0;
    int spoke=0;
    for(int i=0;i<2000;++i) {
        vermin.talkOnceMask=0;const auto before=g.scriptDialogueRevision;
        const bool said=g.creatureTalk(vermin,LocalCreatureTalkEvent::Aggro,&p,0,{&p});
        assert(said==(g.scriptDialogueRevision!=before));
        if(said) {
            ++spoke;const auto& d=g.scriptDialogues.back();
            assert(d.speakerGuid==vermin.guid && d.viewerGuid==p.guid && d.chatType==kLocalChatMonsterSay);
            assert(std::any_of(rule->lines.begin(),rule->lines.end(),[&](const auto& l){return l.text==d.text;}));
            assert(!g.creatureTalk(vermin,LocalCreatureTalkEvent::Aggro,&p,0,{&p})); // NOT_REPEATABLE
        }
    }
    assert(spoke>520 && spoke<680);
    assert(g.scriptDialogues.size()==kLocalMaxScriptDialogues);
    assert(g.talkKeepOnEvadeMask(vermin)==0);
    std::cout<<"PASS event chance ~30 %, NOT_REPEATABLE, SendChat line choice and bounded history\n";

    // Listen range: a say is heard within 25 yards only; other maps never.
    vermin.talkOnceMask=0;LocalRealmPlayer far=p;far.guid=6;far.x=30;LocalRealmPlayer other=p;other.guid=7;other.mapId=1;
    const auto before=g.scriptDialogueRevision;
    for(int i=0;i<200 && g.scriptDialogueRevision==before;++i){vermin.talkOnceMask=0;g.creatureTalk(vermin,LocalCreatureTalkEvent::Aggro,&p,0,{&p,&far,&other});}
    assert(g.scriptDialogueRevision==before+1 && g.scriptDialogues.back().viewerGuid==p.guid);
    std::cout<<"PASS say listen range and map scope\n";

    // Quest reward talk with invoker target and quest filter (Gryan Stoutmantle, 166).
    LocalRealmNpc gryan;gryan.guid=0xF130000000000002ULL;gryan.entry=234;gryan.name="Gryan Stoutmantle";gryan.mapId=0;
    assert(!g.creatureTalk(gryan,LocalCreatureTalkEvent::QuestReward,&p,165,{&p}));
    assert(g.creatureTalk(gryan,LocalCreatureTalkEvent::QuestReward,&p,166,{&p}));
    assert(g.scriptDialogues.back().text.find("Arthas")!=std::string::npos && g.scriptDialogues.back().text.find('$')==std::string::npos);
    assert(g.creatureTalk(gryan,LocalCreatureTalkEvent::QuestReward,&p,166,{&p})); // repeatable row
    std::cout<<"PASS quest reward filter, invoker placeholder and repeatable reward line\n";

    // "%s" is the speaker's name in monster emotes.
    LocalRealmNpc emoter;emoter.guid=0xF130000000000003ULL;emoter.entry=2991;emoter.name="Greatmother Hawkwind";emoter.mapId=0;
    assert(g.creatureTalk(emoter,LocalCreatureTalkEvent::QuestAccept,&p,753,{&p}));
    assert(g.scriptDialogues.back().text.rfind("Greatmother Hawkwind gestures",0)==0);
    std::cout<<"PASS %s speaker substitution\n";

    // Guid scripts replace the entry script for that spawn (Defias Thug 80201).
    LocalRealmNpc thug;thug.entry=38;thug.spawnId=80201;
    auto [b,e]=g.talkRules(thug);assert(b!=e && b->owner==-80201);
    thug.spawnId=1;auto [b2,e2]=g.talkRules(thug);assert(b2!=e2 && b2->owner==38);
    std::cout<<"PASS guid-script precedence over entry scripts\n";

    // Integration: a real aggro edge in the authority tick speaks once.
    LocalRealmPlayer hero;hero.guid=9;hero.name="Hero";game.initializePlayer(hero,true);hero.level=1;
    const auto* spawn=[&]()->const LocalRealmNpc*{
        for(float dx:{0.f,60.f,-60.f})for(float dy:{0.f,60.f,-60.f})
            if(auto* found=spawnNear(game,hero,6,-8810.f+dx,-192.f+dy,82.f))return found;
        return nullptr;
    }();
    if(spawn) {
        const auto guid=spawn->guid;
        bool engaged=false;
        for(int attempt=0;attempt<50&&!engaged;++attempt) {
            auto* npc=g.npc(guid);if(!npc)break;
            hero.x=npc->x+1;hero.y=npc->y;hero.z=npc->z;hero.health=hero.maxHealth;hero.dead=false;
            g.addThreat(*npc,hero.guid,1); // the hero pulled it (Kobold Vermin is not aggressive)
            game.tick(.25f,{&hero});npc=g.npc(guid);engaged=npc&&npc->talkEngaged;
        }
        if(!engaged){auto* npc=g.npc(guid);std::cerr<<"npc "<<(npc?npc->targetGuid:0)<<" hostile="<<(npc&&npc->hostile)<<" aggr="<<(npc&&npc->aggressive)<<" dead="<<(npc&&npc->dead)<<" hero="<<hero.x<<","<<hero.y<<" lvl "<<int(hero.level)<<"\n";}
        assert(engaged);
        std::cout<<"PASS authority aggro edge arms creature speech\n";
    } else std::cout<<"SKIP aggro integration: no Kobold Vermin spawn streamed near the probe\n";

    // UPDATE_OOC timer: Farmer Furlbrow speaks after exactly 10 s, then every 1 202 s.
    LocalRealmNpc farmer;farmer.guid=0xF130000000000004ULL;farmer.entry=237;farmer.name="Farmer Furlbrow";farmer.mapId=0;
    p.x=p.y=p.z=0;const auto quiet=g.scriptDialogueRevision;
    assert(!g.runSmartTimers(farmer,9999,nullptr,{&p},[](const auto&){assert(false);}));assert(g.scriptDialogueRevision==quiet);
    assert(g.runSmartTimers(farmer,1,nullptr,{&p},[](const auto&){assert(false);}));assert(g.scriptDialogueRevision==quiet+1);
    assert(!g.runSmartTimers(farmer,1201999,nullptr,{&p},[](const auto&){}));
    assert(g.runSmartTimers(farmer,1,nullptr,{&p},[](const auto&){}));
    farmer.targetGuid=p.guid;assert(!g.runSmartTimers(farmer,5000000,&p,{&p},[](const auto&){})); // OOC only
    std::cout<<"PASS UPDATE_OOC initial/repeat timers pause in combat\n";

    // HEALTH_PCT 0-15 % FLEE_FOR_ASSIST (Murloc, NOT_REPEATABLE, with emote).
    {
        LocalRealmPlayer hunter;hunter.guid=11;hunter.name="Hunter";game.initializePlayer(hunter,true);hunter.level=10;
        auto* runt=spawnNear(game,hunter,285,-9473.71f,-431.412f,59.8586f);assert(runt);
        const auto guid=runt->guid;
        const LocalRealmNpc* helper=nullptr;
        for(const auto& m:g.npcs)if((m.entry==285||m.entry==735)&&m.guid!=guid&&!m.dead&&distance2(runt->x,runt->y,runt->z,m.x,m.y,m.z)<=30*30){helper=&m;break;}
        hunter.x=runt->x+1;hunter.y=runt->y;hunter.z=runt->z;
        g.addThreat(*runt,hunter.guid,1);game.tick(.1f,{&hunter});runt=g.npc(guid);
        if(!runt||runt->targetGuid!=hunter.guid)std::cerr<<"runt "<<(runt?runt->targetGuid:0)<<" dead="<<(runt&&runt->dead)<<" hunter dead="<<hunter.dead<<" hp="<<hunter.health<<" guid="<<hunter.guid<<" canAttack="<<(runt&&game.canAttack(hunter,*runt))<<" faction="<<(runt?game.content().npc(runt->entry)->faction:0)<<" hostile="<<(runt&&runt->hostile)<<" dist="<<(runt?std::sqrt(distance2(runt->x,runt->y,runt->z,runt->homeX,runt->homeY,runt->homeZ)):0)<<"\n";
        assert(runt&&runt->targetGuid==hunter.guid);
        runt->health=std::max(1u,(runt->maxHealth*3+99)/100);const auto before=g.scriptDialogueRevision;
        hunter.health=hunter.maxHealth;game.tick(.1f,{&hunter});runt=g.npc(guid);
        assert(runt->fleeMode==1||runt->fleeMode==2);
        assert(g.scriptDialogueRevision>before && g.scriptDialogues.back().chatType==kLocalChatMonsterEmote &&
               g.scriptDialogues.back().text=="Murloc attempts to run away in fear!");
        assert(runt->fleeMode==(helper?1:2));
        bool assisted=false;
        for(int i=0;i<200 && runt->fleeMode;++i) {
            hunter.health=hunter.maxHealth;hunter.dead=false;game.tick(.1f,{&hunter});runt=g.npc(guid);if(!runt)break;
            if(helper)for(const auto& m:g.npcs)if(m.assistTargetGuid==hunter.guid||(m.guid!=guid&&m.targetGuid==hunter.guid))assisted=true;
        }
        assert(runt && !runt->fleeMode && runt->targetGuid==hunter.guid);
        assert(!helper||assisted);
        // NOT_REPEATABLE: still under 15 % but no second flight.
        const auto emotes=g.scriptDialogueRevision;hunter.health=hunter.maxHealth;game.tick(.1f,{&hunter});runt=g.npc(guid);
        assert(runt && !runt->fleeMode && g.scriptDialogueRevision==emotes);
        std::cout<<"PASS HEALTH_PCT flee for assistance: emote, seek/timed flight, CallAssistance and resume; once only"<<(helper?"":" (no helper in range)")<<"\n";
    }

    LocalScriptDialogue bad{1,1,1,0,0,"x",13};assert(!game.setRemoteScriptDialogues({bad}));
    bad.chatType=kLocalChatMonsterYell;assert(game.setRemoteScriptDialogues({bad}));
    std::cout<<"PASS dialogue chat-type validation\n";
}
