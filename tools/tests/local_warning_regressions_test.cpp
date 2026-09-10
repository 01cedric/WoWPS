#include "local_group_rewards_fixture.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x)+" at line "+std::to_string(__LINE__)); } while (0)

struct Fixture {
    LocalGameplay game;
    std::shared_ptr<LocalWorldContent> content=rewardContent();
    LocalRealmPlayer p=rewardPlayer(1);
    std::string result;
    Fixture() { p.mana=p.maxMana=100000;p.money=1000000;p.regenerationTimer=-10000; }
    void load() { game.useContent(content); }
    bool run(LocalAction action,uint64_t target=0,uint32_t id=0,uint64_t service=0) {
        LocalRealmCommand cmd{action,target,id};cmd.serviceNpcGuid=service;
        return game.execute(p,cmd,{&p},result);
    }
};
static LocalSpellDefinition heal(uint32_t id) {
    LocalSpellDefinition s;s.id=id;s.name="Synthetic heal";s.clientSpell=true;
    s.heal=1;s.mana=10;s.cooldownMs=10000;s.range=100;s.allowableClasses=1;
    return s;
}
static void cooldownCapacity() {
    Fixture f;f.content->spells.clear();f.p.knownSpells.clear();
    for(uint32_t id=1;id<=18;++id) {f.content->spells.push_back(heal(id));f.p.knownSpells.push_back(id);}
    f.content->spells.back().castTimeMs=500;
    f.load();f.p.health=1;
    for(uint32_t id=1;id<=16;++id) CHECK(f.run(LocalAction::CastSpell,1,id));
    const auto mana=f.p.mana,health=f.p.health,revision=f.p.castRevision;
    CHECK(!f.run(LocalAction::CastSpell,1,17));
    CHECK(!f.run(LocalAction::CastSpell,1,18));
    CHECK(f.p.mana==mana && f.p.health==health && f.p.castRevision==revision && !f.p.castingSpellId);
    // An expired slot is reusable even before the next simulation tick.
    f.p.cooldowns[0].remainingMs=0;
    CHECK(f.run(LocalAction::CastSpell,1,17));CHECK(f.p.cooldowns.size()==16);
    CHECK(!f.run(LocalAction::CastSpell,1,17));
    f.p.cooldowns.erase(f.p.cooldowns.begin());
    CHECK(f.run(LocalAction::CastSpell,1,18));
    f.p.cooldowns.push_back({1,10000}); // Capacity changes while the cast is pending.
    const auto before=f.p.mana;
    f.game.tick(.25f,{&f.p});f.game.tick(.25f,{&f.p});
    CHECK(f.p.mana==before && !f.p.castingSpellId && f.p.castStatus==LocalCastStatus::Failed);
}
static void rankTraining() {
    Fixture f;f.content->spells.clear();f.p.knownSpells={1};f.p.level=80;
    for(uint32_t id=1;id<=50;++id) {auto s=heal(id);s.baseLevel=id==2?2:id==3?3:1;f.content->spells.push_back(s);}
    f.content->spells[0].supercededBySpell=2;f.content->spells[1].supercededBySpell=3;
    for(uint32_t id=4;id<=50;++id) f.p.knownSpells.push_back(id);
    CHECK(f.p.knownSpells.size()==LocalGameplay::MaxSpells);
    f.p.cooldowns={{1,7000}};f.load();
    auto trainer=rewardNpc();trainer.hostile=false;trainer.classTrainer=true;trainer.trainerClass=1;
    auto wrong=trainer;wrong.guid=11;wrong.trainerClass=2;wrong.x=1;
    trainer.x=2;f.game.setRemoteNpcs({trainer,wrong});
    const auto money=f.p.money;
    CHECK(!f.run(LocalAction::LearnSpell,0,3,wrong.guid));CHECK(f.p.money==money);
    CHECK(f.run(LocalAction::LearnSpell,0,3,trainer.guid));
    CHECK(f.p.knownSpells.size()==LocalGameplay::MaxSpells);
    CHECK(std::find(f.p.knownSpells.begin(),f.p.knownSpells.end(),1)==f.p.knownSpells.end());
    CHECK(std::find(f.p.knownSpells.begin(),f.p.knownSpells.end(),3)!=f.p.knownSpells.end());
    CHECK(f.p.cooldowns.size()==1 && f.p.cooldowns[0].spellId==3 && f.p.cooldowns[0].remainingMs==7000);
    CHECK(!f.run(LocalAction::CastSpell,1,3));
    CHECK(!f.run(LocalAction::LearnSpell,0,1,trainer.guid));
    CHECK(f.game.validatePlayer(f.p,f.result));
}
static void worldRescue() {
    Fixture f;f.load();f.p.hasHome=true;f.p.homeMapId=0;f.p.homeX=2;f.p.homeY=3;f.p.homeZ=4;
    f.p.z=kLocalWorldFloorZ-10;f.p.mountSpellId=1;f.p.movementState=kLocalMovementFalling;
    f.p.falling=true;f.p.fallStartZ=1000;f.p.castingSpellId=1;f.p.castTarget=10;
    f.p.castRemainingMs=100;f.p.flight.active=true;f.p.transportEntry=99;
    f.p.transportOffsetX=8;f.p.transportLastYaw=2;f.p.cooldowns={{1,5000}};
    const auto revision=f.p.positionRevision;
    f.game.tick(.01f,{&f.p});
    CHECK(f.p.x==2 && f.p.y==3 && f.p.z==4 && f.p.positionRevision==revision+1);
    CHECK(!f.p.mountSpellId && !f.p.movementState && !f.p.falling);
    CHECK(!f.p.castingSpellId && !f.p.castTarget && f.p.castStatus==LocalCastStatus::Interrupted);
    CHECK(!f.p.flight.active && !f.p.transportEntry && !f.p.transportOffsetX && !f.p.transportLastYaw);
    CHECK(f.p.health==f.p.maxHealth && f.p.fallRevision==f.p.positionRevision && f.p.fallStartZ==4);
    CHECK(f.p.cooldowns.size()==1 && f.p.cooldowns[0].remainingMs==4990);
}
static void questMoney() {
    Fixture f;auto& q=f.content->quests[0];q.turnInEntry=50;q.money=10;q.xp=10;
    q.objectives={{LocalQuestObjective::Type::Collect,117,1}};q.rewardItem=117;q.rewardCount=2;
    f.p.inventory={{117,1}};f.p.quests={{1,LocalQuestStatus::Complete,{1}}};f.p.money=999999995;
    f.load();auto npc=rewardNpc();npc.hostile=false;npc.questGiver=true;f.game.setRemoteNpcs({npc});
    const auto xp=f.p.xp;
    CHECK(!f.run(LocalAction::TurnInQuest,npc.guid,1));
    CHECK(f.p.money==999999995 && f.p.xp==xp && f.p.inventory.size()==1 && f.p.inventory[0].count==1);
    CHECK(f.p.quests.size()==1 && f.p.completedQuestIds.empty());
    f.p.money=999999990;CHECK(f.run(LocalAction::TurnInQuest,npc.guid,1));
    CHECK(f.p.money==1000000000 && f.p.inventory[0].count==2 && f.p.quests.empty());
    CHECK(!f.run(LocalAction::TurnInQuest,npc.guid,1));
}
static void selectedInn() {
    Fixture f;f.load();auto first=rewardNpc();first.hostile=false;first.innkeeper=true;first.x=1;
    auto selected=first;selected.guid=11;selected.x=3;f.game.setRemoteNpcs({first,selected});
    CHECK(!f.run(LocalAction::SetHome,0,0,999));CHECK(!f.p.hasHome);
    CHECK(f.run(LocalAction::SetHome,0,0,selected.guid));CHECK(f.p.homeX==3);
    selected.x=100;f.game.setRemoteNpcs({first,selected});
    CHECK(!f.run(LocalAction::SetHome,0,0,selected.guid));CHECK(f.p.homeX==3);
    CHECK(f.run(LocalAction::SetHome));CHECK(f.p.homeX==1); // Legacy nearest-service request.
}
int main() {
    int failed=0;
    for(const auto& test:std::vector<std::pair<const char*,void(*)()>>{
        {"cooldown capacity and completion revalidation",cooldownCapacity},
        {"selected trainer, full spellbook, skipped ranks and cooldown inheritance",rankTraining},
        {"world-floor rescue clears transient travel state",worldRescue},
        {"quest money cap preserves the complete reward",questMoney},
        {"selected inn identity and range",selectedInn}}) {
        try {test.second();std::cout<<"PASS "<<test.first<<'\n';}
        catch(const std::exception& e) {++failed;std::cout<<"FAIL "<<test.first<<": "<<e.what()<<'\n';}
    }
    return failed?1:0;
}
