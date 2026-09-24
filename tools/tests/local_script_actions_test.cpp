#include "game/local_gameplay.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace wowee::game;

static const LocalRealmNpc* actor(const LocalGameplay& game,uint32_t id) {
    for(const auto& npc:game.npcs())if(npc.scriptActorId==id)return &npc;
    return nullptr;
}

int main(int argc,char** argv) {
    assert(argc==2);std::string error;LocalGameplay game;assert(game.loadContent(argv[1],error));
    assert(LocalGameplay::validPendingScriptKills({{1,1,0,1}})); // Zero XP still carries kill/script credit.
    assert(!LocalGameplay::validPendingScriptKills({{0,1,1,1}}));
    assert(!LocalGameplay::validPendingScriptKills({{1,0,1,1}}));
    assert(!LocalGameplay::validPendingScriptKills({{1,1,1,0}}));
    assert(!LocalGameplay::validPendingScriptKills({{1,1,uint64_t(UINT32_MAX)+1,1}}));
    std::vector<LocalPendingScriptKill> oversized;
    for(size_t i=0;i<=LocalGameplay::MaxPendingScriptKills;++i)oversized.push_back({i+1,uint32_t(i+1),1,1});
    assert(!LocalGameplay::validPendingScriptKills(oversized));
    // Keep this transaction test independent of the authority's intentionally
    // random white-hit table.  A level-80 attacker against the level-1 fixture
    // has no miss/dodge/block slice, so the one lethal swing is deterministic.
    LocalRealmPlayer player;player.guid=1;player.name="Actions";game.initializePlayer(player,true,80);
    game.tick(0,{&player});
    const auto guide=std::find_if(game.npcs().begin(),game.npcs().end(),[](const auto& npc){return npc.entry==50;});
    assert(guide!=game.npcs().end());
    assert(game.execute(player,{LocalAction::AcceptQuest,guide->guid,1},{&player},error));
    assert(actor(game,100) && actor(game,100)->x==5);
    assert(game.scriptDialogues().size()==1 && game.scriptDialogues()[0].speakerGuid==actor(game,100)->guid);
    game.tick(.02f,{&player});
    assert(game.scriptDialogues().size()==2); // Timer action, authored order retained.

    // The whole action deck preflights before publication.
    const auto checkpoint=game.scriptActionCheckpoint();
    assert(!game.executeScriptActions({8,999},{&player},error));
    assert(!actor(game,200) && game.scriptDialogues()==checkpoint.dialogues);
    assert(game.executeScriptActions({8},{&player},error) && actor(game,200));
    game.tick(.02f,{&player});assert(!actor(game,200));
    assert(game.executeScriptActions({8},{&player},error) && actor(game,200)); // expired slot is reusable

    // Checkpoint/restore is the wider realm-save rollback hook.
    const auto beforeDialogue=game.scriptActionCheckpoint();
    assert(game.executeScriptActions({4},{&player},error));
    assert(game.scriptDialogues().size()==beforeDialogue.dialogues.size()+1);
    game.restoreScriptActionCheckpoint(beforeDialogue);
    assert(game.scriptDialogues()==checkpoint.dialogues);

    // NpcKill and the resulting QuestComplete append one ordered transaction:
    // old actor despawns, completion actor spawns/engages, dialogue publishes.
    const auto target=std::find_if(game.npcs().begin(),game.npcs().end(),[](const auto& npc){return npc.entry==61;});
    assert(target!=game.npcs().end());const auto targetGuid=target->guid;
    assert(game.execute(player,{LocalAction::Attack,targetGuid},{&player},error));
    game.tick(3.f,{&player});
    if(actor(game,100)||!actor(game,101)||actor(game,101)->targetGuid!=player.guid)
        std::cerr<<"chain state actor100="<<(actor(game,100)!=nullptr)<<" actor101="<<(actor(game,101)!=nullptr)
                 <<" target="<<(actor(game,101)?actor(game,101)->targetGuid:0)<<" quest="<<int(player.quests[0].status)
                 <<" progress="<<player.quests[0].progress[0]<<" pending="<<game.scriptActionCheckpoint().pendingKills.size()<<"\n";
    assert(!actor(game,100) && actor(game,101) && actor(game,101)->targetGuid==player.guid);
    assert(player.quests.size()==1 && player.quests[0].status==LocalQuestStatus::Complete);
    assert(game.scriptDialogues().back().speakerGuid==player.guid && game.scriptDialogues().back().viewerGuid==player.guid);

    // Guest history replacement validates the complete deck before mutation.
    auto remote=game.scriptDialogues();assert(game.setRemoteScriptDialogues(remote));
    auto bad=remote;bad.back().revision=bad.front().revision;assert(!game.setRemoteScriptDialogues(bad));
    assert(game.scriptDialogues()==remote);assert(game.setRemoteScriptDialogues({})&&game.scriptDialogues().empty());

    // A saturated deferred-fact queue applies backpressure before publishing
    // an NPC death. Offline facts remain durable authority state rather than
    // being discarded at the stable boundary. Once those owners' facts are
    // explicitly resolved, the next lethal swing commits the complete chain.
    LocalGameplay pressure;assert(pressure.loadContent(argv[1],error));
    LocalRealmPlayer pressurePlayer;pressurePlayer.guid=2;pressurePlayer.name="Pressure";
    pressure.initializePlayer(pressurePlayer,true,80);pressure.tick(0,{&pressurePlayer});
    const auto pressureGuide=std::find_if(pressure.npcs().begin(),pressure.npcs().end(),[](const auto& npc){return npc.entry==50;});
    assert(pressureGuide!=pressure.npcs().end());
    assert(pressure.execute(pressurePlayer,{LocalAction::AcceptQuest,pressureGuide->guid,1},{&pressurePlayer},error));
    auto saturated=pressure.scriptActionCheckpoint();
    for(auto& npc:saturated.npcs)if(npc.scriptActorId==100)npc.scriptLifetimeMs=600000;
    for(size_t i=0;i<LocalGameplay::MaxPendingScriptKills;++i)
        saturated.pendingKills.push_back({1000+i,uint32_t(1000+i),0,1});
    pressure.restoreScriptActionCheckpoint(std::move(saturated));
    const auto pressureTarget=std::find_if(pressure.npcs().begin(),pressure.npcs().end(),[](const auto& npc){return npc.entry==61;});
    assert(pressureTarget!=pressure.npcs().end());const auto pressureTargetGuid=pressureTarget->guid;
    const auto xpBeforePressure=pressurePlayer.xp;
    pressurePlayer.attackTarget=pressureTargetGuid;pressurePlayer.attackTimer=0;
    pressure.tick(.02f,{&pressurePlayer});
    const auto refused=std::find_if(pressure.npcs().begin(),pressure.npcs().end(),[&](const auto& npc){return npc.guid==pressureTargetGuid;});
    assert(refused!=pressure.npcs().end()&&!refused->dead&&refused->health==1);
    assert(pressurePlayer.xp==xpBeforePressure&&actor(pressure,100));
    assert(pressurePlayer.quests[0].status==LocalQuestStatus::Active&&pressurePlayer.quests[0].progress[0]==0);
    assert(pressure.scriptActionCheckpoint().pendingKills.size()==LocalGameplay::MaxPendingScriptKills);
    auto resolved=pressure.scriptActionCheckpoint();resolved.pendingKills.clear();
    pressure.restoreScriptActionCheckpoint(std::move(resolved));
    assert(pressure.execute(pressurePlayer,{LocalAction::Attack,pressureTargetGuid},{&pressurePlayer},error));
    pressurePlayer.attackTimer=0;pressure.tick(.02f,{&pressurePlayer});
    const auto admitted=std::find_if(pressure.npcs().begin(),pressure.npcs().end(),[&](const auto& npc){return npc.guid==pressureTargetGuid;});
    assert(admitted!=pressure.npcs().end()&&admitted->dead);
    assert(pressurePlayer.quests[0].status==LocalQuestStatus::Complete&&!actor(pressure,100)&&actor(pressure,101));
    std::cout<<"PASS atomic actions, dialogue, timer, kill/completion chain, expiry reuse and rollback checkpoint\n";

    const auto original=nlohmann::json::parse(std::ifstream(argv[1]));
    char directory[]="/tmp/wowps-script-actions-XXXXXX";assert(mkdtemp(directory));
    const auto path=std::string(directory)+"/world.json";
    for(int scenario=0;scenario<5;++scenario) {
        auto json=original;
        if(scenario==0)json["scriptActions"][0]["lifetimeMs"]=0;
        if(scenario==1)json["scriptActions"][1]["text"]=std::string(256,'x');
        if(scenario==2)json["scriptTriggers"][0]["actionIds"]={1,1};
        if(scenario==3)json["scriptActions"][0]["npcEntry"]=999;
        if(scenario==4)json["scriptTimers"][0]["actionIds"]={999};
        std::ofstream(path)<<json.dump();LocalGameplay rejected;assert(!rejected.loadContent(path,error));
    }
    std::filesystem::remove_all(directory);
    std::cout<<"PASS lifetime/text/duplicate/order/reference content validation\n";
}
