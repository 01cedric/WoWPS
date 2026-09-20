#include "local_group_rewards_fixture.hpp"
#include "game/local_proc_rules.hpp"
#include <iostream>

// Authored component data exercises existing runtime actions only. It does not
// admit new client spells or unlock a class talent to manufacture coverage.
struct Fixture {
    std::shared_ptr<LocalWorldContent> c=rewardContent();LocalGameplay game;
    LocalRealmPlayer p=rewardPlayer(1),target=rewardPlayer(2);LocalRealmNpc n=rewardNpc();std::string message;
    Fixture(bool heal=false,uint32_t castTime=0) {
        auto& d=c->spells[0];d.clientSpell=true;d.sourceDamageClass=1;d.schoolMask=4;d.spellFamily=3;
        d.castTimeMs=castTime;d.damage=heal?0:10;d.heal=heal?10:0;
        p.classId=target.classId=8;p.race=target.race=1;p.level=target.level=80;
        p.health=target.health=50;p.maxMana=target.maxMana=100;p.mana=target.mana=0;
        n.health=n.maxHealth=1000000;n.level=80;n.attackTimer=1000;n.x=1;
        game.useContent(c);game.setRemoteNpcs({n});
    }
    bool cast(uint64_t to=10) {p.globalCooldownMs=0;p.cooldowns.clear();return game.execute(p,{LocalAction::CastSpell,to,1},{&p,&target},message);}
    void finishAura(LocalRealmPlayer& owner,uint32_t flags) {
        LocalSpellDefinition d;d.id=100+owner.guid;d.name="FINISH component";d.durationMs=60000;
        d.proc.effect=LocalProcEffect::RestoreMana;d.proc.spellId=1000+d.id;d.proc.amount=3;d.proc.chance=100;
        d.proc.charges=1;d.proc.flags=flags;d.proc.phaseMask=LocalProcPhaseFinish;d.proc.spellTypeMask=4;
        assert(validLocalProc(d));c->spells.push_back(d);
        LocalStatAura a{d.id,d.durationMs,0,0,owner.guid};a.procCharges=1;owner.statAuras.push_back(a);
    }
};
int main() {
    // Component classification, not a claim that ranged weapon actions have
    // source-complete gameplay admission. Explicit CAST values must agree with
    // retained client damage-class metadata before FINISH copies them.
    for(uint8_t damageClass=0;damageClass<4;++damageClass) {
        Fixture f;f.c->spells[0].sourceDamageClass=damageClass;
        assert(f.cast());unsigned casts=0,finishes=0;
        const auto expected=localProcSpellAttackType(damageClass);
        for(const auto& e:f.game.combatEvents()) {
            if(e.kind==LocalCombatEventKind::SpellCast){++casts;assert(e.attackType==expected&&e.spellTypeMask==7);}
            if(e.kind==LocalCombatEventKind::SpellFinish){++finishes;assert(e.attackType==expected&&e.spellTypeMask==7&&e.target==0);}
        }
        assert(casts==1&&finishes==1);
    }
    for(bool heal:{false,true}) {
        Fixture f(heal);f.finishAura(f.p,heal?0x4000:0x10000);f.finishAura(f.target,0x8000);
        assert(f.cast(heal?2:10));const auto events=f.game.combatEvents();
        uint64_t cast=0,hit=0,finish=0;unsigned procs=0;
        for(const auto& e:events) {
            if(e.kind==LocalCombatEventKind::SpellCast){assert(e.spellTypeMask==7);cast=e.sequence;}
            if(e.kind==LocalCombatEventKind::DirectHeal||e.kind==LocalCombatEventKind::SpellDamage){assert(cast);hit=e.sequence;}
            if(e.kind==LocalCombatEventKind::SpellFinish){assert(hit&&hit<e.sequence);finish=e.sequence;assert(e.source==1&&!e.target&&e.spellTypeMask==7);}
            if(e.kind==LocalCombatEventKind::ProcMana){assert(finish&&e.parentSequence==finish&&e.auraOwnerGuid==1);++procs;}
        }
        assert(cast&&hit&&finish&&procs==1&&f.p.mana==3&&f.target.mana==0);
        assert(f.target.statAuras.size()==1&&f.target.statAuras[0].procCharges==1);
    }
    for(unsigned scenario=0;scenario<3;++scenario) {
        // Use a live player recipient for cast-time lifecycle checks. The
        // authored NPC has no catalog spawn or encounter target and is
        // correctly removed by the authority region refresh during ticking.
        Fixture f(true,1000);assert(f.cast(2));assert(f.game.combatEvents().empty());
        if(scenario==0)assert(f.game.execute(f.p,{LocalAction::CancelCast},{&f.p,&f.target},f.message));
        if(scenario==1)f.p.knownSpells.clear(); // Completion validation fails.
        for(unsigned i=0;i<4;++i)f.game.tick(.25f,{&f.p,&f.target});
        unsigned finish=0;for(const auto& e:f.game.combatEvents())finish+=e.kind==LocalCombatEventKind::SpellFinish;
        assert(finish==(scenario==2?1u:0u));
    }
    {
        Fixture f;f.p.knownSpells.clear();assert(!f.cast());assert(f.game.combatEvents().empty());
    }
    {
        Fixture f;unsigned criticals=0,normal=0;uint64_t last=0;
        for(unsigned i=0;i<512;++i) {
            assert(f.cast());bool critical=false,finish=false;
            for(const auto& e:f.game.combatEvents())if(e.sequence>last) {
                if(e.kind==LocalCombatEventKind::SpellDamage)critical|=e.outcome==LocalMeleeOutcome::Critical;
                if(e.kind==LocalCombatEventKind::SpellFinish){finish=true;assert(localProcEventHitMask(e)==(LocalProcHitNormal|(critical?LocalProcHitCritical:0u)));}
            }
            assert(finish);criticals+=critical;normal+=!critical;last=f.game.combatEvents().back().sequence;
        }
        assert(criticals&&normal);
    }
    std::cout<<"PASS FINISH: public damage/heal casts, CAST all-types, source-only FINISH callback after HIT, normal+critical mask, cast-time completion, cancellation and failed completion suppression\n";
}
