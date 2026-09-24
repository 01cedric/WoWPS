// Generated SmartAI cast family: real client decode of every profiled spell,
// multi-row timers, AURA_NOT_PRESENT, creature periodic damage on players and
// its harmful aura presentation.
#include "local_group_rewards_fixture.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_aura_presentation.hpp"
#include "game/local_equipment.hpp"
#include "game/local_melee.hpp"
#include "game/local_armor.hpp"
#include "game/local_combat_reach.hpp"
#include "game/local_quest_dialogue.hpp"
#include "pipeline/dbc_loader.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <map>
#include <set>
using namespace wowee;
using namespace wowee::game;

static std::vector<uint8_t> bytes(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};
}

int main(int argc,char** argv) {
    assert(argc==2);const std::filesystem::path dir=argv[1];
    std::cout<<std::unitbuf; // every PASS line survives a later abort
    pipeline::DBCFile spell,range,cast,duration,radius,summons;
    assert(spell.load(bytes(dir/"Spell.dbc"))&&range.load(bytes(dir/"SpellRange.dbc"))&&cast.load(bytes(dir/"SpellCastTimes.dbc"))&&duration.load(bytes(dir/"SpellDuration.dbc"))&&radius.load(bytes(dir/"SpellRadius.dbc")));
    // 2.38: SummonProperties.dbc classifies the creature summons.
    assert(summons.load(bytes(dir/"SummonProperties.dbc"))&&summons.getFieldCount()==6);
    detail::ClientSpellTables t;t.spells=&spell;t.ranges=&range;t.casts=&cast;t.durations=&duration;t.radii=&radius;t.summons=&summons;t.ready=true;
    t.buildIndex(&spell,t.spellIndex);t.buildIndex(&range,t.rangeIndex);t.buildIndex(&cast,t.castIndex);t.buildIndex(&duration,t.durationIndex);t.buildIndex(&radius,t.radiusIndex);t.buildIndex(&summons,t.summonIndex);
    std::vector<LocalSpellDefinition> spells;const auto retained=importLocalNpcSpells(t,spells);
    std::set<uint32_t> ids;for(const auto& p:kLocalNpcSpellProfiles)if(p.spellId())ids.insert(p.spellId());
    std::cerr<<"decoded "<<retained<<" of "<<ids.size()<<" profiled spells\n";
    for(uint32_t id:ids)if(std::none_of(spells.begin(),spells.end(),[&](const auto& d){return d.id==id;})) {
        LocalSpellDefinition d;d.id=id;const auto row=detail::ClientSpellTables::lookup(t.spellIndex,id);
        decodeLocalNpcGenericSpell(t,uint32_t(row),d);std::cerr<<"  not retained "<<id<<": "<<d.unsupportedReason<<"\n";
    }
    // Every retained definition is creature-only and carries a shape: a
    // hostile one with damage, weapon, aura, interrupt, knockback, trigger,
    // 2.37 direct (power burn / drain, dispel, charge, threat, kill credit,
    // item) or aura (resistance, cast speed, hit chance, avoidance, disarm)
    // effects, or a friendly one (heals, buffs, cosmetic auras).
    for(const auto& d:spells) {
        const bool shaped=d.npcOnly&&!d.allowableClasses&&d.unsupportedReason.empty()&&d.npcTargetShape<=6&&
        (d.npcPositive||d.npcCosmetic||d.npcSummonEntry||d.npcGroundAura||d.damage>0||d.periodicDamage>0||d.npcSlowPercent||d.npcArmorAmount||d.npcArmorAmountMax||d.npcArmorPerLevel!=0||d.npcArmorPercent||d.npcArmorPercentWide||d.npcWeaponEffect||
         d.npcWeaponPercent||d.npcPlayerControl||d.npcInterrupt||d.npcKnockbackSpeedXY>0||d.npcKnockbackZ.set||d.npcTriggerSpellId||d.npcPeriodicTriggerSpellId||
         d.npcDamageTakenFlat.set||d.npcDamageTakenPct.set||d.npcHealingPct.set||d.npcHaste.set||d.npcDamagePct.set||d.npcDamageFlat.set||d.npcAttackPower.set||
         d.npcResistance.set||d.npcCastSpeed.set||d.npcHitChance.set||d.npcDodge.set||d.npcParry.set||d.npcBlock.set||d.npcDisarm||d.npcPowerBurn.set||d.npcPowerDrain.set||
         d.npcDispelType||d.npcDispelMechanic||d.npcCharge||d.npcThreatPct||d.npcKillCredit||d.npcCreateItem||
         // 2.39: self instakill / stun / root / invisibility; a school damage effect of zero (BasePoints -1, 29148) keeps its slot
         d.npcInstakillSelf||d.npcSelfControl||d.npcInvisible||d.directEffectSlot!=255);
        if(!shaped)std::cerr<<"  unshaped "<<d.id<<" shape="<<unsigned(d.npcTargetShape)<<" reason="<<d.unsupportedReason<<"\n";
        assert(shaped);
    }
    const auto poison=std::find_if(spells.begin(),spells.end(),[](const auto& d){return d.id==11918;});
    assert(poison!=spells.end()&&poison->periodicDamage==9&&poison->periodicIntervalMs==3000&&poison->durationMs==15000&&poison->schoolMask==8);
    // 2.37: 1622 profiled spells (six party area auras among them stay
    // refused); the spells they trigger or proc decode too and one reviewed
    // NPC snare (8058) keeps its own decoder, so 1662 are retained.
    // ManaCostPerlevel (column 43) never reaches a creature's cost, so the
    // SCALES_WITH_CREATURE_LEVEL whelp bolts decode with their flat mana.
    std::cerr<<"retained="<<retained<<" ids="<<ids.size()<<"\n";
    // 2.37: the report's 1562 profiled spells, plus the triggered spells that
    // decode with them and the reviewed snare 8058.
    // 2.38: 1970 spells cast by script rows (the report's `spells`), 47 more
    // cast from timed action lists and the triggered / proc spells: 2068
    // retained; summons and persistent area auras decode now.
    // 2.39: 2134 profiled ids, 61 more from the lists, the triggered and proc
    // spells: 2272 retained (self stuns / roots / invisibility, item
    // creation, permanent periodic triggers, self triggers, instakill, plain
    // script effects and the mage creature's Arcane Explosion decode now;
    // 8058 keeps failing the generic shape as before).
    // 2.40: 2180 profiled ids (the gossip / emote owners' casts), 2320 retained.
    assert(retained==2320 && ids.size()==2180);
    std::cout<<"PASS client decode of the generated SmartAI cast family\n";

    // Forest Spider (30): 80 % Poison, AURA_NOT_PRESENT, 11 s initial, 15-20 s repeat.
    const auto range30=localNpcSpellProfiles(30);assert(range30.second-range30.first==1&&range30.first->castFlags()==kLocalSmartCastAuraNotPresent);
    assert(localNpcSpellDamageScale(30,20,true,20)==1.f&&localNpcSpellDamageScale(30,30,true,20)>1.f);
    auto c=rewardContent();c->npcs[0].id=30;c->npcs[0].level=20;c->npcs[0].health=100000;c->npcs[0].damage=0;
    c->spells.push_back(*poison);
    LocalGameplay game;game.useContent(c);game.seedGameObjectRandom(1);
    LocalRealmPlayer p=rewardPlayer(1);p.level=20;p.health=p.maxHealth=1000;p.x=3;
    LocalRealmNpc n=rewardNpc();n.entry=30;n.level=20;n.health=n.maxHealth=100000;n.targetGuid=p.guid;n.threat[0]={p.guid,100000};n.attackTimer=10000;
    game.tick(0,{&p});game.setRemoteNpcs({n});
    unsigned elapsed=0;uint32_t firstDamageAt=0;unsigned ticks=0;
    while(elapsed<90000) {
        const auto before=p.health;game.tick(.25f,{&p});elapsed+=250;
        if(p.health<before){if(!firstDamageAt)firstDamageAt=elapsed;++ticks;}
        if(!p.harmfulAuras.empty()) {
            const auto& view=p.harmfulAuras[0];assert(view.spellId==11918&&view.casterGuid==n.guid&&view.durationMs==15000&&view.remainingMs<=15000);
            // Presentation: appended after buffs with the harmful flag.
            const auto index=localOwnerAuraCount(p)-1;const auto shown=localOwnerAuraAt(p,*c,index);
            assert(shown.spellId==11918&&(shown.flags&0x80));
        }
        auto* npc=&game.npcs()[0];
        // AURA_NOT_PRESENT: never a second application while the poison ticks.
        assert(p.harmfulAuras.size()<=1);(void)npc;
    }
    if(!(firstDamageAt>=11000+3000-500 && ticks>=5)){const auto& m=game.npcs()[0];std::cerr<<"first="<<firstDamageAt<<" ticks="<<ticks<<" timer="<<m.npcSpellTimerMs<<" init="<<m.npcSpellTimerInitialized<<" range="<<poison->range<<" cast="<<poison->castTimeMs<<" target="<<m.targetGuid<<" hp="<<p.health<<"\n";}
    assert(firstDamageAt>=11000+3000-500 && ticks>=5);
    // Nature DoT ticks for exactly 9 at spell level.
    std::cout<<"PASS Forest Spider poison: 80 % chance, 3 s ticks for 15 s, harmful view, no re-application while present ("<<ticks<<" ticks)\n";

    // The DoT ends with the target's death and never outlives it.
    p.health=5;auto guard=0;while(!p.dead&&guard++<400)game.tick(.25f,{&p});
    assert(p.dead);game.tick(.25f,{&p});assert(p.harmfulAuras.empty());
    std::cout<<"PASS lethal periodic tick kills through the ordinary death path and clears the debuff\n";

    // Creature mana (2.31): Roogug (6168) casts Lightning Bolt 9532 (90 mana at
    // spell level 20) every 3 s. At level 20 the cost is exactly 90; the pool is
    // ceil(BaseMana(20, paladin) * ModMana). Casting stops at SPELL_FAILED_NO_POWER,
    // the COMBAT_MOVE row lets it close to melee, and five seconds after the last
    // cast the in-combat 17 mana per 2 s regeneration buys the next bolt.
    {
        const auto bolt=std::find_if(spells.begin(),spells.end(),[](const auto& d){return d.id==9532;});
        assert(bolt!=spells.end()&&bolt->mana==90&&bolt->castTimeMs>0);
        const auto* roogug=localNpcSpellProfile(6168);assert(roogug&&roogug->unitClass==2&&roogug->regenMana);
        const auto maxMana=localNpcMaxMana(*roogug,20);assert(maxMana>=180);
        assert(localNpcSpellManaCost(*roogug,20,90,0,maxMana)==90);
        assert(localNpcSpellManaCost(*roogug,10,90,0,maxMana)<90&&localNpcSpellManaCost(*roogug,30,90,0,maxMana)>90);
        auto mc=rewardContent();mc->npcs[0].id=6168;mc->npcs[0].level=20;mc->npcs[0].health=1000000;mc->npcs[0].damage=0;
        mc->spells.push_back(*bolt);
        LocalGameplay mg;mg.seedGameObjectRandom(7);
        LocalRealmPlayer mp=rewardPlayer(1);mp.level=20;mp.health=mp.maxHealth=10000000;mp.x=10;
        LocalRealmNpc mn=rewardNpc();mn.entry=6168;mn.level=20;mn.health=mn.maxHealth=1000000;mn.targetGuid=mp.guid;
        mn.threat[0]={mp.guid,1000000};mn.attackTimer=100000;
        // Spawn-backed, so a disengaged creature stays in the active region.
        mn.spawnId=10;mn.guid=0xf130000000000000ULL|mn.spawnId;
        mc->spawns.push_back({mn.spawnId,mn.entry,mn.mapId,mn.x,mn.y,mn.z,mn.orientation});
        mg.useContent(mc);
        mg.tick(0,{&mp});mg.setRemoteNpcs({mn});
        mg.tick(.25f,{&mp});
        assert(mg.npcs()[0].npcManaReady&&mg.npcs()[0].npcMaxMana==maxMana&&mg.npcs()[0].npcMana==maxMana);
        unsigned casts=0,elapsedMs=0,firstGapAt=0;uint32_t lowest=maxMana;uint32_t lastSpell=0;
        std::vector<unsigned> startTimes;bool closedIn=false;
        // SmartAI range mode: Lightning Bolt's COMBAT_MOVE row is the main
        // spell (40 yd), so the caster keeps a 35 yd chase distance and stays
        // where it stands, 10 yd away.
        assert(bolt->range==40.f&&mg.npcs()[0].npcRangeMode&&mg.npcs()[0].npcAttackDistance==35.f);
        while(elapsedMs<120000) {
            mp.health=mp.maxHealth;
            const auto before=mg.npcs()[0].npcMana;
            mg.tick(.25f,{&mp});elapsedMs+=250;
            const auto& m=mg.npcs()[0];
            if(m.npcMana<before&&before-m.npcMana==90){++casts;startTimes.push_back(elapsedMs);}
            lowest=std::min(lowest,m.npcMana);assert(m.npcMana<=maxMana);
            if(!firstGapAt&&casts==maxMana/90&&m.npcMana<90)firstGapAt=elapsedMs;
            const float dx=m.x-mp.x,dy=m.y-mp.y,distance=std::sqrt(dx*dx+dy*dy);
            if(!firstGapAt)assert(distance>9.9f&&m.npcRangeMode);
            // Out of mana, the COMBAT_MOVE row's NO_POWER ends range mode and
            // the creature closes to melee (SetCurrentRangeMode(false)).
            if(firstGapAt&&!m.npcRangeMode&&distance<=3.01f)closedIn=true;
            (void)lastSpell;
        }
        assert(closedIn);
        std::cerr<<"mana="<<maxMana<<" casts="<<casts<<" lowest="<<lowest<<" firstGap="<<firstGapAt<<"\n";
        assert(firstGapAt&&lowest<90);
        assert(casts>maxMana/90);
        // Full pool: bolts every ~4.25 s. Afterwards each bolt needs the 5 s of
        // silence plus at least two 17-mana ticks and its 3 s cast bar.
        for(size_t i=1;i<=maxMana/90-1;++i)assert(startTimes[i]-startTimes[i-1]<5000);
        for(size_t i=maxMana/90;i<startTimes.size();++i)assert(startTimes[i]-startTimes[i-1]>=5000+2*2000+3000-250);
        std::cout<<"PASS creature mana: "<<maxMana/90<<" bolts from a full pool at range, no cast below cost, then melee range and the 5-second rule with 17 per 2 s ("<<casts<<" bolts in 120 s)\n";
        // Leaving combat: a third of the pool every 2 s.
        auto state=mg.npcs()[0];state.targetGuid=0;state.threat={};state.npcMana=0;state.npcManaRegenMs=2000;
        // A player beyond aggro range but inside the active region.
        mg.setRemoteNpcs({state});mp.x=100;
        std::vector<uint32_t> seen;
        for(int i=0;i<24;++i){mg.tick(.25f,{&mp});assert(mg.npcs().size()==1&&!mg.npcs()[0].targetGuid);seen.push_back(mg.npcs()[0].npcMana);}
        const auto& back=mg.npcs()[0];
        assert(seen[6]==0&&seen[7]==maxMana/3&&seen[15]==2*(maxMana/3)&&back.npcMana==3*(maxMana/3));
        std::cout<<"PASS out-of-combat creature mana: a third of the pool every 2 s (mana="<<back.npcMana<<" of "<<maxMana<<")\n";
        // Respawn recreates the pool full (Creature::InitStatsForLevel).
        auto corpse=mg.npcs()[0];corpse.dead=true;corpse.health=0;corpse.respawnTimer=0.1f;corpse.npcMana=0;
        mg.setRemoteNpcs({corpse});mp.x=150;
        for(int i=0;i<3;++i)mg.tick(.25f,{&mp});
        assert(mg.npcs().size()==1&&mg.npcs()[0].guid==corpse.guid&&!mg.npcs()[0].dead&&mg.npcs()[0].npcMana==maxMana);
        std::cout<<"PASS respawn restores a full creature mana pool\n";
    }

    // 2.32 creature auras. Decode: Frostbolt 20822 is direct Frost damage plus a
    // fixed 50 % slow for 4 s; Corrosive Poison 3396 is Nature periodic damage
    // plus armor -60 with RealPointsPerLevel -2.4.
    const auto spellOf=[&](uint32_t id){const auto it=std::find_if(spells.begin(),spells.end(),[&](const auto& d){return d.id==id;});assert(it!=spells.end());return *it;};
    const auto frost=spellOf(20822),corrosive=spellOf(3396);
    assert(frost.damage>0&&frost.npcSlowPercent==50&&frost.durationMs==4000&&!frost.npcArmorAmount&&!frost.periodicDamage);
    assert(corrosive.periodicDamage>0&&corrosive.npcArmorAmount==-60&&corrosive.npcArmorPerLevel<-2.39f&&corrosive.npcArmorPerLevel>-2.41f&&
           !corrosive.damage&&!corrosive.npcSlowPercent&&corrosive.durationMs==30000);
    std::cout<<"PASS creature aura decode: Frostbolt damage + 50 % slow, Corrosive Poison DoT + armor reduction\n";
    // A small arena: spawn-backed creatures of one entry against one player.
    struct Arena {
        std::shared_ptr<LocalWorldContent> c;LocalGameplay g;LocalRealmPlayer p,q;std::vector<LocalRealmNpc> n;bool two=false;
        // 2.36: an optional second player (q), extra content spells, and a
        // spawn id base (a spawn guid script is keyed by the spawn id).
        Arena(uint32_t entry,uint32_t level,const LocalSpellDefinition& spell,unsigned count,uint32_t weapon=0,bool second=false,
              std::vector<LocalSpellDefinition> extra={},uint32_t spawnBase=20,bool guidScript=false,
              std::vector<LocalNpcDefinition> extraNpcs={},std::vector<LocalCreatureTextGroup> texts={}):two(second) {
            c=rewardContent();c->npcs[0].id=entry;c->npcs[0].level=level;c->npcs[0].health=1000000;c->npcs[0].damage=weapon;
            // 2.38: the definitions of summoned creatures and the text groups TALK rows speak.
            for(const auto& d:extraNpcs)c->npcs.push_back(d);
            std::sort(c->npcs.begin(),c->npcs.end(),[](const auto& a,const auto& b){return a.id<b.id;});
            c->creatureTextGroups=texts;
            std::sort(c->creatureTextGroups.begin(),c->creatureTextGroups.end(),[](const auto& a,const auto& b){return std::make_pair(a.entry,a.group)<std::make_pair(b.entry,b.group);});
            if(guidScript)c->creatureGuidScripts.push_back(spawnBase);
            c->spells.push_back(spell);for(const auto& e:extra)c->spells.push_back(e);
            std::sort(c->spells.begin(),c->spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
            LocalItemDefinition plate;plate.id=900;plate.name="Test chest";plate.inventoryType=5;plate.armor=500;c->items.push_back(plate);
            g.seedGameObjectRandom(11);
            p=rewardPlayer(1);p.level=level;p.health=p.maxHealth=10000000;p.x=10;
            q=rewardPlayer(2);q.level=level;q.health=q.maxHealth=10000000;q.x=10;q.y=2;
            for(auto* player:{&p,&q}) {
                for(size_t slot=0;slot<player->equipment.size();++slot)if(localEquipmentFits(5,0,slot)){player->equipment[slot]=900;break;}
                player->inventory.push_back({900,1});
                for(const auto& e:extra)player->knownSpells.push_back(e.id);
            }
            for(unsigned i=0;i<count;++i) {
                LocalRealmNpc m=rewardNpc();m.entry=entry;m.level=level;m.health=m.maxHealth=1000000;m.targetGuid=p.guid;
                m.threat[0]={p.guid,1000000};m.attackTimer=weapon?0.f:100000.f;m.spawnId=spawnBase+i;m.guid=0xf130000000000000ULL|m.spawnId;m.y=float(i);m.homeY=m.y;
                c->spawns.push_back({m.spawnId,m.entry,m.mapId,m.x,m.y,m.z,m.orientation});n.push_back(m);
            }
            g.useContent(c);g.tick(0,party());g.setRemoteNpcs(n);
        }
        std::vector<LocalRealmPlayer*> party(){return two?std::vector<LocalRealmPlayer*>{&p,&q}:std::vector<LocalRealmPlayer*>{&p};}
        void step(){p.health=p.maxHealth;q.health=q.maxHealth;g.tick(.25f,party());}
        // The creature's state, rewritten through the authority's roster.
        void alter(const std::function<void(LocalRealmNpc&)>& f){auto state=g.npcs();for(auto& m:state)f(m);g.setRemoteNpcs(state);}
    };
    {
        // Hatecrest Sorceress (5336): Frostbolt with COMBAT_MOVE at level 20.
        Arena a(5336,20,frost,1);
        unsigned elapsed=0;bool hit=false;
        while(elapsed<30000&&!hit){a.step();elapsed+=250;hit=!a.p.harmfulAuras.empty();}
        assert(hit);
        const auto view=a.p.harmfulAuras[0];
        assert(view.spellId==20822&&view.slowPercent==50&&view.armorModifier==0&&view.durationMs==4000&&view.casterGuid==a.n[0].guid);
        const auto events=a.g.combatEvents();
        assert(std::any_of(events.begin(),events.end(),[](const auto& e){return e.spell==20822&&e.kind==LocalCombatEventKind::SpellDamage&&e.attempted>0;}));
        // The caster dies; its slow still runs out on its own after 4 s.
        auto dead=a.g.npcs()[0];dead.dead=true;dead.health=0;dead.respawnTimer=600;dead.npcCastingSpellId=0;dead.targetGuid=0;dead.threat={};
        a.g.setRemoteNpcs({dead});
        unsigned left=0;while(!a.p.harmfulAuras.empty()&&left<10000){a.step();left+=250;}
        assert(left>=3000&&left<=4250);
        std::cout<<"PASS Frostbolt: damage, 50 % slow view for 4 s outliving its dead caster ("<<left<<" ms left)\n";
    }
    {
        // SpellEffectInfo::CalcValue per effect: a level term (RealPointsPerLevel)
        // is added on the clamped caster level minus max(BaseLevel, SpellLevel)
        // and switches creature scaling off for that effect; without it the
        // effect scales with creature_classlevelstats.
        const auto firstDamage=[&](LocalSpellDefinition spell,uint32_t level) {
            Arena a(5336,level,spell,1);unsigned elapsed=0;
            while(elapsed<60000){a.step();elapsed+=250;
                for(const auto& e:a.g.combatEvents())if(e.spell==20822&&e.kind==LocalCombatEventKind::SpellDamage&&e.attempted)return e.attempted;}
            assert(false);return 0u;
        };
        auto leveled=frost;leveled.damage=leveled.damageMax=100;leveled.damagePerLevel=2;leveled.baseLevel=10;leveled.spellLevel=15;leveled.maxLevel=0;
        assert(firstDamage(leveled,25)==100+20);
        auto scaled=leveled;scaled.damagePerLevel=0;
        const auto scale=localNpcSpellDamageScale(5336,25,scaled.npcScales,scaled.spellLevel);assert(scale>1.f);
        assert(firstDamage(scaled,25)==uint32_t(100.f*scale));
        std::cout<<"PASS CalcValue per effect: level term 100+int(10*2)=120 unscaled; without it 100 x "<<scale<<"\n";
    }
    {
        // SmartAI range mode against a player 45 yd away: Hatecrest Sorceress
        // (5336) chases to 40 - 5 yd plus contact distance and both combat
        // reaches and casts from there, never closing to melee.
        Arena a(5336,20,frost,1);a.p.x=45;
        unsigned elapsed=0;bool cast=false;float nearest=1000;
        while(elapsed<20000){a.p.x=45;a.step();elapsed+=250;
            const auto& m=a.g.npcs()[0];nearest=std::min(nearest,std::fabs(a.p.x-m.x));
            for(const auto& e:a.g.combatEvents())cast=cast||(e.spell==20822&&e.kind==LocalCombatEventKind::SpellCast);}
        assert(frost.range==40.f&&a.g.npcs()[0].npcAttackDistance==35.f);
        const float stop=35.f+0.5f+localCreatureCombatReach(a.c->npc(5336))+kLocalDefaultCombatReach;
        assert(cast&&a.g.npcs()[0].npcRangeMode&&nearest>=stop-0.6f&&nearest<=stop+0.01f);
        std::cout<<"PASS range mode: the Frostbolt caster stopped "<<nearest<<" yd from a player 45 yd away (chase distance 35 yd) and cast\n";
    }
    {
        // Two Hatecrest Sorceresses: Frostbolt has no periodic effect, so a
        // second caster's application replaces the first (Aura::CanStackWith).
        Arena a(5336,20,frost,2);
        unsigned elapsed=0;std::set<uint64_t> casters;size_t most=0;
        while(elapsed<40000){a.step();elapsed+=250;most=std::max(most,a.p.harmfulAuras.size());for(const auto& v:a.p.harmfulAuras)casters.insert(v.casterGuid);}
        assert(most==1&&casters.size()==2);
        std::cout<<"PASS non-periodic creature aura from a second caster replaces the first\n";
    }
    {
        // Elder Moss Creeper (2348) at level 20: Corrosive Poison armor is
        // -60 + int(-2.4 * (20 - max(BaseLevel, SpellLevel 17))) = -67.
        Arena a(2348,20,corrosive,2);
        auto bare=a.p;bare.harmfulAuras.clear();
        const auto baseArmor=localMeleeArmor(bare,*a.c);assert(baseArmor>=500);
        unsigned elapsed=0;size_t most=0;int32_t sum=0;
        while(elapsed<60000){a.step();elapsed+=250;
            if(a.p.harmfulAuras.size()>most){most=a.p.harmfulAuras.size();sum=0;for(const auto& v:a.p.harmfulAuras)sum+=v.armorModifier;
                assert(localMeleeArmor(a.p,*a.c)==uint32_t(std::max<int64_t>(0,int64_t(baseArmor)+sum)));}}
        const int32_t expected=-60+int32_t(float(20-std::max<uint32_t>(corrosive.baseLevel,corrosive.spellLevel))*corrosive.npcArmorPerLevel);
        assert(expected==-67);
        for(const auto& v:a.p.harmfulAuras)assert(v.spellId==3396&&v.armorModifier==expected&&v.slowPercent==0);
        // A periodic spell stacks per caster.
        assert(most==2&&sum==2*expected);
        std::cout<<"PASS Corrosive Poison: per-level armor reduction "<<expected<<" per caster in TOTAL_VALUE, stacks across casters with its DoT (armor "<<baseArmor<<" -> "<<baseArmor+sum<<")\n";
    }
    {
        // Poison Sprite (12216): Poison Bolt 21067 (damage + 5 s-tick DoT, 10 s,
        // StackAmount 3) every 2.5-3 s. Reapplication adds a stack up to three,
        // refreshes the duration and recalculates the amount x stacks, while
        // the running tick timer is kept (AuraEffect::CalculatePeriodic).
        const auto bolt=spellOf(21067);assert(bolt.maxAuraStacks==3&&bolt.damage&&bolt.periodicDamage&&bolt.periodicIntervalMs==5000);
        Arena a(12216,20,bolt,1);
        unsigned elapsed=0;uint8_t most=0;std::vector<unsigned> ticks;std::vector<uint32_t> amounts;uint64_t seen=0;
        while(elapsed<40000){a.step();elapsed+=250;
            for(const auto& v:a.p.harmfulAuras){assert(v.spellId==21067&&v.stacks<=3);most=std::max(most,v.stacks);}
            for(const auto& e:a.g.combatEvents())if(e.sequence>seen){seen=e.sequence;
                if(e.spell==21067&&e.kind==LocalCombatEventKind::PeriodicDamage){ticks.push_back(elapsed);amounts.push_back(e.attempted);}}}
        assert(most==3&&ticks.size()>=5);
        for(size_t i=1;i<ticks.size();++i)assert(ticks[i]-ticks[i-1]==5000);
        assert(amounts.back()>amounts.front());
        std::cout<<"PASS stacking DoT: Poison Bolt reaches 3 stacks, ticks every 5 s through reapplications ("<<ticks.size()<<" ticks, "<<amounts.front()<<" -> "<<amounts.back()<<")\n";
    }
    // 2.33 creature melee specials. Unit::MeleeSpellHitResult on exact rolls:
    // equal levels, 5 % miss, 10 % dodge, 5 % parry, no shield.
    {
        LocalMeleeStats st;st.dodge=10;st.parry=5;st.block=0;st.missBonus=0;
        LocalRealmNpc m=rewardNpc();m.level=20;m.x=m.y=0;
        LocalRealmPlayer v=rewardPlayer(9);v.level=20;v.x=3;v.y=0;v.orientation=3.14159265f;
        LocalSpellDefinition sp;sp.sourceDamageClass=2;sp.sourceDirectDamage=true;
        const auto at=[&](uint32_t roll){return localRollNpcMeleeSpell(m,v,st,sp,roll);};
        assert(at(0)==LocalMeleeOutcome::Miss&&at(499)==LocalMeleeOutcome::Miss&&at(500)==LocalMeleeOutcome::Dodge&&
               at(1499)==LocalMeleeOutcome::Dodge&&at(1500)==LocalMeleeOutcome::Parry&&at(1999)==LocalMeleeOutcome::Parry&&
               at(2000)==LocalMeleeOutcome::Hit&&at(10000)==LocalMeleeOutcome::Hit);
        v.orientation=0;   // attacked from behind: a player neither dodges nor parries
        assert(at(499)==LocalMeleeOutcome::Miss&&at(500)==LocalMeleeOutcome::Hit);
        v.orientation=3.14159265f;v.castingSpellId=1;   // no active defense while casting
        assert(at(500)==LocalMeleeOutcome::Hit);v.castingSpellId=0;
        m.level=22;   // two levels above: miss 5 - 0.2, dodge and parry -0.4 each
        assert(at(479)==LocalMeleeOutcome::Miss&&at(480)==LocalMeleeOutcome::Dodge&&at(1439)==LocalMeleeOutcome::Dodge&&
               at(1440)==LocalMeleeOutcome::Parry&&at(1899)==LocalMeleeOutcome::Parry&&at(1900)==LocalMeleeOutcome::Hit);
        m.level=20;
        auto noDefense=sp;noDefense.sourceNoActiveDefense=true;
        assert(localRollNpcMeleeSpell(m,v,st,noDefense,499)==LocalMeleeOutcome::Miss&&localRollNpcMeleeSpell(m,v,st,noDefense,500)==LocalMeleeOutcome::Hit);
        auto sure=sp;sure.sourceAlwaysHit=true;assert(localRollNpcMeleeSpell(m,v,st,sure,0)==LocalMeleeOutcome::Hit);
        auto noMiss=sp;noMiss.sourceNoAttackMiss=true;assert(localRollNpcMeleeSpell(m,v,st,noMiss,0)==LocalMeleeOutcome::Dodge);
        // A full block exists only for a COMPLETELY_BLOCKED spell without direct damage.
        st.block=10;auto blockable=sp;blockable.sourceCompletelyBlocked=true;blockable.sourceDirectDamage=false;
        assert(localRollNpcMeleeSpell(m,v,st,blockable,2000)==LocalMeleeOutcome::Block&&localRollNpcMeleeSpell(m,v,st,blockable,2999)==LocalMeleeOutcome::Block&&
               localRollNpcMeleeSpell(m,v,st,blockable,3000)==LocalMeleeOutcome::Hit&&at(2000)==LocalMeleeOutcome::Hit);
        // Unit::isSpellBlocked: a separate partial-block roll against the block chance.
        assert(localNpcMeleeSpellBlocked(m,v,st,sp,999)&&!localNpcMeleeSpellBlocked(m,v,st,sp,1000));
        v.orientation=0;assert(!localNpcMeleeSpellBlocked(m,v,st,sp,0));
        std::cout<<"PASS Unit::MeleeSpellHitResult table: miss/dodge/parry bands, behind, casting, level difference, attributes, full and partial block\n";
    }
    {
        // Farmer Solliden (1936): Strike 11976, ON_NEXT_SWING, WEAPON_DAMAGE +5
        // (+1.5 per level). The queued special replaces the next white swing.
        const auto strike=spellOf(11976);
        assert(strike.npcNextSwing&&strike.npcWeaponEffect&&strike.npcWeaponScales&&strike.sourceDamageClass==2&&
               strike.npcWeaponBonus==5&&strike.npcWeaponBonusPerLevel>1.49f&&strike.npcWeaponBonusPerLevel<1.51f&&!strike.npcWeaponPercent);
        Arena a(1936,20,strike,1,30);
        unsigned elapsed=0,specials=0,whites=0,hits=0;uint64_t seen=0;uint32_t hitDamage=0,swingsAtHit=0;
        while(elapsed<60000){a.step();elapsed+=250;
            for(const auto& e:a.g.combatEvents())if(e.sequence>seen){seen=e.sequence;
                if(e.kind==LocalCombatEventKind::NpcMelee)++whites;
                if(e.spell==11976&&e.kind==LocalCombatEventKind::SpellDamage){
                    ++specials;assert(e.attackType==LocalCombatAttackType::Melee);
                    if(e.outcome==LocalMeleeOutcome::Hit&&!hits++){hitDamage=e.attempted;swingsAtHit=whites+specials;}}}}
        // One swing every 2 s: every swing is either white or the special.
        assert(specials>=4&&hits>=1&&whites+specials>=28&&whites+specials<=31);
        // CalcValue: 5 + int((20 - max(BaseLevel, SpellLevel)) * 1.5); no creature scaling with a level term.
        const int32_t level=20-int32_t(std::max<uint32_t>(strike.baseLevel,strike.spellLevel));
        const uint32_t raw=30+5+uint32_t(int32_t(float(level)*strike.npcWeaponBonusPerLevel));
        auto bare=a.p;bare.harmfulAuras.clear();
        const uint32_t expected=localArmorReducedDamage(raw,localMeleeArmor(bare,*a.c),20);
        assert(hitDamage==expected);
        std::cout<<"PASS next-swing Strike: "<<specials<<" specials replace white swings ("<<whites<<" white), weapon 30 + bonus -> "<<raw<<" raw, "<<hitDamage<<" after armor (swing "<<swingsAtHit<<")\n";
    }
    {
        // Black Ravager (628): Rend 13443, a bleed (mechanic 15) DoT ignoring armor.
        const auto rend=spellOf(13443);assert(rend.sourceDamageClass==2&&rend.periodicDamage==9&&rend.periodicIgnoresArmor&&rend.durationMs==15000);
        Arena a(628,20,rend,1);
        unsigned elapsed=0;std::vector<uint32_t> ticks;uint64_t seen=0;
        while(elapsed<40000&&ticks.size()<3){a.step();elapsed+=250;
            for(const auto& e:a.g.combatEvents())if(e.sequence>seen){seen=e.sequence;
                if(e.spell==13443&&e.kind==LocalCombatEventKind::PeriodicDamage)ticks.push_back(e.attempted);}}
        assert(ticks.size()==3);for(auto t:ticks)assert(t==9);
        std::cout<<"PASS Rend: melee-rolled bleed ticks 9 through 570 armor\n";
    }
    {
        // Mossflayer Scout (8560): Hamstring 9080, 20 % weapon damage plus a 60 % slow.
        const auto ham=spellOf(9080);assert(ham.npcWeaponPercent==20&&!ham.npcWeaponEffect&&ham.npcSlowPercent==60&&ham.durationMs==10000);
        Arena a(8560,20,ham,1,100);
        unsigned elapsed=0;bool slowed=false;uint32_t damage=0;uint64_t seen=0;
        while(elapsed<40000&&!(slowed&&damage)){a.step();elapsed+=250;
            for(const auto& v:a.p.harmfulAuras)if(v.spellId==9080&&v.slowPercent==60)slowed=true;
            for(const auto& e:a.g.combatEvents())if(e.sequence>seen){seen=e.sequence;
                if(e.spell==9080&&e.kind==LocalCombatEventKind::SpellDamage&&e.outcome==LocalMeleeOutcome::Hit)damage=e.attempted;}}
        auto bare=a.p;bare.harmfulAuras.clear();
        assert(slowed&&damage==localArmorReducedDamage(20,localMeleeArmor(bare,*a.c),20));
        std::cout<<"PASS Hamstring: 20 % of the creature's weapon damage and a 60 % slow view\n";
    }
    {
        // Defias Miner (598): Pierce Armor 6016, MOD_RESISTANCE_PCT -50 % (TOTAL_PCT).
        // It is also a next-swing special, so the creature must swing.
        const auto pierce=spellOf(6016);assert(pierce.npcArmorPercent==-50&&pierce.durationMs==20000&&pierce.npcNextSwing);
        Arena a(598,20,pierce,1,10);
        auto bare=a.p;bare.harmfulAuras.clear();const auto before=localMeleeArmor(bare,*a.c);
        unsigned elapsed=0;bool applied=false;
        while(elapsed<40000&&!applied){a.step();elapsed+=250;for(const auto& v:a.p.harmfulAuras)applied=applied||(v.spellId==6016&&v.armorPercent==-50);}
        assert(applied&&localMeleeArmor(a.p,*a.c)==uint32_t(float(before)*0.5f));
        std::cout<<"PASS Pierce Armor: armor "<<before<<" -> "<<localMeleeArmor(a.p,*a.c)<<" (TOTAL_PCT -50 %)\n";
    }
    {
        // 2.34 ranged specials. Redridge Poacher (424): Shoot 6660 with
        // COMBAT_MOVE, DmgClass RANGED, USES_RANGED_SLOT (+500 ms cast),
        // physical 36-48 at SpellLevel 20, 5-30 yd (Spell::CheckRange's npc
        // shooter minimum adds the melee range), a 40 yd/s missile. The melee
        // table without dodge and parry; armor applies.
        const auto shoot=spellOf(6660);
        assert(shoot.sourceDamageClass==3&&shoot.castTimeMs==500&&shoot.damage==36&&shoot.damageMax==48&&shoot.minRange==5.f&&shoot.range==30.f&&shoot.sourceProjectileSpeed==40.f);
        {
            Arena a(424,20,shoot,1);a.p.x=15;
            unsigned elapsed=0,hits=0,misses=0;uint64_t seen=0;uint32_t lo=~0u,hi=0;
            while(elapsed<120000){a.p.x=15;a.step();elapsed+=250;
                for(const auto& e:a.g.combatEvents())if(e.sequence>seen){seen=e.sequence;
                    if(e.spell!=6660||e.kind!=LocalCombatEventKind::SpellDamage)continue;
                    assert(e.attackType==LocalCombatAttackType::Ranged&&e.outcome!=LocalMeleeOutcome::Dodge&&e.outcome!=LocalMeleeOutcome::Parry);
                    if(e.outcome==LocalMeleeOutcome::Hit){++hits;lo=std::min(lo,e.attempted);hi=std::max(hi,e.attempted);}else ++misses;}}
            auto bare=a.p;bare.harmfulAuras.clear();const auto armor=localMeleeArmor(bare,*a.c);
            assert(hits>=10&&lo>=localArmorReducedDamage(36,armor,20)&&hi<=localArmorReducedDamage(48,armor,20));
            std::cout<<"PASS ranged Shoot: "<<hits<<" hits "<<lo<<"-"<<hi<<" after armor, "<<misses<<" misses, never dodged or parried\n";
        }
        {
            Arena a(424,20,shoot,1);
            unsigned elapsed=0;uint64_t seen=0;bool cast=false;
            while(elapsed<20000){a.p.x=3;a.step();elapsed+=250;
                for(const auto& e:a.g.combatEvents())if(e.sequence>seen){seen=e.sequence;cast=cast||(e.spell==6660&&e.kind==LocalCombatEventKind::SpellCast);}}
            assert(!cast);
            std::cout<<"PASS ranged minimum range: no Shoot at 3 yd (5 yd plus the melee range)\n";
        }
    }
    {
        // 2.35 creature controls on players. Brawler (1207): Backhand 6253, a
        // 2 s MOD_STUN (mechanic stunned -> DIMINISHING_CONTROLLED_STUN,
        // DRTYPE_ALL). Repeated within the 15 s window it lasts 2000, 1000 and
        // 500 ms, then the player is immune; a stunned player's cast is refused.
        const auto backhand=spellOf(6253);assert(backhand.npcPlayerControl==1&&backhand.durationMs==2000);
        Arena a(1207,20,backhand,1,10);
        unsigned elapsed=0;bool wasStunned=false,refused=false;std::vector<uint32_t> durations;uint64_t seen=0;unsigned immune=0;
        while(elapsed<60000){a.step();elapsed+=250;
            const bool stunned=(localPlayerControl(a.p)&1u)!=0;
            if(stunned&&!wasStunned)for(const auto& v:a.p.harmfulAuras)if(v.controlKind==1)durations.push_back(v.durationMs);
            if(stunned&&!refused) {
                a.p.globalCooldownMs=0;std::string why;
                assert(!a.g.execute(a.p,{LocalAction::CastSpell,a.n[0].guid,1},{&a.p},why));
                refused=why=="Can't do that while stunned";
            }
            wasStunned=stunned;
            for(const auto& e:a.g.combatEvents())if(e.sequence>seen){seen=e.sequence;if(e.spell==6253&&e.outcome==LocalMeleeOutcome::Immune)++immune;}
        }
        assert(refused&&durations.size()>=3&&durations[0]==2000&&durations[1]==1000&&durations[2]==500);
        std::cout<<"PASS creature stun: Backhand lasts "<<durations[0]<<"/"<<durations[1]<<"/"<<durations[2]
                 <<" ms under diminishing returns ("<<immune<<" immune hits), and a stunned player's cast is refused\n";
    }
    {
        // Thistleshrub Dew Collector (5481): Entangling Roots 11922, MOD_ROOT
        // with a DoT; a rooted player is held in place but may still act.
        const auto roots=spellOf(11922);assert(roots.npcPlayerControl==2&&roots.periodicDamage);
        Arena a(5481,20,roots,1);
        unsigned elapsed=0;bool rooted=false,acted=false;
        while(elapsed<30000&&!rooted){a.step();elapsed+=250;rooted=localPlayerControl(a.p)==2u;}
        if(rooted){a.p.globalCooldownMs=0;std::string why;a.g.execute(a.p,{LocalAction::CastSpell,a.n[0].guid,1},{&a.p},why);acted=why!="Can't do that while stunned";}
        assert(rooted&&acted);
        std::cout<<"PASS creature root: Entangling Roots holds the player (control 2) without refusing actions\n";
    }
    // ---------------------------------------------------------------------
    // 2.36 SmartAI v2: self buffs, areas, cones, chains, knockbacks, fears,
    // confuses, silences, interrupt lockouts, triggered spells, heals, HoTs,
    // ally buffs, damage-taken and healing-taken debuffs, guid scripts.
    // ---------------------------------------------------------------------
    const auto hitEvents=[](const LocalGameplay& g,uint64_t& seen,auto&& f){for(const auto& e:g.combatEvents())if(e.sequence>seen){seen=e.sequence;f(e);}};
    {
        // Shadowhide Brute (432): Enrage 8599 on itself at 30 % health
        // (HEALTH_PCT 0-30, NOT_REPEATABLE): MOD_MELEE_HASTE +30 % and
        // MOD_DAMAGE_PERCENT_DONE (physical) 10 % + 1.0 per level, 2 min.
        // CalcValue at level 20 (BaseLevel 1): 10 + 19 = 29 %.
        const auto enrage=spellOf(8599);
        assert(enrage.npcPositive&&enrage.npcTargetShape==1&&enrage.npcHaste.set&&enrage.npcHaste.low==30&&enrage.npcDamagePct.set&&enrage.npcDamagePct.low==10&&
               enrage.npcDamagePct.perLevel>0.99f&&enrage.npcDamagePct.perLevel<1.01f&&enrage.npcDamagePctSchool==1&&enrage.durationMs==120000&&!enrage.castTimeMs);
        const auto* brute=localNpcSpellProfile(432);assert(brute&&brute->event==kLocalSmartEventHealthPct&&brute->p2==30&&brute->once()&&brute->target==kLocalSmartTargetSelf);
        Arena a(432,20,enrage,1,100);
        unsigned elapsed=0;uint64_t seen=0;std::vector<unsigned> swings;
        while(elapsed<12000){a.step();elapsed+=250;hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.kind==LocalCombatEventKind::NpcMelee)swings.push_back(elapsed);});assert(a.g.npcs()[0].npcBuffs.empty());}
        assert(swings.size()>=4);for(size_t i=1;i<swings.size();++i)assert(swings[i]-swings[i-1]==2000);
        a.alter([](LocalRealmNpc& m){m.health=m.maxHealth/4;});
        unsigned applied=0;swings.clear();uint32_t plainHit=0;
        auto bare=a.p;bare.harmfulAuras.clear();const auto armor=localMeleeArmor(bare,*a.c);
        while(elapsed<32000){a.step();elapsed+=250;
            const auto& m=a.g.npcs()[0];
            if(!applied&&!m.npcBuffs.empty()){applied=elapsed;seen=UINT64_MAX;for(const auto& e:a.g.combatEvents())seen=std::min(seen,e.sequence);seen=a.g.combatEvents().empty()?0:a.g.combatEvents().back().sequence;}
            if(applied)hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.kind==LocalCombatEventKind::NpcMelee){swings.push_back(elapsed);if(e.outcome==LocalMeleeOutcome::Hit)plainHit=e.attempted;}});
        }
        const auto& m=a.g.npcs()[0];
        assert(applied&&applied<=12000+1000&&m.npcBuffs.size()==1&&m.npcBuffs[0].spellId==8599&&m.npcBuffs[0].hastePct==30&&m.npcBuffs[0].damagePct==29&&
               m.npcBuffs[0].durationMs==120000&&m.npcBuffs[0].remainingMs<=120000&&m.npcBuffs[0].casterGuid==m.guid);
        // 2 s x 100/130 = 1.54 s between swings (measured at the 250 ms tick),
        // and every plain hit is 100 x 1.29 = 129 before armor.
        assert(swings.size()>=8);for(size_t i=2;i<swings.size();++i)assert(swings[i]-swings[i-1]<=1750);
        assert(plainHit==localArmorReducedDamage(129,armor,20));
        // NOT_REPEATABLE: once the buff is gone the row never fires again.
        a.alter([](LocalRealmNpc& n){n.npcBuffs.clear();});
        for(unsigned i=0;i<40;++i){a.step();assert(a.g.npcs()[0].npcBuffs.empty());}
        std::cout<<"PASS Enrage self-buff at 30 % health: haste 30 % (swings every "<<swings[3]-swings[2]<<" ms), damage 29 % ("<<plainHit<<" per hit after armor), never re-applied\n";
    }
    {
        // Ironjaw Basilisk (1551): Crystal Flash 5106, a 2 s cast, 24-degree
        // cone (10 yd) MOD_STUN for 15 s broken by damage. The player in front
        // is stunned, a player behind the creature is not.
        const auto flash=spellOf(5106);
        assert(flash.npcTargetShape==4&&flash.npcConeDegrees==24&&flash.npcAreaRadius==10.f&&flash.npcPlayerControl==1&&flash.npcBreakOnDamage&&flash.castTimeMs==2000&&flash.durationMs==15000);
        Arena a(1551,20,flash,1,0,true);a.q.x=1;a.q.y=0;
        unsigned elapsed=0,stunnedAt=0;
        while(elapsed<60000&&!stunnedAt){a.q.x=1;a.q.y=0;a.step();elapsed+=250;if(localPlayerControl(a.p)&1u)stunnedAt=elapsed;assert(a.q.harmfulAuras.empty());}
        assert(stunnedAt);
        const auto view=a.p.harmfulAuras[0];assert(view.spellId==5106&&view.controlKind==1&&view.breakOnDamage&&view.durationMs==15000);
        unsigned held=0;while(localPlayerControl(a.p)&1u){a.q.x=1;a.q.y=0;a.step();held+=250;assert(a.q.harmfulAuras.empty());assert(held<20000);}
        assert(held>=14500&&held<=15250);
        std::cout<<"PASS cone stun: Crystal Flash stuns the player in front for "<<held<<" ms and never the player behind\n";
        // With a weapon the creature's next swing breaks the stun (Unit::DealDamage,
        // AURA_INTERRUPT_FLAG_TAKE_DAMAGE).
        // The creature's attack timer keeps running through its cast bar, so
        // the swing that breaks the stun may land in the very tick the stun
        // does (DoMeleeAttackIfReady right after the cast): follow the events.
        Arena b(1551,20,flash,1,50);
        elapsed=0;unsigned appliedAt=0,swungAt=0,brokenAt=0;uint64_t seen=0;
        while(elapsed<60000&&!brokenAt){b.step();elapsed+=250;
            hitEvents(b.g,seen,[&](const LocalCombatEvent& e){
                if(e.spell==5106&&e.kind==LocalCombatEventKind::SpellHit&&e.outcome==LocalMeleeOutcome::Hit&&!appliedAt)appliedAt=elapsed;
                if(appliedAt&&!swungAt&&e.kind==LocalCombatEventKind::NpcMelee&&!localMeleeAvoided(e.outcome))swungAt=elapsed;});
            if(swungAt&&!(localPlayerControl(b.p)&1u))brokenAt=elapsed;
            if(appliedAt&&!swungAt)assert(localPlayerControl(b.p)&1u);}
        assert(appliedAt&&swungAt&&brokenAt&&swungAt-appliedAt<=2250&&brokenAt-swungAt<=250);
        std::cout<<"PASS damage breaks the stun: applied at "<<appliedAt<<" ms, the swing at "<<swungAt<<" ms ends it\n";
    }
    {
        // Green Scalebane (744): Cleave 15496 on the next swing, 110 % weapon
        // damage chaining to a second enemy within 10 yd in front of the
        // creature (Spell::SearchChainTargets, melee), both at full amount.
        const auto cleave=spellOf(15496);
        assert(cleave.npcNextSwing&&cleave.npcWeaponPercent==110&&cleave.npcChainTargets==2&&cleave.npcChainMultiplier==1.f&&cleave.sourceDamageClass==2);
        Arena a(744,20,cleave,1,100,true);
        unsigned elapsed=0;uint64_t seen=0;std::map<unsigned,std::set<uint64_t>> hitsAt;std::map<uint64_t,uint32_t> plain;
        while(elapsed<40000){a.step();elapsed+=250;hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==15496&&e.kind==LocalCombatEventKind::SpellDamage){hitsAt[elapsed].insert(e.target);if(e.outcome==LocalMeleeOutcome::Hit)plain[e.target]=e.attempted;}});}
        unsigned both=0;for(const auto& [t,targets]:hitsAt)if(targets.size()==2)++both;
        auto bare=a.p;bare.harmfulAuras.clear();const auto armor=localMeleeArmor(bare,*a.c);
        assert(both>=2&&plain.count(a.p.guid)&&plain.count(a.q.guid)&&plain[a.p.guid]==localArmorReducedDamage(110,armor,20)&&plain[a.q.guid]==plain[a.p.guid]);
        // Behind the creature the second player is not a chain target.
        Arena b(744,20,cleave,1,100,true);b.q.x=1;b.q.y=0;
        elapsed=0;seen=0;std::set<uint64_t> targets;
        while(elapsed<40000){b.q.x=1;b.q.y=0;b.step();elapsed+=250;hitEvents(b.g,seen,[&](const LocalCombatEvent& e){if(e.spell==15496&&e.kind==LocalCombatEventKind::SpellDamage)targets.insert(e.target);});}
        assert(targets.size()==1&&targets.count(b.p.guid));
        std::cout<<"PASS Cleave chains to a second enemy in front ("<<both<<" double hits of "<<plain[a.p.guid]<<"), never to one behind\n";
    }
    {
        // Gordunni Brute (5232): Uppercut 10966, weapon damage +50 and
        // EffectKnockBack (MiscValue 10 -> 1.0 yd/s away, BasePoints 94-156
        // -> 9.4-15.6 yd/s up), a triggered cast (flag 2) every 8-15 s.
        const auto uppercut=spellOf(10966);
        assert(uppercut.npcKnockbackSpeedXY==1.f&&uppercut.npcKnockbackZ.set&&uppercut.npcKnockbackZ.low==94&&uppercut.npcKnockbackZ.high==156&&uppercut.npcWeaponEffect&&uppercut.npcWeaponBonus==50);
        Arena a(5232,20,uppercut,1,100);
        unsigned elapsed=0;
        while(elapsed<90000&&!a.p.knockbackSequence){a.step();elapsed+=250;}
        assert(a.p.knockbackSequence==1&&a.p.knockbackSpeedXY==1.f&&a.p.knockbackSpeedZ>=9.4f&&a.p.knockbackSpeedZ<=15.6f);
        // Away from the creature: it stands west of the player.
        assert(a.p.knockbackCos>0.99f&&std::fabs(a.p.knockbackSin)<0.05f);
        const auto first=a.p.knockbackSequence;
        while(elapsed<90000&&a.p.knockbackSequence==first){a.step();elapsed+=250;}
        assert(a.p.knockbackSequence==2);
        std::cout<<"PASS Uppercut knockback: sequence "<<a.p.knockbackSequence<<", "<<a.p.knockbackSpeedXY<<" yd/s away and "<<a.p.knockbackSpeedZ<<" yd/s up\n";
    }
    {
        // Scorpid Terror (4139): Terrify 7399, MOD_FEAR 4 s on a random enemy
        // (DIMINISHING_FEAR, DRTYPE_PLAYER): 4000/2000/1000 ms then immune;
        // a feared player's actions are refused.
        const auto terrify=spellOf(7399);assert(terrify.npcPlayerControl==3&&terrify.durationMs==4000&&!terrify.npcBreakOnDamage);
        // Terrify is a magic spell and may miss (a miss leaves the diminishing
        // level alone); the level rises only while the next application comes
        // within 15 s of the previous removal, so the expected durations are
        // derived from the observed application and removal times.
        Arena a(4139,20,terrify,1);
        unsigned elapsed=0,removedAt=0,level=0;bool wasFeared=false,refused=false;std::vector<uint32_t> durations,expected;uint64_t seen=0;unsigned immune=0;
        while(elapsed<150000&&immune<1){a.step();elapsed+=250;
            const bool feared=(localPlayerControl(a.p)&4u)!=0;
            if(feared&&!wasFeared){for(const auto& v:a.p.harmfulAuras)if(v.controlKind==3)durations.push_back(v.durationMs);
                if(removedAt&&elapsed-removedAt>15000)level=0;expected.push_back(4000u>>level);++level;}
            if(!feared&&wasFeared)removedAt=elapsed;
            if(feared&&!refused){a.p.globalCooldownMs=0;std::string why;assert(!a.g.execute(a.p,{LocalAction::CastSpell,a.n[0].guid,1},{&a.p},why));refused=why=="Can't do that while fleeing";assert(localPlayerMovementHeld(a.p));}
            wasFeared=feared;
            hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==7399&&e.outcome==LocalMeleeOutcome::Immune){++immune;if(removedAt&&elapsed-removedAt>15000)level=0;assert(level>=3);}});
        }
        std::cerr<<"fear durations:";for(auto d:durations)std::cerr<<" "<<d;std::cerr<<" immune="<<immune<<"\n";
        assert(refused&&durations.size()>=3&&durations==expected&&immune==1);
        assert(std::count(durations.begin(),durations.end(),2000u)&&std::count(durations.begin(),durations.end(),1000u));
        std::cout<<"PASS creature fear: Terrify lasts";for(auto d:durations)std::cout<<" "<<d;std::cout<<" ms under diminishing returns, then immune; a fleeing player's cast is refused\n";
    }
    {
        // Shoveltusk (23690): Head Butt 42320, MOD_CONFUSE 3 s (no mechanic,
        // no diminishing), AURA_NOT_PRESENT.
        const auto headButt=spellOf(42320);assert(headButt.npcPlayerControl==4&&headButt.durationMs==3000);
        Arena a(23690,20,headButt,1);
        unsigned elapsed=0;bool confused=false,refused=false;
        while(elapsed<40000&&!(confused&&refused)){a.step();elapsed+=250;
            if(localPlayerControl(a.p)&8u){confused=true;a.p.globalCooldownMs=0;std::string why;assert(!a.g.execute(a.p,{LocalAction::Attack,a.n[0].guid,0},{&a.p},why));refused=why=="Can't do that while confused";}}
        assert(confused&&refused);
        std::cout<<"PASS creature confuse: Head Butt (control 4) refuses the player's attack\n";
    }
    {
        // Daggerspine Screamer (2370): Deafening Screech 3589, MOD_SILENCE 8 s
        // around the creature (20 yd): a cast of a spell with
        // SPELL_PREVENTION_TYPE_SILENCE is refused, an ability is not.
        const auto screech=spellOf(3589);assert(screech.npcPlayerControl==5&&screech.npcTargetShape==2&&screech.npcAreaRadius==20.f&&screech.durationMs==8000);
        LocalSpellDefinition silenceable;silenceable.id=3;silenceable.name="Silenceable bolt";silenceable.damage=5;silenceable.range=100;silenceable.preventionType=kLocalPreventionSilence;silenceable.schoolMask=4;
        Arena a(2370,20,screech,1,0,false,{silenceable});
        unsigned elapsed=0;bool silenced=false;std::string refusedWhy,allowedWhy;bool allowed=false;
        while(elapsed<40000&&!silenced){a.step();elapsed+=250;
            if(localPlayerControl(a.p)&16u){silenced=true;
                a.p.globalCooldownMs=0;assert(!a.g.execute(a.p,{LocalAction::CastSpell,a.n[0].guid,3},{&a.p},refusedWhy));
                a.p.globalCooldownMs=0;allowed=a.g.execute(a.p,{LocalAction::CastSpell,a.n[0].guid,1},{&a.p},allowedWhy);}}
        assert(silenced&&refusedWhy=="Can't do that while silenced"&&allowed&&!localPlayerMovementHeld(a.p));
        std::cout<<"PASS creature silence: Deafening Screech refuses a silence-prevented spell, not an ability\n";
    }
    {
        // Boulderfist Crusher (17134): Pulverize 2676, 70-80 damage around the
        // creature plus Spell::EffectInterruptCast: the player's cast bar is
        // interrupted and its school locked for Pulverize's 2 s duration
        // (Unit::ProhibitSpellSchool -> SPELL_FAILED_NOT_READY).
        const auto pulverize=spellOf(2676);assert(pulverize.npcInterrupt&&pulverize.durationMs==2000&&pulverize.damage==70&&pulverize.npcTargetShape==2);
        LocalSpellDefinition slow;slow.id=4;slow.name="Long frost cast";slow.damage=1;slow.range=100;slow.castTimeMs=6000;slow.schoolMask=4;
        Arena a(17134,20,pulverize,1,0,false,{slow});
        unsigned elapsed=0,lockedAt=0,clearedAt=0;std::string why;bool refused=false,accepted=false;
        while(elapsed<90000&&!(lockedAt&&clearedAt&&refused&&accepted)){a.step();elapsed+=250;
            if(!a.p.schoolLockouts.empty()){
                if(!lockedAt){lockedAt=elapsed;assert(a.p.schoolLockouts.size()==1&&a.p.schoolLockouts[0].schoolMask==4&&a.p.schoolLockouts[0].remainingMs<=2000&&a.p.schoolLockouts[0].remainingMs>=1750&&!a.p.castingSpellId&&a.p.castStatus==LocalCastStatus::Interrupted);}
                a.p.globalCooldownMs=0;if(!a.g.execute(a.p,{LocalAction::CastSpell,a.n[0].guid,4},{&a.p},why))refused=refused||why=="Ability is not ready yet";
                assert(!a.p.castingSpellId);
            } else {
                if(lockedAt&&!clearedAt)clearedAt=elapsed;
                if(!a.p.castingSpellId){a.p.globalCooldownMs=0;if(a.g.execute(a.p,{LocalAction::CastSpell,a.n[0].guid,4},{&a.p},why)){if(lockedAt)accepted=true;}else std::cerr<<"cast refused: "<<why<<"\n";}
            }
        }
        assert(lockedAt&&clearedAt&&refused&&accepted&&clearedAt-lockedAt>=1750&&clearedAt-lockedAt<=2250);
        std::cout<<"PASS Pulverize interrupt: the frost cast is interrupted, frost locked for "<<clearedAt-lockedAt<<" ms (\"Ability is not ready yet\"), then castable again\n";
    }
    {
        // Verdan the Everliving (5775): Grasping Vines 8142 around itself:
        // 150-250 damage, MOD_ROOT 10 s and Spell::EffectTriggerSpell of
        // Knockdown 5164 (MOD_STUN 2 s) on the same targets.
        const auto vines=spellOf(8142),knockdown=spellOf(5164);
        assert(vines.npcTriggerSpellId==5164&&vines.npcPlayerControl==2&&vines.damage==150&&vines.durationMs==10000&&knockdown.npcPlayerControl==1&&knockdown.durationMs==2000);
        Arena a(5775,20,vines,1,0,false,{knockdown});
        auto bare=a.p;bare.harmfulAuras.clear();const auto armor=localMeleeArmor(bare,*a.c);
        unsigned elapsed=0;bool both=false,damaged=false,triggered=false;uint64_t seen=0;
        while(elapsed<40000&&!(both&&damaged&&triggered)){a.step();elapsed+=250;
            both=both||localPlayerControl(a.p)==3u;
            hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==8142&&e.kind==LocalCombatEventKind::SpellDamage&&e.attempted>=localArmorReducedDamage(150,armor,20))damaged=true;if(e.spell==5164&&e.kind==LocalCombatEventKind::SpellCast)triggered=true;});}
        if(!(both&&damaged&&triggered))std::cerr<<"vines: both="<<both<<" damaged="<<damaged<<" triggered="<<triggered<<" control="<<unsigned(localPlayerControl(a.p))<<" views="<<a.p.harmfulAuras.size()<<"\n";
        assert(both&&damaged&&triggered);
        std::cout<<"PASS Grasping Vines: area damage, root and the triggered Knockdown stun (control 3) land together\n";
    }
    {
        // Bael'dun Appraiser (2990): Lesser Heal 2052 on itself below 50 %
        // health (HEALTH_PCT 0-50, every 12 s), 2 s cast, 71-85 + 1.1 per
        // level on the clamped level (MaxLevel 9, BaseLevel 4: +5) = 76-90.
        const auto heal=spellOf(2052);assert(heal.npcPositive&&heal.npcTargetShape==1&&heal.npcHealAmount.set&&heal.npcHealAmount.low==71&&heal.npcHealAmount.high==85&&heal.castTimeMs==2000&&heal.maxLevel==9&&heal.baseLevel==4);
        Arena a(2990,20,heal,1);
        for(unsigned i=0;i<8;++i)a.step();
        assert(a.g.npcs()[0].npcManaReady&&a.g.npcs()[0].npcMana>0);
        a.alter([](LocalRealmNpc& m){m.health=m.maxHealth*2/5;});
        unsigned elapsed=0;uint32_t healed=0,effective=0;uint64_t seen=0;const uint32_t before=a.g.npcs()[0].health;
        while(elapsed<20000&&!healed){a.step();elapsed+=250;hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==2052&&e.kind==LocalCombatEventKind::DirectHeal){healed=e.attempted;effective=e.effective;}});}
        assert(healed>=76&&healed<=90&&effective==healed&&a.g.npcs()[0].health==before+healed&&elapsed>=2000);
        std::cout<<"PASS Lesser Heal at 40 % health: the creature heals itself for "<<healed<<" after a 2 s cast\n";
    }
    {
        // Blackwood Ursa (2170): Rejuvenation 1058 on the friendly unit missing
        // 200 health within 40 yd (FRIENDLY_HEALTH, action invoker): 14 per
        // 3 s for 15 s, scaled with the creature level (no level term).
        const auto rejuv=spellOf(1058);assert(rejuv.npcPositive&&rejuv.periodicHeal==14&&rejuv.periodicIntervalMs==3000&&rejuv.durationMs==15000&&rejuv.npcPeriodicHealAmount.scales);
        Arena a(2170,20,rejuv,1);
        for(unsigned i=0;i<4;++i)a.step();
        // A deficit of 250: the five ticks bring it under the 200 the event
        // asks for, so its 18-21 s repeat finds no hurt friend and the HoT
        // is not refreshed.
        a.alter([](LocalRealmNpc& m){m.health=m.maxHealth-250;});
        unsigned elapsed=0;std::vector<unsigned> ticks;uint32_t amount=0;uint64_t seen=0;
        while(elapsed<30000){a.step();elapsed+=250;hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==1058&&e.kind==LocalCombatEventKind::PeriodicHeal){ticks.push_back(elapsed);amount=e.attempted;}});}
        const auto scale=localNpcSpellDamageScale(2170,20,rejuv.npcScales,rejuv.spellLevel);
        std::cerr<<"rejuvenation ticks="<<ticks.size()<<" amount="<<amount<<" scale="<<scale<<" buffs="<<a.g.npcs()[0].npcBuffs.size()<<"\n";
        assert(ticks.size()==5&&amount==uint32_t(14.f*scale)&&amount>=14&&amount<=56);
        for(size_t i=1;i<ticks.size();++i)assert(ticks[i]-ticks[i-1]==3000);
        assert(a.g.npcs()[0].health==a.g.npcs()[0].maxHealth-250+5*amount&&a.g.npcs()[0].npcBuffs.empty());
        std::cout<<"PASS Rejuvenation on the hurt creature: 5 ticks of "<<amount<<" every 3 s, then the buff expires\n";
    }
    {
        // Hyldnir Overseer (29426): Battle Shout 30931 on friendly creatures
        // within 15 yd including itself: MOD_ATTACK_POWER 4 + 4.5 per level
        // (SpellLevel 1) = 89 at level 20, 20 s; melee damage +AP/14 x 2.
        const auto shout=spellOf(30931);assert(shout.npcPositive&&shout.npcTargetShape==5&&shout.npcAreaRadius==15.f&&shout.npcAttackPower.set&&shout.npcAttackPower.low==4&&shout.durationMs==20000);
        // The Overseer's second row (SPELLHIT -> 46182) must be usable too,
        // or the whole script stays off (smartRowsUsable).
        Arena a(29426,20,shout,2,100,false,{spellOf(46182)});
        unsigned elapsed=0;bool buffed=false;uint64_t seen=0;uint32_t plainHit=0;
        while(elapsed<20000){a.step();elapsed+=250;
            const auto& npcs=a.g.npcs();
            if(!buffed&&npcs.size()==2&&!npcs[0].npcBuffs.empty()&&!npcs[1].npcBuffs.empty()){buffed=true;seen=a.g.combatEvents().empty()?0:a.g.combatEvents().back().sequence;
                for(const auto& m:npcs)assert(m.npcBuffs.size()==1&&m.npcBuffs[0].spellId==30931&&m.npcBuffs[0].attackPower==89&&(m.npcBuffs[0].casterGuid==npcs[0].guid||m.npcBuffs[0].casterGuid==npcs[1].guid));}
            // Both creatures shout: the same spell from the other caster replaces
            // rather than stacks (Aura::CanStackWith, no periodic effect).
            if(buffed)for(const auto& m:npcs)assert(m.npcBuffs.size()<=1);
            if(buffed)hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.kind==LocalCombatEventKind::NpcMelee&&e.outcome==LocalMeleeOutcome::Hit&&e.source==a.n[0].guid)plainHit=e.attempted;});}
        auto bare=a.p;bare.harmfulAuras.clear();const auto armor=localMeleeArmor(bare,*a.c);
        assert(buffed&&plainHit==localArmorReducedDamage(100+89/7,armor,20));
        std::cout<<"PASS Battle Shout buffs both creatures (+89 attack power): hits "<<plainHit<<" after armor\n";
    }
    {
        // Moonrage Bloodhowler (1924): Blood Howl 3264 around the creature,
        // MOD_DAMAGE_TAKEN (physical) 5 + 0.3 per level = 8 at level 20 for
        // 15 s (AURA_NOT_PRESENT); the creature's swings deal 8 more.
        const auto howl=spellOf(3264);assert(howl.npcDamageTakenFlat.set&&howl.npcDamageTakenFlat.low==5&&howl.npcDamageTakenSchool==1&&howl.durationMs==15000&&howl.npcTargetShape==2);
        Arena a(1924,20,howl,1,100);
        unsigned elapsed=0;uint64_t seen=0;uint32_t withView=0,without=0;
        auto bare=a.p;bare.harmfulAuras.clear();const auto armor=localMeleeArmor(bare,*a.c);
        while(elapsed<30000&&!(withView&&without)){a.step();elapsed+=250;
            const bool debuffed=std::any_of(a.p.harmfulAuras.begin(),a.p.harmfulAuras.end(),[](const auto& v){return v.spellId==3264&&v.damageTakenFlat==8&&v.schoolMask==1;});
            hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.kind==LocalCombatEventKind::NpcMelee&&e.outcome==LocalMeleeOutcome::Hit){if(debuffed)withView=e.attempted;else without=e.attempted;}});}
        if(!(without==localArmorReducedDamage(100,armor,20)&&withView==localArmorReducedDamage(108,armor,20))){std::cerr<<"blood howl: without="<<without<<" with="<<withView<<" expected "<<localArmorReducedDamage(100,armor,20)<<"/"<<localArmorReducedDamage(108,armor,20)<<" views="<<a.p.harmfulAuras.size();for(const auto& v:a.p.harmfulAuras)std::cerr<<" ["<<v.spellId<<" flat="<<v.damageTakenFlat<<" school="<<unsigned(v.schoolMask)<<"]";std::cerr<<"\n";}
        assert(without==localArmorReducedDamage(100,armor,20)&&withView==localArmorReducedDamage(108,armor,20));
        std::cout<<"PASS Blood Howl: physical damage taken +8 ("<<without<<" -> "<<withView<<" per hit)\n";
    }
    {
        // Sergeant Malthus (814): Veil of Shadow 7068, MOD_HEALING_PCT -75 %
        // for 15 s: the player's own heal lands for a quarter.
        const auto veil=spellOf(7068);assert(veil.npcHealingPct.set&&veil.npcHealingPct.low==-75&&veil.durationMs==15000);
        LocalSpellDefinition mend;mend.id=5;mend.name="Self mend";mend.heal=400;mend.range=40;
        Arena a(814,20,veil,1,0,false,{mend});
        unsigned elapsed=0;bool veiled=false;uint32_t healed=0;uint64_t seen=0;LocalMeleeOutcome outcome=LocalMeleeOutcome::Hit;
        while(elapsed<40000&&!healed){a.step();elapsed+=250;
            if(!veiled)veiled=std::any_of(a.p.harmfulAuras.begin(),a.p.harmfulAuras.end(),[](const auto& v){return v.spellId==7068&&v.healingPct==-75;});
            if(veiled){a.p.health=a.p.maxHealth-10000;a.p.globalCooldownMs=0;std::string why;assert(a.g.execute(a.p,{LocalAction::CastSpell,a.p.guid,5},{&a.p},why));
                hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==5&&e.kind==LocalCombatEventKind::DirectHeal){healed=e.attempted;outcome=e.outcome;}});}}
        assert(veiled&&localPlayerHealingTaken(a.p,1000)==250&&healed==(outcome==LocalMeleeOutcome::Critical?150u:100u));
        std::cout<<"PASS Veil of Shadow: healing taken -75 % (a 400 heal lands for "<<healed<<")\n";
    }
    {
        // A spawn guid script: Darnassian Druid spawn 82291 (entry 16331) owns
        // its own rows (Faerie Fire 25602 on the victim, Healing Touch at a
        // friend's low health); the entry itself has no script.
        assert(localNpcSmartOwner(16331,82291,true)==-82291&&localNpcSmartOwner(16331,82291,false)==16331&&localNpcSmartOwner(16331,0,true)==16331);
        const auto rows=localNpcSmartRows(-82291);assert(rows.second-rows.first==2&&rows.first->spellId()==25602&&(rows.first+1)->spellId()==11431);
        assert(localNpcSpellProfiles(16331).second==localNpcSpellProfiles(16331).first);
        const auto fire=spellOf(25602);assert(fire.npcArmorAmount<0&&fire.durationMs==20000);
        Arena a(16331,20,fire,1,0,false,{spellOf(11431)},82291,true);
        unsigned elapsed=0;bool landed=false;
        while(elapsed<30000&&!landed){a.step();elapsed+=250;landed=std::any_of(a.p.harmfulAuras.begin(),a.p.harmfulAuras.end(),[](const auto& v){return v.spellId==25602&&v.armorModifier<0;});}
        assert(landed);
        // The same entry at another spawn id runs no script at all.
        Arena b(16331,20,fire,1,0,false,{spellOf(11431)},20);
        elapsed=0;while(elapsed<30000){b.step();elapsed+=250;assert(b.p.harmfulAuras.empty());}
        std::cout<<"PASS guid script: spawn 82291 casts Faerie Fire, spawn 20 of the same entry casts nothing\n";
    }

    // ------------------------------------------------------------- 2.37
    const auto buffOf=[](const LocalGameplay& g,uint32_t spellId)->const LocalNpcBuff*{for(const auto& b:g.npcs()[0].npcBuffs)if(b.spellId==spellId)return &b;return nullptr;};
    const auto castAt=[](Arena& a,LocalRealmPlayer& caster,uint32_t spell,uint64_t target){caster.globalCooldownMs=0;caster.cooldowns.clear();std::string why;const bool ok=a.g.execute(caster,{LocalAction::CastSpell,target,spell},a.party(),why);if(!ok)std::cerr<<"cast "<<spell<<" refused: "<<why<<"\n";return ok;};
    {
        // Razormane Quilboar (3111): Fire Shield 5280, SPELL_AURA_DAMAGE_SHIELD
        // 4 Fire on itself out of combat (UPDATE_OOC 3 s). Unit::DealMeleeDamage's
        // shield loop answers every swing that dealt damage with the amount,
        // after the creature's magic hit roll against the attacker; a spell
        // hit draws nothing.
        const auto shield=spellOf(5280);assert(shield.npcDamageShield.set&&shield.npcDamageShield.low==4&&shield.npcPositive&&shield.schoolMask==8);
        Arena a(3111,20,shield,1);
        unsigned elapsed=0;
        while(elapsed<20000&&!buffOf(a.g,5280)){a.step();elapsed+=250;}
        const auto* b=buffOf(a.g,5280);assert(b&&b->damageShield==4&&b->damageShieldSchool==8);
        uint64_t seen=0;unsigned landed=0,shielded=0,spellHits=0;
        for(const auto& e:a.g.combatEvents())seen=std::max(seen,e.sequence);
        while(elapsed<50000){a.p.attackTarget=a.n[0].guid;a.p.orientation=std::atan2(a.g.npcs()[0].y-a.p.y,a.g.npcs()[0].x-a.p.x);if(elapsed%2000==0)castAt(a,a.p,1,a.n[0].guid);a.step();elapsed+=250;
            hitEvents(a.g,seen,[&](const LocalCombatEvent& e){
                if(e.kind==LocalCombatEventKind::PlayerMelee&&e.effective)++landed;
                if(e.kind==LocalCombatEventKind::SpellDamage&&e.spell==1&&e.effective)++spellHits;
                if(e.kind==LocalCombatEventKind::SpellDamage&&e.spell==5280&&e.target==a.p.guid&&e.outcome==LocalMeleeOutcome::Hit){++shielded;assert(e.attempted==4&&e.schoolMask==8&&e.source==a.n[0].guid);}});}
        if(!(landed>=8&&spellHits>=5&&shielded>=landed*7/10&&shielded<=landed))std::cerr<<"fire shield: landed="<<landed<<" spellHits="<<spellHits<<" shielded="<<shielded<<" npc.x="<<a.g.npcs()[0].x<<" p.x="<<a.p.x<<" target="<<a.p.attackTarget<<" timer="<<a.p.attackTimer<<"\n";
        assert(landed>=8&&spellHits>=5&&shielded>=landed*7/10&&shielded<=landed);
        std::cout<<"PASS Fire Shield: "<<shielded<<" of "<<landed<<" landed swings draw 4 Fire damage shield hits; "<<spellHits<<" spell hits draw none\n";
    }
    {
        // Vile Tutor (8548): Shadow Shield 12040 (triggered self-cast in
        // combat): SCHOOL_ABSORB 200 + 10 per level of every school and a
        // 10 + 0.5 per level Shadow damage shield. The absorb pool takes the
        // player's hits first (Unit::CalcAbsorbResist: absorbed, nothing
        // effective), the buff ends with its last point, then damage lands.
        const auto shadow=spellOf(12040);assert(shadow.npcAbsorb.set&&shadow.npcAbsorb.low==200&&shadow.npcAbsorbSchool==127&&shadow.npcDamageShield.set);
        Arena a(8548,20,shadow,1);
        unsigned elapsed=0;
        while(elapsed<25000&&!buffOf(a.g,12040)){a.step();elapsed+=250;}
        const auto* b=buffOf(a.g,12040);assert(b);
        const uint32_t pool=b->absorbRemaining;assert(pool>=200&&b->damageShield>=10);
        const uint32_t healthBefore=a.g.npcs()[0].health;
        uint64_t seen=0;uint32_t absorbed=0,effective=0;
        for(const auto& e:a.g.combatEvents())seen=std::max(seen,e.sequence);
        while(elapsed<60000&&effective<50){castAt(a,a.p,1,a.n[0].guid);a.step();elapsed+=250;
            hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.kind==LocalCombatEventKind::SpellDamage&&e.spell==1){absorbed+=e.absorbed;effective+=e.effective;}});}
        assert(absorbed==pool&&effective>=50&&!buffOf(a.g,12040)&&a.g.npcs()[0].health==healthBefore-effective);
        std::cout<<"PASS Shadow Shield: "<<absorbed<<" absorbed by the pool, the buff ends with it, then "<<effective<<" land\n";
    }
    {
        // Shadowpine Witch (16341): Lightning Shield 12550 on itself -
        // PROC_TRIGGER_DAMAGE 2 + 2 per level Nature (40 at level 20), proc
        // flags 0x222a8 (every hit taken), 50 % chance, 3 charges. Each hit
        // the player lands rolls the proc; a proc deals the amount to the
        // attacker (HandleProcTriggerDamageAuraProc) and spends a charge; the
        // aura leaves with the third. (Thorns 8788 on the warrior-class
        // Whirlwind Ripper is never cast: 185 mana on a creature without mana
        // is SPELL_FAILED_NO_POWER at the pin.)
        const auto lightning=spellOf(12550);assert(lightning.npcProcDamage.set&&lightning.npcProcDamage.low==2&&lightning.npcProcFlags==0x222a8&&lightning.npcProcChance==50&&lightning.npcProcCharges==3&&lightning.schoolMask==8);
        Arena a(16341,20,lightning,1);
        unsigned elapsed=0;
        while(elapsed<40000&&!buffOf(a.g,12550)){a.step();elapsed+=250;}
        const auto* b=buffOf(a.g,12550);assert(b&&b->procDamage==40&&b->procCharges==3&&b->procSchool==8);
        uint64_t seen=0;unsigned procs=0,hits=0;
        for(const auto& e:a.g.combatEvents())seen=std::max(seen,e.sequence);
        while(elapsed<120000&&buffOf(a.g,12550)){castAt(a,a.p,1,a.n[0].guid);a.step();elapsed+=250;
            hitEvents(a.g,seen,[&](const LocalCombatEvent& e){
                if(e.kind==LocalCombatEventKind::SpellDamage&&e.spell==1&&!localMeleeAvoided(e.outcome))++hits;
                if(e.kind==LocalCombatEventKind::SpellDamage&&e.spell==12550&&e.target==a.p.guid){++procs;assert(e.attempted==40&&e.schoolMask==8);}});}
        assert(procs==3&&hits>=3&&!buffOf(a.g,12550));
        std::cout<<"PASS Lightning Shield: 3 proc charges deal 40 Nature to the attacker over "<<hits<<" hits, then the aura leaves\n";
    }
    {
        // Burning Blade Fanatic (3197): 5262 on itself - PROC_TRIGGER_SPELL
        // 5263 (3 Fire damage) on its own melee hits (proc flags 0x14, 100 %).
        // Every landed swing casts the trigger at the victim
        // (HandleProcTriggerSpellAuraProc: the aura owner at the proc target).
        const auto fumble=spellOf(5262),spark=spellOf(5263);
        assert(fumble.npcProcSpellId==5263&&fumble.npcProcFlags==0x14&&fumble.npcProcChance==100&&!fumble.npcProcCharges&&spark.damage==3);
        Arena a(3197,20,fumble,1,50,false,{spark});
        unsigned elapsed=0;
        while(elapsed<15000&&!buffOf(a.g,5262)){a.step();elapsed+=250;}
        assert(buffOf(a.g,5262)&&buffOf(a.g,5262)->procSpellId==5263);
        uint64_t seen=0;unsigned swings=0,sparks=0;
        for(const auto& e:a.g.combatEvents())seen=std::max(seen,e.sequence);
        // The aura lasts 10 s of each 16-22 s cycle: only the swings under it count.
        while(elapsed<60000){const bool under=buffOf(a.g,5262)!=nullptr;a.step();elapsed+=250;
            hitEvents(a.g,seen,[&](const LocalCombatEvent& e){
                if(e.kind==LocalCombatEventKind::NpcMelee&&!localMeleeAvoided(e.outcome)&&under)++swings;
                if(e.kind==LocalCombatEventKind::SpellDamage&&e.spell==5263&&e.target==a.p.guid&&e.outcome==LocalMeleeOutcome::Hit){++sparks;assert(under);}});}
        assert(swings>=6&&sparks>=swings*8/10&&sparks<=swings);
        std::cout<<"PASS proc trigger: "<<sparks<<" of "<<swings<<" landed swings cast the 3 Fire trigger at the victim\n";
    }
    {
        // Bloodscalp Warrior (587): Disarm 6713 (a melee special, 5 s) -
        // MOD_DISARM on the player: Player::CanUseAttackType is false, so the
        // main hand's weapon and parry are gone until it ends.
        const auto disarm=spellOf(6713);assert(disarm.npcDisarm&&disarm.durationMs==5000&&disarm.sourceDamageClass==2);
        // A next-swing special: the creature swings with a weapon.
        Arena a(587,20,disarm,1,50,false,{spellOf(11972),spellOf(8599)});
        unsigned elapsed=0;
        while(elapsed<60000&&!localPlayerDisarmed(a.p)){a.step();elapsed+=250;}
        assert(localPlayerDisarmed(a.p)&&localPlayerViewModifiers(a.p,1).disarmed);
        const auto view=std::find_if(a.p.harmfulAuras.begin(),a.p.harmfulAuras.end(),[](const auto& v){return v.spellId==6713;});
        assert(view!=a.p.harmfulAuras.end()&&view->disarmed&&view->durationMs==5000&&localMeleeStats(a.p,*a.c).parry==0&&localWeaponAmounts(a.p,*a.c).seconds==2.f);
        unsigned held=0;while(localPlayerDisarmed(a.p)&&held<20000){a.step();held+=250;}
        assert(held>=4500&&held<=5250);
        std::cout<<"PASS Disarm: the player is disarmed for "<<held<<" ms (no parry, the unarmed 2 s swing)\n";
    }
    {
        // Bleak Worg (3861): Wavering Will 7127 on the victim - MOD_MELEE_HASTE
        // -25, MOD_DECREASE_SPEED -20 and MOD_CASTING_SPEED_NOT_STACK -20 (the
        // base points plus the one-sided die) for 60 s: a 2 s cast takes
        // 2400 ms (ApplyCastTimePercentMod).
        const auto will=spellOf(7127);assert(will.npcCastSpeed.set&&will.npcCastSpeed.low==-20&&will.npcHaste.set&&will.npcSlowPercent==20&&will.durationMs==60000);
        Arena a(3861,20,will,1);
        unsigned elapsed=0;
        while(elapsed<40000&&a.p.harmfulAuras.empty()){a.step();elapsed+=250;}
        const auto view=std::find_if(a.p.harmfulAuras.begin(),a.p.harmfulAuras.end(),[](const auto& v){return v.spellId==7127;});
        assert(view!=a.p.harmfulAuras.end()&&view->castSpeedPct==-20&&view->hastePct==-25&&view->slowPercent==20);
        assert(localPlayerViewModifiers(a.p,0).castSpeedPct==-20&&localPlayerCastTimeModified(a.p,2000)==2400);
        std::cout<<"PASS Wavering Will: cast speed -20 % (a 2 s cast takes "<<localPlayerCastTimeModified(a.p,2000)<<" ms), haste -25 %, slow 20 %\n";
    }
    {
        // Mudsnout Gnoll (2372): Sling Dirt 3650 in melee range (RANGE 0-5 yd)
        // - MOD_HIT_CHANCE -50 on the player for 15 s: the character's melee
        // hit chance drops by 50 (Player::UpdateMeleeHitChances).
        const auto dirt=spellOf(3650);assert(dirt.npcHitChance.set&&dirt.npcHitChance.low==-50&&dirt.durationMs==15000);
        Arena a(2372,20,dirt,1);
        unsigned elapsed=0;
        while(elapsed<60000&&a.p.harmfulAuras.empty()){a.step();elapsed+=250;}
        const auto view=std::find_if(a.p.harmfulAuras.begin(),a.p.harmfulAuras.end(),[](const auto& v){return v.spellId==3650;});
        assert(view!=a.p.harmfulAuras.end()&&view->hitChancePct==-50&&localPlayerViewModifiers(a.p,1).hitChancePct==-50);
        auto warrior=a.p;warrior.classId=1;
        const auto with=localMeleeStats(warrior,*a.c);warrior.harmfulAuras.clear();const auto without=localMeleeStats(warrior,*a.c);
        assert(with.hit==without.hit-50.f);
        std::cout<<"PASS Sling Dirt: melee hit chance "<<without.hit<<" -> "<<with.hit<<"\n";
    }
    {
        // Encrusted Surf Crawler (3108): 5426 on itself - MOD_DODGE_PERCENT
        // +50 for 6 s: GetUnitDodgeChance adds it, so about half of the
        // player's swings are dodged instead of one in twenty.
        const auto crawl=spellOf(5426);assert(crawl.npcDodge.set&&crawl.npcDodge.low==50&&crawl.npcPositive&&crawl.durationMs==6000);
        Arena a(3108,20,crawl,1);
        unsigned elapsed=0;
        while(elapsed<60000&&!buffOf(a.g,5426)){a.step();elapsed+=250;}
        assert(buffOf(a.g,5426)&&buffOf(a.g,5426)->dodgePct==50);
        // The buff lasts 6 s per 21-57 s cycle: the swings under it (one per
        // tick with the timer forced) are counted across several cycles.
        uint64_t seen=0;unsigned swings=0,dodged=0;
        for(const auto& e:a.g.combatEvents())seen=std::max(seen,e.sequence);
        while(swings<150&&elapsed<900000){const bool under=buffOf(a.g,5426)!=nullptr;a.p.attackTarget=under?a.n[0].guid:0;a.p.attackTimer=0;a.p.orientation=std::atan2(a.g.npcs()[0].y-a.p.y,a.g.npcs()[0].x-a.p.x);a.step();elapsed+=250;
            hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.kind==LocalCombatEventKind::PlayerMelee&&under){++swings;if(e.outcome==LocalMeleeOutcome::Dodge)++dodged;}});}
        assert(swings>=150&&dodged>=swings*42/100&&dodged<=swings*68/100);
        std::cout<<"PASS dodge buff: "<<dodged<<" of "<<swings<<" swings dodged with +50 % dodge\n";
    }
    {
        // Bristleback Battleboar (2954): 3385 at AGGRO (50 %) - MOD_INCREASE_SPEED
        // +60 and physical damage +2.5 per level for 4 s: the creature runs 1.6
        // times as fast (Unit::UpdateSpeed's largest bonus), 6.4 yd per second here.
        const auto charge=spellOf(3385);assert(charge.npcSpeed.set&&charge.npcSpeed.low==60&&charge.npcDamageFlat.set&&charge.durationMs==4000);
        float best=0;bool buffed=false;
        for(unsigned seed=0;seed<8&&!buffed;++seed) {
            Arena a(2954,20,charge,1);a.g.seedGameObjectRandom(100+seed);
            for(unsigned k=0;k<12;++k){const float before=a.g.npcs()[0].x;a.p.x=40;a.step();best=std::max(best,a.g.npcs()[0].x-before);if(buffOf(a.g,3385))buffed=true;}
        }
        assert(buffed&&best>1.5f&&best<1.7f);
        std::cout<<"PASS speed buff: the buffed creature closes "<<best<<" yd per 250 ms tick (1.6 x 4 yd/s)\n";
    }
    {
        // Nal'taszar (4066): Mana Burn 8211 on the victim at 20-40 % of its own
        // health - Spell::EffectPowerBurn: 225-241 (+1.5 per level) mana taken,
        // half of it dealt as Shadow damage.
        const auto burn=spellOf(8211);assert(burn.npcPowerBurn.set&&burn.npcPowerBurn.low==225&&burn.npcPowerBurnMultiplier==0.5f&&burn.schoolMask==32);
        Arena a(4066,20,burn,1,0,false,{spellOf(15305)});
        // A mage (the fixture's character is a warrior without mana): its pool
        // is 811 at level 20 once the authority recomputes it; 800 is set for
        // every step so the burn always finds mana.
        a.p.classId=8;a.p.race=1;a.p.resourceType=LocalResourceType::Mana;
        a.alter([](LocalRealmNpc& m){m.health=m.maxHealth*3/10;});
        unsigned elapsed=0;uint64_t seen=0;uint32_t damage=0,after=0;
        while(elapsed<60000&&!damage){a.p.mana=a.p.maxMana=800;a.step();elapsed+=250;
            hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.kind==LocalCombatEventKind::SpellDamage&&e.spell==8211&&e.target==a.p.guid&&e.attempted){damage=e.attempted;after=a.p.mana;}});}
        assert(damage);
        const uint32_t burned=800-after;
        // The tick's own regeneration may give a few points back in the same step.
        assert(damage*2>=200&&damage*2<=300&&burned<=damage*2+1&&burned+40>=damage*2);
        std::cout<<"PASS Mana Burn: "<<burned<<" mana burned, "<<damage<<" Shadow damage\n";
    }
    {
        // Malor the Zealous (11032): 10310 on itself at 10 % health -
        // SPELL_EFFECT_HEAL_MAX_HEALTH (its full maximum) and ENERGIZE 550 mana.
        const auto lay=spellOf(10310);assert(lay.npcHealMax&&lay.npcEnergize.set&&lay.npcEnergize.low==550&&lay.npcPositive);
        Arena a(11032,20,lay,1,0,false,{spellOf(12734),spellOf(16172),spellOf(15493)});
        a.step();
        a.alter([](LocalRealmNpc& m){m.health=m.maxHealth/20;m.npcMana=0;});
        unsigned elapsed=0;uint64_t seen=0;uint32_t healed=0;
        while(elapsed<20000&&!healed){a.step();elapsed+=250;
            hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.kind==LocalCombatEventKind::DirectHeal&&e.spell==10310)healed=e.attempted;});}
        const auto& m=a.g.npcs()[0];
        assert(healed==m.maxHealth&&m.health==m.maxHealth&&m.npcMana>=550);
        std::cout<<"PASS heal max health + energize: "<<healed<<" healed, mana "<<m.npcMana<<"\n";
    }
    {
        // Nagaz (2320): 18797 on itself every 6.5-7 s - SPELL_EFFECT_ADD_EXTRA_ATTACKS
        // 1: the next swing brings a second one at once (AttackerStateUpdate's
        // extra attacks), so two swings land in the same tick.
        const auto extra=spellOf(18797);assert(extra.npcExtraAttacks==1&&extra.npcPositive);
        Arena a(2320,20,extra,1,50);
        unsigned elapsed=0;uint64_t seen=0;unsigned doubles=0;std::map<unsigned,unsigned> swingsAt;
        while(elapsed<40000){a.step();elapsed+=250;
            hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.kind==LocalCombatEventKind::NpcMelee)++swingsAt[elapsed];});}
        for(const auto& [t,count]:swingsAt)if(count==2)++doubles;
        assert(doubles>=3&&doubles<=8);
        std::cout<<"PASS extra attacks: "<<doubles<<" double swings in 40 s\n";
    }
    {
        // Defias Tower Patroller (7052): SPELLHIT by a player's spell 53 (and
        // twenty others) -> SMART_ACTION_DIE: Unit::Kill(me, me) with the loot
        // recipient credited, the DEATH rows, loot and respawn as for any kill.
        // 2.39: the tagger is credited only once players dealt half the
        // health (Creature::IsDamageEnoughForLootingAndReward), so the
        // patroller stands at 12 health for the 10-damage Backstab (7 after
        // its armor).
        LocalSpellDefinition backstab;backstab.id=53;backstab.name="Backstab";backstab.damage=10;backstab.range=100;
        Arena a(7052,20,spellOf(5679),1,0,false,{backstab});
        a.alter([](LocalRealmNpc& m){m.maxHealth=m.health=12;});
        a.step();assert(!a.g.npcs()[0].dead);
        assert(castAt(a,a.p,53,a.n[0].guid));a.step();
        const auto& m=a.g.npcs()[0];
        assert(m.dead&&m.lootable&&m.lootOwner==a.p.guid);
        const auto events=a.g.combatEvents();
        assert(std::any_of(events.begin(),events.end(),[&](const auto& e){return e.kind==LocalCombatEventKind::Death&&e.source==a.n[0].guid;}));
        std::cout<<"PASS SMART_ACTION_DIE: the creature dies to the script on a Backstab hit, credited to its tagger\n";
    }
    {
        // Shatterhorn (24178): FORCE_DESPAWN 8 s after its death - the corpse
        // stays for 8 s, then RemoveCorpse hides it (CORPSE_REMOVED rows) and
        // the respawn timer starts.
        LocalSpellDefinition punch;punch.id=43209;punch.name="Punch";punch.damage=10;punch.range=100;
        Arena a(24178,20,spellOf(12734),1,0,false,{punch});
        a.alter([](LocalRealmNpc& m){m.health=5;});
        assert(castAt(a,a.p,1,a.n[0].guid));a.step();
        assert(a.g.npcs()[0].dead&&!a.g.npcs()[0].npcDespawned&&a.g.npcVisibleTo(a.p,a.g.npcs()[0]));
        unsigned elapsed=0;while(elapsed<20000&&!a.g.npcs()[0].npcDespawned){a.step();elapsed+=250;}
        assert(a.g.npcs()[0].npcDespawned&&elapsed>=7750&&elapsed<=8500&&!a.g.npcVisibleTo(a.p,a.g.npcs()[0])&&!a.g.npcs()[0].lootable);
        std::cout<<"PASS FORCE_DESPAWN: the corpse vanishes "<<elapsed<<" ms after the death\n";
    }
    {
        // Protean Horror (20865): SPELLHIT 36327 (50 %) -> KILL_UNIT on itself.
        LocalSpellDefinition bolt;bolt.id=36327;bolt.name="Bolt";bolt.damage=10;bolt.range=100;
        Arena a(20865,20,spellOf(36612),1,0,false,{bolt});
        unsigned casts=0;while(casts<40&&!a.g.npcs()[0].dead){castAt(a,a.p,36327,a.n[0].guid);a.step();++casts;}
        assert(a.g.npcs()[0].dead&&casts>=1&&casts<40);
        std::cout<<"PASS KILL_UNIT: the creature killed itself after "<<casts<<" hits (50 % per hit)\n";
    }
    {
        // Defias Renegade Mage (450): FLEE at 15 % health (with the emote):
        // MoveFleeing - the creature runs from its victim for 7 s.
        Arena a(450,20,spellOf(20793),1,50,false,{spellOf(4979),spellOf(134),spellOf(3052)});
        unsigned elapsed=0;while(elapsed<3000){a.step();elapsed+=250;}
        const auto dist=[&](){const auto& m=a.g.npcs()[0];return std::sqrt((a.p.x-m.x)*(a.p.x-m.x)+(a.p.y-m.y)*(a.p.y-m.y));};
        const float before=dist();
        a.alter([](LocalRealmNpc& m){m.health=m.maxHealth/10;});
        for(unsigned k=0;k<8;++k)a.step();
        const auto& m=a.g.npcs()[0];
        const float after=dist();
        assert(m.fleeMode==2&&after>before+3.f);
        std::cout<<"PASS FLEE: the creature ran from "<<before<<" to "<<after<<" yd away\n";
    }
    {
        // Hyldnir Overseer spawn 118748 (entry 29426): REACT_DEFENSIVE at RESET -
        // MoveInLineOfSight starts no attack; the first hit engages it.
        Arena a(29426,20,spellOf(30931),1,50,false,{spellOf(46182)},118748,true);
        a.alter([](LocalRealmNpc& m){m.targetGuid=0;m.threat={};});
        unsigned elapsed=0;while(elapsed<5000){a.p.x=8;a.step();elapsed+=250;assert(!a.g.npcs()[0].targetGuid);}
        assert(a.g.npcs()[0].npcReactState==1);
        assert(castAt(a,a.p,1,a.n[0].guid));a.step();
        assert(a.g.npcs()[0].targetGuid==a.p.guid);
        std::cout<<"PASS REACT_DEFENSIVE: no attack on sight for 5 s, engaged by the first hit\n";
    }
    {
        // Felguard Annihilator (17400): THREAT_ALL_PCT -100 every 12 s -
        // ModifyThreatByPercent(-100) on every entry of its threat list.
        Arena a(17400,20,spellOf(18072),1,0,true,{spellOf(15615)});
        a.alter([&](LocalRealmNpc& m){m.threat[1]={a.q.guid,500000};});
        unsigned elapsed=0;bool wiped=false;
        while(elapsed<15000&&!wiped){a.step();elapsed+=250;const auto& m=a.g.npcs()[0];wiped=m.threat[0].guid&&!m.threat[0].amount&&m.threat[1].guid&&!m.threat[1].amount;}
        assert(wiped&&elapsed>=11750&&elapsed<=12500);
        std::cout<<"PASS THREAT_ALL_PCT: the whole threat list wiped at "<<elapsed<<" ms\n";
    }
    {
        // Hamhock (1717): FRIENDLY_MISSING_BUFF Bloodlust 6742 within 30 yd
        // every 10 s in combat: a friendly creature in combat without the aura
        // is the invoker, the row casts Bloodlust at it. Two Hamhocks buff
        // each other once their own 5 s self-casts have run out.
        const auto lust=spellOf(6742);assert(lust.npcHaste.set&&lust.durationMs==30000);
        Arena a(1717,20,lust,2,0,true,{spellOf(421)});
        unsigned elapsed=0;
        while(elapsed<8000){a.step();elapsed+=250;}
        for(const auto& m:a.g.npcs())assert(std::any_of(m.npcBuffs.begin(),m.npcBuffs.end(),[&](const auto& b){return b.spellId==6742&&b.casterGuid==m.guid;}));
        // The second Hamhock loses its Bloodlust (its own once-only self-cast
        // row is spent): the next FRIENDLY_MISSING_BUFF row to fire - either
        // Hamhock's, the buffless one being the invoker either way - casts
        // Bloodlust at it within the 10 s repeat.
        a.alter([&](LocalRealmNpc& m){if(m.guid==a.n[1].guid)m.npcBuffs.clear();});
        uint64_t caster=0;
        while(elapsed<30000&&!caster){a.step();elapsed+=250;
            for(const auto& b:a.g.npcs()[1].npcBuffs)if(b.spellId==6742)caster=b.casterGuid;}
        assert(caster&&elapsed<=20250);
        std::cout<<"PASS FRIENDLY_MISSING_BUFF: the second Hamhock was re-buffed at "<<elapsed<<" ms by "<<(caster==a.n[0].guid?"the first":"itself")<<"\n";
    }
    {
        // Bone Construct (14605): IS_BEHIND_TARGET (3-10 s, within 5 yd) ->
        // Exploit Weakness 8355 at the victim: only while the creature stands
        // outside the victim's front half-circle.
        const auto exploit=spellOf(8355);assert(exploit.npcNextSwing);
        Arena a(14605,20,exploit,1,50);
        unsigned elapsed=0;uint64_t seen=0;unsigned front=0;
        // Facing the creature: never.
        while(elapsed<15000){a.p.orientation=std::atan2(a.g.npcs()[0].y-a.p.y,a.g.npcs()[0].x-a.p.x);a.step();elapsed+=250;hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==8355&&e.kind==LocalCombatEventKind::SpellCast)++front;});}
        assert(front==0);
        unsigned behind=0;
        while(elapsed<40000){a.p.orientation=std::atan2(a.g.npcs()[0].y-a.p.y,a.g.npcs()[0].x-a.p.x)+3.14159265f;a.step();elapsed+=250;hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==8355&&e.kind==LocalCombatEventKind::SpellCast)++behind;});}
        assert(behind>=2);
        std::cout<<"PASS IS_BEHIND_TARGET: "<<behind<<" Exploit Weakness casts from behind, none from the front\n";
    }
    {
        // Skeletal Horror (202): NEAR_PLAYERS (at least 2 within 5 yd, first
        // 5 s, then 9-13 s) -> Terrify 7399 at the victim. One player alone
        // never triggers it; a second one within 5 yd does.
        const auto terrify=spellOf(7399);
        Arena a(202,20,terrify,1,0,true);a.q.x=30;a.q.y=30;
        unsigned elapsed=0;uint64_t seen=0;unsigned alone=0,together=0;
        while(elapsed<20000){a.q.x=30;a.q.y=30;a.step();elapsed+=250;hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==7399&&e.kind==LocalCombatEventKind::SpellCast)++alone;});}
        assert(alone==0);
        while(elapsed<50000){a.q.x=a.g.npcs()[0].x+1;a.q.y=a.g.npcs()[0].y+1;a.step();elapsed+=250;hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==7399&&e.kind==LocalCombatEventKind::SpellCast)++together;});}
        assert(together>=1);
        std::cout<<"PASS NEAR_PLAYERS: Terrify only once two players stand within 5 yd ("<<together<<" casts)\n";
    }
    {
        // Spectral Charger (15547): Charge 29320 at the FARTHEST player (at the
        // pin: the closest of the threat list within 40 yd), then
        // SPELLHIT_TARGET 29320 -> 29321 (a fear around itself).
        const auto charge=spellOf(29320),fear=spellOf(29321);assert(charge.npcCharge&&fear.npcPlayerControl==3);
        Arena a(15547,20,charge,1,0,true,{fear});a.q.x=25;a.q.y=0;
        a.alter([&](LocalRealmNpc& m){m.threat[1]={a.q.guid,1};});
        unsigned elapsed=0;uint64_t seen=0;std::set<uint64_t> charged;unsigned fears=0;
        while(elapsed<30000){a.q.x=25;a.q.y=0;a.step();elapsed+=250;hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==29320&&e.kind==LocalCombatEventKind::SpellCast)charged.insert(e.target);if(e.spell==29321&&e.kind==LocalCombatEventKind::SpellCast)++fears;});}
        assert(charged.size()==1&&charged.count(a.p.guid)&&fears>=1);
        std::cout<<"PASS FARTHEST + SPELLHIT_TARGET: the charge went to the closest listed player, then the fear followed ("<<fears<<")\n";
    }
    {
        // Shadowfang Moonwalker (3853): Anti-Magic Shield 7121 on itself -
        // SCHOOL_IMMUNITY of every magic school: a Fire spell is IMMUNE, a
        // physical one lands.
        const auto ams=spellOf(7121);assert(ams.npcSchoolImmunity==126&&ams.npcPositive);
        LocalSpellDefinition fire;fire.id=2;fire.name="Fire bolt";fire.damage=10;fire.range=100;fire.schoolMask=4;
        Arena a(3853,20,ams,1,0,false,{fire});
        unsigned elapsed=0;while(elapsed<30000&&!buffOf(a.g,7121)){a.step();elapsed+=250;}
        assert(buffOf(a.g,7121)&&buffOf(a.g,7121)->schoolImmunity==126);
        uint64_t seen=0;for(const auto& e:a.g.combatEvents())seen=std::max(seen,e.sequence);
        castAt(a,a.p,2,a.n[0].guid);castAt(a,a.p,1,a.n[0].guid);a.step();
        bool immune=false,landed=false;
        hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==2&&e.outcome==LocalMeleeOutcome::Immune)immune=true;if(e.spell==1&&e.kind==LocalCombatEventKind::SpellDamage&&e.effective)landed=true;});
        assert(immune&&landed);
        std::cout<<"PASS SCHOOL_IMMUNITY: the Fire bolt is immune, the physical hit lands\n";
    }
    {
        // Elder Ashenvale Bear (3810): 4148 on itself at 10 % health -
        // MOD_INCREASE_HEALTH 251 + 12.5 per level: max health and health rise
        // by the amount (HandleAuraModIncreaseHealth).
        const auto grow=spellOf(4148);assert(grow.npcMaxHealth.set&&grow.npcMaxHealth.low==252);
        Arena a(3810,20,grow,1);
        a.alter([](LocalRealmNpc& m){m.health=m.maxHealth/20;});
        const uint32_t maxBefore=a.g.npcs()[0].maxHealth,before=a.g.npcs()[0].health;
        unsigned elapsed=0;while(elapsed<20000&&!buffOf(a.g,4148)){a.step();elapsed+=250;}
        const auto* b=buffOf(a.g,4148);assert(b&&b->maxHealth>=252);
        const auto& m=a.g.npcs()[0];
        assert(m.maxHealth==maxBefore+uint32_t(b->maxHealth)&&m.health==before+uint32_t(b->maxHealth));
        std::cout<<"PASS MOD_INCREASE_HEALTH: max health "<<maxBefore<<" -> "<<m.maxHealth<<"\n";
    }
    {
        // Captain Flat Tusk (5824): ADD_AURA Battle Stance 7165 on itself at
        // RESET (once): Unit::AddAura without a cast - the buff is there from
        // the first tick.
        Arena a(5824,20,spellOf(7165),1,0,false,{spellOf(25710)});
        a.step();
        assert(buffOf(a.g,7165)&&buffOf(a.g,7165)->indefinite);
        std::cout<<"PASS ADD_AURA: Battle Stance present from the reset\n";
    }

    // ------------------------------------------------------------------ 2.38
    // Summons, movement, escort paths, ground auras and the text timer.
    std::cout<<"-- 2.38: summons, movement, escort paths, ground auras, text timers --\n";
    const auto textGroup=[](uint32_t entry,uint8_t group,const std::string& text,uint8_t type=kLocalChatMonsterSay){LocalCreatureTextGroup g;g.entry=entry;g.group=group;g.lines.push_back({text,type,100.f});return g;};
    const auto bagCount=[](const LocalRealmPlayer& p,uint32_t item){uint32_t n=0;for(const auto& s:p.inventory)if(s.itemId==item)n+=s.count;return n;};
    const auto liveSummons=[](const LocalGameplay& g,uint32_t entry){unsigned c=0;for(const auto& m:g.npcs())if(m.entry==entry&&m.npcSummonType&&!m.npcUnsummoned)++c;return c;};
    const auto npcOf=[](const LocalGameplay& g,uint64_t guid)->const LocalRealmNpc*{for(const auto& m:g.npcs())if(m.guid==guid)return &m;return nullptr;};
    // A scene with explicit positions: definitions, spells, text groups, the
    // map, the player and the spawns (spawn id, entry, x, y, z, o).
    struct Stage {
        std::shared_ptr<LocalWorldContent> c;LocalGameplay g;LocalRealmPlayer p;std::vector<LocalRealmNpc> n;
        // 2.39: the spawn's default movement (creature.MovementType 1 random
        // within `wander`, 2 the waypoint_data path `pathId`).
        struct Spawn { uint32_t id,entry;float x,y,z,o;uint8_t movementType=0;float wander=0;uint32_t pathId=0; };
        // 2.40: the gossip tables of the scene (menus, texts, the owners' menu and npcflag).
        struct Gossip { std::vector<LocalGossipMenu> menus; std::vector<LocalGossipText> texts; std::vector<LocalGossipOwner> owners; std::vector<LocalItemDefinition> items; };
        Stage(std::vector<LocalNpcDefinition> defs,std::vector<LocalSpellDefinition> extra,std::vector<LocalCreatureTextGroup> texts,uint32_t mapId,
              float px,float py,float pz,std::vector<Spawn> spawns,std::vector<uint32_t> guidScripts={},
              std::map<uint32_t,std::vector<LocalWaypointNode>> paths={},Gossip gossip={}) {
            c=rewardContent();
            c->waypointPaths=std::move(paths);
            for(auto& m:gossip.menus)c->gossipMenus[m.id]=m;
            for(auto& x:gossip.texts)c->gossipTexts[x.id]=x;
            for(auto& o:gossip.owners)c->gossipOwners[o.entry]=o;
            for(auto& i:gossip.items)c->items.push_back(i);
            std::sort(c->items.begin(),c->items.end(),[](const auto& a,const auto& b){return a.id<b.id;});
            for(const auto& d:defs)c->npcs.push_back(d);
            std::sort(c->npcs.begin(),c->npcs.end(),[](const auto& a,const auto& b){return a.id<b.id;});
            for(const auto& e:extra)c->spells.push_back(e);
            std::sort(c->spells.begin(),c->spells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
            c->creatureTextGroups=texts;
            std::sort(c->creatureTextGroups.begin(),c->creatureTextGroups.end(),[](const auto& a,const auto& b){return std::make_pair(a.entry,a.group)<std::make_pair(b.entry,b.group);});
            c->creatureGuidScripts=guidScripts;std::sort(c->creatureGuidScripts.begin(),c->creatureGuidScripts.end());
            g.seedGameObjectRandom(11);
            p=rewardPlayer(1);p.level=60;p.health=p.maxHealth=10000000;p.mapId=mapId;p.x=px;p.y=py;p.z=pz;
            for(const auto& sp:spawns) {
                const auto def=std::find_if(c->npcs.begin(),c->npcs.end(),[&](const auto& d){return d.id==sp.entry;});assert(def!=c->npcs.end());
                LocalRealmNpc m=rewardNpc();m.entry=sp.entry;m.name=def->name;m.level=def->level;m.health=m.maxHealth=def->health;m.hostile=def->hostile;m.questGiver=def->questGiver;
                m.spawnId=sp.id;m.guid=0xf130000000000000ULL|sp.id;m.mapId=mapId;m.x=m.homeX=m.spawnX=sp.x;m.y=m.homeY=m.spawnY=sp.y;m.z=m.homeZ=m.spawnZ=sp.z;
                m.orientation=m.spawnOrientation=sp.o;m.targetGuid=0;m.threat={};m.attackTimer=100000.f;m.lootOwner=0;
                if(sp.movementType==1&&sp.wander>0){m.npcDefaultMotion=1;m.npcWanderDistance=sp.wander;}
                else if(sp.movementType==2&&sp.pathId){m.npcDefaultMotion=2;m.patrolPathId=m.patrolLoadedPath=sp.pathId;m.patrolRepeat=true;}
                c->spawns.push_back({sp.id,sp.entry,mapId,sp.x,sp.y,sp.z,sp.o});n.push_back(m);
            }
            // The roster first: a zero tick with the spawns alone would stream
            // the creatures in and run their RESET rows on copies the roster
            // then replaces (a second summon, a second walk).
            g.useContent(c);g.setRemoteNpcs(n);g.tick(0,{&p});
        }
        void step(){p.health=p.maxHealth;g.tick(.25f,{&p});}
        bool act(LocalAction action,uint64_t target,uint32_t id=0,uint32_t bid=0){LocalRealmCommand cmd{action,target,id};cmd.bid=bid;std::string result;const bool ok=g.execute(p,cmd,{&p},result);if(!ok)std::cerr<<"  action "<<int(action)<<" refused: "<<result<<"\n";return ok;}
    };
    {
        // Dungar Longdrink (352): AGGRO -> SUMMON_CREATURE 9526 (Enraged
        // Gryphon, TIMED_DESPAWN_OUT_OF_COMBAT 30 s) at itself, twice through
        // the link, then "Guards! Help me!". The gryphons are aggressive and
        // engage the player; once the player is beyond reach they leave 30 s
        // after their combat ended (TempSummon::Update) and are pruned.
        LocalNpcDefinition gryphon;gryphon.id=9526;gryphon.name="Enraged Gryphon";gryphon.health=17742;gryphon.level=65;gryphon.hostile=true;gryphon.aggroRadius=20;gryphon.damage=0;gryphon.respawnSeconds=30;gryphon.combatReach=2;
        Arena a(352,65,spellOf(18106),1,0,false,{},20,false,{gryphon},{textGroup(352,0,"Guards! Help me!",kLocalChatMonsterYell)});
        a.step();
        assert(liveSummons(a.g,9526)==2&&a.g.npcs().size()==3);
        for(const auto& m:a.g.npcs())if(m.entry==9526) {
            if(!(m.npcSummonType==kLocalSummonTimedOutOfCombat&&m.npcSummoner==a.n[0].guid&&m.npcSummonLifetimeMs==30000&&std::fabs(m.x-a.n[0].x)<0.01f&&m.level==65&&m.health==17742))
                std::cerr<<"gryphon type="<<unsigned(m.npcSummonType)<<" summoner="<<m.npcSummoner<<" life="<<m.npcSummonLifetimeMs<<" x="<<m.x<<" vs "<<a.n[0].x<<" level="<<unsigned(m.level)<<" health="<<m.health<<"\n";
            assert(m.npcSummonType==kLocalSummonTimedOutOfCombat&&m.npcSummoner==a.n[0].guid&&m.npcSummonLifetimeMs==30000&&std::fabs(m.x-a.n[0].x)<0.01f&&m.level==65&&m.health==17742);
        }
        assert(a.g.npcs()[0].smartSummons.size()==2);
        assert(std::any_of(a.g.scriptDialogues().begin(),a.g.scriptDialogues().end(),[&](const auto& d){return d.speakerGuid==a.n[0].guid&&d.text=="Guards! Help me!";}));
        unsigned elapsed=250;bool engaged=false;
        while(elapsed<5000&&!engaged){a.step();elapsed+=250;engaged=true;for(const auto& m:a.g.npcs())if(m.entry==9526&&m.targetGuid!=a.p.guid)engaged=false;}
        assert(engaged);
        a.p.x=100;
        unsigned left=0;while(left<45000&&liveSummons(a.g,9526)){a.step();left+=250;}
        assert(left>=30000&&left<=32000);
        a.step();assert(a.g.npcs().size()==1&&a.g.npcs()[0].smartSummons.empty());
        std::cout<<"PASS SUMMON_CREATURE: two Enraged Gryphons at Dungar, engaged the player, gone "<<left<<" ms after combat\n";
    }
    {
        // Rotted One (948): DEATH -> Summon Flesh Eating Worms 3428 (TRIGGERED:
        // a dead caster may still cast a triggered spell, Spell::CheckCast).
        // SummonProperties 64 (wild) takes the count from BasePoints; the
        // worms live 30 s (TIMED_DESPAWN) whatever they do.
        const auto worms=spellOf(3428);assert(worms.npcSummonEntry==2462&&worms.npcSummonCount>=1&&worms.durationMs==30000&&worms.npcSummonCategory==0&&worms.npcSummonDest==kLocalNpcDestCaster);
        LocalNpcDefinition worm;worm.id=2462;worm.name="Flesh Eating Worm";worm.health=100;worm.level=20;worm.hostile=true;worm.aggroRadius=15;worm.damage=0;
        Arena a(948,20,worms,1,0,false,{spellOf(26047)},20,false,{worm});
        a.alter([](LocalRealmNpc& m){m.health=1;});
        assert(castAt(a,a.p,1,a.n[0].guid));a.step();
        assert(a.g.npcs()[0].dead&&liveSummons(a.g,2462)==worms.npcSummonCount);
        for(const auto& m:a.g.npcs())if(m.entry==2462)assert(m.npcSummonType==kLocalSummonTimed&&m.npcSummonTimerMs<=30000&&m.npcSummoner==a.n[0].guid&&!m.npcSummonOwned);
        unsigned left=0;while(left<40000&&liveSummons(a.g,2462)){a.step();left+=250;}
        assert(left>=29000&&left<=30500);
        std::cout<<"PASS spell summon at death: "<<unsigned(worms.npcSummonCount)<<" worms for "<<left<<" ms\n";
    }
    {
        // Kolkar Stormer (3273): AGGRO -> Lightning Cloud 6535 at the victim:
        // school damage around the destination (TARGET_UNIT_DEST_AREA_ENEMY)
        // and a persistent area aura (a dynamic object of 15 s at the victim's
        // position) whose periodic damage (amplitude 5 s: the first tick at
        // 5 s, AuraEffect::CalculatePeriodic) lands on whoever stands inside;
        // leaving the cloud removes it within 500 ms, returning restores it
        // with the object's remaining duration, the object's end removes it
        // for good.
        const auto cloud=spellOf(6535);assert(cloud.npcGroundAura&&cloud.npcGroundDest==kLocalNpcDestTarget&&cloud.npcTargetShape==6&&cloud.durationMs==15000&&cloud.periodicDamage>0&&cloud.damage>0&&cloud.npcGroundRadius>0);
        Arena a(3273,20,cloud,1,0,false,{spellOf(9532)});
        unsigned elapsed=0;bool direct=false;
        while(elapsed<10000&&!direct){a.step();elapsed+=250;for(const auto& e:a.g.combatEvents())if(e.spell==6535&&e.kind==LocalCombatEventKind::SpellDamage&&e.attempted)direct=true;}
        assert(direct);
        const auto carries=[&](){return std::any_of(a.p.harmfulAuras.begin(),a.p.harmfulAuras.end(),[](const auto& v){return v.spellId==6535;});};
        uint64_t seen=0;for(const auto& e:a.g.combatEvents())seen=std::max(seen,e.sequence);
        unsigned ticks=0,inside=0;
        while(inside<6000){a.step();inside+=250;hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==6535&&e.kind==LocalCombatEventKind::PeriodicDamage)++ticks;});}
        assert(ticks>=1&&carries());
        a.p.x=60;for(int i=0;i<3;++i)a.step();
        assert(!carries());
        a.p.x=10;for(int i=0;i<3;++i)a.step();
        assert(carries());
        // The re-entrant carries the object's remainder, not a fresh 15 s.
        assert(std::none_of(a.p.harmfulAuras.begin(),a.p.harmfulAuras.end(),[](const auto& v){return v.spellId==6535&&v.remainingMs>15000-6000-1500;}));
        unsigned rest=0;while(rest<16000){a.step();rest+=250;}
        assert(!carries());
        unsigned late=0;for(const auto& e:a.g.combatEvents())seen=std::max(seen,e.sequence);
        for(int i=0;i<12;++i){a.step();hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==6535&&e.kind==LocalCombatEventKind::PeriodicDamage)++late;});}
        assert(late==0);
        std::cout<<"PASS PERSISTENT_AREA_AURA: Lightning Cloud ticked "<<ticks<<" times inside, none outside, gone with the object\n";
    }
    {
        // Shattered Hand Heathen (spawn 151004, entry 17420, Shattered Halls):
        // its guid script starts escort path 1742004 at RESPAWN (run, no
        // repeat, REACT_AGGRESSIVE) and calls timed list 1742000 at
        // ESCORT_ENDED. The creature runs the five points at 7 yd/s, moves
        // its home along, and stands at the last point when the path ends.
        const auto path=localNpcWaypointPath(1742004);assert(path.second-path.first==5);
        float length=0;{float lx=86.8137f,ly=56.9043f,lz=-13.1241f;for(const auto* w=path.first;w!=path.second;++w){length+=std::sqrt((w->x-lx)*(w->x-lx)+(w->y-ly)*(w->y-ly)+(w->z-lz)*(w->z-lz));lx=w->x;ly=w->y;lz=w->z;}}
        LocalNpcDefinition heathen;heathen.id=17420;heathen.name="Shattered Hand Heathen";heathen.health=10000;heathen.level=70;heathen.hostile=true;heathen.aggroRadius=0;heathen.damage=0;
        Stage s({heathen},{},{},540,70.f,30.f,-13.2f,{{151004,17420,86.8137f,56.9043f,-13.1241f,3.32437f}},{151004});
        s.step();
        assert(s.g.npcs().size()==1&&s.g.npcs()[0].escortActive&&s.g.npcs()[0].escortPathId==1742004&&s.g.npcs()[0].npcReactState==2);
        unsigned elapsed=250;bool sawList=false;
        while(elapsed<30000&&s.g.npcs()[0].escortActive){s.step();elapsed+=250;if(s.g.npcs()[0].smartListId==1742000)sawList=true;}
        const auto& m=s.g.npcs()[0];
        const auto& last=path.first[4];
        if(!(std::fabs(m.x-last.x)<0.3f&&std::fabs(m.y-last.y)<0.3f))std::cerr<<"escort at "<<m.x<<","<<m.y<<" after "<<elapsed<<" ms (length "<<length<<")\n";
        assert(!m.escortActive&&std::fabs(m.x-last.x)<0.3f&&std::fabs(m.y-last.y)<0.3f&&std::fabs(m.homeX-last.x)<0.3f&&std::fabs(m.homeY-last.y)<0.3f);
        assert(float(elapsed)>=length/7.f*1000.f-500.f&&float(elapsed)<=length/7.f*1000.f+1500.f);
        for(int i=0;i<4&&!sawList;++i){s.step();if(s.g.npcs()[0].smartListId==1742000)sawList=true;}
        assert(sawList);
        std::cout<<"PASS ESCORT_START: the heathen ran "<<length<<" yd of path 1742004 in "<<elapsed<<" ms and called its end list\n";
    }
    {
        // Kaskala Defender (spawn 111350, entry 25764): its guid script moves
        // it to a point at RESPAWN (MOVE_TO_POS), sets its home there
        // (SET_HOME_POS at the row position) and despawns it 90 s later.
        LocalNpcDefinition defender;defender.id=25764;defender.name="Kaskala Defender";defender.health=8982;defender.level=71;defender.hostile=true;defender.aggroRadius=0;defender.damage=0;
        Stage s({defender},{},{},571,3040.f,4910.f,1.f,{{111350,25764,3035.2f,4925.79f,2.69066f,4.95899f}},{111350});
        s.step();unsigned elapsed=250;
        assert(s.g.npcs()[0].npcMotion==1);
        while(elapsed<8000&&s.g.npcs()[0].npcMotion){s.step();elapsed+=250;}
        const auto& m=s.g.npcs()[0];
        if(m.npcMotion||std::fabs(m.x-3037.4f)>=0.05f||std::fabs(m.y-4903.99f)>=0.05f)std::cerr<<"defender at "<<m.x<<","<<m.y<<","<<m.z<<" motion "<<int(m.npcMotion)<<" home "<<m.homeX<<","<<m.homeY<<" after "<<elapsed<<" ms\n";
        assert(!m.npcMotion&&std::fabs(m.x-3037.4f)<0.05f&&std::fabs(m.y-4903.99f)<0.05f&&std::fabs(m.homeX-3037.4f)<0.05f&&std::fabs(m.homeY-4903.99f)<0.05f);
        assert(elapsed>=2500&&elapsed<=4500);
        while(elapsed<95000&&!s.g.npcs()[0].npcDespawned){s.step();elapsed+=250;}
        assert(s.g.npcs()[0].npcDespawned&&elapsed>=90000);
        std::cout<<"PASS MOVE_TO_POS + SET_HOME_POS: the defender walked to its post in "<<elapsed<<" ms\n";
    }
    {
        // Image of Loken (27212): UPDATE_OOC 2 s once -> TALK 0 for 6 s; each
        // TEXT_OVER (group, its own entry) speaks the next group for 6 s; five
        // lines 6 s apart (SmartScript's text timer).
        std::vector<LocalCreatureTextGroup> texts;for(uint8_t g=0;g<5;++g)texts.push_back(textGroup(27212,g,"Loken "+std::to_string(g)));
        LocalNpcDefinition loken;loken.id=27212;loken.name="Image of Loken";loken.health=1000;loken.level=80;loken.hostile=false;loken.damage=0;
        Stage s({loken},{},texts,0,5.f,0.f,0.f,{{20,27212,0.f,0.f,0.f,0.f}});
        std::vector<unsigned> at;unsigned elapsed=0;size_t seenLines=0;
        while(elapsed<40000){s.step();elapsed+=250;while(seenLines<s.g.scriptDialogues().size()){const auto& d=s.g.scriptDialogues()[seenLines++];if(d.speakerGuid==s.n[0].guid)at.push_back(elapsed);}}
        assert(at.size()==5);
        for(size_t i=0;i<5;++i)assert(at[i]>=2000+6000*i&&at[i]<=2750+6000*i);
        std::cout<<"PASS TEXT_OVER: five lines at "<<at[0]<<", "<<at[1]<<", "<<at[2]<<", "<<at[3]<<", "<<at[4]<<" ms\n";
    }
    {
        // Lathoric the Black (8391), RESET once: MOVE_TO_POS, then Obsidion
        // 8421 summoned at the row position (TIMED_DESPAWN 45 s), REACT_PASSIVE,
        // walking. After 8 s Obsidion (the closest 8421) speaks five groups of
        // 5 s through TEXT_OVER (entry 8421), Lathoric answers with two of his
        // own (entry 8391), then REMOVE_UNIT_FLAG and ATTACK_START on the
        // closest player - the cutscene ends in combat. Obsidion leaves at 45 s.
        std::vector<LocalCreatureTextGroup> texts;for(uint8_t g=0;g<5;++g)texts.push_back(textGroup(8421,g,"Obsidion "+std::to_string(g)));
        texts.push_back(textGroup(8391,0,"Your task is complete."));texts.push_back(textGroup(8391,1,"Obsidion, Rise and Serve your Master!",kLocalChatMonsterYell));
        LocalNpcDefinition lathoric;lathoric.id=8391;lathoric.name="Lathoric the Black";lathoric.health=5000;lathoric.level=45;lathoric.hostile=true;lathoric.aggroRadius=0;lathoric.damage=0;lathoric.unitFlags=768;
        LocalNpcDefinition obsidion;obsidion.id=8421;obsidion.name="Obsidion";obsidion.health=20000;obsidion.level=50;obsidion.hostile=true;obsidion.aggroRadius=0;obsidion.damage=0;
        Stage s({lathoric,obsidion},{},texts,0,-6470.f,-1250.f,180.2f,{{20,8391,-6470.f,-1240.f,180.19f,3.58f}});
        s.step();
        const auto* l=npcOf(s.g,s.n[0].guid);assert(l&&l->npcMotion==1&&l->npcWalking&&l->npcReactState==0&&liveSummons(s.g,8421)==1);
        uint64_t obsidionGuid=0;for(const auto& m:s.g.npcs())if(m.entry==8421){obsidionGuid=m.guid;assert(m.npcSummonType==kLocalSummonTimed&&m.npcSummonLifetimeMs==45000&&std::fabs(m.x+6481.13f)<0.01f&&std::fabs(m.y+1237.45f)<0.01f);}
        unsigned elapsed=250;while(elapsed<6000&&npcOf(s.g,s.n[0].guid)->npcMotion){s.step();elapsed+=250;}
        l=npcOf(s.g,s.n[0].guid);assert(!l->npcMotion&&std::fabs(l->x+6475.47f)<0.05f&&std::fabs(l->y+1242.28f)<0.05f&&elapsed>=1750&&elapsed<=3000);
        std::vector<std::pair<unsigned,uint64_t>> lines;size_t seenLines=0;
        while(elapsed<43000){s.step();elapsed+=250;while(seenLines<s.g.scriptDialogues().size()){const auto& d=s.g.scriptDialogues()[seenLines++];lines.push_back({elapsed,d.speakerGuid});}}
        for(const auto& line:lines)std::cerr<<"  line at "<<line.first<<" by "<<(line.second==obsidionGuid?"Obsidion":line.second==s.n[0].guid?"Lathoric":"?")<<"\n";
        assert(lines.size()==7);
        // A text timer landing on a tick boundary fires one tick late (the
        // pin's `mTextTimer < diff`), so each 5 s line may drift by a tick.
        for(size_t i=0;i<5;++i)assert(lines[i].second==obsidionGuid&&lines[i].first>=8000+5000*i&&lines[i].first<=8250+5250*i);
        assert(lines[5].second==s.n[0].guid&&lines[5].first>=33000&&lines[5].first<=34500&&lines[6].second==s.n[0].guid&&lines[6].first>=38000&&lines[6].first<=39750);
        l=npcOf(s.g,s.n[0].guid);assert(l->targetGuid==s.p.guid&&!(l->npcUnitFlags&768u)&&l->npcUnitFlagsOverride);
        while(elapsed<47000&&liveSummons(s.g,8421)){s.step();elapsed+=250;}
        assert(!liveSummons(s.g,8421)&&elapsed>=45000);
        std::cout<<"PASS cutscene: Lathoric walked to his mark, Obsidion spoke five lines, Lathoric two, then attacked; Obsidion left at "<<elapsed<<" ms\n";
    }

    // ------------------------------------------------------------------ 2.39
    // Default movement (random wander, waypoint_data patrols), the waypoint
    // SmartAI family, npc flags, and the spell decoder round (self stuns,
    // invisibility, item creation, permanent periodic triggers, instakill).
    std::cout<<"-- 2.39: default movement, patrols, npc flags, self auras --\n";
    {
        // creature.MovementType 1 with wander_distance 5: the creature walks
        // (2.5 yd/s) between the twelve destination points of the pin's
        // RandomMovementGenerator, never beyond the wander distance from its
        // spawn, and pauses between moves (4-8 s after 10 % + 25 % per move).
        LocalNpcDefinition wanderer;wanderer.id=700001;wanderer.name="Wanderer";wanderer.health=1000;wanderer.level=10;wanderer.hostile=false;wanderer.damage=0;
        Stage s({wanderer},{},{},0,60.f,0.f,0.f,{{20,700001,0.f,0.f,0.f,0.f,1,5.f,0}});
        unsigned moves=0,idle=0,elapsed=0;float maxDist=0,maxStep=0;float lx=0,ly=0;
        while(elapsed<60000){s.step();elapsed+=250;const auto& m=s.g.npcs()[0];
            const float step=std::sqrt((m.x-lx)*(m.x-lx)+(m.y-ly)*(m.y-ly));maxStep=std::max(maxStep,step);
            if(step>0.001f)++moves;else ++idle;
            maxDist=std::max(maxDist,std::sqrt(m.x*m.x+m.y*m.y));lx=m.x;ly=m.y;}
        assert(moves>=8&&idle>=8&&maxDist<=5.01f&&maxDist>=2.f&&maxStep<=0.626f);
        std::cout<<"PASS random movement: "<<moves<<" moving ticks, "<<idle<<" idle ticks, farthest "<<maxDist<<" yd, longest step "<<maxStep<<" yd\n";
    }
    {
        // Nurse Lillian (5042, spawn 90458 on waypoint_data path 904580): the
        // patrol walks its eight nodes; nodes 3, 6 and 8 sit on the previous
        // node with a 12 s delay and a facing, and her WAYPOINT_REACHED rows
        // (points 3, 6, 8) run list 504200 (a stand state, then "talk 0").
        // The home position follows her along the path.
        std::map<uint32_t,std::vector<LocalWaypointNode>> paths;
        const float pts[8][4]={{-8760.21f,811.92f,97.7937f,-1000},{-8763.19f,810.935f,97.7457f,-1000},{-8763.19f,810.935f,97.7457f,2.19911f},{-8769.18f,814.315f,97.8737f,-1000},
                               {-8755.52f,814.187f,97.8815f,-1000},{-8755.52f,814.187f,97.8815f,3.83972f},{-8766.35f,820.147f,97.8701f,-1000},{-8766.35f,820.147f,97.8701f,3.87463f}};
        for(int i=0;i<8;++i){LocalWaypointNode n;n.x=pts[i][0];n.y=pts[i][1];n.z=pts[i][2];n.hasOrientation=pts[i][3]>-999;n.orientation=n.hasOrientation?pts[i][3]:0;n.delayMs=(i==2||i==5||i==7)?12000:0;n.moveType=0;n.id=uint16_t(i+1);paths[904580].push_back(n);}
        LocalNpcDefinition lillian;lillian.id=5042;lillian.name="Nurse Lillian";lillian.health=1000;lillian.level=30;lillian.hostile=false;lillian.damage=0;
        // The player stands within say range (25 yd) of every node.
        Stage s({lillian},{},{textGroup(5042,0,"How are you feeling today?")},0,-8756.f,815.f,97.8f,{{90458,5042,-8760.21f,811.92f,97.7937f,0.f,2,0.f,904580}},{},paths);
        std::vector<unsigned> talks;unsigned elapsed=0;size_t seenLines=0;float facingAt3=0;bool homeFollowed=false;
        while(elapsed<50000){s.step();elapsed+=250;const auto& m=s.g.npcs()[0];
            while(seenLines<s.g.scriptDialogues().size()){seenLines++;talks.push_back(elapsed);if(talks.size()==1)facingAt3=m.orientation;}
            if(m.patrolMoving&&std::fabs(m.homeX-m.x)<0.01f&&std::fabs(m.homeY-m.y)<0.01f&&std::fabs(m.x-(-8760.21f))>1.f)homeFollowed=true;}
        for(auto t:talks)std::cerr<<"  Lillian spoke at "<<t<<" ms\n";
        // Node 2 is 3.1 yd away (1.3 s at 2.5 yd/s), node 3 sits on it; each
        // node costs a tick to arrive and one to relaunch.
        assert(talks.size()==3&&talks[0]>=2000&&talks[0]<=3500&&std::fabs(facingAt3-2.19911f)<0.01f);
        assert(talks[1]>=talks[0]+12000+8000&&talks[1]<=talks[0]+12000+10500&&talks[2]>=talks[1]+12000+4500&&talks[2]<=talks[1]+12000+7000);
        assert(homeFollowed&&s.g.npcs()[0].npcDefaultMotion==2);
        std::cout<<"PASS waypoint_data patrol: Nurse Lillian spoke at nodes 3, 6, 8 ("<<talks[0]<<", "<<talks[1]<<", "<<talks[2]<<" ms), her home following her\n";
    }
    {
        // Citizen of Havenshire (spawn 128993, entry 28577): RESPAWN starts
        // path 12899300 (16 running nodes, no repeat), AGGRO pauses the
        // movement (MOVEMENT_PAUSE 0: until resumed), EVADE resumes it, the
        // WAYPOINT_ENDED row of that path despawns the citizen 2 s after the
        // last node.
        std::map<uint32_t,std::vector<LocalWaypointNode>> paths;
        const float pts[16][3]={{2076.47f,-5888.58f,104.194f},{2041.59f,-5906.8f,105.348f},{2024.72f,-5907.8f,104.918f},{2004.06f,-5904.82f,103.987f},{1994.01f,-5904.21f,103.837f},{1975.01f,-5903.21f,103.337f},
                                {1960.9f,-5902.6f,102.147f},{1920.05f,-5909.8f,101.559f},{1886.32f,-5910.06f,103.368f},{1871.56f,-5910.35f,104.016f},{1828.52f,-5925.03f,110.765f},{1796.47f,-5930.46f,116.013f},
                                {1756.46f,-5915.22f,116.101f},{1716.35f,-5893.59f,116.142f},{1682.93f,-5873.77f,116.171f},{1663.34f,-5876.86f,117.099f}};
        float length=0;
        for(int i=0;i<16;++i){LocalWaypointNode n;n.x=pts[i][0];n.y=pts[i][1];n.z=pts[i][2];n.moveType=1;n.id=uint16_t(i+1);paths[12899300].push_back(n);
            if(i)length+=std::sqrt((pts[i][0]-pts[i-1][0])*(pts[i][0]-pts[i-1][0])+(pts[i][1]-pts[i-1][1])*(pts[i][1]-pts[i-1][1])+(pts[i][2]-pts[i-1][2])*(pts[i][2]-pts[i-1][2]));}
        LocalNpcDefinition citizen;citizen.id=28577;citizen.name="Citizen of Havenshire";citizen.health=3000;citizen.level=55;citizen.hostile=true;citizen.aggroRadius=0;citizen.damage=0;
        Stage s({citizen},{},{},609,2090.f,-5880.f,104.2f,{{128993,28577,2090.f,-5880.f,104.2f,0.f}},{128993},paths);
        unsigned elapsed=0;
        s.step();elapsed+=250;
        assert(s.g.npcs()[0].npcDefaultMotion==2&&s.g.npcs()[0].patrolPathId==12899300&&!s.g.npcs()[0].patrolRepeat);
        // Six seconds in: the citizen runs (7 yd/s); the player then attacks.
        while(elapsed<6000){s.step();elapsed+=250;}
        const float runX=s.g.npcs()[0].x;
        assert(s.g.npcs()[0].patrolMoving&&std::fabs(runX-2090.f)>25.f);
        {auto state=s.g.npcs();state[0].targetGuid=s.p.guid;state[0].threat[0]={s.p.guid,1000};s.g.setRemoteNpcs(state);}
        s.p.x=s.g.npcs()[0].x+2;s.p.y=s.g.npcs()[0].y;s.step();elapsed+=250;
        assert(s.g.npcs()[0].patrolStalled&&!s.g.npcs()[0].patrolMoving);
        // The player leaves (beyond 70 yd the victim is lost): the citizen
        // evades (EVADE -> MOVEMENT_RESUME) and runs on from where it stood.
        s.p.x=s.g.npcs()[0].x+120.f;s.p.y=-5880.f;s.p.z=104.2f;
        for(int i=0;i<4;++i){s.step();elapsed+=250;}
        assert(!s.g.npcs()[0].patrolStalled&&s.g.npcs()[0].patrolMoving&&!s.g.npcs()[0].targetGuid);
        // The player follows at 40 yd: the citizen walks far beyond the active
        // radius of its spawn and stays in the roster because it stands near
        // the player (2.39 regions: a creature that walked away from its spawn).
        while(elapsed<120000&&!s.g.npcs()[0].npcDespawned){s.p.x=s.g.npcs()[0].x+40.f;s.p.y=s.g.npcs()[0].y;s.p.z=s.g.npcs()[0].z;s.step();elapsed+=250;assert(s.g.npcs().size()==1);}
        const auto& m=s.g.npcs()[0];
        assert(m.npcDespawned&&m.patrolDone&&!m.patrolLoadedPath&&std::fabs(m.x-1663.34f)<0.5f);
        assert(float(elapsed)>=length/7.f*1000.f+2000.f&&float(elapsed)<=length/7.f*1000.f+2000.f+4000.f);
        std::cout<<"PASS WAYPOINT_START / PAUSE / RESUME / ENDED: the citizen ran "<<length<<" yd, paused for the fight, and despawned at "<<elapsed<<" ms\n";
    }
    {
        // Overseer Azarad (20685): HEALTH_PCT 10-30 (once) -> 35492, a 3 s
        // stun the creature applies to itself (UNIT_FLAG_STUNNED: no swing,
        // no chase), then it fights on.
        const auto stun=spellOf(35492);assert(stun.npcSelfControl==1&&stun.durationMs==3000&&stun.npcPositive);
        Arena a(20685,65,spellOf(35491),1,100,false,{stun});
        a.alter([](LocalRealmNpc& m){m.health=m.maxHealth/5;});
        unsigned elapsed=0;bool stunned=false;
        while(elapsed<10000&&!stunned){a.step();elapsed+=250;for(const auto& b:a.g.npcs()[0].npcBuffs)if(b.spellId==35492&&b.selfControl==1)stunned=true;}
        assert(stunned);
        uint64_t seen=0;for(const auto& e:a.g.combatEvents())seen=std::max(seen,e.sequence);
        const float x0=a.g.npcs()[0].x;a.p.x=x0+15;unsigned swings=0,held=0;
        for(int i=0;i<11;++i){a.step();hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.kind==LocalCombatEventKind::NpcMelee&&e.source==a.n[0].guid)++swings;});if(std::fabs(a.g.npcs()[0].x-x0)<0.01f)++held;}
        assert(swings==0&&held>=11);
        for(int i=0;i<8;++i)a.step();
        assert(std::none_of(a.g.npcs()[0].npcBuffs.begin(),a.g.npcs()[0].npcBuffs.end(),[](const auto& b){return b.spellId==35492;})&&std::fabs(a.g.npcs()[0].x-x0)>0.5f);
        std::cout<<"PASS self stun: Azarad stood stunned for 3 s at 20 % health, then chased again\n";
    }
    {
        // Blink Dragon (3815): HEALTH_PCT 30-60 (once) -> 8611: invisibility,
        // a self stun and a school immunity for 6 s - unseen and unattackable
        // (npcVisibleTo / canAttack), then back.
        const auto blink=spellOf(8611);assert(blink.npcInvisible&&blink.npcSelfControl==1&&blink.durationMs==6000);
        Arena a(3815,30,blink,1);
        a.alter([](LocalRealmNpc& m){m.health=m.maxHealth*45/100;});
        unsigned elapsed=0;bool hidden=false;
        while(elapsed<10000&&!hidden){a.step();elapsed+=250;hidden=!a.g.npcVisibleTo(a.p,a.g.npcs()[0]);}
        assert(hidden&&!a.g.canAttack(a.p,a.g.npcs()[0]));
        unsigned unseen=0;for(int i=0;i<20;++i){a.step();if(!a.g.npcVisibleTo(a.p,a.g.npcs()[0]))++unseen;}
        for(int i=0;i<6;++i)a.step();
        assert(unseen>=20&&a.g.npcVisibleTo(a.p,a.g.npcs()[0])&&a.g.canAttack(a.p,a.g.npcs()[0]));
        std::cout<<"PASS invisibility: the Blink Dragon vanished for 6 s at 45 % health\n";
    }
    {
        // Snowfall Glade Pup (26200): SPELLHIT by 39996 -> a 3 s self stun,
        // then CREATE_ITEM 46773 on the invoker (EffectItemType 35692, one
        // item - the column the 2.37 decoder misread), then FORCE_DESPAWN.
        const auto create=spellOf(46773);assert(create.npcCreateItem==35692&&create.npcCreateItemCount==1);
        LocalSpellDefinition net;net.id=39996;net.name="Net";net.damage=1;net.range=100;
        LocalItemDefinition pup;pup.id=35692;pup.name="Snowfall Glade Pup";pup.stack=1;
        Arena a(26200,70,spellOf(65208),1,0,false,{net,create});
        a.c->items.push_back(pup);std::sort(a.c->items.begin(),a.c->items.end(),[](const auto& x,const auto& y){return x.id<y.id;});
        a.g.useContent(a.c);a.g.tick(0,a.party());a.g.setRemoteNpcs(a.n);
        a.step();
        assert(castAt(a,a.p,39996,a.n[0].guid));
        unsigned elapsed=0;bool got=false;
        while(elapsed<5000&&!got){a.step();elapsed+=250;got=std::any_of(a.p.inventory.begin(),a.p.inventory.end(),[](const auto& st){return st.itemId==35692&&st.count==1;});}
        assert(got&&a.g.npcs()[0].npcDespawned);
        std::cout<<"PASS CREATE_ITEM: the pup handed the player item 35692 and vanished ("<<elapsed<<" ms)\n";
    }
    {
        // Marduk Blackpool (10433): AGGRO -> 17695, a permanent periodic
        // trigger on himself (SpellDuration -1): every 2 s he casts 17697, a
        // Shadow resistance debuff (-100) around him, for as long as he lives.
        const auto aura=spellOf(17695);assert(aura.npcPeriodicTriggerSpellId==17697&&aura.npcPeriodicTriggerIntervalMs==2000&&aura.indefiniteDuration&&!aura.durationMs);
        Arena a(10433,35,aura,1,0,false,{spellOf(17697),spellOf(15284),spellOf(17228),spellOf(12040)});
        unsigned elapsed=0;bool armed=false;
        while(elapsed<3000&&!armed){a.step();elapsed+=250;for(const auto& b:a.g.npcs()[0].npcBuffs)if(b.spellId==17695&&b.indefinite)armed=true;}
        assert(armed);
        uint64_t seen=0;for(const auto& e:a.g.combatEvents())seen=std::max(seen,e.sequence);
        unsigned casts=0;
        for(int i=0;i<40;++i){a.step();hitEvents(a.g,seen,[&](const LocalCombatEvent& e){if(e.spell==17697&&e.kind==LocalCombatEventKind::SpellCast)++casts;});}
        assert(casts>=4&&casts<=6);
        assert(std::any_of(a.p.harmfulAuras.begin(),a.p.harmfulAuras.end(),[](const auto& v){return v.spellId==17697&&v.resistance==-100;}));
        std::cout<<"PASS permanent periodic trigger: Marduk cast his Shadow resistance curse "<<casts<<" times in 10 s\n";
    }
    {
        // Iceskin Sentry (31012): SPELLHIT by 58282 -> KILLEDMONSTER 31012 for
        // the invoker, then 58285 - an instakill of itself: a death without a
        // kill, the DEATH rows and the respawn as for a scripted death.
        const auto kill=spellOf(58285);assert(kill.npcInstakillSelf&&kill.npcPositive);
        LocalSpellDefinition hammer;hammer.id=58282;hammer.name="Hammer";hammer.damage=1;hammer.range=100;
        Arena a(31012,75,kill,1,0,false,{hammer});
        a.c->quests.push_back({});a.c->quests.back().id=13100;a.c->quests.back().title="Sentries";a.c->quests.back().objectives={{LocalQuestObjective::Type::Kill,31012,3}};
        std::sort(a.c->quests.begin(),a.c->quests.end(),[](const auto& x,const auto& y){return x.id<y.id;});
        a.g.useContent(a.c);a.g.tick(0,a.party());a.g.setRemoteNpcs(a.n);
        a.p.quests.push_back({13100,LocalQuestStatus::Active,{0}});
        a.step();assert(castAt(a,a.p,58282,a.n[0].guid));
        unsigned elapsed=0;while(elapsed<3000&&!a.g.npcs()[0].dead){a.step();elapsed+=250;}
        const auto* progress=localQuestProgress(a.p,13100);
        assert(a.g.npcs()[0].dead&&progress&&progress->progress[0]==1);
        std::cout<<"PASS instakill: the sentry credited the player and destroyed itself\n";
    }
    {
        // Hierophant Cenius (25810), a quest giver: AGGRO removes
        // UNIT_NPC_FLAG_QUESTGIVER, RESET (the evade) sets it again.
        // A quest giver is attackable only through a hostile faction relation
        // (npcDisposition), so the stage carries two faction templates: the
        // player's (group 1) and Cenius' (enemy group 1).
        LocalNpcDefinition cenius;cenius.id=25810;cenius.name="Hierophant Cenius";cenius.health=5000;cenius.level=70;cenius.hostile=true;cenius.questGiver=true;cenius.aggroRadius=0;cenius.damage=0;cenius.faction=14;
        Stage s({cenius},{},{},571,150.f,0.f,0.f,{{20,25810,0.f,0.f,0.f,0.f}});
        {
            LocalFactionTemplate mine;mine.id=1;mine.faction=1;mine.factionGroup=1;
            LocalFactionTemplate monster;monster.id=14;monster.faction=14;monster.enemyGroup=1;
            std::array<uint32_t,12> races{};races.fill(1);std::string error;
            assert(s.g.setFactionTemplates({mine,monster},races,error));
        }
        s.step();assert(s.g.npcs()[0].questGiver&&!s.g.npcs()[0].targetGuid);
        s.p.x=3.f;
        {auto state=s.g.npcs();state[0].targetGuid=s.p.guid;state[0].threat[0]={s.p.guid,1000};s.g.setRemoteNpcs(state);}
        s.step();
        assert(!s.g.npcs()[0].questGiver&&s.g.npcs()[0].npcFlagsOverridden&&s.g.npcs()[0].targetGuid==s.p.guid);
        s.p.x=150.f;
        for(int i=0;i<3;++i)s.step();
        assert(s.g.npcs()[0].questGiver&&!s.g.npcs()[0].targetGuid);
        std::cout<<"PASS npc flags: Cenius stopped giving quests in combat and resumed at the reset\n";
    }
    // ------------------------------------------------------------- 2.40 gossip
    {
        // Ambassador Sunsorrow (16287, menu 7178): the template menu lists
        // three texts without conditions (the last one wins, GetGossipTextId),
        // and the GOSSIP_HELLO row sends the menu again with text 8458; the one
        // option needs a blood elf who finished quest 9180 and lacks item 30632.
        // Choosing it runs GOSSIP_SELECT (7178, 0): ADD_ITEM 30632, then
        // SEND_GOSSIP_MENU (7178, 10378) - the songbook text - and the option
        // is gone (the item condition). "$r" expands to the character's race.
        LocalNpcDefinition sunsorrow;sunsorrow.id=16287;sunsorrow.name="Ambassador Sunsorrow";sunsorrow.health=2243;sunsorrow.level=60;sunsorrow.hostile=false;sunsorrow.questGiver=true;sunsorrow.damage=0;
        Stage::Gossip gossip;
        {
            LocalGossipMenu menu;menu.id=7178;
            for(uint32_t id:{8458u,8740u,10378u}){LocalGossipMenuText t;t.textId=id;menu.texts.push_back(t);}
            LocalGossipOption o;o.id=0;o.icon=0;o.type=1;o.npcFlag=1;o.actionMenuId=8312;o.text="What is it that you have for me, ambassador?";
            o.conditions={{2,0,true,30632,1,0},{8,0,false,9180,0,0},{16,0,false,512,0,0}};menu.options.push_back(o);
            gossip.menus.push_back(menu);
            const auto text=[](uint32_t id,const std::string& male){LocalGossipText t;t.id=id;LocalGossipTextVariant v;v.probability=1;v.maleText=male;t.variants.push_back(v);return t;};
            gossip.texts.push_back(text(8458,"Greetings, $r.  I am Ambassador Sunsorrow of the sin'dorei."));
            gossip.texts.push_back(text(8740,"I've heard something of your exploits, $N."));
            gossip.texts.push_back(text(10378,"Just a small songbook that I thought you might like to have."));
            gossip.owners.push_back({16287,7178,3});
            LocalItemDefinition songbook;songbook.id=30632;songbook.name="Sin'dorei Songbook";songbook.stack=1;gossip.items.push_back(songbook);
        }
        Stage s({sunsorrow},{},{},530,3.f,0.f,0.f,{{20,16287,0.f,0.f,0.f,0.f}},{},{},gossip);
        s.p.race=10;s.p.completedQuestIds={9180};
        s.step();
        assert(s.act(LocalAction::Interact,s.g.npcs()[0].guid));
        const auto& page=s.p.gossip;
        assert(page.open()&&page.npcGuid==s.g.npcs()[0].guid&&page.menuId==7178&&page.textId==8458&&page.options.size()==1&&page.options[0].id==0&&page.questMenu);
        LocalGossipText shown;assert(s.g.gossipTextFor(page.textId,shown));
        assert(localGossipPageText(s.p,shown,page.revision)=="Greetings, Blood Elf.  I am Ambassador Sunsorrow of the sin'dorei.");
        const auto revision=page.revision;
        assert(s.act(LocalAction::GossipSelect,s.g.npcs()[0].guid,0,7178));
        assert(bagCount(s.p,30632)==1&&page.open()&&page.textId==10378&&page.options.empty()&&page.revision>revision);
        // A human without the quest sees no option; the last text of the menu
        // is the page's own (the hello row sends 8458 again).
        s.p.race=1;s.p.completedQuestIds.clear();s.p.inventory.clear();
        assert(s.act(LocalAction::Interact,s.g.npcs()[0].guid));
        assert(page.textId==8458&&page.options.empty());
        // Walking away ends the page (gossipTick).
        s.p.x=40.f;s.step();
        assert(!page.open());
        std::cout<<"PASS gossip: Sunsorrow's hello text, the conditioned option, ADD_ITEM and the songbook text, the page closing at range\n";
    }
    {
        // Merideth Carlson (2357, menu 4004, a vendor with npcflag 131): the
        // horse-feed option needs quest 7645 rewarded and no item 18775; the
        // vendor option and the horses text need a human OR an exalted
        // Stormwind reputation (two else groups), the refusal text neither
        // (one group). GOSSIP_SELECT (4004, 1) casts 23304 at the invoker (a
        // CREATE_ITEM of the feed) and CLOSE_GOSSIP ends the page.
        const auto feed=spellOf(23304);assert(feed.npcCreateItem==18775&&feed.npcCreateItemCount>=1);
        LocalNpcDefinition merideth;merideth.id=2357;merideth.name="Merideth Carlson";merideth.health=1163;merideth.level=32;merideth.hostile=false;merideth.damage=0;
        Stage::Gossip gossip;
        {
            LocalGossipMenu menu;menu.id=4004;
            LocalGossipMenuText t1;t1.textId=4859;t1.conditions={{16,0,false,1,0,0},{5,1,false,72,128,0}};menu.texts.push_back(t1);
            LocalGossipMenuText t2;t2.textId=5855;t2.conditions={{5,0,true,72,128,0},{16,0,true,1,0,0}};menu.texts.push_back(t2);
            LocalGossipOption buy;buy.id=0;buy.icon=1;buy.type=3;buy.npcFlag=128;buy.text="I would like to buy from you.";buy.conditions={{16,0,false,1,0,0},{5,1,false,72,128,0}};menu.options.push_back(buy);
            LocalGossipOption feedOption;feedOption.id=1;feedOption.icon=0;feedOption.type=1;feedOption.npcFlag=1;feedOption.text="Merideth, could I have some more manna-enriched horse feed please?";
            feedOption.conditions={{2,0,true,18775,1,0},{2,0,true,18775,1,1},{47,0,false,7645,64,0}};menu.options.push_back(feedOption);
            gossip.menus.push_back(menu);
            const auto text=[](uint32_t id,const std::string& male){LocalGossipText t;t.id=id;LocalGossipTextVariant v;v.probability=1;v.maleText=male;t.variants.push_back(v);return t;};
            gossip.texts.push_back(text(4859,"Years of quality breeding techniques."));gossip.texts.push_back(text(5855,"Sorry, but I am not disposed to sell horses."));
            gossip.owners.push_back({2357,4004,131});
            LocalItemDefinition feedItem;feedItem.id=18775;feedItem.name="Manna-Enriched Horse Feed";feedItem.stack=20;gossip.items.push_back(feedItem);
        }
        Stage s({merideth},{feed},{},0,3.f,0.f,0.f,{{21,2357,0.f,0.f,0.f,0.f}},{},{},gossip);
        {auto state=s.g.npcs();state[0].vendor=true;s.g.setRemoteNpcs(state);}
        s.p.race=3;s.p.completedQuestIds={7645};
        s.step();
        assert(s.act(LocalAction::Interact,s.g.npcs()[0].guid));
        const auto& page=s.p.gossip;
        assert(page.open()&&page.menuId==4004&&page.textId==5855&&page.options.size()==1&&page.options[0].id==1&&page.options[0].type==1);
        assert(s.act(LocalAction::GossipSelect,s.g.npcs()[0].guid,1,4004));
        assert(!page.open());
        for(int i=0;i<6;++i)s.step(); // the 1 s cast lands the feed
        assert(bagCount(s.p,18775)>=1);
        // With the feed in the bags the option is gone; a human sees the
        // horses text and the vendor option (the first else group holds).
        assert(s.act(LocalAction::Interact,s.g.npcs()[0].guid));
        assert(page.open()&&page.options.empty()&&page.textId==5855);
        s.p.race=1;
        assert(s.act(LocalAction::Interact,s.g.npcs()[0].guid));
        assert(page.textId==4859&&page.options.size()==1&&page.options[0].type==3);
        std::cout<<"PASS gossip: Merideth's conditioned texts and options, the feed cast at the invoker and CLOSE_GOSSIP\n";
    }
    {
        // Mountaineer Pebblebitty (3836): RECEIVE_EMOTE rows - emote 77 says
        // text group 210 (5-10 s cooldown), 41 says 211, 17 / 101 / 78 play
        // emotes. A text emote at the creature reaches its script; a second
        // one inside the cooldown does not.
        LocalNpcDefinition pebble;pebble.id=3836;pebble.name="Mountaineer Pebblebitty";pebble.health=2138;pebble.level=44;pebble.hostile=false;pebble.damage=0;
        Stage s({pebble},{},{textGroup(3836,210,"Yeah, yeah, keep it moving."),textGroup(3836,211,"I'm watching you.")},0,3.f,0.f,0.f,{{22,3836,0.f,0.f,0.f,0.f}});
        s.step();
        assert(s.act(LocalAction::TextEmote,s.g.npcs()[0].guid,77));
        s.step();
        assert(s.g.scriptDialogues().size()==1&&s.g.scriptDialogues()[0].text=="Yeah, yeah, keep it moving.");
        assert(s.act(LocalAction::TextEmote,s.g.npcs()[0].guid,77));
        s.step();
        assert(s.g.scriptDialogues().size()==1);
        assert(s.act(LocalAction::TextEmote,s.g.npcs()[0].guid,41));
        s.step();
        assert(s.g.scriptDialogues().size()==2&&s.g.scriptDialogues()[1].text=="I'm watching you.");
        for(int i=0;i<44;++i)s.step();
        assert(s.act(LocalAction::TextEmote,s.g.npcs()[0].guid,77));
        s.step();
        assert(s.g.scriptDialogues().size()==3);
        std::cout<<"PASS RECEIVE_EMOTE: Pebblebitty answered two emotes and kept the 5-10 s cooldown\n";
    }
    {
        // The default page: a vendor with UNIT_NPC_FLAG_GOSSIP and no menu of
        // its own shows menu 0's options that match its flags (the goods); a
        // quest giver without the gossip flag shows the quest list instead.
        LocalNpcDefinition grocer;grocer.id=151;grocer.name="Brog Hamfist";grocer.health=1000;grocer.level=30;grocer.hostile=false;grocer.damage=0;
        LocalNpcDefinition giver;giver.id=823;giver.name="Deputy Willem";giver.health=417;giver.level=18;giver.hostile=false;giver.questGiver=true;giver.damage=0;
        Stage::Gossip gossip;
        {
            LocalGossipMenu zero;zero.id=0;
            LocalGossipOption goods;goods.id=1;goods.icon=1;goods.type=3;goods.npcFlag=128;goods.text="I want to browse your goods";zero.options.push_back(goods);
            LocalGossipOption bank;bank.id=7;bank.icon=6;bank.type=9;bank.npcFlag=131072;bank.text="Show me my bank";zero.options.push_back(bank);
            gossip.menus.push_back(zero);
            gossip.owners.push_back({151,0,129});gossip.owners.push_back({823,0,2});
        }
        Stage s({grocer,giver},{},{},0,3.f,0.f,0.f,{{23,151,0.f,0.f,0.f,0.f},{24,823,6.f,0.f,0.f,0.f}},{},{},gossip);
        {auto state=s.g.npcs();for(auto& m:state)if(m.entry==151)m.vendor=true;s.g.setRemoteNpcs(state);}
        s.step();
        const auto* grocerNpc=npcOf(s.g,0xf130000000000000ULL|23);const auto* giverNpc=npcOf(s.g,0xf130000000000000ULL|24);assert(grocerNpc&&giverNpc);
        assert(s.act(LocalAction::Interact,grocerNpc->guid));
        assert(s.p.gossip.open()&&s.p.gossip.menuId==0&&!s.p.gossip.questMenu&&s.p.gossip.options.size()==1&&s.p.gossip.options[0].type==3&&s.p.gossip.textId==kLocalGossipDefaultText);
        assert(s.act(LocalAction::Interact,giverNpc->guid));
        assert(s.p.gossip.open()&&s.p.gossip.npcGuid==giverNpc->guid&&s.p.gossip.questMenu&&s.p.gossip.options.empty());
        std::cout<<"PASS gossip: the default options of a flagged vendor, the quest list of a plain quest giver\n";
    }
}
