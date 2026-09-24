#include "local_npc_spell_fixture.hpp"

static void ward(NpcSpellFixture& f,uint16_t chance=3000) {
    if(!f.c->spell(543)) {
        LocalSpellDefinition d;d.id=543;d.name="Fire Ward component";d.clientSpell=true;
        d.wardProfile=1;d.buffAbsorb=165;d.absorbSchoolMask=4;d.durationMs=30000;
        f.c->spells.push_back(d);
        std::sort(f.c->spells.begin(),f.c->spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    }
    std::erase_if(f.p.statAuras,[](const auto& aura){return aura.spellId==543;});
    LocalStatAura a{543,30000,f.p.mapId,f.p.instanceId,f.p.guid};a.absorbRemaining=165;
    a.reflectChanceBasisPointsSnapshot=chance;f.p.statAuras.push_back(a);
}
static uint64_t launchReflection(NpcSpellFixture& f) {
    for(unsigned attempt=0;attempt<64;++attempt) {
        ward(f);auto n=f.game.npcs()[0];localResetNpcSpellState(n);n.x=n.homeX=0;
        n.targetGuid=f.p.guid;n.threat[0]={f.p.guid,1000};n.threat[1]={f.q.guid,1};f.game.setRemoteNpcs({n});
        f.arm();f.tick();
        if(f.game.npcs()[0].npcSpellReflected) {
            const auto events=f.spells();assert(!events.empty());return events.back().sequence;
        }
    }
    assert(false&&"Exact 30 percent ward snapshot must produce a deterministic reflected fixture");return 0;
}
static void awaitReflectEvent(NpcSpellFixture& f,uint64_t after) {
    for(unsigned i=0;i<40&&!f.game.npcs()[0].npcSpellReflectReturn;++i)f.game.tick(.025f,{&f.p,&f.q});
    assert(f.game.npcs()[0].npcSpellReflectReturn&&f.game.npcs()[0].npcCastRemainingMs);
    bool found=false;for(const auto& e:f.spells())if(e.sequence>after) {
        assert(e.kind!=LocalCombatEventKind::SpellDamage);
        if(e.reflectionOnly) {
            found=true;assert(e.source==f.n.guid&&e.target==f.p.guid&&e.reflectionSource==f.p.guid);
            assert(e.attempted==1&&!e.effective&&!e.absorbed&&e.spellTypeMask==4);
            assert(localProcEventHitMask(e)==LocalProcHitReflect);
            assert(localProcEventFlags(e,f.p.guid)==0x20000&&localProcEventFlags(e,f.n.guid)==0);
        }
    }assert(found);
}
static void reflectionLifecycle(unsigned mode) {
    NpcSpellFixture f(4323);const auto launch=launchReflection(f);const auto health=f.p.health;
    const auto before=f.game.npcs()[0].health;
    // Reflection selection belongs to launch. Removing the ward before the
    // first arrival neither cancels nor rerolls the already reflected missile.
    f.p.statAuras.clear();awaitReflectEvent(f,launch);
    assert(f.p.health==health&&f.game.npcs()[0].health==before);
    auto n=f.game.npcs()[0];n.health=mode?1:before;n.targetGuid=f.q.guid;
    // 2.39: the reward needs half the health dealt by players (the earlier
    // fight of this fixture is implied), else the reflected kill credits nobody.
    if(mode){n.npcPlayerDamage=n.maxHealth;n.npcDamagedByPlayer=true;}
    n.threat[0]={f.q.guid,10000};n.threat[1]={f.p.guid,1};f.game.setRemoteNpcs({n});
    std::vector<LocalRealmPlayer*> players{&f.p,&f.q};
    if(mode==2){f.p.dead=true;f.p.health=0;}
    if(mode==3)++f.p.mapId;
    if(mode==4)players={&f.q};
    for(unsigned i=0;i<40&&f.game.npcs()[0].npcCastingSpellId;++i)f.game.tick(.025f,players);
    assert(!f.game.npcs()[0].npcCastingSpellId);
    unsigned reflectionEvents=0,bounced=0,finishes=0;
    for(const auto& e:f.spells())if(e.sequence>launch) {
        if(e.reflectionOnly)++reflectionEvents;
        if(e.kind==LocalCombatEventKind::SpellDamage) {
            ++bounced;assert(e.source==f.n.guid&&e.target==f.n.guid&&e.reflectionSource==f.p.guid);
            assert(!e.actorIsPlayer&&e.outcome==LocalMeleeOutcome::Hit&&e.attempted>=64&&e.attempted<=86);
            assert(e.effective==(mode?1:e.attempted));assert(!e.absorbed&&!e.blocked);
        }
        if(e.kind==LocalCombatEventKind::SpellFinish)++finishes;
    }
    assert(reflectionEvents==1&&bounced==1&&finishes==1);
    if(mode){assert(f.game.npcs()[0].dead&&f.game.npcs()[0].lootable&&f.game.npcs()[0].lootOwner==f.p.guid);}
    else assert(f.game.npcs()[0].health<before&&f.p.health==health);
}
static void incomingCastInterruption(bool absorb) {
    NpcSpellFixture f;
    for(auto& d:f.c->spells)if(d.id==5401){d.sourceAlwaysHit=true;d.damage=d.damageMax=8;d.damagePerLevel=0;}
    LocalSpellDefinition casting;casting.id=90010;casting.name="Pushback component";casting.castTimeMs=2000;
    casting.interruptFlags=absorb?0x10:2;f.c->spells.push_back(casting);
    f.p.knownSpells.push_back(casting.id);f.p.castingSpellId=casting.id;f.p.castTotalMs=2000;f.p.castRemainingMs=1000;f.p.castTarget=f.p.guid;
    if(absorb) {
        LocalSpellDefinition shield;shield.id=90011;shield.durationMs=60000;shield.buffAbsorb=10000;shield.absorbSchoolMask=8;
        f.c->spells.push_back(shield);LocalStatAura aura{shield.id,60000,f.p.mapId,f.p.instanceId,f.p.guid};
        aura.absorbRemaining=10000;f.p.statAuras.push_back(aura);
    }
    f.arm();f.tick();const auto events=f.spells();assert(events.size()==3);
    assert(events[1].attempted==8&&events[1].effective==(absorb?0:8)&&events[1].absorbed==(absorb?8:0));
    if(absorb)assert(!f.p.castingSpellId&&f.p.castStatus==LocalCastStatus::Interrupted);
    else assert(f.p.castPushbackCount==1&&f.p.castPushbackMs==500&&f.p.castRemainingMs==1250);
}
int main() {
    for(unsigned mode=0;mode<5;++mode)reflectionLifecycle(mode);
    incomingCastInterruption(false);incomingCastInterruption(true);
    {
        NpcSpellFixture f(4323);f.arm();f.tick();assert(!f.game.npcs()[0].npcSpellReflected);
        ward(f,10000); // Component-only strongest snapshot added after launch.
        for(unsigned i=0;i<8&&f.game.npcs()[0].npcCastingSpellId;++i)f.tick();
        for(const auto& e:f.spells())assert(!e.reflectionOnly&&!e.reflectionSource);
    }
    std::cout<<"PASS NPC spell impact: source NPC attribution, 30 percent launch reflection, two flight stages, one bounce, ward removal/late ward, original target death/map/disconnect return, reflected kill/loot, incoming pushback and fully absorbed cast interruption\n";
}
