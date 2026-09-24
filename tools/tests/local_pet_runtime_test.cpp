#include "local_group_rewards_fixture.hpp"
#include "game/local_pet.hpp"
#include "game/local_proc_rules.hpp"
#include <iostream>

// P03/D1 authority runtime. The reviewed source profile identities are checked
// against the real client records by the import audit; this fixture exercises
// the actual summon, its own combat events, its recipient state and the owner
// relationships the shared dispatcher depends on.
namespace {
using namespace wowee::game;

std::shared_ptr<LocalWorldContent> petContent() {
    auto c=rewardContent();
    // Creature 30 is a beast in the compiled creature-type table, so the summon
    // takes the source focus bar; the enemy stays the fixture's own template.
    LocalNpcDefinition beast;beast.id=30;beast.name="Owned beast";beast.health=400;beast.damage=20;
    beast.armor=0;beast.xp=0;beast.respawnSeconds=30;c->npcs.push_back(beast);
    LocalSpellDefinition summon;summon.id=2;summon.name="Summon beast";summon.clientSpell=true;
    summon.range=0;summon.summonPetEntry=30;summon.summonPetKind=uint8_t(LocalPetKind::Controlled);
    summon.summonPetEffectSlot=0;c->spells.push_back(summon);
    std::sort(c->npcs.begin(),c->npcs.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    std::sort(c->spells.begin(),c->spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return c;
}

LocalRealmPlayer petOwner() {
    auto p=rewardPlayer(1);p.level=20;p.knownSpells={1,2};p.health=p.maxHealth=1000;return p;
}

size_t countEvents(LocalGameplay& game,uint64_t after,LocalCombatEventKind kind) {
    size_t total=0;
    for(const auto& e:game.combatEvents())if(e.sequence>after&&e.kind==kind)++total;
    return total;
}
uint64_t lastSequence(LocalGameplay& game) {
    const auto events=game.combatEvents();return events.empty()?0:events.back().sequence;
}
// The spawn refresher rebuilds the roster from world queries and this fixture
// supplies no catalog, so an enemy is kept only while it is engaged. Seeding a
// target reproduces exactly the engaged state the refresher retains.
LocalRealmNpc engagedNpc(uint64_t owner=1) {
    auto n=rewardNpc();n.level=20;n.attackTimer=1000;n.targetGuid=owner;
    n.homeX=n.homeY=n.homeZ=0;return n;
}
void keepNpc(LocalGameplay& game,const LocalRealmNpc& seed) {
    if(game.npcs().empty())game.setRemoteNpcs({seed});
}
}

int main() {
    // --- Acquisition through the real cast path -----------------------------
    {
        LocalGameplay game;game.useContent(petContent());
        auto p=petOwner();std::string result;
        assert(game.pets().empty());
        if(!game.execute(p,{LocalAction::CastSpell,0,2},{&p},result)){std::cerr<<result<<'\n';std::abort();}
        assert(game.pets().size()==1);
        const auto& pet=game.pets()[0];
        assert(pet.ownerGuid==p.guid&&pet.entry==30&&pet.summonSpellId==2);
        assert(pet.kind==LocalPetKind::Controlled&&pet.level==p.level);
        assert(pet.maxHealth==400&&pet.health==400);
        // Source focus pool and system, not the owner's resource.
        assert(pet.resourceType==2&&pet.maxPower==kLocalPetMaxFocus&&pet.power==kLocalPetMaxFocus);
        assert(pet.mapId==p.mapId&&pet.instanceId==p.instanceId&&pet.summonEpoch);
        assert(game.controlledPet(p.guid)==&game.pets()[0]);
        assert(game.controlledPet(999)==nullptr);
        // One controlled summon per owner. previously a second cast destroyed
        // the first and built a new creature; Spell::EffectSummonPet
        // (SpellEffects.cpp:3402-3438) does not - an existing summon of the
        // SAME entry that is alive is moved beside its owner, refilled and
        // KEPT, and the function returns. The GUID and the summon epoch are
        // therefore preserved across a re-cast, which is what makes a callback
        // prepared for that summon stay valid.
        const auto firstGuid=pet.guid,firstEpoch=pet.summonEpoch;
        p.globalCooldownMs=0;p.cooldowns.clear();
        {auto hurt=game.pets();hurt[0].health=1;hurt[0].power=0;game.setRemotePets(hurt);}
        assert(game.execute(p,{LocalAction::CastSpell,0,2},{&p},result));
        assert(game.pets().size()==1&&game.pets()[0].guid==firstGuid&&game.pets()[0].summonEpoch==firstEpoch);
        assert(game.pets()[0].health==game.pets()[0].maxHealth&&game.pets()[0].power==game.pets()[0].maxPower);
        // A stale expected GUID is still refused, and the live one is not.
        assert(!game.execute(p,{LocalAction::DismissPet,firstGuid+1,0},{&p},result));
        assert(game.pets().size()==1);
        assert(game.execute(p,{LocalAction::DismissPet,game.pets()[0].guid,0},{&p},result));
        assert(game.pets().empty());
        assert(!game.execute(p,{LocalAction::DismissPet,0,0},{&p},result));
    }
    // --- Focus regeneration on the authority tick ---------------------------
    {
        LocalGameplay game;game.useContent(petContent());
        auto p=petOwner();std::string result;
        assert(game.execute(p,{LocalAction::CastSpell,0,2},{&p},result));
        auto drained=game.pets();drained[0].power=0;game.setRemotePets(drained);
        assert(game.pets()[0].power==0);
        // The authority advances at most a quarter second per tick.
        for(int i=0;i<15;++i)game.tick(1.0f,{&p});
        assert(game.pets()[0].power==0); // Below one source interval.
        game.tick(1.0f,{&p});
        assert(game.pets()[0].power==kLocalPetFocusRegenAmount);
        for(int i=0;i<256;++i)game.tick(1.0f,{&p});
        assert(game.pets()[0].power==kLocalPetMaxFocus);
    }
    // --- The summon's own combat events -------------------------------------
    {
        LocalGameplay game;game.useContent(petContent());
        auto p=petOwner();std::string result;
        auto enemy=engagedNpc();enemy.health=enemy.maxHealth=1000000;
        game.setRemoteNpcs({enemy});
        assert(game.execute(p,{LocalAction::CastSpell,0,2},{&p},result));
        // The summon takes its target from the owner's engagement; the owner's
        // own swing timer is held off so only the summon's events appear.
        p.attackTarget=enemy.guid;p.attackTimer=1000;
        const auto petGuid=game.pets()[0].guid;
        auto mark=lastSequence(game);
        bool swung=false;
        for(int i=0;i<64&&!swung;++i) {
            keepNpc(game,enemy);game.tick(0.5f,{&p});
            for(const auto& e:game.combatEvents())if(e.sequence>mark&&e.kind==LocalCombatEventKind::PetMelee) {
                swung=true;
                // Source and target are the summon and its victim. The owner is
                // neither, exactly as Unit::AttackerStateUpdate leaves them.
                assert(e.source==petGuid&&e.target==enemy.guid);
                assert(e.attackType==LocalCombatAttackType::Melee&&e.schoolMask==1);
                assert(e.weaponPeriodMs==2000&&!e.spell&&!e.auraSpell);
                assert(localProcEventFlags(e,p.guid)==0);
                assert(localProcEventFlags(e,petGuid)!=0);
            }
        }
        assert(swung);
        // The summon acquires the owner's engaged enemy and holds its own threat.
        assert(game.pets()[0].targetGuid==enemy.guid);
        bool threatened=false;
        for(const auto& row:game.npcs()[0].threat)if(row.guid==petGuid&&row.amount)threatened=true;
        assert(threatened);
        // The owner sees the summon's swing in their own melee view.
        bool viewed=false;
        for(const auto& view:p.meleeViews)if(view.source==petGuid&&view.target==enemy.guid)viewed=true;
        assert(viewed);
    }
    // --- Reward credit follows the owner, the killing blow does not ---------
    {
        LocalGameplay game;game.useContent(petContent());
        auto p=petOwner();p.level=1;std::string result;
        assert(game.execute(p,{LocalAction::CastSpell,0,2},{&p},result));
        auto enemy=engagedNpc();enemy.level=1;enemy.health=enemy.maxHealth=1;
        // 2.39 Creature::IsDamageEnoughForLootingAndReward: the owner struck
        // the enemy before (a pet-only kill rewards nobody at the pin).
        enemy.npcPlayerDamage=1;enemy.npcDamagedByPlayer=true;
        game.setRemoteNpcs({enemy});p.attackTarget=enemy.guid;p.attackTimer=1000;
        const auto petGuid=game.pets()[0].guid;
        const auto xpBefore=p.xp;
        auto mark=lastSequence(game);
        for(int i=0;i<64&&(game.npcs().empty()||!game.npcs()[0].dead);++i){keepNpc(game,enemy);game.tick(0.5f,{&p});}
        assert(game.npcs()[0].dead);
        // Experience, quest credit and the loot tag are the owner's; the KILL
        // observation keeps the summon that actually landed the blow.
        assert(p.xp>xpBefore||p.level>1);
        assert(p.quests[0].progress[0]==1);
        assert(game.npcs()[0].lootOwner==p.guid);
        bool kill=false;
        for(const auto& e:game.combatEvents())if(e.sequence>mark&&e.kind==LocalCombatEventKind::Kill&&e.target==enemy.guid) {
            kill=true;assert(e.source==petGuid);
        }
        assert(kill);
        // The summon drops a dead target rather than pursuing a corpse.
        game.tick(0.1f,{&p});
        assert(game.pets()[0].targetGuid==0);
    }
    // --- The summon is a real target ----------------------------------------
    {
        LocalGameplay game;game.useContent(petContent());
        auto p=petOwner();std::string result;
        assert(game.execute(p,{LocalAction::CastSpell,0,2},{&p},result));
        auto weakened=game.pets();weakened[0].maxHealth=weakened[0].health=40;game.setRemotePets(weakened);
        const auto petGuid=game.pets()[0].guid;
        auto enemy=engagedNpc(petGuid);enemy.health=enemy.maxHealth=1000000;enemy.aggressive=true;
        enemy.attackTimer=0;enemy.threat[0]={petGuid,1000};game.setRemoteNpcs({enemy});
        auto mark=lastSequence(game);
        bool hit=false,killed=false;
        for(int i=0;i<200&&!killed;++i) {
            keepNpc(game,enemy);game.tick(0.5f,{&p});
            for(const auto& e:game.combatEvents())if(e.sequence>mark&&e.target==petGuid) {
                if(e.kind==LocalCombatEventKind::NpcMelee){hit=true;assert(e.source==enemy.guid);}
                if(e.kind==LocalCombatEventKind::Kill)killed=true;
            }
            if(game.pets().empty())break;
        }
        assert(hit&&killed);
        // Nothing the summon takes reaches its owner's health.
        assert(p.health==p.maxHealth);
        // A dead summon is retired on the next authority pass and its enemy
        // stops pursuing a GUID that no longer exists.
        keepNpc(game,enemy);game.tick(0.1f,{&p});
        assert(game.pets().empty());
        assert(game.npcs()[0].targetGuid!=petGuid);
    }
    // --- Owner lifecycle retires the summon ---------------------------------
    {
        for(int scenario=0;scenario<4;++scenario) {
            LocalGameplay game;game.useContent(petContent());
            auto p=petOwner();std::string result;
            assert(game.execute(p,{LocalAction::CastSpell,0,2},{&p},result));
            assert(game.pets().size()==1);
            std::vector<LocalRealmPlayer*> roster{&p};
            switch(scenario) {
                case 0: p.dead=true;break;
                case 1: p.health=0;break;
                case 2: p.instanceId=7;break;
                case 3: roster.clear();break;
            }
            game.tick(0.1f,roster);
            if(!game.pets().empty()){std::cerr<<"scenario "<<scenario<<" retained\n";std::abort();}
        }
    }
    // --- Guardian lifetime ---------------------------------------------------
    {
        LocalGameplay game;game.useContent(petContent());
        auto p=petOwner();std::string result;
        assert(game.execute(p,{LocalAction::CastSpell,0,2},{&p},result));
        auto guardian=game.pets();guardian[0].kind=LocalPetKind::Guardian;guardian[0].remainingMs=1000;
        game.setRemotePets(guardian);
        assert(game.pets().size()==1);
        // The authority advances at most a quarter second per tick.
        for(int i=0;i<3;++i){game.tick(0.5f,{&p});assert(game.pets().size()==1);}
        game.tick(0.5f,{&p});assert(game.pets().empty());
    }
    // --- Replicated rosters are validated ------------------------------------
    {
        LocalGameplay game;game.useContent(petContent());
        auto p=petOwner();std::string result;
        assert(game.execute(p,{LocalAction::CastSpell,0,2},{&p},result));
        const auto good=game.pets();
        auto broken=good;broken[0].guid=5;game.setRemotePets(broken);
        assert(game.pets().size()==1&&game.pets()[0].guid==good[0].guid); // Rejected wholesale.
        std::string error;
        assert(!game.restorePets({broken}, error)&&!error.empty());
        auto restored=good;restored[0].summonEpoch=0;
        assert(game.restorePets(restored,error));
        assert(game.pets().size()==1&&game.pets()[0].summonEpoch);
    }
    std::cout<<"PASS P03/D1 owned-creature runtime: reviewed summon cast, single controlled summon per owner, "
               "explicit dismissal, source focus regeneration, the summon's own melee events/threat/owner view, "
               "owner reward credit with the summon retaining the killing blow, the summon as a real NPC target, "
               "owner death/health/travel/absence and guardian lifetime retirement, and validated replicated rosters\n";
    return 0;
}
