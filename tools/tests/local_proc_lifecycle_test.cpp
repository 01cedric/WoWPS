#include "local_group_rewards_fixture.hpp"
#include "game/local_proc_lifecycle.hpp"
#include "game/local_proc_rules.hpp"
#include "game/local_melee.hpp"
#include <iostream>

struct LifecycleFixture {
    std::shared_ptr<LocalWorldContent> c=rewardContent();LocalGameplay game;
    LocalRealmPlayer tagger=rewardPlayer(1),killer=rewardPlayer(2);std::string message;
    LifecycleFixture() {
        c->npcs[0].id=3;c->spells[0].damage=10000;
        for(auto* p:{&tagger,&killer}){p->classId=8;p->race=1;p->level=80;p->resourceType=LocalResourceType::Mana;p->maxMana=100;p->mana=0;}
        game.useContent(c);
    }
    void aura(LocalRealmPlayer& p,uint32_t id,uint32_t flags,bool requireXp=false) {
        LocalSpellDefinition d;d.id=id;d.name="Lifecycle component";d.durationMs=60000;
        d.proc.effect=LocalProcEffect::RestoreMana;d.proc.spellId=id+1000;d.proc.amount=3;
        d.proc.chance=100;d.proc.charges=1;d.proc.flags=flags;d.proc.attributesMask=requireXp?1:0;
        // Lifecycle flags bypass these mismatched spell/hit filters upstream.
        d.proc.hitMask=LocalProcHitMiss;d.proc.triggerSchoolMask=64;
        d.proc.phaseMask=LocalProcPhaseCast;d.proc.spellTypeMask=4;
        assert(validLocalProc(d));c->spells.push_back(d);
        LocalStatAura a{id,d.durationMs,0,0,p.guid};a.procCharges=1;p.statAuras.push_back(a);
    }
    LocalRealmNpc enemy(uint8_t level=80) {
        auto n=rewardNpc();n.entry=3;n.level=level;return n;
    }
    void kill(uint8_t level=80) {rewardKill(game,killer,{&tagger,&killer},enemy(level));}
};
int main() {
    assert(localProcGrayLevel(5)==0&&localProcGrayLevel(6)==1&&localProcGrayLevel(39)==31);
    assert(localProcGrayLevel(40)==31&&localProcGrayLevel(59)==47&&localProcGrayLevel(60)==51&&localProcGrayLevel(80)==71);
    assert(localNpcExperienceTargetEligible(3,80,72)&&!localNpcExperienceTargetEligible(3,80,71));
    assert(!localNpcExperienceTargetEligible(1,1,80)); // Critter.
    assert(!localNpcExperienceTargetEligible(2523,1,80)); // Totem.
    assert(!localNpcExperienceTargetEligible(1511,1,80)); // NO_XP.
    assert(!localNpcExperienceTargetEligible(999999,1,80)); // Unknown source metadata.
    for(bool gray:{false,true}) {
        LifecycleFixture f;f.aura(f.killer,101,2,true);f.aura(f.tagger,102,2);
        f.kill(gray?71:72);unsigned kills=0,deaths=0,procs=0;uint64_t kill=0,death=0;
        for(const auto& e:f.game.combatEvents()) {
            if(e.kind==LocalCombatEventKind::Kill){++kills;kill=e.sequence;assert(e.source==2&&e.target==10&&!e.spell&&!e.auraSpell&&e.actorIsPlayer&&e.actionTargetKnown&&e.actionTargetHonorOrXpEligible==!gray);}
            if(e.kind==LocalCombatEventKind::Death){++deaths;death=e.sequence;assert(e.source==10&&!e.target&&!e.actorIsPlayer);}
            if(e.kind==LocalCombatEventKind::ProcMana){++procs;assert(e.auraOwnerGuid==2&&e.parentSequence==kill);}
            if(e.kind==LocalCombatEventKind::SpellDamage)assert(kill&&death&&kill<death&&death<e.sequence);
        }
        assert(kills==1&&deaths==1&&procs==unsigned(!gray));
        assert(f.killer.mana==(gray?0u:3u)&&f.tagger.mana==0);
        // Level cap and unrelated tag ownership do not suppress XP-eligible
        // kill procs. Actual XP remains zero at level80 for both participants.
        assert(!f.killer.xp&&!f.tagger.xp);
        const auto count=f.game.combatEvents().size();f.killer.globalCooldownMs=0;
        assert(!f.game.execute(f.killer,{LocalAction::CastSpell,10,1},{&f.tagger,&f.killer},f.message));
        assert(f.game.combatEvents().size()==count);
    }
    {
        LifecycleFixture f;f.aura(f.killer,101,2);f.kill(1);
        assert(f.killer.mana==3); // Unrestricted KILL still observes gray victims.
    }
    {
        LifecycleFixture f;f.aura(f.killer,101,2,true);f.killer.instanceId=1;
        auto n=f.enemy();n.instanceId=1;
        rewardKill(f.game,f.killer,{&f.tagger,&f.killer},n);
        assert(f.killer.mana==0&&f.killer.statAuras[0].procCharges==1); // Aura belongs to instance0.
    }
    {
        LifecycleFixture f;f.aura(f.killer,201,1);f.aura(f.killer,202,0x1000000);
        f.c->npcs[0].damage=1000000;f.game.tick(0,{&f.killer});
        auto n=f.enemy();n.targetGuid=f.killer.guid;n.attackTimer=0;n.lootOwner=0;n.threat[0]={f.killer.guid,1};
        f.killer.health=1;f.game.setRemoteNpcs({n});
        for(unsigned i=0;i<24&&!f.killer.dead;++i)f.game.tick(.25f,{&f.killer});
        assert(f.killer.dead);unsigned kills=0,deaths=0,killedProcs=0,deathProcs=0;uint64_t kill=0,death=0;
        for(const auto& e:f.game.combatEvents()) {
            if(e.kind==LocalCombatEventKind::Kill){++kills;kill=e.sequence;assert(e.source==10&&e.target==2&&!e.actorIsPlayer);}
            if(e.kind==LocalCombatEventKind::Death){++deaths;death=e.sequence;assert(e.source==2&&!e.target&&e.actorIsPlayer);}
            if(e.kind==LocalCombatEventKind::ProcMana&&e.auraSpell==201){++killedProcs;assert(e.parentSequence==kill);}
            if(e.kind==LocalCombatEventKind::ProcMana&&e.auraSpell==202){++deathProcs;assert(e.parentSequence==death);}
        }
        assert(kills==1&&deaths==1&&killedProcs==1&&deathProcs==1&&kill<death);
        f.game.tick(.25f,{&f.killer});unsigned repeated=0;
        for(const auto& e:f.game.combatEvents())repeated+=e.kind==LocalCombatEventKind::Death;
        assert(repeated==1);
    }
    {
        LifecycleFixture f;f.aura(f.killer,201,1);f.aura(f.killer,202,0x1000000);
        f.game.tick(0,{&f.killer});f.killer.health=1;
        f.killer.z=100;f.killer.movementState=kLocalMovementFalling;f.game.tick(.01f,{&f.killer});
        f.killer.z=0;f.killer.movementState=0;f.game.tick(.01f,{&f.killer});
        assert(f.killer.dead);unsigned kills=0,deaths=0,killedProcs=0,deathProcs=0;
        for(const auto& e:f.game.combatEvents()) {
            kills+=e.kind==LocalCombatEventKind::Kill;deaths+=e.kind==LocalCombatEventKind::Death;
            killedProcs+=e.kind==LocalCombatEventKind::ProcMana&&e.auraSpell==201;
            deathProcs+=e.kind==LocalCombatEventKind::ProcMana&&e.auraSpell==202;
        }
        assert(!kills&&deaths==1&&!killedProcs&&deathProcs==1);
    }
    std::cout<<"PASS lifecycle: public lethal casts, killing-blow versus tag ownership, level-cap and gray XP predicates, source creature exclusions, instance gating, NPC lethal KILLED then DEATH, environmental DEATH only, aura callbacks before death, corpse retry suppression\n";
}
