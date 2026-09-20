#include "local_group_rewards_fixture.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_proc_talents.hpp"
#include "game/local_stat_auras.hpp"
#include "game/local_combo.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace wowee;
namespace {
struct Fixture {
    std::shared_ptr<LocalWorldContent> c=rewardContent();
    LocalGameplay game;LocalRealmPlayer p=rewardPlayer(1);LocalRealmNpc n=rewardNpc();std::string message;
    uint32_t childId;uint64_t sequence=0;
    Fixture(const std::vector<LocalSpellDefinition>& spells,bool druid=false):childId(druid?16870:12536) {
        c->spells=spells;c->npcs[0].health=1000000;c->npcs[0].armor=0;
        game.useContent(c);p.classId=druid?11:8;p.race=1;p.level=80;p.resourceType=LocalResourceType::Mana;
        p.health=p.maxHealth=1000000;p.mana=p.maxMana=1000000;p.manaRegenDelayMs=5000;
        // Runtime fixtures exercise the admitted current rank. The separate
        // progression audit reports blocked natural paths; this is no unlock.
        p.talents={{druid?827u:75u,uint8_t(druid?1:5)}};
        for(const auto& d:spells)if(d.unsupportedReason.empty()&&!d.passive&&!d.triggeredOnly&&(d.allowableClasses&(1u<<(p.classId-1))))p.knownSpells.push_back(d.id);
        n.health=n.maxHealth=1000000;n.level=80;n.attackTimer=1000;n.x=1;n.targetGuid=p.guid;n.threat[0]={p.guid,1000};game.setRemoteNpcs({n});
        assert(c->spell(childId)&&validLocalProc(*c->spell(childId)));
        assert(c->spell(childId)->proc.attributesMask==12&&c->spell(childId)->proc.sourceEffectMask==1);
    }
    LocalStatAura* aura(uint32_t id=0) {for(auto& a:p.statAuras)if(a.spellId==(id?id:childId)&&a.remainingMs)return &a;return nullptr;}
    void arm(uint32_t duration=15000) {
        LocalStatAura a{childId,duration,p.mapId,p.instanceId,p.guid};a.procCharges=1;a.procAmountSnapshot=1;a.hasProcAmountSnapshot=true;
        a.costModGeneration=nextLocalCostModGeneration(p);
        auto* old=aura();if(old)*old=a;else p.statAuras.push_back(a);
    }
    bool cast(uint32_t id,uint64_t target=10) {
        p.globalCooldownMs=0;p.cooldowns.clear();p.categoryCooldowns.clear();
        const bool ok=game.execute(p,{LocalAction::CastSpell,target,id},{&p},message);
        if(!ok)std::cerr<<"cast "<<id<<": "<<message<<"\n";return ok;
    }
    void advance(unsigned ms) {while(ms){const auto step=std::min(ms,250u);game.tick(step/1000.f,{&p});ms-=step;}}
    std::vector<LocalCombatEvent> events() {
        std::vector<LocalCombatEvent> out;for(const auto& e:game.combatEvents())if(e.sequence>sequence)out.push_back(e);
        if(!out.empty())sequence=out.back().sequence;return out;
    }
};
unsigned consumed(const std::vector<LocalCombatEvent>& events,uint32_t id) {
    return unsigned(std::count_if(events.begin(),events.end(),[&](const auto& e){return e.kind==LocalCombatEventKind::ProcAura&&e.spell==id&&!e.auraApplied;}));
}
}
int main(int argc,char** argv) {
    assert(argc==2);std::map<std::string,pipeline::DBCFile> tables;
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon","SkillLineAbility","SkillLine","Talent","SpellRuneCost","SpellRadius","TalentTab"}) {
        std::ifstream in(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),{}};assert(tables[name].load(bytes));
    }
    const auto get=[&](const char* name){return &tables.at(name);};
    auto imported=importClientStarterSpells(get("Spell"),get("SpellRange"),get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SkillLineAbility"),get("SkillLine"),get("Talent"),get("SpellRuneCost"),get("SpellRadius"));
    detail::importClientTalents(imported,get("Talent"),get("TalentTab"),get("Spell"),get("SpellRange"),get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SpellRuneCost"),get("SpellRadius"));
    unsigned refreshes=0,casts=0;
    for(bool druid:{false,true}) {
        unsigned familyRefreshes=0;
        Fixture f(imported.spells,druid);const uint32_t instant=druid?774:2136;
        const auto* d=f.c->spell(instant);assert(d&&d->unsupportedReason.empty());
        for(unsigned i=0;i<128;++i) {
            f.arm();const auto identity=f.aura()->costModGeneration;f.p.mana=0;
            const auto before=f.p.statAuras;
            assert(localSpellResourceCost(f.p,*f.c,*d)==0&&f.p.statAuras==before);
            assert(f.cast(instant,druid?f.p.guid:f.n.guid));++casts;
            const auto events=f.events();assert(consumed(events,f.childId)==1);
            uint64_t castSequence=0,consumeSequence=0,hitSequence=0;
            for(const auto& e:events) {
                if(e.kind==LocalCombatEventKind::SpellCast){castSequence=e.sequence;assert(e.appliedCostAuraGeneration==identity);}
                if(e.kind==LocalCombatEventKind::ProcAura&&!e.auraApplied){consumeSequence=e.sequence;assert(e.parentSequence==castSequence);}
                if(e.kind==LocalCombatEventKind::SpellDamage||e.kind==LocalCombatEventKind::SpellHit)hitSequence=e.sequence;
            }
            assert(castSequence&&consumeSequence>castSequence&&hitSequence>consumeSequence);
            if(auto* a=f.aura()){assert(a->costModGeneration!=identity&&a->procCharges==1);++refreshes;++familyRefreshes;}
            assert(f.p.mana==0&&!f.p.castCostPrepared&&validLocalStatAuras(f.p));
        }
        assert(familyRefreshes>0);
        const auto& parent=*localTalentSpell(*f.c,druid?827:75,druid?1:5);
        if(druid) {
            LocalCombatEvent e;e.source=f.p.guid;e.target=f.p.guid;e.spell=774;e.kind=LocalCombatEventKind::SpellHit;
            e.attackType=LocalCombatAttackType::Magic;e.positiveSpell=true;e.spellTypeMask=4;e.omenProcEligible=true;
            assert(localProcMatches(parent.proc,e,f.p.guid));
            e.sourceRawCastTimeMs=0;assert(localProcChanceBasisPoints(parent.proc,e)==875);
            e.sourceRawCastTimeMs=3000;assert(localProcChanceBasisPoints(parent.proc,e)==1750);
            e.kind=LocalCombatEventKind::PeriodicHeal;assert(!localProcMatches(parent.proc,e,f.p.guid));
            e.kind=LocalCombatEventKind::SpellHit;e.omenProcEligible=false;assert(!localProcMatches(parent.proc,e,f.p.guid));
        }
    }
    assert(refreshes>0);
    // Prepare-time cost survives natural expiry, explicit refresh and removal.
    for(unsigned scenario=0;scenario<6;++scenario) {
        Fixture f(imported.spells);f.arm(scenario==0?1:15000);f.p.mana=0;
        assert(f.cast(116));assert(f.p.castingSpellId==116&&f.p.castCostPrepared&&f.p.castPreparedCost==0);
        const auto identity=f.p.castCostModGeneration;
        if(scenario==1)f.arm();
        if(scenario==2)assert(f.game.execute(f.p,{LocalAction::CancelStatAura,0,f.childId},{&f.p},f.message));
        if(scenario==3){assert(f.game.execute(f.p,{LocalAction::CancelCast},{&f.p},f.message));assert(f.aura()&&f.aura()->procCharges==1&&!f.p.castCostPrepared);continue;}
        if(scenario==4){f.n.x=f.n.homeX=55;f.game.setRemoteNpcs({f.n});}
        if(scenario==5){f.game.initializePlayer(f.p,false);assert(!f.p.castingSpellId&&!f.p.castCostPrepared&&f.aura()&&f.aura()->procCharges==1);continue;}
        f.advance(2000);const auto events=f.events();assert(consumed(events,f.childId)==0);
        assert(!f.p.castingSpellId&&!f.p.castCostPrepared&&f.p.mana==0);
        if(scenario==4){assert(f.p.castStatus==LocalCastStatus::Failed&&f.aura()&&f.aura()->procCharges==1);}
        else {if(f.p.castStatus!=LocalCastStatus::Finished)std::cerr<<"scenario="<<scenario<<" status="<<unsigned(f.p.castStatus)<<" spell="<<f.p.castingSpellId<<" health="<<f.p.health<<" msg="<<f.message<<"\n";assert(f.p.castStatus==LocalCastStatus::Finished);}
        if(scenario==1)assert(f.aura()&&f.aura()->costModGeneration!=identity&&f.aura()->procCharges==1);
    }
    // A charge obtained after preparation cannot change or pay for that cast.
    {
        Fixture f(imported.spells);f.p.mana=1000;const auto cost=localSpellResourceCost(f.p,*f.c,*f.c->spell(116));
        assert(cost>0&&f.cast(116));f.arm();assert(f.p.castPreparedCost==cost&&f.p.castCostModGeneration==0);
        f.advance(2000);assert(f.p.mana==1000-cost&&consumed(f.events(),f.childId)==0&&f.aura());
    }
    // Recasting a buff after Clearcasting erases the first entry must preserve
    // both the matching replacement and an unrelated following aura.
    {
        Fixture f(imported.spells,true);const auto* buff=f.c->spell(467);assert(buff&&buff->unsupportedReason.empty());
        f.arm();LocalStatAura thorns{467,buff->durationMs,f.p.mapId,f.p.instanceId,f.p.guid};
        thorns.procAmountSnapshot=localProcAmountAtApplication(f.p,*f.c,*buff);thorns.hasProcAmountSnapshot=true;
        f.p.statAuras.push_back(thorns);const auto* earth=f.c->spell(974);assert(earth&&earth->unsupportedReason.empty());
        LocalStatAura other{974,5000,f.p.mapId,f.p.instanceId,2};other.procCharges=earth->proc.charges;
        other.procAmountSnapshot=localProcAmountAtApplication(f.p,*f.c,*earth);other.hasProcAmountSnapshot=true;f.p.statAuras.push_back(other);
        f.p.mana=0;assert(f.cast(467,f.p.guid));assert(f.aura(467)&&f.aura(974));
        assert(std::count_if(f.p.statAuras.begin(),f.p.statAuras.end(),[](auto& a){return a.spellId==467;})==1);
        assert(consumed(f.events(),f.childId)==1);
    }
    // Clearcasting removes Bite's base cost only. The source damage effect
    // still spends up to 30 extra energy on a landed finisher.
    {
        Fixture f(imported.spells,true);const auto* bite=f.c->spell(22568);assert(bite&&bite->unsupportedReason.empty());
        enterLocalForm(f.p,*localFormProfile(768));unsigned landed=0;
        for(unsigned i=0;i<32;++i) {
            f.arm();f.p.mana=50;addLocalCombo(f.p,f.n,3);f.game.setRemoteNpcs({f.n});
            assert(f.cast(22568));const auto events=f.events();assert(consumed(events,f.childId)==1);
            for(const auto& e:events)if(e.kind==LocalCombatEventKind::SpellDamage) {
                if(localMeleeAvoided(e.outcome))assert(f.p.mana==50);
                else {assert(f.p.mana==20);++landed;}
            }
        }
        assert(landed);
    }
    // Zero authored cost, wrong family, stale location/caster and missing
    // current parent talent never consume a valid charge through a UI quote.
    {
        Fixture f(imported.spells);f.arm();auto s=*f.c->spell(116);s.mana=0;s.manaPercent=0;
        assert(localSpellResourceCost(f.p,*f.c,s)==0&&f.aura());s=*f.c->spell(116);s.spellFamily=7;
        assert(localSpellResourceCost(f.p,*f.c,s)>0);s=*f.c->spell(116);f.aura()->casterGuid=2;
        assert(localSpellResourceCost(f.p,*f.c,s)>0);f.aura()->casterGuid=f.p.guid;f.aura()->instanceId=1;
        assert(localSpellResourceCost(f.p,*f.c,s)>0);f.aura()->instanceId=0;f.p.talents.clear();assert(localSpellResourceCost(f.p,*f.c,s)>0);
    }
    std::cout<<"PASS Clearcasting source-backed casts="<<casts<<" refreshed="<<refreshes
        <<"; prepare/expire/refresh/cancel/remove/fail/load, initial aura hit PPM, buff vector identity\n";
}
