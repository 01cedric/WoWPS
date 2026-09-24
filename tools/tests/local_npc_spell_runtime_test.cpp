#include "local_npc_spell_fixture.hpp"
int main() {
    // The three originally reviewed direct-damage entries; the generated
    // family's other rows are covered by local_npc_spell_family_test.
    for(uint32_t entry:{4008u,4323u,5858u}) {
        const auto& profile=*localNpcSpellProfile(entry);
        NpcSpellFixture f(profile.entry);unsigned elapsed=0;
        while(!f.game.npcs()[0].npcCastingSpellId&&elapsed<5000){f.tick();elapsed+=250;}
        assert(f.game.npcs()[0].npcCastingSpellId==profile.spellId());
        assert(elapsed>=profile.p1&&elapsed<=profile.p2+250);
        assert(f.spells().empty());const auto x=f.game.npcs()[0].x;
        f.tick();assert(f.game.npcs()[0].x==x);assert(f.spells().empty());
        while(f.game.npcs()[0].npcCastingSpellId&&elapsed<10000){f.tick();elapsed+=250;}
        const auto events=f.spells();assert(events.size()==3);
        assert(events[0].kind==LocalCombatEventKind::SpellCast&&events[0].target==f.p.guid);
        assert(events[1].kind==LocalCombatEventKind::SpellDamage&&events[1].target==f.p.guid);
        assert(events[2].kind==LocalCombatEventKind::SpellFinish&&!events[2].target);
        assert(events[0].sequence<events[1].sequence&&events[1].sequence<events[2].sequence);
        assert(events[0].spellTypeMask==7&&events[2].spellTypeMask==7);
        assert(events[1].attackType==LocalCombatAttackType::Magic&&!events[1].actorIsPlayer);
        assert(f.game.npcs()[0].npcSpellTimerMs>0);
    }
    for(unsigned invalid=0;invalid<6;++invalid) {
        NpcSpellFixture f;f.arm();
        if(invalid==0)f.p.dead=true;
        if(invalid==1)f.p.flight.active=true;
        if(invalid==2)++f.p.mapId;
        if(invalid==3)++f.p.instanceId;
        if(invalid==4)f.p.x=40; // Still engaged, but outside the cast's range.
        if(invalid==5){auto n=f.game.npcs()[0];n.dead=true;n.respawnTimer=60;f.game.setRemoteNpcs({n});}
        f.tick();assert(f.spells().empty());assert(!f.game.npcs()[0].npcCastingSpellId);
    }
    {
        NpcSpellFixture f;f.arm();auto n=f.game.npcs()[0];n.targetGuid=f.q.guid;n.threat[0]={f.q.guid,100000};
        f.game.setRemoteNpcs({n});f.tick();const auto events=f.spells();assert(events.size()==3);
        assert(events[1].target==f.p.guid); // A threat switch never redirects an in-progress cast.
    }
    {
        NpcSpellFixture f(4323);f.arm();f.tick();auto events=f.spells();
        assert(events.size()==1&&events[0].kind==LocalCombatEventKind::SpellCast);
        assert(f.game.npcs()[0].npcSpellLaunched);f.p.x=45; // Launched missile retains its recipient beyond cast range.
        f.tick(3);events=f.spells();assert(events.size()==3&&events[1].target==f.p.guid);
    }
    {
        NpcSpellFixture f;auto n=f.game.npcs()[0];n.npcSpellTimerInitialized=true;n.npcSpellTimerMs=1;
        f.game.setRemoteNpcs({n});f.p.x=40;f.tick();const auto* profile=localNpcSpellProfile(n.entry);
        assert(f.spells().empty()&&!f.game.npcs()[0].npcCastingSpellId);
        assert(f.game.npcs()[0].npcSpellTimerMs>=profile->p3&&f.game.npcs()[0].npcSpellTimerMs<=profile->p4);
    }
    {
        // A fully absorbed incoming NPC magic hit reaches the target's proc
        // dispatcher and consumes its own shield capacity, never the bystander.
        NpcSpellFixture f;LocalSpellDefinition shield;shield.id=90000;shield.name="Shield component";
        shield.durationMs=60000;shield.buffAbsorb=10000;shield.absorbSchoolMask=8;f.c->spells.push_back(shield);
        LocalSpellDefinition proc;proc.id=90001;proc.name="Incoming magic component";proc.durationMs=60000;
        proc.proc.effect=LocalProcEffect::RestoreMana;proc.proc.spellId=91001;proc.proc.amount=3;proc.proc.chance=100;
        proc.proc.charges=1;proc.proc.hitMask=LocalProcHitAbsorb;proc.proc.flags=0x20000;proc.proc.phaseMask=LocalProcPhaseHit;proc.proc.spellTypeMask=1;
        assert(validLocalProc(proc));f.c->spells.push_back(proc);
        LocalStatAura a{shield.id,60000,0,0,f.p.guid};a.absorbRemaining=10000;f.p.statAuras.push_back(a);
        LocalStatAura b{proc.id,60000,0,0,f.p.guid};b.procCharges=1;f.p.statAuras.push_back(b);
        bool absorbed=false;
        for(unsigned attempt=0;attempt<32&&!absorbed;++attempt){f.arm();f.tick();for(const auto& e:f.spells())if(e.absorbed){absorbed=true;assert(!e.effective);}}
        assert(absorbed&&f.p.mana>=3&&f.q.mana==0);
        bool owner=false;for(const auto& e:f.game.combatEvents())if(e.kind==LocalCombatEventKind::ProcMana){assert(e.auraOwnerGuid==f.p.guid);owner=true;}
        assert(owner);
    }
    std::cout<<"PASS NPC authority spells: exact-profile timers, cast bars, CAST/HIT/FINISH, incoming ownership/absorption, missile flight, target retention, retry interval and lifecycle cancellation\n";
}
