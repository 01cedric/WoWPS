#include "local_ignite_fixture.hpp"
#include "game/local_melee.hpp"
#include "game/local_npc_spell_profiles.hpp"
namespace {
struct Runtime {
    std::shared_ptr<LocalWorldContent> c;
    LocalRealmPlayer p=rewardPlayer(1),q=rewardPlayer(2);
    LocalRealmNpc n=rewardNpc();LocalGameplay game;std::string error;
    uint64_t seen=0;std::vector<LocalCombatEvent> observed;
    explicit Runtime(const std::shared_ptr<LocalWorldContent>& source,uint32_t damage=1000,uint32_t school=4,bool molten=false,uint8_t rank=5):c(std::make_shared<LocalWorldContent>(*source)) {
        p.race=q.race=1;p.classId=q.classId=8;p.level=q.level=80;
        p.health=p.maxHealth=q.health=q.maxHealth=1000000;p.mana=p.maxMana=q.mana=q.maxMana=1000000;
        n.health=n.maxHealth=1000000;n.level=1;n.attackTimer=100000;n.x=n.homeX=1;
        c->npcs[0].health=1000000;c->npcs[0].armor=0;
        // Explicit component spell and crit modifier make timing arithmetic
        // deterministic. Ignite and its five real talent ranks remain imported.
        LocalSpellDefinition spell;spell.id=900037;spell.name="Ignite timing fixture";spell.clientSpell=true;
        spell.allowableClasses=128;spell.sourceDamageClass=1;spell.schoolMask=school;spell.spellFamily=3;
        spell.spellFamilyFlags={2,molten?8u:0u,0};spell.damage=spell.damageMax=damage;spell.range=30;spell.resourceType=0;
        c->spells.push_back(spell);
        LocalSpellDefinition crit;crit.id=900038;crit.name="Deterministic crit fixture";crit.talentId=900038;
        crit.talentRank=1;crit.passive=true;crit.passiveSpellCritPct=100;crit.allowableClasses=128;c->spells.push_back(crit);
        std::sort(c->spells.begin(),c->spells.end(),[](auto& a,auto& b){return a.id<b.id;});
        c->talentIndexReady=false;c->talentSpellIndex.clear();
        for(auto* player:{&p,&q}) {
            for(unsigned i=0;i<5;++i)assert(learnLocalTalent(*player,*c,26,i,error));
            for(unsigned i=0;i<rank;++i)assert(learnLocalTalent(*player,*c,34,i,error));
            player->talents.push_back({900038,1});player->knownSpells.push_back(900037);
        }
        game.useContent(c);game.tick(0,{&p,&q});game.setRemoteNpcs({n});capture();
        assert(localSpellCritChance(p,*c,4)>=100);
    }
    void capture(){for(const auto& event:game.combatEvents())if(event.sequence>seen){observed.push_back(event);seen=event.sequence;}}
    void keepTargetEngaged() {
        // A present bystander is not a combat participant. Keep the victim in
        // legitimate combat when testing caster departure, so an unrelated
        // evade/life-epoch reset does not remove the aura under observation.
        auto target=game.npcs()[0];
        auto slot=std::find_if(target.threat.begin(),target.threat.end(),[&](const auto& threat){return threat.guid==q.guid||!threat.guid;});
        assert(slot!=target.threat.end());*slot={q.guid,1000};game.setRemoteNpcs({target});
    }
    void cast(LocalRealmPlayer* caster=nullptr) {
        if(!caster)caster=&p;caster->globalCooldownMs=0;caster->cooldowns.clear();caster->categoryCooldowns.clear();caster->mana=caster->maxMana;
        if(!game.execute(*caster,{LocalAction::CastSpell,n.guid,900037},{&p,&q},error)){std::cerr<<error<<'\n';assert(false);}capture();
    }
    void advance(unsigned ms,bool casterPresent=true) {
        while(ms){unsigned step=std::min(ms,100u);game.tick(float(step)/1000, casterPresent?std::vector<LocalRealmPlayer*>{&p,&q}:std::vector<LocalRealmPlayer*>{&q});ms-=step;capture();}
    }
    std::vector<LocalCombatEvent> ticks(uint64_t caster=0) const {
        std::vector<LocalCombatEvent> result;for(const auto& e:observed)if(e.spell==12654&&e.kind==LocalCombatEventKind::PeriodicDamage&&(!caster||e.source==caster))result.push_back(e);return result;
    }
    size_t applications() const {return std::count_if(observed.begin(),observed.end(),[](const auto& e){return e.spell==12654&&e.kind==LocalCombatEventKind::ProcAura&&e.auraApplied;});}
};
}
int main(int argc,char** argv) {
    assert(argc==2);IgniteSource0237 source(argv[1]);const auto content=source.content();
    {
        Runtime f(content);f.cast();assert(f.applications()==0);f.advance(399);assert(f.applications()==0);
        f.advance(1);assert(f.applications()==1&&f.ticks().empty());
        auto origin=std::find_if(f.observed.begin(),f.observed.end(),[](const auto& e){return e.kind==LocalCombatEventKind::SpellDamage;});assert(origin!=f.observed.end());
        for(const auto& e:f.observed)if(e.spell==12654&&e.kind==LocalCombatEventKind::ProcAura) {
            assert(e.parentSequence==origin->sequence&&e.rootSequence==origin->rootSequence&&e.procDepth==origin->procDepth+1);
            assert(e.auraOwnerGuid==f.p.guid&&e.auraCasterGuid==f.p.guid);
        }
        f.advance(1999);assert(f.ticks().empty());
        f.advance(1);assert(f.ticks().size()==1&&f.ticks()[0].attempted==300);f.advance(2000);
        assert(f.ticks().size()==2&&f.ticks()[1].attempted==300);f.advance(2000);assert(f.ticks().size()==2);
        for(auto e:f.ticks())assert(e.source==f.p.guid&&e.target==f.n.guid&&e.outcome==LocalMeleeOutcome::Hit&&e.schoolMask==4);
    }
    {
        Runtime f(content);f.cast();f.advance(2400);assert(f.ticks().size()==1);
        f.cast();f.advance(2400);auto ticks=f.ticks();assert(ticks.size()==2&&ticks.back().attempted==450);
        f.advance(2000);assert(f.ticks().size()==3&&f.ticks().back().attempted==450);
    }
    {
        // Refresh before the first tick carries both outstanding old ticks.
        Runtime f(content);f.cast();f.advance(400);f.cast();f.advance(2400);
        assert(f.ticks().size()==1&&f.ticks()[0].attempted==600);
    }
    {
        // Both procs inspect the same empty aura state before their400ms queue
        // runs. Source blizzlike munching overwrites, rather than accumulating.
        Runtime f(content);f.cast();f.cast();f.advance(2400);
        assert(f.applications()==2&&f.ticks().size()==1&&f.ticks()[0].attempted==300);
    }
    {
        Runtime f(content);f.cast();f.cast(&f.q);f.advance(2400);
        assert(f.ticks(f.p.guid).size()==1&&f.ticks(f.q.guid).size()==1);
        assert(f.ticks(f.p.guid)[0].attempted==300&&f.ticks(f.q.guid)[0].attempted==300);
    }
    {
        // Removing the talent after proc selection does not undo an already
        // scheduled source cast, and later casts no longer produce Ignite.
        Runtime f(content);f.cast();std::erase_if(f.p.talents,[](auto talent){return talent.first==34;});
        f.advance(2400);assert(f.ticks().size()==1&&f.ticks()[0].attempted==300);
        f.cast();f.advance(4400);assert(f.applications()==1&&f.ticks().size()==2);
    }
    for(unsigned mode=0;mode<4;++mode) {
        Runtime f(content);f.cast();f.keepTargetEngaged();
        if(mode==0){auto replacement=f.n;replacement.combatEpoch=f.game.npcs()[0].combatEpoch+1;f.game.setRemoteNpcs({replacement});}
        if(mode==1){f.p.mapId=1;}
        if(mode==2){f.p.dead=true;f.p.health=0;}
        f.advance(4400,mode!=3);
        if(mode==2){assert(f.applications()==1&&f.ticks().size()==2);} // Child allows a dead caster.
        else assert(f.applications()==0&&f.ticks().empty());
    }
    for(bool differentMap:{false,true}) {
        Runtime f(content);f.cast();f.keepTargetEngaged();f.advance(400);
        if(differentMap)f.p.mapId=1;
        f.advance(4000,differentMap);assert(f.ticks().size()==2);
        for(auto e:f.ticks())assert(e.source==0&&e.auraCasterGuid==f.p.guid&&e.target==f.n.guid&&e.attempted==300);
    }
    {
        Runtime f(content);f.cast();f.keepTargetEngaged();f.advance(400);f.p.dead=true;f.p.health=0;
        f.advance(4000);assert(f.ticks().size()==2&&f.ticks()[0].attempted==300);
    }
    {
        Runtime f(content);f.cast();f.advance(400);auto replacement=f.n;
        replacement.combatEpoch=f.game.npcs()[0].combatEpoch+1;f.game.setRemoteNpcs({replacement});
        f.advance(4000);assert(f.ticks().empty());
    }
    {
        Runtime f(content,1000,16);f.cast();f.advance(4400);assert(f.applications()==0&&f.ticks().empty());
        Runtime molten(content,1000,4,true);molten.cast();molten.advance(4400);assert(molten.applications()==0&&molten.ticks().empty());
    }
    {
        // Small eligible crits still install a zero-damage source aura.
        Runtime f(content,1,4,false,1);f.cast();f.advance(4400);assert(f.applications()==1);
        assert(f.ticks().size()==2);for(auto e:f.ticks())assert(!e.attempted&&!e.effective);
    }
    {
        Runtime f(content);f.p.knownSpells.push_back(12654);
        assert(!f.game.execute(f.p,{LocalAction::CastSpell,f.n.guid,12654},{&f.p,&f.q},f.error));
        assert(f.game.npcs()[0].health==1000000);
    }
    for(bool moltenShields:{false,true}) {
        // Imported Molten Armor and its actual retaliation leaf receive a
        // real authority NPC spell impact. Both cases retain learned Ignite.
        Runtime f(content);f.c->npcs[0].id=4008;f.c->npcs[0].damage=0;
        f.n.entry=4008;f.n.level=80;f.n.targetGuid=f.p.guid;f.n.attackTimer=100000;
        f.p.x=4;f.q.x=5;f.n.x=f.n.homeX=1;
        const auto* profile=localNpcSpellProfile(4008);assert(profile&&profile->spellId==5401);
        assert(f.c->spell(profile->spellId)&&f.c->spell(profile->spellId)->npcOnly);
        const auto* armor=f.c->spell(30482);assert(armor&&armor->unsupportedReason.empty()&&armor->procCanCrit);
        const auto* retaliation=f.c->spell(armor->proc.spellId);assert(retaliation&&retaliation->triggeredOnly);
        if(moltenShields) {
            for(auto [talent,count]:{std::pair{27u,2u},std::pair{28u,2u},std::pair{29u,1u},std::pair{24u,2u}})
                for(unsigned rank=0;rank<count;++rank)assert(learnLocalTalent(f.p,*f.c,talent,rank,f.error));
        }
        f.game.setRemoteNpcs({f.n});f.p.knownSpells.push_back(30482);f.p.mana=f.p.maxMana;
        assert(f.game.execute(f.p,{LocalAction::CastSpell,f.p.guid,30482},{&f.p,&f.q},f.error));f.capture();
        unsigned incoming=0,retaliations=0;
        for(unsigned attempt=0;attempt<32;++attempt) {
            // Equal-level real hit rolls; each independent incoming cast starts
            // with a living target, so death cannot suppress its retaliation.
            f.p.health=f.p.maxHealth;
            auto npc=f.game.npcs()[0];npc.targetGuid=f.p.guid;npc.threat[0]={f.p.guid,100000};npc.attackTimer=100000;
            npc.npcSpellTimerInitialized=true;npc.npcSpellTimerMs=9000;npc.npcCastingSpellId=profile->spellId;
            npc.npcCastTargetGuid=f.p.guid;npc.npcCastRemainingMs=1;npc.npcSpellLaunched=false;
            f.game.setRemoteNpcs({npc});f.advance(100);
        }
        for(const auto& e:f.observed) {
            if(e.kind==LocalCombatEventKind::SpellDamage&&e.source==f.n.guid&&e.spell==profile->spellId&&e.effective)++incoming;
            if(e.kind==LocalCombatEventKind::ProcDamage&&e.spell==retaliation->id) {
                assert(e.source==f.p.guid&&e.target==f.n.guid&&e.auraSpell==30482);
                assert(e.auraOwnerGuid==f.p.guid&&e.auraCasterGuid==f.p.guid);
                assert(e.outcome==LocalMeleeOutcome::Critical);++retaliations;
            }
        }
        assert(incoming>0);assert(retaliations==(moltenShields?incoming:0));
        f.advance(4400);assert(!f.applications()&&f.ticks().empty());
    }
    std::cout<<"PASS Ignite runtime:400ms queue, two2s ticks, rolling remainder, timer reset, simultaneous munching, independent casters, queued/resolved respec, target epochs, pending caster map/missing exclusion, active orphan and dead-caster continuation, school/Molten exclusion, zero amount, internal cast rejection, actual NPC magic/Molten Shields retaliation and Ignite feedback exclusion\n";
}
