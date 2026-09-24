#include "game/local_spell_amount.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace wowee::game;

namespace wowee::game {
const LocalSpellDefinition* LocalWorldContent::spell(uint32_t id) const {
    auto it=std::lower_bound(spells.begin(),spells.end(),id,[](const auto& a,uint32_t b){return a.id<b;});
    return it!=spells.end()&&it->id==id?&*it:nullptr;
}
}

static LocalPassiveCastModifier mod(uint8_t op,bool pct,int amount){
    LocalPassiveCastModifier m{};m.operation=op;m.percentage=pct;m.active=true;m.amount=amount;m.mask={1,0,0};return m;
}
static LocalSpellDefinition talent(uint32_t talentId,uint8_t op,bool pct,int amount){
    LocalSpellDefinition t{};t.id=1000+talentId;t.talentId=talentId;t.talentRank=1;t.passive=true;
    t.allowableClasses=1u<<(8-1);t.spellFamily=3;t.spellFamilyFlags={1,0,0};t.passiveCastModifiers[0]=mod(op,pct,amount);return t;
}
int main(){
    LocalWorldContent c;
    LocalSpellDefinition cast{};cast.id=42;cast.spellFamily=3;cast.spellFamilyFlags={1,0,0};
    cast.directEffectSlot=0;cast.periodicEffectSlot=2;cast.durationMs=10000;cast.globalCooldownMs=1500;
    c.spells={cast,talent(1,16,false,4),talent(2,7,false,5),talent(3,1,true,20),talent(4,21,false,-500),
              talent(5,3,false,25),talent(6,23,true,50)};
    std::sort(c.spells.begin(),c.spells.end(),[](auto&a,auto&b){return a.id<b.id;});
    LocalRealmPlayer p{};p.classId=8;p.level=80;p.health=p.maxHealth=1000;
    p.talents={{1,1},{2,1},{3,1},{4,1},{5,1},{6,1}};
    const auto* d=c.spell(42);assert(d);
    assert(localTalentCastModifier(p,c,*d,16,false)==4); // regression: positive hit must not clamp to zero
    assert(localTalentCastModifier(p,c,*d,7,false)==5);
    assert(localSpellDuration(p,c,*d)==12000);
    assert(localSpellGlobalCooldown(p,c,*d)==1000);
    assert(localSpellEffectAmountAfterTalents(p,c,*d,100,false)==125); // effect 1 flat
    assert(localSpellEffectAmountAfterTalents(p,c,*d,100,true)==150);  // effect 3 percent
    std::cout<<"PASS spell modifier runtime: hit, crit, duration, gcd, effect1/effect3\n";
}
