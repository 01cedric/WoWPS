#include "local_group_rewards_fixture.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_class_pools.hpp"
#include "game/local_melee.hpp"
#include "game/local_stat_auras.hpp"
#include "game/local_talents.hpp"
#include "game/local_progression_modifiers.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace wowee;
namespace {
struct Fixture {
    std::shared_ptr<LocalWorldContent> c=rewardContent();LocalGameplay game;
    LocalRealmPlayer p=rewardPlayer(1),q=rewardPlayer(2);std::string message;
    Fixture(const std::vector<LocalSpellDefinition>& spells) {
        c->classResources=true;c->spells=spells;c->npcs[0].health=1000000;c->npcs[0].armor=0;
        std::sort(c->spells.begin(),c->spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        game.useContent(c);p.classId=11;p.race=4;p.level=80;p.resourceType=LocalResourceType::Mana;
        q=p;q.guid=2;q.name="Recipient";
        p.knownSpells=q.knownSpells={1126,5185,5176,8921,768,5487,1822};
        game.initializePlayer(p,false);game.initializePlayer(q,false);p.health=p.maxHealth/2;p.mana=p.maxMana/2;
    }
    void ready(){p.globalCooldownMs=0;p.cooldowns.clear();p.categoryCooldowns.clear();}
    bool action(LocalAction kind,uint32_t id=0,uint64_t target=0,uint32_t rank=0) {
        LocalRealmCommand command;command.action=kind;command.id=id;command.target=target;command.bid=rank;
        const bool ok=game.execute(p,command,{&p,&q},message);
        if(!ok)std::cerr<<"action "<<unsigned(kind)<<" id "<<id<<": "<<message<<'\n';return ok;
    }
    void learn(uint32_t id,unsigned ranks){for(unsigned rank=0;rank<ranks;++rank)assert(action(LocalAction::LearnTalent,id,0,rank));}
    void opening(){learn(823,3);learn(821,2);}
    void cast(uint32_t id,uint64_t target=0){ready();assert(action(LocalAction::CastSpell,id,target));}
    void advance(unsigned ms,const std::vector<LocalRealmPlayer*>& players){while(ms){const auto step=std::min(ms,250u);game.tick(step/1000.f,players);ms-=step;}}
    const LocalStatAura& mark()const{for(const auto& a:q.statAuras)if(a.spellId==1126)return a;assert(false);std::abort();}
    void enemy(){auto n=rewardNpc();n.x=1;n.health=n.maxHealth=1000000;n.level=80;n.attackTimer=10000;n.targetGuid=p.guid;n.threat[0]={p.guid,1000};game.setRemoteNpcs({n});}
};
uint32_t sourceAmount(const LocalRealmPlayer& p,const LocalSpellDefinition& d,bool periodic=false) {
    const uint32_t lo=periodic?d.periodicDamage:d.damage,hi=periodic?d.periodicDamageMax:d.damageMax;
    const auto level=d.maxLevel?std::min(uint32_t(p.level),d.maxLevel):uint32_t(p.level);
    return uint32_t((double(lo)+std::max(lo,hi))*.5+double(level>d.baseLevel?level-d.baseLevel:0)*(periodic?d.periodicDamagePerLevel:d.damagePerLevel));
}
}
int main(int argc,char** argv) {
    assert(argc==2);std::map<std::string,pipeline::DBCFile> tables;
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon","SkillLineAbility","SkillLine","Talent","SpellRuneCost","SpellRadius","TalentTab"}) {
        std::ifstream in(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),{}};assert(tables[name].load(bytes));
    }
    const auto get=[&](const char* n){return &tables.at(n);};
    auto imported=importClientStarterSpells(get("Spell"),get("SpellRange"),get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SkillLineAbility"),get("SkillLine"),get("Talent"),get("SpellRuneCost"),get("SpellRadius"));
    detail::importClientTalents(imported,get("Talent"),get("TalentTab"),get("Spell"),get("SpellRange"),get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SpellRuneCost"),get("SpellRadius"));
    {
        Fixture f(imported.spells);const auto health=f.p.health,mana=f.p.mana;
        const auto baseHealth=f.p.maxHealth,baseMana=f.p.maxMana;
        const auto baseStats=localMeleeStats(f.p,*f.c);f.opening();
        assert(f.p.health==health&&f.p.mana==mana);
        const auto stats=localMeleeStats(f.p,*f.c);assert(stats.base==baseStats.base);
        for(size_t i=0;i<5;++i)assert(stats.attributes[i]==int(float(baseStats.attributes[i])*1.02f));
        f.learn(824,5);f.learn(827,1);assert(localTalentPointsSpent(f.p)==11);
        const auto* touch=f.c->spell(5185);assert(touch&&touch->castTimeMs>500);
        f.cast(5185,f.p.guid);assert(f.p.castingSpellId==5185&&f.p.castTotalMs==touch->castTimeMs-500);
        assert(f.action(LocalAction::CancelCast));
        // Receiving an enhanced buff uses its original caster, independently
        // of the recipient's talents and future presence of that caster.
        const auto* mark=f.c->spell(1126);assert(mark&&mark->buffArmor);
        f.cast(1126,f.q.guid);const auto enhanced=uint32_t(float(mark->buffArmor)*1.4f);
        assert(f.mark().casterGuid==f.p.guid&&f.mark().buffArmorSnapshot==enhanced);
        assert(localStatAuraBonus(f.q,*f.c,true)==enhanced);
        f.advance(250,{&f.q});assert(localStatAuraBonus(f.q,*f.c,true)==enhanced);
        f.p.health=f.p.maxHealth;f.p.mana=f.p.maxMana;
        assert(f.action(LocalAction::ResetTalents));
        assert(f.p.maxHealth==baseHealth&&f.p.maxMana==baseMana);
        assert(f.p.health==f.p.maxHealth&&f.p.mana==f.p.maxMana);
        assert(localMeleeStats(f.p,*f.c).attributes==baseStats.attributes);
        assert(localStatAuraBonus(f.q,*f.c,true)==enhanced);
        f.cast(1126,f.q.guid);assert(f.mark().buffArmorSnapshot==mark->buffArmor);
        assert(localStatAuraBonus(f.q,*f.c,true)==mark->buffArmor);
        f.q.talents={{821,2},{823,3}};
        f.cast(1126,f.q.guid);assert(f.mark().buffArmorSnapshot==mark->buffArmor);
        f.game.initializePlayer(f.q,false);assert(localStatAuraBonus(f.q,*f.c,true)==mark->buffArmor);
        f.q.statAuras.front().remainingMs=1;f.advance(1,{&f.q});assert(localStatAuraBonus(f.q,*f.c,true)==0);
    }
    {
        Fixture f(imported.spells);f.opening();f.learn(826,3);
        for(const auto id:{768u,5487u}) {
            const auto* form=f.c->spell(id);assert(form&&form->unsupportedReason.empty());
            f.p.mana=f.p.maxMana;const auto before=f.p.mana;
            const auto base=uint64_t(form->mana)+uint64_t(localClassBaseMana(f.p))*form->manaPercent/100;
            const auto cost=uint32_t(base*70/100);assert(localSpellResourceCost(f.p,*f.c,*form)==cost);
            f.cast(id);assert(f.p.formSpellId==id&&f.p.druidMana==before-cost);
            assert(f.action(LocalAction::CancelForm,id));assert(f.p.mana==before-cost);
        }
        // The pinned server correction makes ranks 2/3 persistent beyond the
        // erroneous six-second raw DBC duration, as well as after reinitialize.
        f.advance(6250,{&f.p,&f.q});f.game.initializePlayer(f.p,false);
        assert(localTalentCastModifier(f.p,*f.c,*f.c->spell(768),14,true)==-30);
    }
    {
        Fixture f(imported.spells);f.opening();f.learn(824,5);f.p.mana=f.p.maxMana;
        f.cast(768);f.enemy();const auto* rake=f.c->spell(1822);assert(rake&&rake->unsupportedReason.empty());
        const auto ap=uint32_t(localMeleeStats(f.p,*f.c).attackPower);
        const auto directBase=uint32_t(sourceAmount(f.p,*rake)+ap*.01);
        const auto periodicBase=uint32_t(sourceAmount(f.p,*rake,true)+ap*.06);
        bool hit=false;uint64_t seen=0;
        for(unsigned attempt=0;attempt<64&&!hit;++attempt) {
            f.p.mana=100;f.cast(1822,10);
            for(const auto& e:f.game.combatEvents())if(e.sequence>seen&&e.spell==1822&&e.kind==LocalCombatEventKind::SpellDamage&&e.outcome==LocalMeleeOutcome::Hit) {
                assert(e.attempted==uint32_t(float(directBase)*1.1f));hit=true;
            }
            if(!f.game.combatEvents().empty())seen=f.game.combatEvents().back().sequence;
        }
        assert(hit);f.p.attackTarget=0;
        f.advance(3000,{&f.p,&f.q});bool tick=false;
        for(const auto& e:f.game.combatEvents())if(e.sequence>seen&&e.spell==1822&&e.kind==LocalCombatEventKind::PeriodicDamage){assert(e.attempted==uint32_t(float(periodicBase)*1.1f));tick=true;}
        assert(tick);
        // Preserve the target and active bleed but end this fixture's combat,
        // allowing the ordinary ResetTalents transaction before the next tick.
        auto n=f.game.npcs().front();n.targetGuid=0;n.threat={};n.playerThreat={};f.game.setRemoteNpcs({n});f.p.attackTarget=0;
        assert(f.action(LocalAction::ResetTalents));seen=f.game.combatEvents().back().sequence;
        n.targetGuid=f.p.guid;n.threat[0]={f.p.guid,1000};f.game.setRemoteNpcs({n});
        f.advance(3000,{&f.p,&f.q});tick=false;
        for(const auto& e:f.game.combatEvents())if(e.sequence>seen&&e.spell==1822&&e.kind==LocalCombatEventKind::PeriodicDamage){assert(e.attempted==uint32_t(float(periodicBase)*1.1f));tick=true;}
        assert(tick);
    }
    {
        Fixture f(imported.spells);f.opening();f.learn(824,5);f.enemy();const auto* moonfire=f.c->spell(8921);assert(moonfire);
        // the implementation gave player-to-creature magic its own hit roll, so a single cast
        // at an equal-level target can legitimately resist and emit no effective
        // damage. Retry the way the Rake block above does; every cast that does
        // land is still checked against the source amount, so the assertion is
        // unchanged in strength - only the "at least one landed" wait is added.
        bool hit=false;uint64_t seen=0;
        for(unsigned attempt=0;attempt<64&&!hit;++attempt) {
            f.p.mana=f.p.maxMana;f.cast(8921,10);
            for(const auto& e:f.game.combatEvents())if(e.sequence>seen&&e.spell==8921&&e.kind==LocalCombatEventKind::SpellDamage&&e.effective) {
                const auto base=sourceAmount(f.p,*moonfire);const auto expected=e.outcome==LocalMeleeOutcome::Critical?base*3/2:base;
                assert(e.attempted==expected);hit=true;
            }
            if(!f.game.combatEvents().empty())seen=f.game.combatEvents().back().sequence;
        }
        assert(hit);
    }
    std::cout<<"PASS: real normal Omen learning; Improved Mark allstats/no-refill/clamp, original-caster armor snapshot, recipient/reset/disconnect/load/expiry; corrected Natural Shapeshifter3 real cat/bear costs and persistence; Healing Touch timer; Naturalist Rake direct/periodic snapshot after reset and magic exclusion\n";
}
