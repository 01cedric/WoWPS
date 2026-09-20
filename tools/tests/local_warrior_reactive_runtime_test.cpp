#include "local_group_rewards_fixture.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_reactive_talents.hpp"
#include "game/local_stat_auras.hpp"
#include "game/local_armor.hpp"
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
    uint64_t seen=0;
    Fixture(const std::vector<LocalSpellDefinition>& spells,unsigned rank=3) {
        c->spells=spells;c->npcs[0].health=1000000;c->npcs[0].armor=0;c->npcs[0].damage=100;
        // Explicit fixed-damage component fixtures isolate multiplier ordering;
        // admission and all talent/child data remain the real source profiles.
        LocalSpellDefinition direct;direct.id=900001;direct.name="Physical amount fixture";
        direct.clientSpell=true;direct.sourceCantCrit=true;
        direct.damage=100;direct.schoolMask=1;direct.range=50;c->spells.push_back(direct);
        auto dot=direct;dot.id=900002;dot.name="Physical periodic fixture";dot.damage=0;
        dot.periodicDamage=100;dot.durationMs=4000;dot.periodicIntervalMs=1000;c->spells.push_back(dot);
        auto magic=direct;magic.id=900003;magic.name="Magic amount fixture";magic.clientSpell=true;
        magic.sourceDamageClass=1;magic.sourceCantCrit=true;magic.schoolMask=4;c->spells.push_back(magic);
        LocalSpellDefinition observer;observer.id=900004;observer.name="Periodic observer fixture";
        observer.passive=true;observer.allowableClasses=1;observer.talentId=9999;observer.talentRank=1;
        observer.proc.effect=LocalProcEffect::RestorePower;observer.proc.spellId=900005;
        observer.proc.flags=0x40000;observer.proc.chance=100;observer.proc.amount=7;observer.proc.resourceType=1;
        assert(validLocalProc(observer));c->spells.push_back(observer);
        std::sort(c->spells.begin(),c->spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        game.useContent(c);p.classId=1;p.level=80;p.resourceType=LocalResourceType::Rage;p.mana=p.maxMana=100;
        game.initializePlayer(p,false);
        // Full ordinary Fury path, also audited independently against DBC.
        for(auto [talent,ranks]:{std::pair{157u,5u},{2250u,3u},{159u,5u},{661u,3u},{1581u,5u},{1657u,3u},{165u,1u}})
            for(unsigned r=0;r<ranks;++r)assert(learnLocalTalent(p,*c,talent,r,message));
        for(auto& t:p.talents)if(t.first==661)t.second=uint8_t(rank);
        p.talents.push_back({9999,1});
        p.knownSpells.insert(p.knownSpells.end(),{900001,900002,900003});
        p.health=p.maxHealth;
        n.health=n.maxHealth=1000000;n.level=80;n.x=n.homeX=1;n.targetGuid=p.guid;n.threat[0]={p.guid,1000};n.attackTimer=1000;
        game.setRemoteNpcs({n});
    }
    LocalStatAura* aura(uint32_t id) {for(auto& a:p.statAuras)if(a.spellId==id&&a.remainingMs)return &a;return nullptr;}
    bool cast(uint32_t id,uint64_t target=10) {
        p.globalCooldownMs=0;p.cooldowns.clear();p.categoryCooldowns.clear();p.mana=100;
        const bool ok=game.execute(p,{LocalAction::CastSpell,target,id},{&p},message);
        if(!ok)std::cerr<<"cast "<<id<<": "<<message<<'\n';return ok;
    }
    void advance(unsigned ms) {while(ms){const auto step=std::min(ms,250u);game.tick(step/1000.f,{&p});ms-=step;}}
    std::vector<LocalCombatEvent> events() {
        std::vector<LocalCombatEvent> result;for(const auto& e:game.combatEvents())if(e.sequence>seen)result.push_back(e);
        if(!result.empty())seen=result.back().sequence;return result;
    }
    void incoming() {n.attackTimer=0;game.setRemoteNpcs({n});advance(10);}
    void pauseEnemy() {n.attackTimer=1000;game.setRemoteNpcs({n});}
};
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
    unsigned criticalTriggers=0,totalTicks=0;
    for(unsigned rank=1;rank<=3;++rank) {
        Fixture f(imported.spells,rank);const uint32_t id=rank==1?16488:rank==2?16490:16491;
        const auto* child=f.c->spell(id);assert(child&&validLocalProc(*child)&&child->unsupportedReason.empty());
        for(unsigned hit=0;hit<1024&&!f.aura(id);++hit) {
            f.p.health=f.p.maxHealth;f.incoming();
            bool critical=false;
            for(const auto& e:f.events()) {
                if(e.kind==LocalCombatEventKind::NpcMelee&&e.outcome==LocalMeleeOutcome::Critical){++criticalTriggers;critical=true;}
                assert(e.kind!=LocalCombatEventKind::PeriodicHeal);
            }
            assert(bool(f.aura(id))==critical);
        }
        assert(f.aura(id)&&f.aura(id)->remainingMs==6000&&f.aura(id)->manaRegenRemainder==0&&validLocalStatAuras(f.p));
        f.pauseEnemy();f.p.health=1;f.advance(child->periodicIntervalMs-250);
        for(const auto& e:f.events())assert(e.kind!=LocalCombatEventKind::PeriodicHeal);
        // CURRENT maximum health, not a trigger-time snapshot; explicit pool
        // transition immediately before the due tick exercises the runtime.
        f.p.maxHealth=20000;f.p.health=1;f.advance(250);unsigned ticks=0;
        for(const auto& e:f.events()) {
            assert(e.spell!=900005);
            if(e.kind==LocalCombatEventKind::PeriodicHeal){assert(e.spell==id&&e.attempted==200&&e.effective==200);++ticks;}
        }
        assert(ticks==1);f.advance(6000-child->periodicIntervalMs);
        for(const auto& e:f.events())if(e.kind==LocalCombatEventKind::PeriodicHeal){assert(e.spell==id);++ticks;}
        assert(ticks==rank*2&&!f.aura(id));totalTicks+=ticks;
        // Refresh resets the interval instead of carrying prior progress.
        f.p.statAuras.push_back({id,5000,f.p.mapId,f.p.instanceId,f.p.guid});
        for(unsigned hit=0;hit<1024;++hit) {
            f.aura(id)->manaRegenRemainder=child->periodicIntervalMs-100;
            f.p.health=f.p.maxHealth;f.incoming();bool crit=false;
            for(const auto& e:f.events())if(e.kind==LocalCombatEventKind::NpcMelee)crit=e.outcome==LocalMeleeOutcome::Critical;
            if(crit){assert(f.aura(id)->remainingMs==6000&&f.aura(id)->manaRegenRemainder==0);break;}
            assert(hit!=1023);
        }
        f.pauseEnemy();f.p.health=f.p.maxHealth;f.advance(child->periodicIntervalMs);bool overheal=false;
        for(const auto& e:f.events())if(e.kind==LocalCombatEventKind::PeriodicHeal){assert(!e.effective&&e.attempted>0);overheal=true;}
        assert(overheal);
        // Failed direct access, wrong current rank, location and death cleanup.
        f.p.knownSpells.push_back(id);assert(!f.cast(id,f.p.guid));
        f.p.talents.clear();f.advance(1);assert(!f.aura(id));
        f.p.talents={{661,uint8_t(rank)}};f.p.statAuras.push_back({id,6000,1,0,f.p.guid});f.advance(1);assert(!f.aura(id));
        f.p.statAuras.push_back({id,6000,0,0,f.p.guid});f.p.dead=true;f.advance(1);assert(!f.aura(id));
    }
    {
        Fixture f(imported.spells);assert(f.cast(12292,f.p.guid));
        assert(f.aura(12292)&&f.aura(12292)->remainingMs==30000&&f.p.mana==90);
        assert(localPhysicalDamageAfterTalents(f.p,*f.c,100)==120);
        // Source float multiplier then uint32 truncation (not exact rational).
        assert(localIncomingDamageAfterTalents(f.p,*f.c,100)==104);
        bool incoming=false;
        for(unsigned hit=0;hit<128&&!incoming;++hit) {
            f.p.health=f.p.maxHealth;f.incoming();
            for(const auto& e:f.events())if(e.kind==LocalCombatEventKind::NpcMelee&&!localMeleeAvoided(e.outcome)) {
                const uint32_t raw=uint32_t(100*(e.outcome==LocalMeleeOutcome::Critical?4:e.outcome==LocalMeleeOutcome::Crushing?3:2)/2);
                assert(e.attempted==localArmorReducedDamage(localIncomingDamageAfterTalents(f.p,*f.c,raw),localMeleeArmor(f.p,*f.c),80));incoming=true;
            }
        }
        assert(incoming);f.pauseEnemy();
        f.events();assert(f.cast(900001));bool physical=false;
        for(const auto& e:f.events())if(e.kind==LocalCombatEventKind::SpellDamage){assert(e.attempted==120);physical=true;}
        assert(physical);assert(f.cast(900003));
        for(const auto& e:f.events())if(e.kind==LocalCombatEventKind::SpellDamage)assert(e.attempted==100);
        assert(f.cast(900002));assert(f.game.execute(f.p,{LocalAction::CancelStatAura,0,12292},{&f.p},f.message));
        f.events();f.pauseEnemy();f.advance(1000);bool snapshot=false;
        for(const auto& e:f.events())if(e.kind==LocalCombatEventKind::PeriodicDamage){assert(e.attempted==120);snapshot=true;}
        assert(snapshot&&localPhysicalDamageAfterTalents(f.p,*f.c,100)==100&&localIncomingDamageAfterTalents(f.p,*f.c,100)==100);
        assert(f.cast(12292,f.p.guid));f.aura(12292)->remainingMs=1;f.advance(1);assert(!f.aura(12292));
        assert(f.cast(12292,f.p.guid));f.p.talents.clear();f.advance(1);assert(!f.aura(12292));
        f.p.mana=100;f.p.cooldowns.clear();f.p.categoryCooldowns.clear();f.p.globalCooldownMs=0;
        const auto before=f.p;
        assert(!f.game.execute(f.p,{LocalAction::CastSpell,f.p.guid,12292},{&f.p},f.message));
        assert(f.message=="Learn the required damage talent first"&&f.p.mana==before.mana&&
            f.p.cooldowns.empty()&&f.p.categoryCooldowns.empty()&&f.p.globalCooldownMs==0&&f.p.statAuras==before.statAuras);
    }
    std::cout<<"PASS Warrior reactive runtime: incoming critical triggers="<<criticalTriggers<<", percent-health ticks="<<totalTicks
        <<"; all ranks/current health/refresh/overheal/access/lifecycle, Death Wish benefit/penalty/magic exclusion/periodic snapshot\n";
}
