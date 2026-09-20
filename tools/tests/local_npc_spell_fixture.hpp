#pragma once
#include "local_group_rewards_fixture.hpp"
#include "game/local_npc_spell_runtime.hpp"
#include "game/local_npc_spell_profiles.hpp"
#include "game/local_proc_rules.hpp"
#include <iostream>

// Public authority simulation with the exact three admitted AI profiles. The
// separate DBC/source audit establishes admission; these fixtures isolate event
// timing, recipient ownership and lifecycle behavior without requiring client data.
struct NpcSpellFixture {
    std::shared_ptr<LocalWorldContent> c=rewardContent();LocalGameplay game;
    LocalRealmPlayer p=rewardPlayer(1),q=rewardPlayer(2);LocalRealmNpc n=rewardNpc();
    explicit NpcSpellFixture(uint32_t entry=4008) {
        const auto* profile=localNpcSpellProfile(entry);assert(profile);
        p.level=q.level=20;p.classId=q.classId=8;p.health=q.health=1000;p.maxHealth=q.maxHealth=1000;
        p.x=12;q.x=13;p.mana=q.mana=0;
        c->npcs[0].id=entry;c->npcs[0].level=20;c->npcs[0].health=100000;c->npcs[0].damage=0;
        LocalSpellDefinition d;d.id=profile->spellId;d.name="Admitted NPC spell fixture";d.clientSpell=true;d.npcOnly=true;
        d.sourceDamageClass=1;d.schoolMask=d.id==5401?8:4;d.sourceRawCastTimeMs=d.castTimeMs=d.id==5401?2000:3000;
        d.range=d.id==5401?30:40;d.damage=d.id==5401?8:64;d.damageMax=d.id==5401?12:86;
        d.sourceProjectileSpeed=d.id==5401?0:24;d.baseLevel=d.id==5401?5:20;d.damagePerLevel=d.id==5401?1.4f:0;
        c->spells.push_back(d);n.entry=entry;n.level=20;n.health=n.maxHealth=100000;n.targetGuid=p.guid;n.threat[0]={p.guid,100000};n.attackTimer=10000;
        game.useContent(c);game.tick(0,{&p,&q});game.setRemoteNpcs({n});
    }
    void tick(unsigned times=1) {for(unsigned i=0;i<times;++i)game.tick(.25f,{&p,&q});}
    void arm(uint32_t remaining=250,bool launched=false) {
        auto copy=game.npcs()[0];copy.npcSpellTimerInitialized=true;copy.npcSpellTimerMs=9000;
        copy.npcCastingSpellId=localNpcSpellProfile(copy.entry)->spellId;copy.npcCastTargetGuid=p.guid;
        copy.npcCastRemainingMs=remaining;copy.npcSpellLaunched=launched;game.setRemoteNpcs({copy});
    }
    std::vector<LocalCombatEvent> spells()const {
        std::vector<LocalCombatEvent> result;const auto id=localNpcSpellProfile(n.entry)->spellId;
        for(const auto& e:game.combatEvents())if(e.spell==id&&e.source==n.guid)result.push_back(e);
        return result;
    }
};
