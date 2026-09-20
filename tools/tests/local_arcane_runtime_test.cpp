#include "local_group_rewards_fixture.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_arcane.hpp"
#include "game/local_spell_amount.hpp"
#include "game/local_spell_critical.hpp"
#include "game/local_class_pools.hpp"
#include "game/local_stat_auras.hpp"
#include "game/local_clearcasting.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace wowee;
namespace {
struct Fixture {
    std::shared_ptr<LocalWorldContent> c=rewardContent();LocalGameplay game;
    LocalRealmPlayer p=rewardPlayer(1);LocalRealmNpc n=rewardNpc();std::string message;uint64_t sequence=0;
    Fixture(const std::vector<LocalSpellDefinition>& spells) {
        c->spells=spells;c->npcs[0].health=1000000;c->npcs[0].armor=0;game.useContent(c);
        p.classId=8;p.race=1;p.level=80;p.resourceType=LocalResourceType::Mana;p.health=p.maxHealth=1000000;
        p.manaRegenDelayMs=1000000;p.talents={{80,5}};
        const auto pools=localResourcePools(p,*c);p.mana=p.maxMana=pools.mana;p.health=p.maxHealth=pools.health;
        for(const auto& d:spells)if(d.unsupportedReason.empty()&&!d.passive&&!d.triggeredOnly&&(d.allowableClasses&128))p.knownSpells.push_back(d.id);
        n.health=n.maxHealth=1000000;n.level=80;n.attackTimer=1000;n.x=n.homeX=1;n.targetGuid=p.guid;n.threat[0]={p.guid,1000};game.setRemoteNpcs({n});
    }
    bool cast(uint32_t id,uint64_t target=10) {
        p.globalCooldownMs=0;p.cooldowns.clear();p.categoryCooldowns.clear();
        const bool ok=game.execute(p,{LocalAction::CastSpell,target,id},{&p},message);
        if(!ok)std::cerr<<"cast "<<id<<": "<<message<<"\n";return ok;
    }
    void advance(unsigned ms) {while(ms){const auto n=std::min(ms,250u);game.tick(n/1000.f,{&p});ms-=n;}}
    std::vector<LocalCombatEvent> events(){std::vector<LocalCombatEvent> out;for(const auto& e:game.combatEvents())if(e.sequence>sequence)out.push_back(e);if(!out.empty())sequence=out.back().sequence;return out;}
    void stack(unsigned count) {p.statAuras.clear();for(unsigned k=0;k<count;++k)assert(applyLocalArcaneBlast(p,*c,*c->spell(30451)));}
};
}
int main(int argc,char** argv) {
    assert(argc==2);std::map<std::string,pipeline::DBCFile> tables;
    for(const char* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon","SpellRadius","SpellRuneCost","SkillLine","SkillLineAbility","Talent","TalentTab"}) {
        std::ifstream in(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),{}};assert(tables[name].load(bytes));
    }
    const auto get=[&](const char* n){return &tables.at(n);};
    auto imported=importClientStarterSpells(get("Spell"),get("SpellRange"),get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SkillLineAbility"),get("SkillLine"),get("Talent"),get("SpellRuneCost"),get("SpellRadius"));
    detail::importClientTalents(imported,get("Talent"),get("TalentTab"),get("Spell"),get("SpellRange"),get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SpellRuneCost"),get("SpellRadius"));
    unsigned casts=0;
    for(uint32_t id:{30451,42894,42896,42897}) {
        Fixture f(imported.spells);const auto* d=f.c->spell(id);assert(d&&d->unsupportedReason.empty());
        const uint32_t baseMana=uint64_t(localBaseMana(f.p))*7/100;
        for(unsigned n=0;n<6;++n) {
            const unsigned stacks=std::min(n,4u);assert(localArcaneBlastStacks(f.p,*f.c)==stacks);
            f.p.mana=f.p.maxMana;const auto before=f.p.mana;const auto quoted=localSpellResourceCost(f.p,*f.c,*d);
            assert(quoted==baseMana*(100+175*stacks)/100);
            assert(f.cast(id));assert(f.p.mana==before&&f.p.castPreparedCost==quoted);
            f.advance(2500);assert(f.p.castStatus==LocalCastStatus::Finished);assert(f.p.mana==before-quoted);
            assert(localArcaneBlastStacks(f.p,*f.c)==std::min(n+1,4u));
            bool hit=false;for(const auto& e:f.events())if(e.kind==LocalCombatEventKind::SpellDamage&&e.spell==id) {
                const unsigned effectiveLevel=std::min<unsigned>(f.p.level,d->maxLevel);
                const auto base=uint32_t((double(d->damage)+d->damageMax)*.5+double(effectiveLevel-d->baseLevel)*d->damagePerLevel);
                const auto damage=uint32_t(uint64_t(base)*(100+15*stacks)/100);
                assert(e.attempted==(e.outcome==LocalMeleeOutcome::Critical?localMagicCriticalAmount(damage):damage));hit=true;
            }
            assert(hit&&validLocalStatAuras(f.p));++casts;
        }
        f.advance(6000);assert(!localArcaneBlastStacks(f.p,*f.c));
    }
    for(unsigned scenario=0;scenario<4;++scenario) {
        Fixture f(imported.spells);f.stack(4);const auto* d=f.c->spell(30451);f.p.statAuras[0].remainingMs=1000;
        const auto before=f.p.mana,quoted=localSpellResourceCost(f.p,*f.c,*d);assert(f.cast(d->id));
        if(scenario==0){assert(f.game.execute(f.p,{LocalAction::CancelCast},{&f.p},f.message));assert(f.p.mana==before&&localArcaneBlastStacks(f.p,*f.c)==4);continue;}
        if(scenario==1){f.p.statAuras.clear();} // Reserved cost survives removal.
        if(scenario==2){assert(applyLocalArcaneBlast(f.p,*f.c,*d));} // Refresh after preparation.
        f.advance(2500);assert(f.p.castStatus==LocalCastStatus::Finished&&f.p.mana==before-quoted);
        assert(localArcaneBlastStacks(f.p,*f.c)==(scenario==2?4:1));++casts;
    }
    {
        Fixture f(imported.spells);f.stack(4);f.p.talents={{75,5},{80,5}};
        LocalStatAura charge{12536,15000,f.p.mapId,f.p.instanceId,f.p.guid};charge.procCharges=1;charge.procAmountSnapshot=1;charge.hasProcAmountSnapshot=true;
        charge.costModGeneration=nextLocalCostModGeneration(f.p);f.p.statAuras.push_back(charge);f.p.mana=0;
        assert(!localSpellResourceCost(f.p,*f.c,*f.c->spell(30451)));assert(f.cast(30451));f.advance(2500);
        assert(f.p.castStatus==LocalCastStatus::Finished&&f.p.mana==0&&localArcaneBlastStacks(f.p,*f.c)==4);++casts;
        bool consumed=false;for(const auto& e:f.events())if(e.kind==LocalCombatEventKind::ProcAura&&e.spell==12536&&!e.auraApplied)consumed=true;assert(consumed);
    }
    {
        Fixture f(imported.spells);f.stack(4);auto second=f.n;second.guid=11;second.x=second.homeX=2;f.game.setRemoteNpcs({f.n,second});
        const auto* d=f.c->spell(1449);assert(d&&d->unsupportedReason.empty());assert(f.cast(1449,0));
        unsigned hits=0;uint32_t base=uint32_t((double(d->damage)+d->damageMax)*.5+double(std::min<unsigned>(f.p.level,d->maxLevel)-d->baseLevel)*d->damagePerLevel);
        const uint32_t amount=uint64_t(base)*160/100;
        for(const auto& e:f.events())if(e.kind==LocalCombatEventKind::SpellDamage&&e.spell==1449){assert(e.attempted==(e.outcome==LocalMeleeOutcome::Critical?localMagicCriticalAmount(amount):amount));++hits;}
        assert(hits==2&&!localArcaneBlastStacks(f.p,*f.c));++casts;
    }
    {
        Fixture f(imported.spells);f.stack(3);assert(f.cast(2136));assert(localArcaneBlastStacks(f.p,*f.c)==3);
        auto copy=f.p;copy.classId=11;assert(!localArcaneBlastStacks(copy,*f.c));copy=f.p;copy.statAuras[0].casterGuid=9;assert(!localArcaneBlastStacks(copy,*f.c));
        copy=f.p;copy.mapId=1;assert(!localArcaneBlastStacks(copy,*f.c));f.p.dead=true;f.advance(1);assert(f.p.statAuras.empty());++casts;
    }
    {
        Fixture f(imported.spells);const auto before=f.p.mana;
        for(unsigned i=0;i<kLocalMaxStatAuras;++i)f.p.statAuras.push_back({900000+i,6000,0,0,f.p.guid});
        assert(!f.cast(30451));assert(f.p.mana==before&&!f.p.castingSpellId);
        // The semantic reservation remains required if a proc wants the last
        // free slot during damage. Exercise it repeatedly with actual parent.
        Fixture limited(imported.spells);limited.p.talents={{75,5},{80,5}};
        // Component-only occupancy: authored one-health timed buffs under
        // unique fixture IDs. These records never enter the source importer.
        LocalSpellDefinition occupancy;occupancy.name="Capacity fixture";occupancy.buffHealth=1;occupancy.durationMs=6000;
        assert(localHasTimedAura(occupancy)&&validLocalProc(occupancy));
        for(unsigned i=0;i<kLocalMaxStatAuras-1;++i){auto copy=occupancy;copy.id=900000+i;limited.c->spells.push_back(copy);}
        std::sort(limited.c->spells.begin(),limited.c->spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        for(unsigned iteration=0;iteration<64;++iteration) {
            limited.p.statAuras.clear();limited.p.mana=localResourcePools(limited.p,*limited.c).mana;
            assert(limited.cast(30451));
            limited.advance(2250);limited.p.statAuras.clear();
            for(unsigned i=0;i<kLocalMaxStatAuras-1;++i)limited.p.statAuras.push_back({900000+i,6000,0,0,limited.p.guid});
            limited.advance(250);assert(limited.p.castStatus==LocalCastStatus::Finished);
            assert(localArcaneBlastStacks(limited.p,*limited.c)==1&&limited.p.statAuras.size()==kLocalMaxStatAuras);++casts;
        }
    }
    std::cout<<"PASS: Arcane Blast "<<casts<<" authority casts, four ranks, stacks/cost/damage/max-refresh/expiry, cancellation/preparedcost, Clearcasting, multi-target Explosion, school/class/caster/map/death/capacity guards\n";
}
