#include "game/local_spell_import.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace wowee::game;
using wowee::pipeline::DBCFile;

// Synthetic authored rows in the actual build-12340 column layout. No retail
// spell amounts/names or client data are redistributed by this test.
static uint32_t bits(float value) {uint32_t out;std::memcpy(&out,&value,4);return out;}
static std::unique_ptr<DBCFile> table(const std::vector<std::vector<uint32_t>>& rows) {
    assert(!rows.empty());const auto fields=rows.front().size();
    std::vector<uint8_t> bytes{'W','D','B','C'};
    auto u32=[&](uint32_t v){for(unsigned b=0;b<4;++b)bytes.push_back(uint8_t(v>>(b*8)));};
    u32(rows.size());u32(fields);u32(fields*4);u32(1);
    for(const auto& row:rows) {assert(row.size()==fields);for(const auto value:row)u32(value);}
    bytes.push_back(0);auto out=std::make_unique<DBCFile>();assert(out->load(bytes));return out;
}
static std::vector<uint32_t> spellRow(uint32_t id) {
    std::vector<uint32_t> row(234);
    row[0]=id;row[28]=1;row[37]=6;row[39]=2;row[40]=1;row[42]=15;row[46]=1;row[68]=UINT32_MAX;
    row[71]=6;row[74]=3;row[77]=bits(2);row[80]=9;row[86]=21;row[95]=8;row[98]=1000;
    return row;
}
static LocalSpellImport imported() {
    auto low=spellRow(900001),high=spellRow(900002),cast=spellRow(900003),self=spellRow(900004);
    high[74]=1;high[77]=0;high[80]=20;cast[28]=2;self[86]=1;
    auto unsupported=spellRow(900005);unsupported[72]=6;unsupported[87]=21;unsupported[96]=4;
    auto zeroInterval=spellRow(900006);zeroInterval[98]=0;
    auto area=spellRow(900007);area[86]=20;
    auto mixed=spellRow(900008);mixed[72]=2;mixed[87]=6;mixed[81]=1;
    auto invalid=spellRow(900009);invalid[98]=4000;
    std::vector<std::vector<uint32_t>> rows{low,high,cast,self,unsupported,zeroInterval,area,mixed,invalid};
    for(uint32_t id=900010;id<=900018;++id)rows.push_back(spellRow(id));
    std::vector<std::vector<uint32_t>> abilities;
    for(const auto& row:rows) {
        std::vector<uint32_t> ability(14);ability[0]=row[0];ability[1]=1;ability[2]=row[0];ability[4]=1u<<4;
        if(row[0]==900001)ability[8]=900002;
        abilities.push_back(ability);
    }
    std::vector<uint32_t> range(40);range[0]=1;range[3]=bits(10);range[4]=bits(30);
    std::vector<uint32_t> skill(38);skill[0]=1;skill[1]=kLocalSkillCategoryClass;
    auto spells=table(rows),ranges=table({range}),casts=table({{1,0,0,0},{2,1000,0,0}});
    auto durations=table({{1,3000,0,3000}}),lines=table({skill}),links=table(abilities);
    return importClientStarterSpells(spells.get(),ranges.get(),casts.get(),durations.get(),nullptr,links.get(),lines.get());
}
static LocalRealmPlayer player(uint64_t guid) {
    LocalRealmPlayer p;p.guid=guid;p.name="Healing fixture";p.classId=5;p.level=8;
    p.x=p.y=p.z=0;p.health=10;p.maxHealth=1000;p.mana=p.maxMana=100000;
    p.regenerationTimer=-10000; // isolate spell healing from the independent regeneration system
    for(uint32_t id=900001;id<=900018;++id)p.knownSpells.push_back(id);
    return p;
}
static void advance(LocalGameplay& game,const std::vector<LocalRealmPlayer*>& players,unsigned milliseconds) {
    while(milliseconds) {const auto step=std::min(milliseconds,250u);game.tick(float(step)/1000,players);milliseconds-=step;}
}
struct Fixture {
    LocalGameplay game;
    LocalRealmPlayer caster=player(1),target=player(2);
    std::vector<LocalRealmPlayer*> players{&caster,&target};
    std::string result;
    Fixture() {
        const auto spells=imported();std::string error;
        assert(game.setStarterSpells(spells.spells,spells.diagnostic,error));
    }
    bool cast(uint32_t id=900001) {return game.execute(caster,{LocalAction::CastSpell,target.guid,id},players,result);}
};
static void testImportAndTicks() {
    Fixture f;const auto* spell=f.game.content().spell(900001);assert(spell);
    assert(spell->unsupportedReason.empty() && spell->periodicHeal==10 && spell->periodicHealMax==12);
    assert(spell->periodicHealPerLevel==2 && spell->baseLevel==2 && spell->maxLevel==6);
    assert(spell->durationMs==3000 && spell->periodicIntervalMs==1000 && spell->range==30);
    assert(spell->supercededBySpell==900002 && spell->allowableClasses==(1u<<4));
    for(uint32_t id=900005;id<=900009;++id)assert(!f.game.content().spell(id));
    assert(f.cast());assert(f.target.health==10 && f.caster.mana==99985);
    advance(f.game,f.players,999);assert(f.target.health==10);
    advance(f.game,f.players,1);assert(f.target.health==29); // midpoint 11 + four source-scaled levels * 2
    advance(f.game,f.players,2000);assert(f.target.health==67); // includes the final tick at expiry
    advance(f.game,f.players,3000);assert(f.target.health==67);
    f.target.health=995;assert(f.cast());advance(f.game,f.players,3000);assert(f.target.health==1000);
    std::cout<<"PASS HoT import/ticks: aura8 class import, authored amount/dice/level cap, duration/interval, final tick, overheal, unsupported mixed/area rejection\n";
}
static void testRefreshAndRanks() {
    Fixture f;assert(f.cast());advance(f.game,f.players,500);assert(f.cast());
    advance(f.game,f.players,500);assert(f.target.health==10);
    advance(f.game,f.players,500);assert(f.target.health==29);
    assert(f.cast(900002));const auto money=f.caster.mana,revision=f.caster.castRevision;
    assert(!f.cast(900001));assert(f.caster.mana==money && f.caster.castRevision==revision);
    advance(f.game,f.players,1000);assert(f.target.health==50); // upgraded rank replaces the previous aura
    auto second=player(3);f.players.push_back(&second);
    assert(f.game.execute(second,{LocalAction::CastSpell,f.target.guid,900002},f.players,f.result));
    advance(f.game,f.players,1000);assert(f.target.health==92); // separate casters retain their own effects
    std::cout<<"PASS HoT refresh/ranks: recast resets timer, source-linked upgrade replaces, downgrade rejection is atomic, independent casters\n";
}
static void testRejectionAndInterruption() {
    Fixture f;const auto resource=f.caster.mana;
    LocalFactionTemplate friendly;friendly.id=1;friendly.factionGroup=1;friendly.enemyGroup=2;
    LocalFactionTemplate enemy;enemy.id=2;enemy.factionGroup=2;enemy.enemyGroup=1;
    std::array<uint32_t,12> raceFactions{};raceFactions[1]=1;raceFactions[2]=2;
    assert(f.game.setFactionTemplates({friendly,enemy},raceFactions,f.result));
    f.target.race=2;assert(!f.cast());f.target.race=1;
    f.target.x=31;assert(!f.cast());f.target.x=0;
    f.target.mapId=1;assert(!f.cast());f.target.mapId=0;
    f.target.instanceId=1;assert(!f.cast());f.target.instanceId=0;
    f.target.dead=true;assert(!f.cast());f.target.dead=false;
    f.target.health=0;assert(!f.cast());f.target.health=10;
    assert(!f.cast(900004)); // DBC target 1 cannot be redirected to another player
    assert(f.caster.mana==resource && f.caster.castRevision==0);
    assert(f.cast(900003));assert(f.caster.mana==resource);
    f.caster.x=1;advance(f.game,f.players,250);
    assert(!f.caster.castingSpellId && f.caster.castStatus==LocalCastStatus::Interrupted);
    advance(f.game,f.players,3000);assert(f.target.health==10 && f.caster.mana==resource);
    assert(f.cast(900003));f.target.x=40;advance(f.game,f.players,1000);
    assert(!f.caster.castingSpellId && f.caster.castStatus==LocalCastStatus::Failed);
    assert(f.caster.mana==resource && f.caster.castRevision==0);f.target.x=0;
    assert(f.cast(900003));advance(f.game,f.players,1000);
    assert(f.caster.mana==resource-15 && f.target.health==10 && f.caster.castRevision==1);
    advance(f.game,f.players,1000);assert(f.target.health==29);
    std::cout<<"PASS HoT cast authority: living player/map/instance/range/self targeting, movement interruption, completion revalidation, resources commit once\n";
}
static void testCleanupAndBounds() {
    Fixture f;assert(f.cast());f.target.dead=true;advance(f.game,f.players,250);
    f.target.dead=false;advance(f.game,f.players,3000);assert(f.target.health==10);
    assert(f.cast());f.caster.mapId=1;f.target.mapId=1;advance(f.game,f.players,250);
    f.caster.mapId=f.target.mapId=0;advance(f.game,f.players,3000);assert(f.target.health==10);
    assert(f.cast());f.players.pop_back();advance(f.game,f.players,250);f.players.push_back(&f.target);
    advance(f.game,f.players,3000);assert(f.target.health==10);
    assert(f.cast());f.game.useContent(f.game.sharedContent());advance(f.game,f.players,3000);assert(f.target.health==10);
    // Every accepted new effect occupies a bounded slot. Rejected overflow
    // spends nothing, while refresh remains possible on a full target/realm.
    std::vector<LocalRealmPlayer> targets;
    targets.reserve(33);
    for(unsigned index=0;index<33;++index)targets.push_back(player(100+index));
    std::vector<LocalRealmPlayer*> roster{&f.caster};for(auto& target:targets)roster.push_back(&target);
    for(unsigned index=0;index<32;++index)
        for(uint32_t id=900010;id<900018;++id)
            assert(f.game.execute(f.caster,{LocalAction::CastSpell,targets[index].guid,id},roster,f.result));
    const auto resource=f.caster.mana,revision=f.caster.castRevision;
    assert(!f.game.execute(f.caster,{LocalAction::CastSpell,targets[0].guid,900018},roster,f.result));
    assert(!f.game.execute(f.caster,{LocalAction::CastSpell,targets[32].guid,900010},roster,f.result));
    assert(f.caster.mana==resource && f.caster.castRevision==revision);
    assert(f.game.execute(f.caster,{LocalAction::CastSpell,targets[0].guid,900010},roster,f.result));
    advance(f.game,roster,3000);
    assert(targets[0].health==10+8*3*19 && targets[32].health==10);
    assert(f.game.execute(f.caster,{LocalAction::CastSpell,targets[32].guid,900010},roster,f.result));
    std::cout<<"PASS HoT lifetime/bounds: death, map travel, offline target, content reset, 8/target and 256/realm, atomic overflow, full-capacity refresh and expiry reuse\n";
}
int main() {testImportAndTicks();testRefreshAndRanks();testRejectionAndInterruption();testCleanupAndBounds();}
