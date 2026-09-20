#include "local_group_rewards_fixture.hpp"
#include "game/local_ranged.hpp"
#include "game/local_proc_rules.hpp"
#include <iostream>

// Runtime fixture admits no extra production spell. It instantiates the exact
// public action profiles with real catalog weapon/projectile metadata.
struct RangedFixture {
    std::shared_ptr<LocalWorldContent> c=rewardContent();LocalGameplay game;
    LocalRealmPlayer p=rewardPlayer(1);LocalRealmNpc n=rewardNpc();std::string message;
    uint32_t spellId,ammoId;
    RangedFixture(bool wand=false,uint32_t weaponId=0):spellId(wand?5019:75),ammoId(wand?0:2512) {
        if(!weaponId)weaponId=wand?11287:2504;
        const auto* w=localMeleeItem(weaponId);assert(w);
        if(w->subclass==3)ammoId=2516;
        c->items.clear();
        for(uint32_t id:{weaponId,ammoId})if(id) {
            LocalItemDefinition item;item.id=id;item.name="Source item";item.stack=id==ammoId?1000:1;
            item.inventoryType=id==weaponId?w->inventoryType:24;c->items.push_back(item);
            p.inventory.push_back({id,uint16_t(id==ammoId?100:1)});
        }
        std::sort(c->items.begin(),c->items.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        c->spells.clear();LocalSpellDefinition d;d.id=spellId;d.name=wand?"Shoot":"Auto Shot";d.clientSpell=true;
        d.rangedAutoProfile=wand?2:1;d.sourceDamageClass=wand?1:3;d.schoolMask=1;d.range=wand?30:35;d.minRange=wand?0:5;
        d.requiredItemClass=2;d.requiredItemSubclasses=wand?524288:262156;c->spells.push_back(d);
        p.classId=wand?8:3;p.race=1;p.level=80;p.knownSpells={spellId};p.equipment[17]=weaponId;
        p.mana=0;p.maxMana=100;p.resourceType=LocalResourceType::Mana;p.orientation=0;
        n.level=80;n.x=n.homeX=20;n.y=n.homeY=0;n.health=n.maxHealth=1000000;n.attackTimer=1000;
        // Keep the authored enemy in the region selector at the exact 500ms
        // wand deadline. An unbacked remote NPC is correctly evicted then.
        n.spawnId=10;n.guid=0xf130000000000000ULL|n.spawnId;
        c->spawns.push_back({n.spawnId,n.entry,n.mapId,n.x,n.y,n.z,n.orientation});
        game.useContent(c);game.tick(0,{&p});game.setRemoteNpcs({n});
    }
    bool start(){return game.execute(p,{LocalAction::CastSpell,n.guid,spellId},{&p},message);}
    unsigned shots()const {unsigned n=0;for(const auto& e:game.combatEvents())n+=e.kind==LocalCombatEventKind::PlayerRanged;return n;}
    uint32_t ammoCount()const {uint32_t n=0;for(const auto& s:p.inventory)if(s.itemId==ammoId)n+=s.count;return n;}
    void step(float dt){game.tick(dt,{&p});}
};
int main() {
    for(uint32_t weapon:{2504u,2509u,15807u}) {
        RangedFixture f(false,weapon);assert(f.start());assert(!f.shots());f.step(.001f);assert(f.shots()==1&&f.ammoCount()==99);
        const auto w=localRangedAmounts(f.p,*f.c,f.c->spells[0]);assert(w.active);
        bool cast=false,hit=false,finish=false;
        for(const auto& e:f.game.combatEvents()) {
            if(e.kind==LocalCombatEventKind::SpellCast){cast=true;assert(localProcEventFlags(e,1)==0x40);}
            if(e.kind==LocalCombatEventKind::PlayerRanged){hit=true;assert(cast&&e.attackType==LocalCombatAttackType::Ranged&&e.weaponPeriodMs==w.basePeriodMs);assert((localProcEventFlags(e,999)==0));}
            if(e.kind==LocalCombatEventKind::SpellFinish){finish=true;assert(hit&&!e.target&&localProcEventFlags(e,1)==0x40);}
        }
        assert(cast&&hit&&finish);
        const auto remaining=f.p.rangedRemainingMs;
        assert(f.start()&&!f.p.rangedAutoSpellId);assert(f.start()&&f.p.rangedRemainingMs==remaining);
        f.step(.001f);assert(f.shots()==1&&f.ammoCount()==99);
        assert(f.game.execute(f.p,{LocalAction::StopAttack},{&f.p},f.message));assert(!f.p.rangedAutoSpellId);
    }
    {
        RangedFixture f;
        for(auto [id,flags]:{std::pair{60000u,0x40u},std::pair{60001u,0x4u}}) {
            LocalSpellDefinition d;d.id=id;d.name="Ranged proc fixture";d.durationMs=60000;
            d.proc.effect=LocalProcEffect::RestoreMana;d.proc.spellId=id+100;d.proc.amount=7;d.proc.chance=100;
            d.proc.charges=1;d.proc.flags=flags;d.proc.phaseMask=LocalProcPhaseHit;
            d.proc.hitMask=LocalProcHitNormal|LocalProcHitCritical|LocalProcHitMiss;
            assert(validLocalProc(d));f.c->spells.push_back(d);
            LocalStatAura aura{id,60000,0,0,f.p.guid};aura.procCharges=1;f.p.statAuras.push_back(aura);
        }
        assert(f.start());f.step(.001f);unsigned manaProcs=0;
        for(const auto& e:f.game.combatEvents())if(e.kind==LocalCombatEventKind::ProcMana){++manaProcs;assert(e.auraSpell==60000&&e.effective==7&&e.parentSequence);}
        assert(manaProcs==1&&f.p.statAuras.size()==1&&f.p.statAuras[0].spellId==60001);
    }
    {
        RangedFixture f;f.p.inventory.pop_back();assert(!f.start()&&f.game.combatEvents().empty());
    }
    {
        RangedFixture f;f.game.setRemoteNpcs({f.n});f.p.x=19;assert(!f.start());f.p.x=-20;assert(!f.start());
        f.p.x=0;f.p.orientation=3.14159265f;assert(!f.start());f.p.orientation=0;assert(f.start());
        f.p.positionRevision++;f.step(.1f);assert(!f.shots()&&!f.p.rangedAutoSpellId);
    }
    {
        RangedFixture f;assert(f.start());f.p.x=1;f.step(.1f);assert(!f.shots()&&f.p.rangedAutoSpellId);
        f.step(.1f);assert(f.shots()==1);f.p.equipment[17]=0;f.step(.1f);assert(!f.p.rangedAutoSpellId);
    }
    {
        RangedFixture f(true);const auto item=localMeleeItem(f.p.equipment[17]);assert(item&&item->school[0]);
        assert(f.start());f.step(.25f);assert(!f.shots());f.step(.25f);assert(f.shots()==1);
        for(const auto& e:f.game.combatEvents())if(e.kind==LocalCombatEventKind::PlayerRanged){assert(e.schoolMask==(1u<<item->school[0]));assert(e.attackType==LocalCombatAttackType::Ranged);}
        f.p.x+=1;f.step(.01f);assert(!f.p.rangedAutoSpellId);
    }
    {
        // Stat component: source item4696 carries -15 agility. This directly
        // authored equipment fixture checks signed RAP arithmetic, not item
        // level/proficiency admission through the EquipItem action.
        RangedFixture f(true);f.p.level=1;
        LocalItemDefinition orb;orb.id=4696;orb.name="Source negative agility";orb.inventoryType=23;
        f.c->items.push_back(orb);std::sort(f.c->items.begin(),f.c->items.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        f.p.inventory.push_back({4696,1});f.p.equipment[16]=4696;
        const auto stats=localMeleeStats(f.p,*f.c);assert(stats.attributes[1]<10);
        const auto amount=localRangedAmounts(f.p,*f.c,f.c->spells[0]);assert(amount.active);
        const auto* wand=localMeleeItem(f.p.equipment[17]);assert(amount.low==wand->damage[0]&&amount.high==wand->damage[1]);
    }
    {
        RangedFixture f;auto& d=f.c->spells[0];const auto a=localRangedAmounts(f.p,*f.c,d);assert(a.active);
        unsigned miss=0,crit=0;for(uint32_t roll=0;roll<10000;++roll) {
            // Distinct miss/critical samples: using the same ascending value
            // masks every critical when the base crit chance is below 5%.
            const auto r=localRollRanged(f.p,f.n,a,roll,9999-roll);miss+=r==LocalMeleeOutcome::Miss;crit+=r==LocalMeleeOutcome::Critical;
            assert(r==LocalMeleeOutcome::Hit||r==LocalMeleeOutcome::Miss||r==LocalMeleeOutcome::Critical);
        }
        assert(miss==500&&crit>0);auto high=f.n;high.level=83;
        assert(localRollRanged(f.p,high,a,700,9999)==LocalMeleeOutcome::Miss);
        assert(localRollRanged(f.p,f.n,a,499,9999)==LocalMeleeOutcome::Miss);
        assert(localRollRanged(f.p,f.n,a,500,9999)==LocalMeleeOutcome::Hit);
    }
    std::cout<<"PASS ranged authority: bow/gun/crossbow ammo, real public auto action, CAST/HIT/FINISH flags, rate-limit, stop, movement, map revision, weapon removal, wand delay/school, ranged hit/crit outcomes\n";
}
