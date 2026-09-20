#include "local_group_rewards_fixture.hpp"
#include "game/local_pet.hpp"
#include "game/local_ranged.hpp"
#include "game/local_proc_rules.hpp"
#include <iostream>

// P03/D1 runtime for the owned-creature energize. The source profile identity
// is decoded from the player's own client records; this fixture drives the real
// ranged auto-attack producer and checks the recipient half:
//   spell_hunter.cpp::spell_hun_go_for_the_throat - CheckProc requires an actual
//   pet, HandleEffectProc adds the aura amount to POWER_FOCUS of that pet.
namespace {
using namespace wowee::game;

constexpr uint32_t kTalentId=1683,kParent=34950,kChild=34953,kFocusPerCrit=25;

struct ThroatFixture {
    std::shared_ptr<LocalWorldContent> c=rewardContent();
    LocalGameplay game;
    LocalRealmPlayer p=rewardPlayer(1);
    LocalRealmNpc n=rewardNpc();
    std::string message;
    static constexpr uint32_t weaponId=2504,ammoId=2512,summonId=2;

    ThroatFixture() {
        const auto* w=localMeleeItem(weaponId);assert(w);
        c->items.clear();
        for(uint32_t id:{weaponId,ammoId}) {
            LocalItemDefinition item;item.id=id;item.name="Source item";item.stack=1000;
            item.inventoryType=id==weaponId?w->inventoryType:24;c->items.push_back(item);
        }
        std::sort(c->items.begin(),c->items.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        p.inventory={{weaponId,1},{ammoId,1000}};
        c->spells.clear();
        LocalSpellDefinition shot;shot.id=75;shot.name="Auto Shot";shot.clientSpell=true;
        shot.rangedAutoProfile=1;shot.sourceDamageClass=3;shot.schoolMask=1;shot.range=35;shot.minRange=5;
        shot.requiredItemClass=2;shot.requiredItemSubclasses=262156;c->spells.push_back(shot);
        LocalSpellDefinition summon;summon.id=summonId;summon.name="Summon beast";summon.clientSpell=true;
        summon.range=0;summon.summonPetEntry=30;summon.summonPetKind=uint8_t(LocalPetKind::Controlled);
        summon.summonPetEffectSlot=0;c->spells.push_back(summon);
        c->spells.push_back(talent());
        LocalSpellDefinition child;child.id=kChild;child.clientSpell=true;child.triggeredOnly=true;
        child.allowableClasses=1u<<2;child.procParentTalentId=kTalentId;child.resourceType=255;
        child.range=0;child.schoolMask=1;child.baseLevel=1;child.name="Go for the Throat";
        c->spells.push_back(child);
        std::sort(c->spells.begin(),c->spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        LocalNpcDefinition beast;beast.id=30;beast.name="Owned beast";beast.health=400;beast.damage=20;
        beast.respawnSeconds=30;c->npcs.push_back(beast);
        std::sort(c->npcs.begin(),c->npcs.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        p.classId=3;p.race=1;p.level=80;p.knownSpells={75,summonId};p.equipment[17]=weaponId;
        p.mana=0;p.maxMana=100;p.resourceType=LocalResourceType::Mana;p.orientation=0;
        p.talents={{kTalentId,1}};
        n.level=1;n.x=n.homeX=20;n.y=n.homeY=0;n.health=n.maxHealth=1000000000;n.attackTimer=1000;
        n.spawnId=10;n.guid=0xf130000000000000ULL|n.spawnId;
        c->spawns.push_back({n.spawnId,n.entry,n.mapId,n.x,n.y,n.z,n.orientation});
        game.useContent(c);game.tick(0,{&p});game.setRemoteNpcs({n});
    }
    static LocalSpellDefinition talent() {
        LocalSpellDefinition d;d.id=kParent;d.name="Go for the Throat";d.clientSpell=true;d.passive=true;
        d.talentId=kTalentId;d.talentRank=1;d.allowableClasses=1u<<2;d.buffSelfOnly=true;d.schoolMask=1;
        d.resourceType=255;d.range=0;
        auto& proc=d.proc;proc.effect=LocalProcEffect::RestorePetPower;proc.spellId=kChild;
        proc.flags=0x40;proc.chance=100;proc.amount=kFocusPerCrit;proc.resourceType=2;
        proc.recipient=uint8_t(LocalProcRecipient::OwnedPet);proc.schoolMask=1;
        proc.hitMask=LocalProcHitCritical;proc.spellTypeMask=7;proc.phaseMask=LocalProcPhaseHit;
        return d;
    }
    void summonPet() {
        p.globalCooldownMs=0;p.cooldowns.clear();
        if(!game.execute(p,{LocalAction::CastSpell,0,summonId},{&p},message)){std::cerr<<message<<'\n';std::abort();}
    }
    void resetPetPower(uint32_t power) {
        auto roster=game.pets();assert(roster.size()==1);
        roster[0].power=power;roster[0].powerRegenElapsedMs=0;game.setRemotePets(roster);
        assert(game.pets()[0].power==power);
    }
    uint64_t mark(){const auto e=game.combatEvents();return e.empty()?0:e.back().sequence;}
    void reload(){for(auto& stack:p.inventory)if(stack.itemId==ammoId)stack.count=1000;}
    /// Advance to just before the next shot, then fire it in one short tick so
    /// no source regeneration interval can elapse inside the measurement.
    void fireOneShot() {
        reload();
        if(!p.rangedAutoSpellId)assert(game.execute(p,{LocalAction::CastSpell,n.guid,75},{&p},message));
        for(int i=0;i<64&&p.rangedRemainingMs>200;++i){game.setRemoteNpcs({n});game.tick(0.2f,{&p});}
        game.setRemoteNpcs({n});assert(p.rangedAutoSpellId);
    }
};
}

int main() {
    // --- Source metadata -----------------------------------------------------
    assert(validLocalProc(ThroatFixture::talent()));
    // --- The recipient receives the source amount on a real ranged critical --
    {
        ThroatFixture f;f.summonPet();
        assert(f.game.pets().size()==1&&f.game.pets()[0].resourceType==2);
        const auto petGuid=f.game.pets()[0].guid;
        unsigned criticals=0,normals=0,energized=0;
        for(int shot=0;shot<300&&(criticals<3||normals<3);++shot) {
            f.fireOneShot();
            f.resetPetPower(0);
            const auto mark=f.mark();
            f.game.tick(0.25f,{&f.p});
            bool critical=false,landed=false;unsigned procs=0;
            for(const auto& e:f.game.combatEvents()) {
                if(e.sequence<=mark)continue;
                if(e.kind==LocalCombatEventKind::PlayerRanged) {
                    landed=true;critical=e.outcome==LocalMeleeOutcome::Critical;
                }
                assert(e.kind!=LocalCombatEventKind::ProcMana);
                if(e.kind==LocalCombatEventKind::ProcPower) {
                    ++procs;
                    // Owner and recipient stay distinct: the aura owner is the
                    // source of the observation and the summon is its target.
                    assert(e.source==f.p.guid&&e.target==petGuid);
                    assert(e.spell==kChild&&e.auraSpell==kParent);
                    assert(e.auraOwnerGuid==f.p.guid&&e.auraCasterGuid==f.p.guid);
                    assert(e.attempted==kFocusPerCrit&&e.effective==kFocusPerCrit);
                    assert(e.procDepth==1&&e.parentSequence&&e.rootSequence);
                }
            }
            if(!landed)continue;
            if(critical)++criticals;else ++normals;
            assert(procs==unsigned(critical));
            // Only the summon's bar receives the energize; the hunter's own
            // resource is never an alternate recipient.
            assert(f.game.pets()[0].power==(critical?kFocusPerCrit:0u));
            energized+=procs;
        }
        if(!(criticals>=3&&normals>=3&&energized==criticals)){std::cerr<<"criticals="<<criticals<<" normals="<<normals<<" energized="<<energized<<'\n';std::abort();}
    }
    // --- No summon, no proc --------------------------------------------------
    {
        ThroatFixture f;
        for(int shot=0;shot<24;++shot) {
            f.fireOneShot();
            const auto mark=f.mark();
            f.game.tick(0.05f,{&f.p});
            for(const auto& e:f.game.combatEvents())
                if(e.sequence>mark)assert(e.kind!=LocalCombatEventKind::ProcPower);
        }
        // The same shots energize once an actual summon exists.
        f.summonPet();
        bool energized=false;
        for(int shot=0;shot<200&&!energized;++shot) {
            f.fireOneShot();f.resetPetPower(0);
            const auto mark=f.mark();
            f.game.tick(0.05f,{&f.p});
            for(const auto& e:f.game.combatEvents())
                if(e.sequence>mark&&e.kind==LocalCombatEventKind::ProcPower)energized=true;
        }
        assert(energized);
    }
    // --- A dead summon, a wrong power system and a full bar ------------------
    {
        ThroatFixture f;f.summonPet();
        auto withState=[&](uint8_t resourceType,uint32_t power,bool dead) {
            auto roster=f.game.pets();roster[0].resourceType=resourceType;
            roster[0].maxPower=resourceType==255?0:kLocalPetMaxFocus;
            roster[0].power=resourceType==255?0:power;roster[0].powerRegenElapsedMs=0;
            roster[0].dead=dead;roster[0].health=dead?0:roster[0].maxHealth;
            f.game.setRemotePets(roster);
            assert(f.game.pets().size()==1);
        };
        struct Case { uint8_t resourceType; uint32_t power; bool dead; bool expectProc; uint32_t expectGranted; };
        const Case cases[]={
            {2,kLocalPetMaxFocus,false,true,0},   // Full bar still procs and grants nothing.
            {255,0,false,false,0},                // No bar at all: CheckProc fails.
            {0,10,false,false,0},                 // Wrong power system.
        };
        for(const auto& test:cases) {
            withState(test.resourceType,test.power,test.dead);
            bool sawCritical=false;unsigned procs=0,granted=0;
            for(int shot=0;shot<200&&!sawCritical;++shot) {
                withState(test.resourceType,test.power,test.dead);
                f.fireOneShot();
                const auto mark=f.mark();
                f.game.tick(0.05f,{&f.p});
                for(const auto& e:f.game.combatEvents()) {
                    if(e.sequence<=mark)continue;
                    if(e.kind==LocalCombatEventKind::PlayerRanged&&e.outcome==LocalMeleeOutcome::Critical)sawCritical=true;
                    if(e.kind==LocalCombatEventKind::ProcPower){++procs;granted+=e.effective;}
                }
            }
            assert(sawCritical);
            assert(procs==unsigned(test.expectProc)&&granted==test.expectGranted);
        }
        // A retired summon leaves no recipient behind.
        assert(f.game.execute(f.p,{LocalAction::DismissPet,f.game.pets()[0].guid,0},{&f.p},f.message));
        assert(f.game.pets().empty());
        bool procced=false;
        for(int shot=0;shot<64&&!procced;++shot) {
            f.fireOneShot();
            const auto mark=f.mark();
            f.game.tick(0.05f,{&f.p});
            for(const auto& e:f.game.combatEvents())
                if(e.sequence>mark&&e.kind==LocalCombatEventKind::ProcPower)procced=true;
        }
        assert(!procced);
    }
    // --- The learned rank is the only source of the effect -------------------
    {
        ThroatFixture f;f.summonPet();f.p.talents.clear();
        bool procced=false;
        for(int shot=0;shot<64&&!procced;++shot) {
            f.fireOneShot();f.resetPetPower(0);
            const auto mark=f.mark();
            f.game.tick(0.05f,{&f.p});
            for(const auto& e:f.game.combatEvents())
                if(e.sequence>mark&&e.kind==LocalCombatEventKind::ProcPower)procced=true;
        }
        assert(!procced);
    }
    std::cout<<"PASS P03/D1 Go for the Throat: source-amount focus energize on real ranged criticals, "
               "aura owner and summon recipient kept distinct, no proc without an actual summon, "
               "full bar grants nothing, wrong power system and no bar reject, retirement removes the "
               "recipient and an unlearned rank produces no effect\n";
    return 0;
}
