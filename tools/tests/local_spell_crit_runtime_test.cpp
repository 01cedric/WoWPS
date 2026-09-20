#include "local_group_rewards_fixture.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_spell_critical.hpp"
#include "game/local_melee.hpp"
#include "game/local_services.hpp"
#include "game/local_proc_talents.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace wowee;
namespace {
struct Fixture {
    std::shared_ptr<LocalWorldContent> c=rewardContent();
    LocalGameplay game;LocalRealmPlayer p=rewardPlayer(1),q=rewardPlayer(2),r=rewardPlayer(3);
    LocalRealmNpc n=rewardNpc();std::string message;uint64_t sequence=0;
    Fixture() {
        c->npcs[0].health=1000000;c->npcs[0].armor=0;
        p.race=q.race=r.race=1;p.classId=q.classId=r.classId=8;p.level=q.level=r.level=80;
        p.health=p.maxHealth=q.health=q.maxHealth=r.health=r.maxHealth=1000000;
        p.mana=p.maxMana=q.mana=q.maxMana=r.mana=r.maxMana=1000000;
        // Ten levels below the casters. Since the implementation a hostile magic spell rolls
        // MagicSpellHitResult in the player-to-creature direction, so a
        // same-level target would miss about four casts in a hundred and this
        // fixture - which is about critical outcomes and exact amounts, not
        // about hit - would report a zero-amount Miss instead. At this level
        // difference the hit chance clamps to its ceiling and every cast lands,
        // which is the premise every assertion below was written against. The
        // hit roll itself is covered by local_proc_outcomes_test.cpp.
        n.health=n.maxHealth=1000000;n.attackTimer=1000;n.level=70;n.x=1;
        LocalSpellDefinition s;s.id=900010;s.name="Direct magic fixture";s.clientSpell=true;s.allowableClasses=128;
        s.sourceDamageClass=1;s.schoolMask=4;s.damage=s.damageMax=101;s.range=100;s.resourceType=0;
        c->spells.push_back(s);s.id=900011;s.damage=s.damageMax=0;s.heal=s.healMax=101;c->spells.push_back(s);
        refresh();
    }
    void refresh(){std::sort(c->spells.begin(),c->spells.end(),[](auto& a,auto& b){return a.id<b.id;});game.useContent(c);
        std::array<uint32_t,12> races{};races[1]=1;LocalFactionTemplate f;f.id=f.faction=1;
        assert(game.setFactionTemplates({f},races,message));}
    LocalSpellDefinition& spell(uint32_t id){return *std::find_if(c->spells.begin(),c->spells.end(),[&](auto& d){return d.id==id;});}
    std::vector<LocalCombatEvent> cast(uint32_t id,uint64_t target,bool replaceNpcs=true) {
        if(replaceNpcs)game.setRemoteNpcs({n});
        if(std::find(p.knownSpells.begin(),p.knownSpells.end(),id)==p.knownSpells.end())p.knownSpells.push_back(id);p.globalCooldownMs=0;p.cooldowns.clear();p.categoryCooldowns.clear();p.mana=1000000;
        if(!game.execute(p,{LocalAction::CastSpell,target,id},{&p,&q,&r},message)){std::cerr<<id<<": "<<message<<"\n";assert(false);}
        std::vector<LocalCombatEvent> events;for(const auto& e:game.combatEvents())if(e.sequence>sequence)events.push_back(e);
        if(!events.empty())sequence=events.back().sequence;return events;
    }
};
}
int main(int argc,char** argv) {
    assert(argc==2);unsigned directCritical=0,healCritical=0,retalCritical=0,chains=0;
    assert(localMagicCriticalAmount(101)==151&&localMagicCriticalAmount(1000000)==1000000);
    Fixture f;
    // Execute authority damage and healing; zero-overheal remains a real crit
    // outcome while the replicated view reports the effective health delta.
    for(unsigned i=0;i<1024;++i) {
        for(const auto& e:f.cast(900010,f.n.guid))if(e.kind==LocalCombatEventKind::SpellDamage) {
            const bool crit=e.outcome==LocalMeleeOutcome::Critical;directCritical+=crit;
            assert(e.attempted==(crit?151u:101u));assert(f.p.meleeViews.back().amount==e.effective&&!f.p.meleeViews.back().healing);
        }
        f.p.health=f.p.maxHealth-1;
        for(const auto& e:f.cast(900011,f.p.guid))if(e.kind==LocalCombatEventKind::DirectHeal) {
            const bool crit=e.outcome==LocalMeleeOutcome::Critical;healCritical+=crit;
            assert(e.attempted==(crit?151u:101u)&&e.effective==1);
            assert(f.p.meleeViews.back().healing&&f.p.meleeViews.back().source==f.p.meleeViews.back().target&&f.p.meleeViews.back().outcome==e.outcome);
        }
    }
    assert(directCritical&&directCritical<1024&&healCritical&&healCritical<1024);
    // Attributes and damage class are authoritative eligibility gates for the
    // MAGIC critical: CANT_CRIT, DmgClass 0 and a physical school never take
    // the 1.5x roll. Since the implementation (P05-1) a DmgClass 2 definition is not "never
    // a critical" but a melee-class spell: it takes the melee roll, whose
    // critical is the melee 2x (Unit::SpellCriticalDamageBonus) and never the
    // magic 1.5x, and it can miss, be dodged and parried (this fixture used to
    // encode the no-roll behaviour, where such a row could not crit at all).
    unsigned meleeClassCriticals=0,meleeClassAvoided=0;
    for(unsigned gate=0;gate<4;++gate) {
        auto& s=f.spell(900010);s.sourceCantCrit=gate==0;s.sourceDamageClass=gate==1?0:gate==2?2:1;s.schoolMask=gate==3?1:4;
        for(unsigned i=0;i<128;++i)for(const auto& e:f.cast(s.id,f.n.guid))if(e.kind==LocalCombatEventKind::SpellDamage) {
            if(gate!=2){assert(e.outcome!=LocalMeleeOutcome::Critical);continue;}
            assert(e.attempted!=151u);
            if(e.outcome==LocalMeleeOutcome::Critical){assert(e.attempted==202u);++meleeClassCriticals;}
            else if(localMeleeAvoided(e.outcome)){assert(!e.attempted);++meleeClassAvoided;}
            else assert(e.attempted==101u);
        }
    }
    auto& chain=f.spell(900010);chain.sourceCantCrit=false;chain.sourceDamageClass=1;chain.schoolMask=4;chain.chainTargets=3;chain.chainRadius=10;chain.chainMultiplierPermille=500;
    for(unsigned i=0;i<512;++i) {
        auto a=f.n,b=f.n,c=f.n;b.guid=11;b.x=2;c.guid=12;c.x=3;f.game.setRemoteNpcs({a,b,c});unsigned hop=0;
        for(const auto& e:f.cast(chain.id,a.guid,false))if(e.kind==LocalCombatEventKind::SpellDamage) {
            const uint32_t base[]={101,50,25};assert(e.attempted==(e.outcome==LocalMeleeOutcome::Critical?localMagicCriticalAmount(base[hop]):base[hop]));++hop;
        }
        assert(hop==3);++chains;
    }
    auto& heal=f.spell(900011);heal.chainTargets=3;heal.chainRadius=10;heal.chainMultiplierPermille=500;
    for(unsigned i=0;i<256;++i) {
        f.p.health=f.q.health=f.r.health=1;unsigned hop=0;
        for(const auto& e:f.cast(heal.id,f.p.guid))if(e.kind==LocalCombatEventKind::DirectHeal) {
            const uint32_t base[]={101,50,25};assert(e.attempted==(e.outcome==LocalMeleeOutcome::Critical?localMagicCriticalAmount(base[hop]):base[hop]));++hop;
        }
        assert(hop==3);++chains;
    }
    // Source-backed spell records, with only cast time removed in the fixture
    // so this test focuses on hit/proc behavior; source audit checks cast time.
    std::map<std::string,pipeline::DBCFile> tables;
    for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration"}) {
        std::ifstream in(std::filesystem::path(argv[1])/(std::string(name)+".dbc"),std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),{}};assert(tables[name].load(bytes));
    }
    detail::ClientSpellTables t;t.spells=&tables["Spell"];t.ranges=&tables["SpellRange"];t.casts=&tables["SpellCastTimes"];t.durations=&tables["SpellDuration"];
    detail::ClientSpellTables::buildIndex(t.spells,t.spellIndex);detail::ClientSpellTables::buildIndex(t.ranges,t.rangeIndex);
    detail::ClientSpellTables::buildIndex(t.casts,t.castIndex);detail::ClientSpellTables::buildIndex(t.durations,t.durationIndex);
    auto decode=[&](uint32_t id,uint32_t classes) {LocalSpellDefinition d;d.id=id;d.clientSpell=true;d.allowableClasses=classes;
        const auto row=detail::ClientSpellTables::lookup(t.spellIndex,id);assert(row>=0);
        if(!detail::decodeClientSpell(t,uint32_t(row),d)){std::cerr<<id<<": "<<d.unsupportedReason<<"\n";assert(false);}d.castTimeMs=0;return d;};
    Fixture g;
    for(auto id:{30482u,43045u,43046u}) {
        auto parent=decode(id,128);assert(validLocalProc(parent));g.c->spells.push_back(parent);
        LocalSpellDefinition child;child.id=parent.proc.spellId;child.name="Internal source leaf";child.clientSpell=true;child.triggeredOnly=true;
        child.allowableClasses=128;child.sourceDamageClass=1;child.schoolMask=4;child.spellFamily=3;child.spellFamilyFlags[1]=8;
        child.damage=child.damageMax=parent.proc.amount;child.range=parent.proc.range;g.c->spells.push_back(child);
    }
    // Other retail Mage armor families remain decoder-blocked; exercise the
    // generic exclusive-group invariant with an explicit local fixture. Since
    // the implementation the group is SPELL_SPECIFIC_MAGE_ARMOR from LoadSpellSpecific
    // (SpellInfo.cpp:2135: family 3, flags[0] & 0x12040000), so the fixture
    // carries Mage Armor's own family bit (28) as the importer would derive
    // mageArmorGroup from it, instead of the bare hand-rule field alone.
    LocalSpellDefinition armor;armor.id=900012;armor.name="Mage armor group fixture";armor.clientSpell=true;
    armor.allowableClasses=128;armor.buffArmor=10;armor.buffSelfOnly=true;armor.durationMs=1800000;armor.mageArmorGroup=1;
    armor.spellFamily=3;armor.spellFamilyFlags[0]=0x10000000u;
    g.c->spells.push_back(armor);g.refresh();
    for(auto id:{30482u,43045u,43046u}) {
        g.cast(id,g.p.guid);assert(g.p.statAuras.size()==1&&g.p.statAuras[0].spellId==id);
        assert(localIncomingCritReductionPct(g.p,*g.c)==5);
        const auto& parent=g.spell(id);
        unsigned retalEvents=0,incomingHits=0;
        for(unsigned i=0;i<512;++i) {
            g.p.health=g.p.maxHealth=1000000;g.p.attackTarget=0;auto npc=g.n;npc.attackTimer=0;npc.targetGuid=g.p.guid;npc.threat[0]={g.p.guid,1};
            g.game.setRemoteNpcs({npc});g.game.tick(.001f,{&g.p});
            for(const auto& e:g.game.combatEvents())if(e.sequence>g.sequence&&e.kind==LocalCombatEventKind::NpcMelee&&localProcMatches(parent.proc,e,g.p.guid))++incomingHits;
            for(const auto& e:g.game.combatEvents())if(e.sequence>g.sequence&&e.kind==LocalCombatEventKind::ProcDamage) {
                ++retalEvents;const bool crit=e.outcome==LocalMeleeOutcome::Critical;retalCritical+=crit;
                assert(e.spell==parent.proc.spellId&&e.auraSpell==id&&e.auraCasterGuid==g.p.guid&&e.auraOwnerGuid==g.p.guid);
                assert(e.attempted==(crit?localMagicCriticalAmount(parent.proc.amount):parent.proc.amount));
                assert(e.procDepth==1&&e.parentSequence==e.rootSequence);
            }
            auto events=g.game.combatEvents();if(!events.empty())g.sequence=events.back().sequence;
        }
        assert(retalEvents>0&&retalEvents==incomingHits);
        g.p.knownSpells.push_back(parent.proc.spellId);g.p.globalCooldownMs=0;
        assert(!g.game.execute(g.p,{LocalAction::CastSpell,g.n.guid,parent.proc.spellId},{&g.p},g.message));
        g.cast(900012,g.p.guid);assert(g.p.statAuras.size()==1&&g.p.statAuras[0].spellId==900012&&localIncomingCritReductionPct(g.p,*g.c)==0);
    }
    if(!retalCritical) {
        std::cerr<<"Molten missing crit; last events:";for(const auto& e:g.game.combatEvents())std::cerr<<" kind="<<int(e.kind)<<" spell="<<e.spell<<" out="<<int(e.outcome)<<" target="<<e.target<<" dmg="<<e.attempted;
        std::cerr<<"\n";
    }
    assert(retalCritical);
    g.cast(43046,g.p.guid);g.p.statAuras[0].remainingMs=1;g.game.setRemoteNpcs({});g.game.tick(.002f,{&g.p});assert(g.p.statAuras.empty());
    g.cast(43046,g.p.guid);++g.p.mapId;g.game.tick(.001f,{&g.p});assert(g.p.statAuras.empty());--g.p.mapId;
    g.cast(43046,g.p.guid);g.p.health=0;g.p.dead=true;g.game.tick(.001f,{&g.p});assert(g.p.statAuras.empty());g.p.dead=false;g.p.health=g.p.maxHealth;
    g.cast(43046,g.p.guid);g.p.statAuras.push_back({900012,900000,g.p.mapId,g.p.instanceId,g.p.guid});
    normalizeLocalMageArmors(g.p,*g.c);assert(g.p.statAuras.size()==1&&g.p.statAuras[0].spellId==43046);
    LocalSpellDefinition absorb;absorb.id=900013;absorb.name="Full absorb fixture";absorb.buffAbsorb=1000000;
    absorb.absorbSchoolMask=127;absorb.durationMs=60000;g.c->spells.push_back(absorb);g.refresh();g.sequence=0;
    LocalStatAura absorbAura{absorb.id,60000,g.p.mapId,g.p.instanceId,g.p.guid,1000000};g.p.statAuras.push_back(absorbAura);
    bool fullAbsorb=false,absorbRetal=false;
    for(unsigned i=0;i<32&&!fullAbsorb;++i) {
        auto npc=g.n;npc.attackTimer=0;npc.targetGuid=g.p.guid;npc.threat[0]={g.p.guid,1};g.game.setRemoteNpcs({npc});
        g.game.tick(.001f,{&g.p});uint64_t root=0;
        for(const auto& e:g.game.combatEvents())if(e.sequence>g.sequence&&e.kind==LocalCombatEventKind::NpcMelee&&e.absorbed&&e.effective==0){root=e.sequence;fullAbsorb=true;}
        for(const auto& e:g.game.combatEvents())if(root&&e.kind==LocalCombatEventKind::ProcDamage&&e.parentSequence==root)absorbRetal=true;
        const auto events=g.game.combatEvents();if(!events.empty())g.sequence=events.back().sequence;
    }
    assert(fullAbsorb&&absorbRetal);
    // Old the implementation profiles could know these leaves. Warm initialization removes
    // all internal IDs while preserving the legitimately learned armor.
    for(auto id:{34913u,43043u,43044u})g.p.knownSpells.push_back(id);
    g.game.setRemoteNpcs({});g.game.initializePlayer(g.p,false);
    for(auto id:{34913u,43043u,43044u})assert(std::find(g.p.knownSpells.begin(),g.p.knownSpells.end(),id)==g.p.knownSpells.end());
    assert(std::find(g.p.knownSpells.begin(),g.p.knownSpells.end(),43046)!=g.p.knownSpells.end());
    // Exercise ordinary LearnSpell transactions using Jennea Cannon's actual
    // source class-trainer record, with a local positioned service actor.
    Fixture learner;learner.c=std::make_shared<LocalWorldContent>(*g.c);learner.p.money=100000000;
    LocalNpcDefinition td;td.id=5497;td.name="Jennea Cannon";td.health=100;td.faction=1;learner.c->npcs.push_back(td);learner.refresh();
    const auto* trainerSource=localServiceNpcRecord(5497);assert(trainerSource&&trainerSource->trainerClass==8);
    auto trainer=learner.n;trainer.entry=5497;trainer.hostile=false;trainer.targetGuid=0;trainer.threat={};
    trainer.classTrainer=(trainerSource->npcFlags&kLocalNpcFlagTrainerClass)!=0;trainer.trainerClass=trainerSource->trainerClass;
    learner.game.setRemoteNpcs({trainer});
    const uint32_t parentIds[]={30482,43045,43046};const uint8_t levels[]={62,71,79};
    for(unsigned rank=0;rank<3;++rank) {
        LocalRealmCommand learn{LocalAction::LearnSpell,0,parentIds[rank]};learn.serviceNpcGuid=trainer.guid;
        learner.p.level=levels[rank]-1;assert(!learner.game.execute(learner.p,learn,{&learner.p},learner.message));
        learner.p.level=levels[rank];const auto before=learner.p.money;
        assert(learner.game.execute(learner.p,learn,{&learner.p},learner.message)&&learner.p.money<before&&learner.p.talents.empty());
        assert(std::find(learner.p.knownSpells.begin(),learner.p.knownSpells.end(),parentIds[rank])!=learner.p.knownSpells.end());
        if(rank)assert(std::find(learner.p.knownSpells.begin(),learner.p.knownSpells.end(),parentIds[rank-1])==learner.p.knownSpells.end());
        const auto offers=learner.game.trainableSpells(learner.p,trainer.guid);
        for(auto id:{34913u,43043u,43044u})assert(std::find(offers.begin(),offers.end(),id)==offers.end());
    }
    // Guaranteed Exorcism critical is source creature type based.
    Fixture ex;ex.p.classId=2;ex.c->spells.push_back(decode(879,2));ex.c->npcs[0].id=3;ex.n.entry=3;ex.refresh();
    for(unsigned i=0;i<16;++i)for(const auto& e:ex.cast(879,ex.n.guid))if(e.kind==LocalCombatEventKind::SpellDamage)assert(e.outcome==LocalMeleeOutcome::Critical);
    // Lava Burst observes only its own active Flame Shock. The dot survives
    // the cast, keeps ticking without crits, and another caster cannot qualify.
    Fixture lava;lava.p.classId=7;lava.c->spells.push_back(decode(60043,64));lava.c->spells.push_back(decode(8050,64));lava.refresh();
    lava.cast(8050,lava.n.guid);for(unsigned i=0;i<16;++i)
        for(const auto& e:lava.cast(60043,lava.n.guid,false))if(e.kind==LocalCombatEventKind::SpellDamage)assert(e.outcome==LocalMeleeOutcome::Critical);
    for(unsigned i=0;i<16;++i)lava.game.tick(.25f,{&lava.p});bool tick=false;
    for(const auto& e:lava.game.combatEvents())if(e.kind==LocalCombatEventKind::PeriodicDamage){tick=true;assert(e.outcome==LocalMeleeOutcome::Hit);}assert(tick);
    Fixture foreign;foreign.p.classId=foreign.q.classId=7;foreign.c->spells.push_back(decode(60043,64));foreign.c->spells.push_back(decode(8050,64));foreign.refresh();
    foreign.q.knownSpells.push_back(8050);foreign.game.setRemoteNpcs({foreign.n});
    assert(foreign.game.execute(foreign.q,{LocalAction::CastSpell,foreign.n.guid,8050},{&foreign.p,&foreign.q},foreign.message));
    unsigned ordinary=0;for(unsigned i=0;i<128;++i)for(const auto& e:foreign.cast(60043,foreign.n.guid,false))
        if(e.kind==LocalCombatEventKind::SpellDamage&&e.source==foreign.p.guid&&e.outcome==LocalMeleeOutcome::Hit)++ordinary;
    assert(ordinary);
    std::cout<<"PASS P03 direct spell/heal critical authority: damageCrit="<<directCritical<<" healCrit="<<healCritical<<" meleeClassCrit2x="<<meleeClassCriticals<<" meleeClassAvoided="<<meleeClassAvoided
        <<" chainCasts="<<chains<<" moltenCrit="<<retalCritical<<"; source gates, internal leaves, self-heal views, armor exclusivity/lifecycle, Exorcism/Lava Burst ownership, noncritical periodic ticks, fully absorbed retaliation, teacher levels62/71/79, old leaked-child cleanup\n";
}
