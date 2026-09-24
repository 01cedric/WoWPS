#include "game/local_gameplay.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>

using namespace wowee::game;
using Json=nlohmann::json;

static void write(const std::filesystem::path& path,const Json& value) {
    std::ofstream output(path,std::ios::binary|std::ios::trunc);assert(output);output<<value.dump();assert(output.good());
}
static LocalRealmPlayer playerAt(LocalGameplay& gameplay,const LocalGameObject& object) {
    LocalRealmPlayer player;player.guid=7;player.name="Reviewer";gameplay.initializePlayer(player,true);
    player.mapId=object.mapId;player.x=object.x+1;player.y=object.y;player.z=object.z;player.phaseMask=1;
    return player;
}
static uint32_t count(const LocalRealmPlayer& player,uint32_t item) {
    uint32_t total=0;for(const auto& stack:player.inventory)if(stack.itemId==item)total+=stack.count;return total;
}

int main(int argc,char** argv) {
    assert(argc==2);const auto assets=std::filesystem::path(argv[1]);std::string error;
    LocalGameplay gameplay;
    if(!gameplay.loadContent((assets/"world.json").string(),error)){std::cerr<<error<<"\n";return 1;}
    gameplay.seedGameObjectRandom(12340);gameplay.useContent(gameplay.sharedContent());
    const auto& content=gameplay.content();
    assert(content.gameObjects.size()==861);assert(content.worldEvents.size()==9);assert(content.gameObjectPools.size()==14);
    std::map<LocalGameObjectKind,size_t> kinds;for(const auto& object:content.gameObjects)++kinds[object.kind];
    assert(kinds[LocalGameObjectKind::Decorative]==606 && kinds[LocalGameObjectKind::Chair]==79 &&
           kinds[LocalGameObjectKind::Chest]==67 && kinds[LocalGameObjectKind::Resource]==109);
    // Only doors/chests/resources own shared state; decorations and chairs never do.
    assert(gameplay.gameObjectStates().size()==176);
    for(const auto& state:gameplay.gameObjectStates())assert(localGameObjectStateful(content.gameObject(state.id)->kind));
    assert(gameplay.validateGameObjectStates(gameplay.gameObjectStates()));
    const auto activeInPool=[&](const LocalGameObjectPool& pool){
        size_t active=0;for(auto id:pool.members)if(gameplay.gameObjectState(id)->status!=kLocalGameObjectDormant)++active;return active;
    };
    for(const auto& pool:content.gameObjectPools)assert(activeInPool(pool)==pool.maxActive);
    std::cout<<"PASS 861 reviewed objects: 606 presentation, 79 chairs, 67 chests, 109 resources; 14 pools hold max_limit\n";

    const std::array<uint32_t,8> staticExpected={26785,26786,26794,26795,26796,26797,26798,26799};
    for(auto id:staticExpected) {
        const auto* object=content.gameObject(id);assert(object&&object->kind==LocalGameObjectKind::Decorative&&!object->requiredPhaseMask);
        auto player=playerAt(gameplay,*object);
        assert(localGameObjectVisible(*object,player));assert(!localGameObjectUsable(*object,player));
    }
    const auto* seasonal=content.gameObject(141);assert(seasonal&&seasonal->kind==LocalGameObjectKind::Decorative&&seasonal->requiredPhaseMask);
    const auto event=std::find_if(content.worldEvents.begin(),content.worldEvents.end(),[&](const auto& row){return row.activePhaseMask==seasonal->requiredPhaseMask;});
    assert(event!=content.worldEvents.end()&&event->clock==LocalWorldEventClock::Holiday&&event->holidayId==324&&event->holidayStage==1);
    auto seasonalPlayer=playerAt(gameplay,*seasonal);
    assert(!localGameObjectVisible(*seasonal,seasonalPlayer));seasonalPlayer.phaseMask|=seasonal->requiredPhaseMask;assert(localGameObjectVisible(*seasonal,seasonalPlayer));
    // Arena Tournament: start == end in game_event, so AzerothCore never activates it.
    const auto arena=std::find_if(content.worldEvents.begin(),content.worldEvents.end(),[](const auto& row){return row.clock==LocalWorldEventClock::Interval;});
    assert(arena!=content.worldEvents.end()&&arena->name=="Arena Tournament"&&arena->intervalStartSeconds==arena->intervalEndSeconds);
    for(int year:{2000,2010,2026,2399}){bool resolved=false;assert(!localIntervalEventActive(*arena,{year,1,1,14,0,1},resolved)&&resolved);}
    LocalWorldEventSchedule bells;bells.id=1;bells.name="Hourly Bells";bells.clock=LocalWorldEventClock::Interval;bells.activePhaseMask=2;
    bells.intervalStartSeconds=localDateSeconds(2010,1,1,1,0,0);bells.intervalEndSeconds=localDateSeconds(2030,1,1,1,0,0);bells.occurrenceMinutes=60;bells.lengthMinutes=1;
    assert(validLocalWorldEventSchedule(bells));bool resolved=false;
    assert(localIntervalEventActive(bells,{2026,9,22,13,0,30},resolved)&&resolved);
    assert(!localIntervalEventActive(bells,{2026,9,22,13,1,0},resolved)&&resolved);
    assert(!localIntervalEventActive(bells,{2010,1,1,1,0,0},resolved)); // strict start
    assert(!localIntervalEventActive(bells,{2031,1,1,1,0,30},resolved));
    bells.holidayId=1;assert(!validLocalWorldEventSchedule(bells));
    std::cout<<"PASS holiday/interval phase gates; Arena Tournament stays inactive; hourly interval boundaries\n";

    // Herbalism: requires the profession, rolls original loot, skills up, depletes and rotates its pool.
    const LocalGameObject* herb=nullptr;
    for(const auto& object:content.gameObjects)if(object.entry==1618&&gameplay.gameObjectState(object.id)->status==kLocalGameObjectReady){herb=&object;break;}
    assert(herb&&herb->kind==LocalGameObjectKind::Resource&&herb->requiredSkillId==182&&herb->requiredSkill==0&&herb->poolId);
    auto herbalist=playerAt(gameplay,*herb);
    const LocalRealmCommand gather{LocalAction::UseGameObject,localGameObjectGuid(herb->id),herb->id,gameplay.gameObjectState(herb->id)->revision};
    assert(!localGameObjectUsable(*herb,herbalist,content)&&!gameplay.execute(herbalist,gather,{&herbalist},error));
    herbalist.professions.push_back({182,1,75,0});
    assert(localGameObjectUsable(*herb,herbalist,content));
    assert(gameplay.execute(herbalist,gather,{&herbalist},error));
    const auto peacebloom=count(herbalist,2447);assert(peacebloom>=1&&peacebloom<=3);
    assert(herbalist.professions[0].current==2); // orange: one point per node
    const auto* depleted=gameplay.gameObjectState(herb->id);assert(depleted->status==kLocalGameObjectDepleted&&depleted->remainingMs==herb->respawnMs);
    assert(!gameplay.execute(herbalist,gather,{&herbalist},error));
    const auto* pool=content.gameObjectPool(herb->poolId);assert(activeInPool(*pool)==pool->maxActive);
    {   // Fast-forward: the host clock advances at most 250 ms per tick.
        auto rows=gameplay.gameObjectStates();
        for(auto& row:rows)if(row.id==herb->id)row.remainingMs=1;
        assert(gameplay.restoreGameObjectStates(rows));assert(gameplay.tick(.25f,{}));
    }
    assert(gameplay.gameObjectState(herb->id)->status!=kLocalGameObjectDepleted&&activeInPool(*pool)==pool->maxActive);
    assert(gameplay.validateGameObjectStates(gameplay.gameObjectStates()));
    assert(localGatherSkillChance(0,0)==1000&&localGatherSkillChance(25,0)==750&&localGatherSkillChance(50,0)==250&&localGatherSkillChance(100,0)==0);
    std::cout<<"PASS herbalism lock, original loot, gather skill-up, depletion and pool rotation\n";

    // Mining additionally needs a Mining Pick (TotemCategory 165 members).
    const LocalGameObject* vein=nullptr;
    for(const auto& object:content.gameObjects)if(object.entry==1731&&gameplay.gameObjectState(object.id)->status==kLocalGameObjectReady){vein=&object;break;}
    assert(vein&&vein->requiredSkillId==186&&!vein->toolItemIds.empty());
    auto miner=playerAt(gameplay,*vein);miner.professions.push_back({186,1,75,0});
    assert(!localGameObjectUsable(*vein,miner,content));
    LocalItemStack pick;pick.itemId=2901;pick.count=1;miner.inventory.push_back(pick);
    assert(localGameObjectUsable(*vein,miner,content));
    const LocalRealmCommand mine{LocalAction::UseGameObject,localGameObjectGuid(vein->id),vein->id,gameplay.gameObjectState(vein->id)->revision};
    assert(gameplay.execute(miner,mine,{&miner},error));assert(count(miner,2770)>=1&&count(miner,2770)<=4);
    std::cout<<"PASS mining lock requires skill and pick; copper ore rolled\n";

    // Open chest (Lock 57) needs nothing; a full bag rejects without consuming the chest.
    const LocalGameObject* crate=nullptr;
    for(const auto& object:content.gameObjects)if(object.entry==3719&&gameplay.gameObjectState(object.id)->status==kLocalGameObjectReady){crate=&object;break;}
    assert(crate&&crate->kind==LocalGameObjectKind::Chest);
    auto looter=playerAt(gameplay,*crate);
    const LocalRealmCommand open{LocalAction::UseGameObject,localGameObjectGuid(crate->id),crate->id,gameplay.gameObjectState(crate->id)->revision};
    auto full=looter;while(full.inventory.size()<LocalGameplay::MaxInventory){LocalItemStack junk;junk.itemId=6948;junk.count=1;full.inventory.push_back(junk);}
    assert(!gameplay.execute(full,open,{&full},error)&&gameplay.gameObjectState(crate->id)->status==kLocalGameObjectReady);
    assert(gameplay.execute(looter,open,{&looter},error)&&gameplay.gameObjectState(crate->id)->status==kLocalGameObjectDepleted);
    std::cout<<"PASS open lock chest, atomic full-bag refusal\n";

    // Quest-loot-only chests (GO_FLAG_INTERACT_COND) stay unusable while no quest needs the item.
    size_t questOnly=0;
    for(const auto& object:content.gameObjects)if(object.questLootOnly) {
        ++questOnly;auto player=playerAt(gameplay,object);assert(!localGameObjectUsable(object,player,content));
    }
    assert(questOnly==37);
    std::cout<<"PASS 37 INTERACT_COND chests gated on needed quest loot\n";

    // Loot semantics: ungrouped chance rolls, one pick per group, quest rows only when needed.
    std::vector<LocalGameObjectLootRow> table={{1,100,0,1,1,false},{2,50,0,1,1,false},{3,0,1,1,1,false},{4,0,1,1,1,false},
        {5,25,2,1,1,false},{6,0,2,1,1,false},{7,100,0,2,2,true}};
    assert(validLocalGameObjectLootTable(table));
    std::mt19937 rng(5);std::map<uint32_t,int> seen;
    for(int i=0;i<4000;++i) {
        const auto loot=localRollGameObjectLoot(table,rng,[](uint32_t){return false;});
        int group1=0,group2=0;
        for(const auto& stack:loot){++seen[stack.itemId];group1+=stack.itemId==3||stack.itemId==4;group2+=stack.itemId==5||stack.itemId==6;assert(stack.itemId!=7);}
        assert(group1==1&&group2==1);
    }
    assert(seen[1]==4000&&seen[2]>1800&&seen[2]<2200&&seen[5]>850&&seen[5]<1150&&seen[3]>1800&&seen[3]<2200);
    const auto quest=localRollGameObjectLoot(table,rng,[](uint32_t id){return id==7;});
    assert(std::any_of(quest.begin(),quest.end(),[](const auto& s){return s.itemId==7&&s.count==2;}));
    table.push_back({8,0,0,1,1,false});assert(!validLocalGameObjectLootTable(table));
    std::cout<<"PASS LootTemplate group/chance/quest semantics\n";

    // Chairs: benches have four slots orthogonal to the facing; seats are skipped while occupied.
    const LocalGameObject* bench=nullptr;
    for(const auto& object:content.gameObjects)if(object.kind==LocalGameObjectKind::Chair&&object.chairSlots==4){bench=&object;break;}
    assert(bench&&bench->chairHeight==2&&localChairStandState(*bench)==6);
    const auto first=localChairSlotPosition(*bench,0),last=localChairSlotPosition(*bench,3);
    assert(std::abs(std::hypot(first[0]-last[0],first[1]-last[1])-3*bench->scale)<1e-3f);
    const int nearest=localChairNearestFreeSlot(*bench,first[0],first[1],[](float,float){return false;});assert(nearest==0);
    const int skipped=localChairNearestFreeSlot(*bench,first[0],first[1],[&](float x,float y){return std::hypot(x-first[0],y-first[1])<.1f;});assert(skipped==1);
    assert(localChairNearestFreeSlot(*bench,first[0],first[1],[](float,float){return true;})<0);
    auto sitter=playerAt(gameplay,*bench);assert(localGameObjectUsable(*bench,sitter,content));
    const LocalRealmCommand sit{LocalAction::UseGameObject,localGameObjectGuid(bench->id),bench->id};
    assert(!gameplay.execute(sitter,sit,{&sitter},error));
    std::cout<<"PASS chair slot geometry, occupancy and stand state; no shared authority state\n";

    // Save/restore: dormant pool members survive; a stale row set is renormalised.
    auto states=gameplay.gameObjectStates();assert(gameplay.restoreGameObjectStates(states)&&gameplay.gameObjectStates()==states);
    for(auto& state:states)if(content.gameObject(state.id)->poolId){state.status=0;state.remainingMs=0;}
    assert(gameplay.restoreGameObjectStates(states));
    for(const auto& p:content.gameObjectPools)assert(activeInPool(p)==p.maxActive);
    auto invalid=gameplay.gameObjectStates();
    for(auto& state:invalid)if(!content.gameObject(state.id)->poolId){state.status=kLocalGameObjectDormant;break;}
    assert(!gameplay.validateGameObjectStates(invalid));
    std::cout<<"PASS pooled save restore and dormant-state validation\n";

    const auto temp=std::filesystem::temp_directory_path()/"wowps-reviewed-original-test";std::error_code ec;
    std::filesystem::remove_all(temp,ec);assert(std::filesystem::create_directories(temp));
    std::filesystem::copy_file(assets/"quest_chains.json",temp/"quest_chains.json");
    std::filesystem::copy_file(assets/"creature_talk.json",temp/"creature_talk.json");
    auto world=Json::parse(std::ifstream(assets/"world.json"));
    auto companion=Json::parse(std::ifstream(assets/"reviewed_original_content.json"));
    write(temp/"world.json",world);write(temp/"reviewed_original_content.json",companion);
    companion["unexpected"]=1;write(temp/"reviewed_original_content.json",companion);
    LocalGameplay unknown;assert(!unknown.loadContent((temp/"world.json").string(),error)&&error.find("Unknown reviewed")!=std::string::npos);
    companion.erase("unexpected");companion["sourceCommit"]="wrong";write(temp/"reviewed_original_content.json",companion);
    LocalGameplay commit;assert(!commit.loadContent((temp/"world.json").string(),error)&&error.find("provenance mismatch")!=std::string::npos);
    companion["sourceCommit"]=world["provenance"]["commit"];
    auto decorated=companion;
    for(auto& object:decorated["gameObjects"])if(object["kind"]=="decorative"){object["loot"]=Json::array({{{"itemId",25},{"count",1}}});break;}
    write(temp/"reviewed_original_content.json",decorated);
    LocalGameplay interactive;assert(!interactive.loadContent((temp/"world.json").string(),error)&&error.find("does not support")!=std::string::npos);
    auto pooled=companion;pooled["gameObjectPools"][0]["members"].push_back(26785);
    write(temp/"reviewed_original_content.json",pooled);
    LocalGameplay foreign;assert(!foreign.loadContent((temp/"world.json").string(),error)&&error.find("pool")!=std::string::npos);
    auto badLoot=companion;
    for(auto& object:badLoot["gameObjects"])if(object.contains("lootTable")){object["lootTable"].push_back(object["lootTable"][0]);break;}
    write(temp/"reviewed_original_content.json",badLoot);
    LocalGameplay duplicate;assert(!duplicate.loadContent((temp/"world.json").string(),error)&&error.find("loot table")!=std::string::npos);
    write(temp/"reviewed_original_content.json",companion);
    {   // Creature talk companion: strict fields, provenance and rule invariants.
        auto talk=Json::parse(std::ifstream(assets/"creature_talk.json"));const auto original=talk;
        talk["unexpected"]=1;write(temp/"creature_talk.json",talk);
        LocalGameplay field;assert(!field.loadContent((temp/"world.json").string(),error)&&error.find("Unknown creature talk")!=std::string::npos);
        talk=original;talk["sourceCommit"]="wrong";write(temp/"creature_talk.json",talk);
        LocalGameplay provenance;assert(!provenance.loadContent((temp/"world.json").string(),error)&&error.find("Creature talk provenance")!=std::string::npos);
        talk=original;talk["rules"][0]["event"]="gossip";write(temp/"creature_talk.json",talk);
        LocalGameplay event;assert(!event.loadContent((temp/"world.json").string(),error)&&error.find("Unknown creature talk event")!=std::string::npos);
        talk=original;talk["rules"][0]["keepOnEvade"]=true;talk["rules"][0].erase("once");write(temp/"creature_talk.json",talk);
        LocalGameplay rule;assert(!rule.loadContent((temp/"world.json").string(),error)&&error.find("Invalid creature talk rule")!=std::string::npos);
        write(temp/"creature_talk.json",original);
        auto talkWorld=world;talkWorld["creatureTalk"]="../creature_talk.json";write(temp/"world.json",talkWorld);
        LocalGameplay escape;assert(!escape.loadContent((temp/"world.json").string(),error)&&error.find("sibling filename")!=std::string::npos);
        write(temp/"world.json",world);
        std::filesystem::create_directory_symlink(std::filesystem::absolute(assets/"catalog"),temp/"catalog");
        LocalGameplay restored;if(!restored.loadContent((temp/"world.json").string(),error)){std::cerr<<error<<"\n";assert(false);}
        std::filesystem::remove(temp/"catalog");
    }
    world["reviewedOriginalContent"]="../reviewed_original_content.json";write(temp/"world.json",world);
    LocalGameplay traversal;assert(!traversal.loadContent((temp/"world.json").string(),error)&&error.find("sibling filename")!=std::string::npos);
    std::filesystem::remove_all(temp,ec);
    std::cout<<"PASS strict object/talk companion fields/provenance, kind invariants, exclusive pools, loot tables and sibling paths\n";
}
