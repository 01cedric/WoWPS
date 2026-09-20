#include "local_group_rewards_fixture.hpp"
#include "game/local_melee.hpp"
#include "game/local_proc_talents.hpp"
#include "game/local_stat_auras.hpp"
#include "game/local_aura_identity.hpp"
#include <cmath>
#include <iostream>

// Actual authority swing/cast/lifecycle paths; source-record closure and legal
// talent-tree reachability are independently audited against the real DBC.
namespace {
constexpr uint32_t warriorParents[]{12319,12971,12972,12973,12974};
constexpr uint32_t shamanParents[]{16256,16281,16282,16283,16284};
constexpr uint32_t shamanChildren[]{16257,16277,16278,16279,16280};
bool near(float a,float b){return std::abs(a-b)<.0001f;}
LocalStatAura* active(LocalRealmPlayer& p,uint32_t id) {
    for(auto& a:p.statAuras)if(a.spellId==id&&a.remainingMs)return &a;return nullptr;
}
struct Fixture {
    std::shared_ptr<LocalWorldContent> c=rewardContent();
    LocalGameplay game;LocalRealmPlayer p=rewardPlayer(1);LocalRealmNpc n=rewardNpc();
    LocalSpellDefinition parent,child;uint64_t sequence=0;std::string message;
    Fixture(bool shaman,unsigned rank) {
        const uint32_t classes=shaman?64:1;
        c->npcs[0].health=1000000;c->npcs[0].armor=0;
        LocalItemDefinition weapon;weapon.id=25;weapon.inventoryType=21;weapon.name="Worn Shortsword";weapon.stack=1;c->items.push_back(weapon);
        weapon.id=727;weapon.inventoryType=13;c->items.push_back(weapon);
        std::sort(c->items.begin(),c->items.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        parent.id=(shaman?shamanParents:warriorParents)[rank-1];parent.passive=true;parent.allowableClasses=classes;
        parent.talentId=shaman?602:156;parent.talentRank=rank;parent.name="Flurry parent fixture";
        child.id=shaman?shamanChildren[rank-1]:12965+rank;child.name="Flurry child fixture";
        child.triggeredOnly=true;child.allowableClasses=classes;child.durationMs=15000;child.buffSelfOnly=true;
        child.schoolMask=1;child.spellFamily=shaman?11:4;child.procParentTalentId=parent.talentId;
        child.meleeHastePct=rank*(shaman?6:5);
        auto& cp=child.proc;cp.effect=LocalProcEffect::ConsumeOwnerAuraCharge;cp.spellId=child.id;cp.flags=4;
        cp.chance=100;cp.charges=3;cp.amount=child.meleeHastePct;cp.cooldownMs=shaman?500:0;
        cp.schoolMask=1;cp.spellFamily=child.spellFamily;cp.hitMask=LocalProcHitNormal|LocalProcHitCritical|LocalProcHitAbsorb;
        auto& pp=parent.proc;pp.effect=LocalProcEffect::ApplyOwnerAura;pp.spellId=child.id;pp.flags=20;
        pp.chance=100;pp.amount=child.meleeHastePct;pp.schoolMask=1;pp.spellFamily=child.spellFamily;
        pp.spellTypeMask=1;pp.hitMask=LocalProcHitCritical;pp.allowTriggered=true;
        assert(validLocalProc(parent)&&validLocalProc(child));
        LocalSpellDefinition crit;crit.id=900001;crit.passive=true;crit.talentId=9000;crit.talentRank=1;
        crit.allowableClasses=classes;crit.passiveMeleeCritPct=100;
        c->spells.insert(c->spells.end(),{parent,child,crit});
        if(shaman) {
            // Capability-only component definitions. Source closure and the
            // still-gated normal Dual Wield route are covered separately.
            LocalSpellDefinition spirit;spirit.id=16268;spirit.talentId=616;spirit.talentRank=1;
            spirit.passive=true;spirit.allowableClasses=64;spirit.passiveCanParry=true;
            LocalSpellDefinition dual;dual.id=30798;dual.talentId=1690;dual.talentRank=1;
            dual.passive=true;dual.allowableClasses=64;dual.passiveCanDualWield=true;
            dual.talentPrerequisites[0]=616;
            c->spells.insert(c->spells.end(),{spirit,dual});
        }
        std::sort(c->spells.begin(),c->spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});game.useContent(c);
        p.classId=shaman?7:1;p.level=80;p.health=p.maxHealth=1000000;p.mana=p.maxMana=100;
        p.resourceType=shaman?LocalResourceType::Mana:LocalResourceType::Rage;
        p.talents={{parent.talentId,uint8_t(rank)},{9000,1}};p.inventory={{25,1},{727,1}};p.equipment[15]=25;
        p.meleeWeaponMain=25;p.meleePeriodMain=localMeleeSpeed(p,*c);
        n.health=n.maxHealth=1000000;n.level=80;n.x=1;n.attackTimer=1000;n.targetGuid=p.guid;
    }
    std::vector<LocalCombatEvent> swing(float dt=.001f,bool off=false) {
        game.setRemoteNpcs({n});p.attackTarget=n.guid;p.attackTimer=0;if(off)p.offHandTimer=0;
        game.tick(dt,{&p});std::vector<LocalCombatEvent> out;
        for(const auto& e:game.combatEvents())if(e.sequence>sequence)out.push_back(e);
        if(!out.empty())sequence=out.back().sequence;
        return out;
    }
    void arm(unsigned charges=3,uint32_t cooldown=0) {
        const auto main=localMeleeSpeed(p,*c),off=localMeleeSpeed(p,*c,true);
        p.statAuras.erase(std::remove_if(p.statAuras.begin(),p.statAuras.end(),[&](const auto& a){return a.spellId==child.id;}),p.statAuras.end());
        LocalStatAura a{child.id,15000,p.mapId,p.instanceId,p.guid};a.procCharges=charges;a.procCooldownMs=cooldown;
        a.procAmountSnapshot=child.proc.amount;a.hasProcAmountSnapshot=true;
        // This fixture creates an application exactly as the authority proc
        // producer does, before any unchanged-state snapshot is captured.
        localPrepareAuraApplication(p,a);p.statAuras.push_back(a);
        localRescaleMeleeTimers(p,main,off,*c);
    }
    void quiet(){p.attackTarget=0;game.setRemoteNpcs({});}
};
}
int main() {
    unsigned swings=0,applications=0,consumptions=0,avoids=0;
    for(bool shaman:{false,true})for(unsigned rank=1;rank<=5;++rank) {
        Fixture f(shaman,rank);
        for(unsigned iteration=0;iteration<32;++iteration) {
            const auto* prior=active(f.p,f.child.id);
            const bool prepared=prior&&prior->procCharges&&prior->procCooldownMs<=1;
            const auto priorCharges=prior?prior->procCharges:0;
            auto events=f.swing();bool critical=false,eligible=false;unsigned applied=0,consumed=0;uint8_t consumedCharges=0;
            uint64_t swingSequence=0;
            for(const auto& e:events) {
                if(e.kind==LocalCombatEventKind::PlayerMelee&&e.source==f.p.guid) {
                    ++swings;swingSequence=e.sequence;critical=localProcMatches(f.parent.proc,e,f.p.guid);
                    eligible=localProcMatches(f.child.proc,e,f.p.guid);if(localMeleeAvoided(e.outcome))++avoids;
                    assert(e.weaponPeriodMs==1900);
                }
                if(e.kind==LocalCombatEventKind::ProcAura) {
                    assert(e.source==f.p.guid&&e.target==f.p.guid&&e.spell==f.child.id&&e.procDepth==1);
                    assert(e.auraOwnerGuid==f.p.guid&&e.auraCasterGuid==f.p.guid&&e.parentSequence==swingSequence&&e.rootSequence==swingSequence);
                    assert(localProcEventFlags(e,f.p.guid)==0);
                    if(e.auraApplied){++applied;assert(e.auraCharges==3&&e.auraDurationMs==15000&&e.auraSpell==f.parent.id);}
                    else {++consumed;consumedCharges=e.auraCharges;assert(e.auraSpell==f.child.id);}
                }
            }
            if(!(swingSequence&&applied==unsigned(critical)&&consumed==unsigned(prepared&&eligible)))
                std::cerr<<"dispatch mismatch shaman="<<shaman<<" rank="<<rank<<" iteration="<<iteration<<" swing="<<swingSequence<<" applied="<<applied<<" critical="<<critical<<" consumed="<<consumed<<" prepared="<<prepared<<" eligible="<<eligible<<" auraCount="<<f.p.statAuras.size()<<"\n";
            assert(swingSequence&&applied==unsigned(critical)&&consumed==unsigned(prepared&&eligible));
            if(consumed)assert(consumedCharges==priorCharges-1);
            if(applied)assert(active(f.p,f.child.id)->procCharges==3);
            if(shaman&&consumed)assert(active(f.p,f.child.id)->procCooldownMs==500);
            applications+=applied;consumptions+=consumed;
            assert(validLocalStatAuras(f.p));
        }
        assert(active(f.p,f.child.id));
        assert(near(localMeleeSpeed(f.p,*f.c),1.9f/(1+f.child.meleeHastePct/100.f)));
        // Unrelated magic casts, direct internal casts and failed actions do
        // not spend charges or create another child.
        f.arm();f.p.knownSpells.push_back(f.child.id);f.p.globalCooldownMs=0;f.p.cooldowns.clear();
        const auto before=f.p.statAuras;
        assert(!f.game.execute(f.p,{LocalAction::CastSpell,f.p.guid,f.child.id},{&f.p},f.message));
        assert(f.p.statAuras==before);
        assert(f.game.execute(f.p,{LocalAction::CastSpell,f.n.guid,1},{&f.p},f.message));
        assert(f.p.statAuras==before);
        // Save hydration clamps duration/charges and reconstructs self-caster;
        // no higher-rank aura is accepted for a lower actual allocation.
        f.quiet();active(f.p,f.child.id)->remainingMs=60000;active(f.p,f.child.id)->casterGuid=0;
        f.game.initializePlayer(f.p,false);assert(active(f.p,f.child.id)->remainingMs==15000&&active(f.p,f.child.id)->casterGuid==f.p.guid);
        f.p.talents[0].second=rank==1?2:rank-1;f.game.initializePlayer(f.p,false);assert(!active(f.p,f.child.id));
        // Login deliberately clears weapon scheduling caches. Establish the
        // equipped hand before measuring aura-only timer transitions.
        f.p.talents[0].second=rank;f.p.meleeWeaponMain=25;f.arm();f.p.attackTimer=localMeleeSpeed(f.p,*f.c)*.5f;
        assert(f.game.execute(f.p,{LocalAction::CancelStatAura,0,f.child.id},{&f.p},f.message));
        assert(!active(f.p,f.child.id)&&near(f.p.attackTimer,.95f));
        f.arm();f.p.attackTimer=localMeleeSpeed(f.p,*f.c)*.5f;active(f.p,f.child.id)->remainingMs=1;
        f.game.tick(.002f,{&f.p});assert(!active(f.p,f.child.id)&&near(f.p.attackTimer,.948f));
        f.arm();f.p.attackTimer=localMeleeSpeed(f.p,*f.c)*.5f;++f.p.mapId;
        f.game.tick(.001f,{&f.p});assert(!active(f.p,f.child.id)&&near(f.p.attackTimer,.949f));--f.p.mapId;
        f.arm();f.p.dead=true;f.p.health=0;f.game.tick(.001f,{&f.p});assert(!active(f.p,f.child.id));f.p.dead=false;f.p.health=1000;
        f.arm();assert(f.game.execute(f.p,{LocalAction::ResetTalents},{&f.p},f.message));assert(f.p.statAuras.empty()&&f.p.talents.empty());
        // Matching guards use explicit source outcomes, including full absorb
        // suppressing critical parent refresh while consuming the old charge.
        LocalCombatEvent e;e.kind=LocalCombatEventKind::PlayerMelee;e.source=1;e.target=10;e.attackType=LocalCombatAttackType::Melee;
        e.outcome=LocalMeleeOutcome::Critical;e.attempted=100;e.absorbed=100;
        assert(localProcMatches(f.child.proc,e,1)&&!localProcMatches(f.parent.proc,e,1));
        e.absorbed=0;e.blocked=100;e.outcome=LocalMeleeOutcome::Block;assert(!localProcMatches(f.child.proc,e,1));
        e.blocked=0;e.outcome=LocalMeleeOutcome::Miss;assert(!localProcMatches(f.child.proc,e,1));
        e.outcome=LocalMeleeOutcome::Critical;e.effective=100;e.procDepth=1;
        assert(localProcMatches(f.child.proc,e,1)&&localProcMatches(f.parent.proc,e,1));
        e.kind=LocalCombatEventKind::SpellDamage;assert(!localProcMatches(f.child.proc,e,1)&&localProcMatches(f.parent.proc,e,1));
        if(shaman)for(unsigned excluded:{17364u,60103u,0u}) {
            e.kind=LocalCombatEventKind::PlayerMelee;e.spell=excluded;e.spellFamily=excluded?0:11;e.spellFamilyFlags[0]=excluded?0:0x00800000;
            assert(!localProcMatches(f.child.proc,e,1));
        }
    }
    assert(swings==320&&applications&&consumptions&&avoids);
    // Source dual wield staggers the opposite hand by 200ms before callbacks.
    // Two successful hands inside 500ms spend two Warrior charges but only
    // one Shaman charge even if either critical refreshes all three charges.
    for(bool shaman:{false,true}) {
        Fixture f(shaman,5);f.p.equipment[16]=727;f.p.meleeWeaponOff=727;
        if(shaman) {
            f.p.talents.emplace_back(616,1);f.p.talents.emplace_back(1690,1);
            std::sort(f.p.talents.begin(),f.p.talents.end());
        }
        f.arm();unsigned paired=0;
        for(unsigned i=0;i<64;++i) {
            f.arm();auto events=f.swing(.001f,true);unsigned hands=0,consumed=0;
            for(const auto& e:events)if(e.kind==LocalCombatEventKind::PlayerMelee&&e.source==1)++hands;
            else if(e.kind==LocalCombatEventKind::ProcAura&&!e.auraApplied)++consumed;
            assert(hands==1&&near(f.p.offHandTimer,.2f));
            f.game.setRemoteNpcs({f.n});f.game.tick(.22f,{&f.p});
            for(const auto& e:f.game.combatEvents())if(e.sequence>f.sequence) {
                if(e.kind==LocalCombatEventKind::PlayerMelee&&e.source==1){++hands;assert(e.offHand);}
                else if(e.kind==LocalCombatEventKind::ProcAura&&!e.auraApplied)++consumed;
            }
            f.sequence=f.game.combatEvents().back().sequence;
            assert(hands==2);assert(consumed<=(shaman?1u:2u));if(consumed==(shaman?1u:2u))++paired;
        }
        assert(paired);
        // Fresh application scales the newly installed opposite-hand delay,
        // while the completed main hand resets using its post-proc period.
        bool applied=false;
        for(unsigned i=0;i<32&&!applied;++i) {
            f.p.statAuras.clear();const auto events=f.swing(.001f,true);
            for(const auto& e:events)if(e.kind==LocalCombatEventKind::ProcAura&&e.auraApplied)applied=true;
            if(applied) {
                assert(near(f.p.offHandTimer,.2f/(1+f.child.meleeHastePct/100.f)));
                assert(near(f.p.attackTimer,1.9f/(1+f.child.meleeHastePct/100.f)));
            }
        }
        assert(applied);
    }
    // Invalid direct prerequisite allocations must neither activate a parent
    // nor retain its otherwise well-formed saved child.
    for(bool shaman:{false,true}) {
        Fixture f(shaman,5);
        for(auto& s:f.c->spells)if(s.id==f.parent.id)s.talentPrerequisites[0]=9998;
        f.game.useContent(f.c);f.arm();f.game.initializePlayer(f.p,false);assert(!active(f.p,f.child.id));
        for(unsigned i=0;i<32;++i) {
            for(const auto& e:f.swing())assert(e.kind!=LocalCombatEventKind::ProcAura);
            assert(!active(f.p,f.child.id));
        }
    }
    // Capacity and nested callback cases exercise real dispatcher storage.
    for(unsigned count:{15u,16u})for(unsigned shieldCharges:{1u,2u}) {
        Fixture f(false,5);
        for(unsigned i=0;i<count;++i) {
            LocalSpellDefinition s;s.id=910000+i;s.durationMs=60000;s.buffArmor=1;
            if(i==0){s.buffArmor=0;s.proc.effect=LocalProcEffect::DamageAttacker;s.proc.spellId=99999;s.proc.flags=4;s.proc.chance=100;
                s.proc.charges=shieldCharges;s.proc.amount=1;s.proc.range=20;s.proc.schoolMask=4;}
            assert(validLocalProc(s));f.c->spells.push_back(s);
        }
        std::sort(f.c->spells.begin(),f.c->spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});f.game.useContent(f.c);
        bool sawCritical=false;
        for(unsigned attempt=0;attempt<32&&!sawCritical;++attempt) {
            f.p.statAuras.clear();
            for(unsigned i=0;i<count;++i){LocalStatAura a{910000+i,60000,0,0,1};if(i==0){a.procCharges=shieldCharges;a.procAmountSnapshot=1;a.hasProcAmountSnapshot=true;}f.p.statAuras.push_back(a);}
            for(const auto& e:f.swing())if(e.kind==LocalCombatEventKind::PlayerMelee&&e.outcome==LocalMeleeOutcome::Critical)sawCritical=true;
        }
        assert(sawCritical);
        assert(bool(active(f.p,f.child.id))==(count<16||shieldCharges==1));
        assert(f.p.statAuras.size()<=kLocalMaxStatAuras&&validLocalStatAuras(f.p));
    }
    std::cout<<"PASS Flurry runtime: 320 authority main-hand events across ten profiles; "<<applications<<" critical applications, "<<consumptions<<" prepared consumptions, "<<avoids<<" avoided hits; 256 dual-hand events; nested callback/capacity, source filters, internal-only, rank hydration, cancel/expiry/map/death/reset and timer progress\n";
}
