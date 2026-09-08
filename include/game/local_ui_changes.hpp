#pragma once
#include "game/local_gameplay.hpp"
namespace wowee::game {
// Input to the local FrameXML bridge. Avoid synthetic BAG/QUEST notifications
// on every polling tick: those handlers rebuild entire retail panels.
struct LocalUiChanges {
    enum : unsigned { Life=1, Power=2, Experience=4, Money=8, Bags=16, Quests=32, Spells=64, Cooldowns=128, Target=256, Mounts=512 };
    bool initialized=false;
    uint32_t health=0,maxHealth=0,power=0,maxPower=0,xp=0,xpMax=0,money=0;
    uint8_t level=0,resource=0;
    bool dead=false;
    uint64_t target=0;
    uint32_t targetHealth=0,targetMaxHealth=0,gcd=0,mountSpell=0;
    size_t rewarded=0;
    std::vector<LocalItemStack> bags;
    std::array<uint32_t,kLocalEquipmentSlotCount> equipment{};
    std::vector<LocalQuestProgress> quests;
    std::vector<uint32_t> spells;
    std::vector<LocalCooldown> cooldowns;
    unsigned observe(const LocalRealmPlayer& p,uint64_t t,uint32_t hp,uint32_t maxHp) {
        unsigned out=initialized?0u:1023u;
        if(health!=p.health || maxHealth!=p.maxHealth || dead!=p.dead) out|=Life;
        if(power!=p.mana || maxPower!=p.maxMana || resource!=uint8_t(p.resourceType)) out|=Power;
        if(xp!=p.xp || xpMax!=p.xpToLevel || level!=p.level) out|=Experience;
        if(money!=p.money) out|=Money;
        bool same=bags.size()==p.inventory.size() && equipment==p.equipment;
        for(size_t i=0;same && i<bags.size();++i) same=bags[i].itemId==p.inventory[i].itemId && bags[i].count==p.inventory[i].count;
        if(!same) out|=Bags;
        same=quests.size()==p.quests.size() && rewarded==p.completedQuestIds.size();
        for(size_t i=0;same && i<quests.size();++i) same=quests[i].id==p.quests[i].id && quests[i].status==p.quests[i].status && quests[i].progress==p.quests[i].progress;
        if(!same) out|=Quests;
        if(spells!=p.knownSpells) out|=Spells;
        if(mountSpell!=p.mountSpellId)out|=Mounts;
        same=gcd==p.globalCooldownMs && cooldowns.size()==p.cooldowns.size();
        for(size_t i=0;same && i<cooldowns.size();++i) same=cooldowns[i].spellId==p.cooldowns[i].spellId && cooldowns[i].remainingMs==p.cooldowns[i].remainingMs;
        if(!same) out|=Cooldowns;
        if(target!=t || targetHealth!=hp || targetMaxHealth!=maxHp) out|=Target;
        initialized=true;
        health=p.health;maxHealth=p.maxHealth;dead=p.dead;
        power=p.mana;maxPower=p.maxMana;resource=uint8_t(p.resourceType);
        xp=p.xp;xpMax=p.xpToLevel;level=p.level;money=p.money;
        if(out&Bags){bags=p.inventory;equipment=p.equipment;}
        if(out&Quests){quests=p.quests;rewarded=p.completedQuestIds.size();}
        if(out&Spells)spells=p.knownSpells;
        if(out&Cooldowns){cooldowns=p.cooldowns;gcd=p.globalCooldownMs;}
        mountSpell=p.mountSpellId;
        target=t;targetHealth=hp;targetMaxHealth=maxHp;
        return out;
    }
};
}
