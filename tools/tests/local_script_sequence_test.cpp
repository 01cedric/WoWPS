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
    assert(argc==2);std::string error;LocalGameplay game;
    assert(game.loadContent(argv[1],error));
    LocalRealmPlayer p;p.guid=1;p.name="Sequence";game.initializePlayer(p,true);
    game.tick(0,{&p});assert(!game.npcs().empty());
    assert(game.execute(p,{LocalAction::AcceptQuest,game.npcs()[0].guid,1},{&p},error));
    p.x=20;game.tick(.005f,{&p});
    assert(p.scriptAreaIds==std::vector<uint32_t>{800} && localScriptState(p,610)==1);
    assert(p.quests[0].progress[0]==1 && p.scriptTimers[0].remainingMs==100);
    p.x=22.25f;game.tick(.01f,{&p}); // Hysteresis retains the existing edge.
    assert(localScriptState(p,610)==1 && !localScriptState(p,611));
    Writer save;writeProgress(save,p);auto restored=p;restored.scriptAreaIds.clear();
    Reader read(save.bytes.data(),save.bytes.size());assert(readProgress(read,restored) && read.done());
    game.initializePlayer(restored,false);p=restored;game.tick(0,{&p});
    assert(localScriptState(p,610)==1 && p.scriptAreaIds==std::vector<uint32_t>{800});
    game.tick(.1f,{&p});
    assert(p.quests[0].status==LocalQuestStatus::Complete && localScriptState(p,613)==1);
    assert(p.scriptTimers.size()==1 && p.scriptTimers[0].timerId==902 && p.scriptTimers[0].remainingMs==100);
    assert(localScriptState(p,612)==0); // Newly scheduled action cannot run in the same tick.
    game.tick(.1f,{&p});assert(localScriptState(p,612)==1 && p.scriptTimers.empty());
    p.x=22.6f;game.tick(0,{&p});assert(p.scriptAreaIds.empty() && localScriptState(p,611)==1);
    p.x=22.25f;game.tick(0,{&p});assert(p.scriptAreaIds.empty()); // Not inside entry radius.
    p.x=20;game.tick(0,{&p});assert(localScriptState(p,610)==2);
    p.instanceId=7;game.tick(0,{&p});
    assert(p.scriptAreaInstanceId==7 && localScriptState(p,610)==3 && localScriptState(p,611)==2);
    p.phaseMask=2;game.tick(0,{&p});assert(p.scriptAreaIds.empty() && p.scriptTimers.empty());
    p.phaseMask=1;game.tick(0,{&p});assert(p.scriptAreaIds.size()==1);
    p.dead=true;p.health=0;game.tick(0,{&p});assert(p.scriptAreaIds.empty() && p.scriptTimers.empty());
    std::cout<<"PASS area enter/leave hysteresis, reload edge retention, instance/phase/death edges, chained quest completion\n";

    // One position sample is atomic across all areas and their event rows.
    LocalGameplay atomic;assert(atomic.loadContent(argv[1],error));auto content=atomic.sharedContent();
    content->scriptAreas.push_back({801,0,20,0,0,2,.5f,1,0});
    LocalScriptTrigger fail;fail.kind=LocalScriptTriggerKind::AreaEnter;fail.sourceId=801;
    fail.scriptId=620;fail.valueOp=LocalScriptValueOp::Add;fail.value=1;content->scriptTriggers.push_back(fail);
    LocalRealmPlayer q;q.guid=2;q.name="Atomic";atomic.initializePlayer(q,true);q.x=20;
    assert(localSetScriptState(q,620,INT32_MAX));const auto states=q.scriptStates;
    atomic.tick(0,{&q});assert(q.scriptAreaIds.empty() && q.scriptStates==states && q.scriptTimers.empty());
    localSetScriptState(q,620,0);atomic.tick(0,{&q});
    assert(q.scriptAreaIds==std::vector<uint32_t>({800,801}) && localScriptState(q,610)==1);
    std::cout<<"PASS atomic multi-area event rollback and retry without duplicate credit\n";

    // Simultaneous expiry: an earlier timer may cancel or restart a later ID.
    LocalGameplay scheduler;auto chain=std::make_shared<LocalWorldContent>();
    LocalScriptTimerAction first;first.timerId=1;first.scriptId=700;first.valueOp=LocalScriptValueOp::Add;first.value=1;first.cancelTimerId=2;
    LocalScriptTimerAction second;second.timerId=2;second.scriptId=701;second.valueOp=LocalScriptValueOp::Add;second.value=1;
    chain->scriptTimerActions={first,second};scheduler.useContent(chain);
    LocalRealmPlayer timed;timed.guid=3;timed.name="Timer";
    localSetScriptTimer(timed,1,10);localSetScriptTimer(timed,2,10);scheduler.tick(.02f,{&timed});
    assert(localScriptState(timed,700)==1 && !localScriptState(timed,701) && timed.scriptTimers.empty());
    chain->scriptTimerActions[0].cancelTimerId=0;chain->scriptTimerActions[0].scheduleTimerId=2;chain->scriptTimerActions[0].scheduleDelayMs=100;
    localSetScriptTimer(timed,1,10);localSetScriptTimer(timed,2,10);scheduler.tick(.02f,{&timed});
    assert(!localScriptState(timed,701) && timed.scriptTimers.size()==1 && timed.scriptTimers[0].remainingMs==100);
    scheduler.tick(.1f,{&timed});assert(localScriptState(timed,701)==1);
    chain->scriptTimerActions[0].scheduleTimerId=1;chain->scriptTimerActions[0].scheduleDelayMs=1;
    localSetScriptTimer(timed,1,1);const auto counter=localScriptState(timed,700);scheduler.tick(.25f,{&timed});
    assert(localScriptState(timed,700)==counter+1 && timed.scriptTimers.size()==1);
    std::cout<<"PASS simultaneous timer cancellation/restart, next-tick chaining and bounded self-rescheduling\n";

    // Old saves default to no area edges; malformed membership is rejected.
    auto encoded=q;encoded.instanceId=0;encoded.scriptAreaInstanceId=0;
    Writer old;writeProgress(old,encoded,38);auto migrated=encoded;
    Reader oldRead(old.bytes.data(),old.bytes.size());assert(readProgress(oldRead,migrated,38) && oldRead.done());
    assert(migrated.scriptAreaIds.empty() && !migrated.scriptAreaInstanceId);
    for(int scenario=0;scenario<3;++scenario) {
        auto bad=encoded;
        if(scenario==0)bad.scriptAreaIds={800,800};
        if(scenario==1)bad.scriptAreaIds={0};
        if(scenario==2){bad.scriptAreaIds.clear();bad.scriptAreaInstanceId=1;}
        Writer w;writeProgress(w,bad);Reader r(w.bytes.data(),w.bytes.size());LocalRealmPlayer target;
        assert(!readProgress(r,target));
    }
    // The owner-progress reader used over LAN consumes the same Save39 fields.
    Writer wire;writeProgress(wire,encoded);Reader wr(wire.bytes.data(),wire.bytes.size());auto remote=encoded;
    assert(readProgress(wr,remote) && wr.done() && remote.scriptAreaIds==encoded.scriptAreaIds);
    std::cout<<"PASS Save38 migration, Save39/LAN94 area codec and malformed membership rejection\n";

    const auto original=nlohmann::json::parse(std::ifstream(argv[1]));
    char directory[]="/tmp/wowps-area-content-XXXXXX";assert(mkdtemp(directory));
    const auto path=std::string(directory)+"/world.json";
    for(int scenario=0;scenario<6;++scenario) {
        auto json=original;
        if(scenario==0)json["scriptAreas"][0]["radius"]=0;
        if(scenario==1)json["scriptTriggers"][1]["sourceId"]=999;
        if(scenario==2)json["scriptTimers"][0]["scheduleTimerId"]=999;
        if(scenario==3)json["scriptTimers"][0]["scheduleDelayMs"]=0;
        if(scenario==4)json["scriptAreas"].push_back(json["scriptAreas"][0]);
        if(scenario==5)json["scriptTimers"][0]["cancelTimerId"]=902;
        std::ofstream(path)<<json.dump();LocalGameplay rejected;assert(!rejected.loadContent(path,error));
    }
    std::filesystem::remove_all(directory);
    std::cout<<"PASS area geometry/ID validation and chained-timer reference/ambiguity validation\n";
}
