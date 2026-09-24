#include "game/local_gameplay.hpp"
#include "game/local_quest_chain_json.hpp"
#include "game/local_quest_eligibility.hpp"
#include "game/local_quest_dialogue.hpp"
#include "game/local_quest_marker.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
using namespace wowee::game;
using Json=nlohmann::json;

int main(int argc,char** argv) {
    assert(argc==2 || argc==3);std::string error;LocalGameplay game;
    assert(game.loadContent(argv[1],error));
    LocalRealmPlayer base;base.guid=1;base.name="ChainTest";game.initializePlayer(base,true);
    game.tick(0,{&base});assert(game.npcs().size()==1);
    const auto npc=game.npcs()[0];
    const auto* quest=game.content().quest(10);assert(quest && quest->chainGate.defined && quest->prerequisite==999);
    const auto agrees=[&](LocalRealmPlayer player,uint32_t id,bool expected) {
        const auto* q=game.content().quest(id);assert(q);
        assert((!localQuestAcceptanceError(player,*q))==expected);
        assert(localQuestOffered(player,npc,*q)==expected);
        assert((localQuestMarkerStatus(player,npc,{*q})==QuestGiverStatus::AVAILABLE)==expected);
        assert(game.execute(player,{LocalAction::AcceptQuest,npc.guid,id},{&player},error)==expected);
    };
    agrees(base,10,false);
    auto p=base;p.completedQuestIds={1};agrees(p,10,true);
    p.completedQuestIds={2};agrees(p,10,false);
    p.completedQuestIds={2,3};agrees(p,10,true);
    p.completedQuestIds={999};agrees(p,10,false);
    p.completedQuestIds={1};p.level=21;agrees(p,10,false);
    p.level=20;agrees(p,10,true);
    std::cout<<"PASS original alternatives OR, negative-group AND, scalar replacement and maximum level agree in host/dialogue/marker\n";

    agrees(base,11,false);
    p=base;p.quests={{1,LocalQuestStatus::Active,{}}};agrees(p,11,true);
    p.quests[0].status=LocalQuestStatus::Complete;agrees(p,11,true);
    p.quests[0].status=LocalQuestStatus::Rewarded;agrees(p,11,true);
    p.quests.clear();p.completedQuestIds={1};agrees(p,11,true);
    p=base;agrees(p,12,true);
    p.quests={{2,LocalQuestStatus::Active,{}}};agrees(p,12,false);
    p.quests[0].status=LocalQuestStatus::Complete;agrees(p,12,false);
    p.quests.clear();p.completedQuestIds={2};agrees(p,12,false);
    p=base;p.quests={{3,LocalQuestStatus::Active,{}}};agrees(p,12,false);
    p.quests[0].status=LocalQuestStatus::Complete;agrees(p,12,false);
    p.quests.clear();p.completedQuestIds={3};agrees(p,12,true);
    agrees(base,13,false);agrees(base,14,false);
    std::cout<<"PASS active/complete/rewarded predecessor states, forward/reverse breadcrumb predicates and unsupported/missing metadata fail closed\n";

    auto legacy=*quest;legacy.chainGate={};legacy.prerequisite=1;
    assert(!localQuestChainSatisfied(base,legacy));p=base;p.completedQuestIds={1};assert(localQuestChainSatisfied(p,legacy));
    auto reputation=*quest;reputation.requiredMinRepFaction=72;reputation.requiredMinRepValue=100;
    assert(localQuestAcceptanceError(p,reputation));p.reputations={{72,100}};assert(!localQuestAcceptanceError(p,reputation));
    reputation.requiredMaxRepFaction=72;reputation.requiredMaxRepValue=100;
    assert(localQuestAcceptanceError(p,reputation));
    auto classGate=*quest;classGate.allowableClasses=2;assert(localQuestAcceptanceError(p,classGate));
    auto skill=*quest;skill.requiredSkill=164;assert(localQuestAcceptanceError(p,skill));
    // Existing accepted quests can still be handed in after gates become false.
    p=base;p.quests={{10,LocalQuestStatus::Complete,{2}}};assert(localQuestOffered(p,npc,*quest));
    assert(localQuestMarkerStatus(p,npc,{*quest})==QuestGiverStatus::REWARD);
    std::cout<<"PASS legacy compatibility, reputation/race-class/skill gates and already accepted hand-in visibility\n";

    const auto schema2=Json::parse(R"({"schemaVersion":2,"quests":[{"id":10,"alternatives":[[]],"orderedPrevious":[
        {"when":{"questId":1,"statusMask":8},"require":[{"questId":2,"statusMask":8}]},
        {"when":{"questId":3,"statusMask":8},"require":[]}]}]})");
    auto ordered=*quest;ordered.chainGate=parseLocalQuestChainCatalog(schema2).at(10);
    p=base;p.completedQuestIds={1,3};assert(!localQuestChainSatisfied(p,ordered));
    p.completedQuestIds={3};assert(localQuestChainSatisfied(p,ordered));
    p.completedQuestIds={1,2};assert(localQuestChainSatisfied(p,ordered));
    p.completedQuestIds.clear();assert(!localQuestChainSatisfied(p,ordered));
    std::cout<<"PASS ordered predecessor first-match rejection cannot be bypassed by a later rewarded alternative\n";

    auto playerRules=Json::parse(R"({"schemaVersion":2,"quests":[{"id":10,"alternatives":[[
        {"itemId":900,"count":3,"includeBank":true,"negated":false},
        {"factionId":72,"rankMask":16,"negated":false},
        {"spellId":54197,"negated":false}]]}]})");
    auto conditional=*quest;conditional.chainGate=parseLocalQuestChainCatalog(playerRules).at(10);
    p=base;p.inventory={{900,1}};p.bank={{900,2}};p.reputations={{72,3000}};p.knownSpells={54197};
    assert(!localQuestAcceptanceError(p,conditional));
    assert(localQuestOffered(p,npc,conditional));
    assert(localQuestMarkerStatus(p,npc,{conditional})==QuestGiverStatus::AVAILABLE);
    p.bank[0].count=1;assert(localQuestAcceptanceError(p,conditional));p.bank[0].count=2;
    p.reputations[0].standing=2999;assert(localQuestAcceptanceError(p,conditional));p.reputations[0].standing=3000;
    p.knownSpells.clear();assert(localQuestAcceptanceError(p,conditional));p.knownSpells={54197};
    for(size_t i=0;i<3;++i) {
        auto negated=playerRules;negated["quests"][0]["alternatives"][0][i]["negated"]=true;
        conditional.chainGate=parseLocalQuestChainCatalog(negated).at(10);
        assert(!localQuestChainSatisfied(p,conditional));
        auto missing=p;
        if(i==0)missing.bank.fill({});
        if(i==1)missing.reputations[0].standing=0;
        if(i==2)missing.knownSpells.clear();
        assert(localQuestChainSatisfied(missing,conditional));
    }
    std::cout<<"PASS inventory plus bank threshold, exact reputation-rank boundary, known spell and negation agree in admission/dialogue/marker\n";

    const auto rulesPath=std::filesystem::path(argv[1]).parent_path()/"quest_chain_rules.json";
    const auto original=Json::parse(std::ifstream(rulesPath));
    for(unsigned scenario=0;scenario<17;++scenario) {
        auto json=original;auto& row=json["quests"][0];
        switch(scenario) {
        case 0:json["schemaVersion"]=3;break;
        case 1:row["id"]=0;break;
        case 2:json["quests"].push_back(row);break;
        case 3:row["maxLevel"]=81;break;
        case 4:row["alternatives"][0][0]["statusMask"]=0;break;
        case 5:row["alternatives"][0][0]["statusMask"]=15;break;
        case 6:row["alternatives"][0][0]["questId"]=-1;break;
        case 7:row["alternatives"][0].push_back(row["alternatives"][0][0]);break;
        case 8:row["alternatives"]=Json::array();break;
        case 9:row["unsupportedReason"]="blocked";break;
        case 10:row["alternatives"].push_back(Json::array());break;
        case 11:row["alternatives"].push_back(row["alternatives"][0]);break;
        case 12:row["unknownRule"]=true;break;
        case 13:row["alternatives"][0][0]["extraRule"]=true;break;
        case 14:row["maxLevel"]=1.5;break;
        case 15:for(unsigned i=4;i<21;++i)row["alternatives"][0].push_back({{"questId",i},{"statusMask",8}});break;
        case 16:for(unsigned i=4;i<21;++i)row["alternatives"].push_back(Json::array({Json::array({{"questId",i},{"statusMask",8}})}));break;
        }
        bool rejected=false;try {parseLocalQuestChainCatalog(json);}catch(const std::exception&) {rejected=true;}
        assert(rejected);
    }
    std::cout<<"PASS 17 malformed companion schemas, duplicate/unknown fields, status masks and bounded expressions\n";
    for(unsigned scenario=0;scenario<12;++scenario) {
        auto json=schema2;auto& row=json["quests"][0];
        switch(scenario) {
        case 0:json["schemaVersion"]=1;break;
        case 1:row["orderedPrevious"]=Json::array();break;
        case 2:row["orderedPrevious"][0]["when"]["questId"]=0;break;
        case 3:row["orderedPrevious"][0]["when"]["statusMask"]=15;break;
        case 4:row["orderedPrevious"][0]["unknown"]=1;break;
        case 5:row["orderedPrevious"][0]["require"].push_back(row["orderedPrevious"][0]["when"]);break;
        case 6:row["orderedPrevious"].push_back(row["orderedPrevious"][0]);break;
        case 7:for(unsigned i=4;i<21;++i)row["orderedPrevious"].push_back({{"when",{{"questId",i},{"statusMask",8}}},{"require",Json::array()}});break;
        case 8:for(unsigned i=4;i<21;++i)row["orderedPrevious"][0]["require"].push_back({{"questId",i},{"statusMask",8}});break;
        case 9:json=playerRules;json["quests"][0]["alternatives"][0][0]["includeBank"]=1;break;
        case 10:json=playerRules;json["quests"][0]["alternatives"][0][1]["rankMask"]=256;break;
        case 11:json=playerRules;json["quests"][0]["alternatives"][0][2]["negated"]=1;break;
        }
        bool rejected=false;try {parseLocalQuestChainCatalog(json);}catch(const std::exception&) {rejected=true;}
        assert(rejected);
    }
    std::cout<<"PASS 12 malformed schema2 ordered/player predicates and size/type bounds\n";

    char directory[]="/tmp/wowps-quest-chain-XXXXXX";assert(mkdtemp(directory));
    const auto copiedWorld=std::filesystem::path(directory)/"world.json";
    const auto copiedRules=std::filesystem::path(directory)/"quest_chain_rules.json";
    std::filesystem::copy_file(argv[1],copiedWorld);
    LocalGameplay missing;assert(!missing.loadContent(copiedWorld.string(),error));
    std::filesystem::copy_file(rulesPath,copiedRules);
    LocalGameplay first;assert(first.loadContent(copiedWorld.string(),error));
    assert(first.content().fingerprint==game.content().fingerprint);
    auto changed=original;changed["quests"][0]["maxLevel"]=19;std::ofstream(copiedRules)<<changed.dump();
    LocalGameplay second;assert(second.loadContent(copiedWorld.string(),error));
    assert(second.content().fingerprint!=first.content().fingerprint);
    std::filesystem::remove_all(directory);
    std::cout<<"PASS required companion missing-file rejection and immutable rules included in LAN content fingerprint\n";

    if(argc==3) {
        LocalGameplay production;assert(production.loadContent(argv[2],error));
        const auto& content=production.content();assert(content.questChainCatalogRequired && content.catalog);
        size_t checked=0,lazy=0,blocked=0;
        for(const auto& entry:content.questChainGates) {
            const auto* q=content.quest(entry.first);assert(q && q->chainGate.defined);
            assert(q->chainGate.unsupportedReason==entry.second.unsupportedReason);
            assert(q->chainGate.alternatives.size()==entry.second.alternatives.size());
            ++checked;if(!q->chainGate.unsupportedReason.empty())++blocked;
            if(std::none_of(content.quests.begin(),content.quests.end(),[&](const auto& authored){return authored.id==q->id;}))++lazy;
        }
        assert(checked==950 && lazy && blocked==3);
        LocalGameplay fresh;assert(fresh.loadContent(argv[2],error));
        for(const auto& entry:content.questChainGates) {
            const auto* q=content.quest(entry.first);
            for(const auto& offered:fresh.content().questsForNpc(q->giverEntry))assert(offered.chainGate.defined);
        }
        std::cout<<"PASS production companion: "<<checked<<" quests, "<<lazy<<" lazy catalog rows, "<<blocked<<" blocked; direct and NPC-list reads attach gates\n";
    }
}
