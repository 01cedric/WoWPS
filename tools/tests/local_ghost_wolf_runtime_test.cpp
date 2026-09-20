#include "local_group_rewards_fixture.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_class_pools.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace wowee;
namespace {
struct Fixture {
    std::shared_ptr<LocalWorldContent> c=rewardContent();
    LocalGameplay game;LocalRealmPlayer p=rewardPlayer(1);std::string message;
    Fixture(const LocalSpellDefinition& wolf) {
        c->classResources=true;c->spells.push_back(wolf);
        LocalSpellDefinition mount;mount.id=458;mount.name="Mount fixture";mount.mountDisplayId=2404;mount.mountSpeedPercent=60;
        mount.clientSpell=true;mount.baseLevel=1;c->spells.push_back(mount);
        std::sort(c->spells.begin(),c->spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});game.useContent(c);
        p.classId=7;p.race=2;p.level=16;p.knownSpells={1,458,2645};p.resourceType=LocalResourceType::Mana;
        game.initializePlayer(p,false);p.mana=p.maxMana-50;p.health=p.maxHealth/2;p.manaRegenDelayMs=5000;
    }
    bool cast(){return game.execute(p,{LocalAction::CastSpell,0,2645},{&p},message);}
    void advance(unsigned milliseconds){while(milliseconds){auto step=std::min(250u,milliseconds);game.tick(step/1000.f,{&p});milliseconds-=step;}}
    void ready(){p.globalCooldownMs=0;p.cooldowns.clear();p.categoryCooldowns.clear();}
};
}
int main(int argc,char** argv) {
    assert(argc==2);std::map<std::string,pipeline::DBCFile> tables;
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration"}) {
        std::ifstream file(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};assert(tables[name].load(bytes));
    }
    detail::ClientSpellTables t;t.spells=&tables["Spell"];t.ranges=&tables["SpellRange"];
    t.casts=&tables["SpellCastTimes"];t.durations=&tables["SpellDuration"];
    detail::ClientSpellTables::buildIndex(t.spells,t.spellIndex);detail::ClientSpellTables::buildIndex(t.ranges,t.rangeIndex);
    detail::ClientSpellTables::buildIndex(t.casts,t.castIndex);detail::ClientSpellTables::buildIndex(t.durations,t.durationIndex);
    LocalSpellDefinition wolf;wolf.id=2645;wolf.clientSpell=true;
    assert(detail::decodeClientSpell(t,detail::ClientSpellTables::lookup(t.spellIndex,2645),wolf));
    {
        Fixture f(wolf);const auto before=f.p.mana,capacity=f.p.maxMana;
        const auto cost=localClassBaseMana(f.p)*6/100;assert(cost>0&&before>cost);
        assert(f.cast());assert(f.p.castingSpellId==2645&&f.p.castTotalMs==2000&&f.p.mana==before&&!f.p.formSpellId);
        f.advance(1750);assert(f.p.castingSpellId==2645&&!f.p.formSpellId&&f.p.mana==before);
        f.advance(250);assert(!f.p.castingSpellId&&f.p.castStatus==LocalCastStatus::Finished&&f.p.formSpellId==2645);
        assert(f.p.mana==before-cost&&f.p.maxMana==capacity&&f.p.resourceType==LocalResourceType::Mana);
        assert(!f.p.druidMana&&!f.p.druidManaRemainder&&localFormDisplay(f.p)==4613&&localFormRunPercent(f.p)==140.f);
        f.ready();assert(!f.game.execute(f.p,{LocalAction::CastSpell,0,458},{&f.p},f.message));assert(!f.p.mountSpellId);
        assert(!f.cast());assert(f.p.mana==before-cost);
        f.advance(5250);
        for(const auto& event:f.game.combatEvents())assert(event.kind!=LocalCombatEventKind::PeriodicHeal);
        assert(f.game.execute(f.p,{LocalAction::CancelForm,0,2645},{&f.p},f.message));
        assert(!f.p.formSpellId&&f.p.resourceType==LocalResourceType::Mana&&f.p.maxMana==capacity);
    }
    {
        Fixture f(wolf);const auto mana=f.p.mana;f.p.movementState=kLocalMovementIndoors;
        assert(!f.cast()&&!f.p.castingSpellId&&!f.p.formSpellId&&f.p.mana==mana);
        f.p.movementState=0;assert(f.cast());f.advance(250);f.p.movementState=kLocalMovementIndoors;f.advance(250);
        assert(!f.p.castingSpellId&&!f.p.formSpellId&&f.p.mana==mana&&f.p.castStatus==LocalCastStatus::Interrupted);
        f.p.movementState=0;f.ready();assert(f.cast());f.advance(2000);assert(f.p.formSpellId==2645);
        const auto paid=f.p.mana;f.p.movementState=kLocalMovementIndoors;f.advance(1);
        assert(!f.p.formSpellId&&f.p.mana==paid&&localFormRunPercent(f.p)==100.f);
    }
    {
        Fixture f(wolf);f.p.formSpellId=2645;f.game.initializePlayer(f.p,false);
        assert(f.p.formSpellId==2645&&validLocalFormState(f.p));
        f.p.movementState=kLocalMovementIndoors;f.game.initializePlayer(f.p,false);assert(!f.p.formSpellId);
        f.p.movementState=0;f.p.formSpellId=2645;std::erase(f.p.knownSpells,2645);f.game.initializePlayer(f.p,false);assert(!f.p.formSpellId);
        f.p.knownSpells.push_back(2645);f.p.formSpellId=2645;
        auto* definition=const_cast<LocalSpellDefinition*>(f.c->spell(2645));definition->unsupportedReason="Source closure unavailable";
        f.game.initializePlayer(f.p,false);assert(!f.p.formSpellId);
    }
    std::cout<<"PASS: actual Ghost Wolf authority cast time/cost, mana preservation, cancellation, mount rejection, indoor cast rejection/interruption/form removal, no zero-glyph heal, load normalization\n";
}
