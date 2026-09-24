#include "game/local_melee.hpp"
#include "game/local_aura_presentation.hpp"
#include "game/local_class_pools.hpp"
#include "game/local_regeneration_rates.hpp"
#include "game/local_forms.hpp"
#include "game/local_form_boosts.hpp"
#include "game/local_talents.hpp"
#include "game/local_stat_auras.hpp"
#include "game/local_proc_talents.hpp"
#include "game/local_reactive_talents.hpp"
#include "game/local_feral_talents.hpp"
#include "game/local_proc_lifecycle.hpp"
#include "game/local_pet.hpp"
#include <cmath>
#include <iterator>
#include <limits>
namespace wowee::game {
namespace {
constexpr LocalMeleeItem items[]={
#include "game/local_melee_items_generated.inc"
};
struct EquipmentBonusArmor {uint32_t id;float amount;};
constexpr EquipmentBonusArmor equipmentBonusArmor[]={
#include "game/local_equipment_bonus_armor_generated.inc"
};
struct ClassPool {uint8_t clazz,level;uint32_t health,mana;};
constexpr ClassPool classPools[]={
#include "game/local_class_pools_generated.inc"
};
struct ItemPool {uint32_t id;int32_t health,mana;};
constexpr ItemPool itemPools[]={
#include "game/local_resource_items_generated.inc"
};
struct RegenRatio {uint8_t clazz,level;float healthBase,healthMore,mana;};
constexpr RegenRatio regenRatios[]={
#include "game/local_regeneration_ratios_generated.inc"
};
struct RegenItem {uint32_t id;int32_t manaPer5,healthPer5;};
constexpr RegenItem regenItems[]={
#include "game/local_regeneration_items_generated.inc"
};
const ClassPool* classPool(const LocalRealmPlayer& p){
    const auto key=unsigned(p.classId)*100+p.level;
    const auto it=std::lower_bound(std::begin(classPools),std::end(classPools),key,[](const auto& a,unsigned b){return unsigned(a.clazz)*100+a.level<b;});
    return it!=std::end(classPools)&&unsigned(it->clazz)*100+it->level==key?it:nullptr;
}
struct Level {uint8_t clazz,level;std::array<int32_t,5> stats;};
constexpr Level levels[]={
#include "game/local_melee_levels_generated.inc"
};
struct Race {uint8_t race;std::array<int32_t,5> stats;};
constexpr Race races[]={
#include "game/local_melee_races_generated.inc"
};
struct Ratio {uint8_t clazz,level;std::array<float,10> values;};
constexpr Ratio ratios[]={
#include "game/local_melee_ratios_generated.inc"
};
struct SpellCritRatio {uint8_t clazz,level;float base,intellect,ratingToPercent,rangedRatingToPercent;};
constexpr SpellCritRatio spellCritRatios[]={
#include "game/local_spell_crit_ratios_generated.inc"
};
struct SpellCritItem {uint32_t id;int32_t rating,rangedRating;};
constexpr SpellCritItem spellCritItems[]={
#include "game/local_spell_crit_items_generated.inc"
};
struct Npc {uint32_t id,flags,type,rank;};constexpr Npc npcs[]={
#include "game/local_melee_npcs_generated.inc"
};
const LocalMeleeItem* worn(const LocalRealmPlayer& p,const LocalWorldContent& c,size_t slot){
    const auto id=p.equipment[slot];if(!id)return nullptr;const auto* item=c.item(id);
    if(!item||!localEquipmentFits(item->inventoryType,item->slot,slot))return nullptr;
    uint64_t copies=0;for(const auto& stack:p.inventory)if(stack.itemId==id)copies+=stack.count;
    if(uint64_t(std::count(p.equipment.begin(),p.equipment.begin()+slot+1,id))>copies)return nullptr;
    return localMeleeItem(id);
}
const LocalSpellDefinition* activeMoltenArmor(const LocalRealmPlayer& p,const LocalWorldContent& c){
    if(!p.guid||p.dead||!p.health||p.classId!=8||p.statAuras.size()>kLocalMaxStatAuras)return nullptr;
    const LocalSpellDefinition* active=nullptr;
    for(const auto& aura:p.statAuras){
        const auto* d=c.spell(aura.spellId);
        if(!d||(d->id!=30482&&d->id!=43045&&d->id!=43046)||!d->unsupportedReason.empty()||
           d->passive||d->triggeredOnly||!d->mageArmorGroup||d->spiritCritRatingPct!=35||
           d->incomingCritReductionPct!=5||!localHasTimedAura(*d)||!validLocalProc(*d)||
           !aura.remainingMs||aura.remainingMs>d->durationMs||aura.mapId!=p.mapId||
           aura.instanceId!=p.instanceId||aura.casterGuid!=p.guid||aura.stacks!=1||
           aura.procCharges>d->proc.charges||(d->proc.charges&&!aura.procCharges)||
           aura.absorbRemaining>1000000||aura.procCooldownMs>60000||aura.manaRegenRemainder>=5000||
           aura.procAmountSnapshot>1000000||(aura.hasProcAmountSnapshot!=(aura.procAmountSnapshot!=0)))continue;
        const auto* child=c.spell(d->proc.spellId);
        if(!child||!child->triggeredOnly||!child->unsupportedReason.empty()||child->sourceDamageClass!=1||
           child->sourceCantCrit||child->damage!=d->proc.amount||child->schoolMask!=4||
           child->spellFamily!=3||child->spellFamilyFlags!=std::array<uint32_t,3>{0,8,0})continue;
        // Armor ranks share one group. Never add stale or duplicated ranks.
        if(active)return nullptr;
        active=d;
    }
    return active;
}
bool front(float x,float y,float orientation,float tx,float ty){return (tx-x)*std::cos(orientation)+(ty-y)*std::sin(orientation)>=0;}
float diminish(float value,float cap,float k){return value>0?value*cap/(value+cap*k):0;}
constexpr float dodgeBase[]={.036640f,.034943f,-.040873f,.020957f,.034178f,.036640f,.021080f,.036587f,.024211f,0,.056097f};
constexpr float dodgeScale[]={.85f,1,1.11f,2,1,.85f,1.6f,1,.97f,0,2};
constexpr float dodgeCap[]={88.129021f,88.129021f,145.560408f,145.560408f,150.375940f,88.129021f,145.560408f,150.375940f,150.375940f,0,116.890707f};
constexpr float parryCap[]={47.003525f,47.003525f,145.560408f,145.560408f,0,47.003525f,145.560408f,0,0,0,0};
constexpr float k[]={.956f,.956f,.988f,.988f,.983f,.956f,.988f,.983f,.983f,0,.972f};
}
const LocalMeleeItem* localMeleeItem(uint32_t id){auto it=std::lower_bound(std::begin(items),std::end(items),id,[](const auto& a,uint32_t b){return a.id<b;});return it!=std::end(items)&&it->id==id?it:nullptr;}
uint32_t localNpcMeleeFlags(uint32_t id){auto it=std::lower_bound(std::begin(npcs),std::end(npcs),id,[](const auto& a,uint32_t b){return a.id<b;});return it!=std::end(npcs)&&it->id==id?it->flags:0;}
uint32_t localNpcCreatureType(uint32_t id){auto it=std::lower_bound(std::begin(npcs),std::end(npcs),id,[](const auto& a,uint32_t b){return a.id<b;});return it!=std::end(npcs)&&it->id==id?it->type:0;}
bool localNpcIsDemonOrUndead(uint32_t id){const auto type=localNpcCreatureType(id);return type==3||type==6;}
bool localNpcExperienceTargetEligible(uint32_t id,uint8_t actorLevel,uint8_t targetLevel) {
    if(!actorLevel||!targetLevel||targetLevel<=localProcGrayLevel(actorLevel))return false;
    const auto it=std::lower_bound(std::begin(npcs),std::end(npcs),id,[](const auto& a,uint32_t b){return a.id<b;});
    // Every current LocalRealmNpc is an ordinary creature, never a player pet.
    // Reject unknown templates because their NO_XP and creature-type metadata
    // cannot be proven. Totem11, Critter8, flags_extra NO_XP0x40 are upstream.
    return it!=std::end(npcs)&&it->id==id&&it->type!=8&&it->type!=11&&!(it->flags&0x40u);
}
// Armor is derived from already-computed attributes, so Armor -> AP never
// re-enters localMeleeStats through the public armor accessor.
static uint32_t armorFromAttributes(const LocalRealmPlayer& p,const LocalWorldContent& c,int32_t agility){
    uint64_t baseArmor=0;float flatArmor=0;
    for(size_t slot=0;slot<p.equipment.size();++slot){const auto id=p.equipment[slot];const auto* item=id?c.item(id):nullptr;
        if(!item||!localEquipmentFits(item->inventoryType,item->slot,slot))continue;
        uint64_t copies=0;for(const auto& stack:p.inventory)if(stack.itemId==id)copies+=stack.count;
        if(uint64_t(std::count(p.equipment.begin(),p.equipment.begin()+slot+1,id))>copies)continue;
        const auto* metadata=localMeleeItem(id);
        const bool base=metadata&&metadata->itemClass==4&&
            ((metadata->subclass>=1&&metadata->subclass<=4)||metadata->subclass==6);
        const auto it=std::lower_bound(std::begin(equipmentBonusArmor),std::end(equipmentBonusArmor),id,
            [](const auto& a,uint32_t b){return a.id<b;});
        const float extra=it!=std::end(equipmentBonusArmor)&&it->id==id?it->amount:0.f;
        const auto adjusted=item->armor?uint64_t(std::max(int64_t(0),int64_t(item->armor)-int32_t(extra))):0;
        if(base)baseArmor+=adjusted;else flatArmor+=adjusted;
        flatArmor+=std::max(0.f,extra);
    }
    // Player::_ApplyItemBonuses keeps bonus/miscellaneous armor in TOTAL_VALUE.
    // Bear form and Thick Hide multiply only the equipment BASE_VALUE, before
    // agility, flat equipment armor and Mark of the Wild are added.
    const auto* form=localActiveForm(p);
    const float formMultiplier=form?float(form->armorPercent)/100.f:1.f;
    const float armorMultiplier=formMultiplier*localTalentEquipmentArmorMultiplier(p,c);
    // Keep fractional positive weapon modifiers in TOTAL_VALUE too. Source
    // Player::UpdateArmor truncates only after summing the complete float value.
    float totalValue=flatArmor;
    totalValue+=localTalentBonus(p,c,true);
    totalValue+=localStatAuraBonus(p,c,true);
    // Creature MOD_RESISTANCE on armor (HandleAuraModResistance): TOTAL_VALUE,
    // one resolved amount per aura as the authority applied it.
    for(const auto& view:p.harmfulAuras)totalValue+=float(view.armorModifier);
    float armor=float(baseArmor)*armorMultiplier;
    // Preserve source float addition order: agility precedes TOTAL_VALUE.
    // Reordering fractional equipment bonuses can change integer armor by one.
    armor+=float(std::max(0,agility))*2;
    armor+=totalValue;
    // Creature MOD_RESISTANCE_PCT on armor: TOTAL_PCT, one multiplier per aura.
    for(const auto& view:p.harmfulAuras)if(view.armorPercent)armor*=float(100+view.armorPercent)/100.f;
    return uint32_t(std::clamp(armor,0.f,1000000.f));
}
static bool activeStatTalent(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition* d){
    return d&&d->passive&&d->unsupportedReason.empty()&&!p.dead&&p.classId>=1&&p.classId<=11&&
        validLocalTalents(p)&&(d->allowableClasses&(1u<<(p.classId-1)))&&localTalentPrerequisitesReady(p,c,*d);
}
static float offhandTalentMultiplier(const LocalRealmPlayer& p,const LocalWorldContent& c){
    float factor=1;
    for(auto [id,rank]:p.talents)if(const auto* d=localTalentSpell(c,id,rank);activeStatTalent(p,c,d)&&d->passiveOffhandDamagePct)
        factor*=1+float(d->passiveOffhandDamagePct)/100;
    return factor;
}
static float weaponHitTalentBonus(const LocalRealmPlayer& p,const LocalWorldContent& c,bool offhandUsable,bool feral){
    if(feral)return 0;
    float amount=0;
    for(auto [id,rank]:p.talents){
        const auto* d=localTalentSpell(c,id,rank);
        if(!activeStatTalent(p,c,d)||!d->passiveWeaponHitPct)continue;
        bool matches=d->requiredItemClass<0;
        for(size_t slot:{size_t(15),size_t(16),size_t(17)}){
            if(slot==16&&!offhandUsable)continue;
            const auto* item=worn(p,c,slot);
            if(!item||item->itemClass!=2)continue;
            if(d->requiredItemClass>=0&&item->itemClass!=uint32_t(d->requiredItemClass))continue;
            if(d->requiredItemSubclasses&&(item->subclass>=32||!(d->requiredItemSubclasses&(1u<<item->subclass))))continue;
            if(d->requiredInventoryTypes&&(item->inventoryType>=32||!(d->requiredInventoryTypes&(1u<<item->inventoryType))))continue;
            matches=true;
        }
        if(matches)amount+=d->passiveWeaponHitPct;
    }
    return std::min(100.f,amount);
}
LocalMeleeStats localMeleeStats(const LocalRealmPlayer& p,const LocalWorldContent& c){
    LocalMeleeStats s;if(p.classId<1||p.classId>11||p.classId==10)return s;
    const auto level=std::clamp(unsigned(p.level),1u,80u);const auto key=unsigned(p.classId)*100+level;
    auto base=std::lower_bound(std::begin(levels),std::end(levels),key,[](const auto& a,unsigned b){return unsigned(a.clazz)*100+a.level<b;});
    if(base==std::end(levels)||unsigned(base->clazz)*100+base->level!=key)return s;
    s.base=base->stats;s.sourceStats=true;
    for(const auto& r:races)if(r.race==p.race)for(size_t i=0;i<5;++i)s.base[i]+=r.stats[i];
    s.attributes=s.base;int64_t ap=0,blockValue=0;
    for(size_t slot=0;slot<19;++slot)if(const auto* item=worn(p,c,slot)){
        // Scaling/heirloom tables and item-spell effects are tracked separately.
        if(item->scaling)continue;
        for(size_t i=0;i<5;++i)s.attributes[i]=std::clamp(s.attributes[i]+item->stats[i],0,1000000);
        ap+=item->stats[5];blockValue+=item->stats[14];
        for(size_t i=0;i<8;++i)s.ratings[i]=std::clamp(s.ratings[i]+item->stats[6+i],0,1000000);
    }
    // The active form's boost spells multiply total stats exactly as a talent
    // does (aura 137; SpellAuraEffects.cpp:1350-1445 casts them on entry), so
    // the two multipliers compose in one truncation, the way the source's
    // single UNIT_FIELD_STAT write does.
    for(size_t i=0;i<s.attributes.size();++i)
        s.attributes[i]=int32_t(std::clamp(float(s.attributes[i])*localTalentTotalStatMultiplier(p,c,i)*
                                           localFormBoostStatMultiplier(p,c,i),0.f,1000000.f));
    const auto str=s.attributes[0],agi=s.attributes[1];
    const auto* form=localActiveForm(p);const bool druidFeral=form&&(form->form==1||form->form==5||form->form==8);
    const bool feral=druidFeral||(form&&form->form==16);
    switch(p.classId){case 1:case 2:case 6:ap+=3*level+2*str-20;break;case 3:case 4:case 7:ap+=2*level+str+agi-20;break;case 11:ap+=2*str-20+(feral&&form->form==1?agi:0);break;default:ap+=str-10;}
    // The flat aura-99 term the form's boost spells carry. Three of the ten
    // boosts serving the nine modelled forms have one - Cat 3025, Bear 1178 and
    // Dire Bear 9635 - and previously all three were simply absent, which cost
    // a level-80 naked druid 160 / 120 / 240 attack power. Bear's 1178 has
    // MaxLevel 40, so its term plateaus at 120 from level 40 up.
    ap+=int64_t(localFormBoostAttackPower(p,c));
    const auto* mh=worn(p,c,15);const auto* oh=worn(p,c,16);
    if(druidFeral&&mh&&mh->itemClass==2&&mh->delay&&!mh->scaling){const auto dps=(mh->damage[0]+mh->damage[1]+mh->damage[2]+mh->damage[3])*500/mh->delay;ap+=std::max(0,int(dps*14)-767);}
    const auto armor=armorFromAttributes(p,c,agi);
    for(auto [id,rank]:p.talents)if(const auto* d=localTalentSpell(c,id,rank);activeStatTalent(p,c,d)&&d->passiveArmorAttackPowerDivisor)
        ap+=armor/d->passiveArmorAttackPowerDivisor;
    float intellectAttackPower=0;
    for(auto [id,rank]:p.talents)if(const auto* d=localTalentSpell(c,id,rank);activeStatTalent(p,c,d)&&d->passiveIntellectAttackPowerPct)
        intellectAttackPower+=float(std::max(0,s.attributes[3]))*d->passiveIntellectAttackPowerPct/100.f;
    // Source accumulates fractional stat-based AP in float, then truncates the
    // TOTAL_VALUE field once. Ranged AP has its own independent source formula.
    // MOD_ATTACK_POWER from creature views (a demoralizing debuff) enters the
    // same TOTAL_VALUE (HandleAuraModAttackPower).
    s.attackPower=float(std::clamp(ap+int64_t(intellectAttackPower)+int64_t(localPlayerViewModifiers(p,1).attackPower),int64_t(0),int64_t(1000000)));
    // Local training baseline; additional proficiency grants remain separate work.
    bool talentDual=false,talentParry=false;
    uint32_t dualHit=0;
    for(auto [id,rank]:p.talents)if(const auto* d=localTalentSpell(c,id,rank);activeStatTalent(p,c,d)) {
        talentDual=talentDual||d->passiveCanDualWield;talentParry=talentParry||d->passiveCanParry;
        dualHit+=d->passiveDualWieldHitPct;
    }
    const bool dual=talentDual||(level>=20&&(p.classId==1||p.classId==3||p.classId==4))||p.classId==6;
    s.offHand=!feral&&dual&&oh&&oh->itemClass==2&&(!mh||mh->inventoryType!=17);
    const bool canParry=talentParry||(p.classId==1&&level>=6)||(p.classId==2&&level>=8)||(p.classId==3&&level>=20)||(p.classId==4&&level>=12)||p.classId==6;
    // Ghost Wolf hides usable weapons, not armor: the source block check uses
    // GetUseableItemByPos/GetShield and retains a valid equipped shield.
    const bool shield=oh&&oh->itemClass==4&&oh->subclass==6&&oh->block&&!druidFeral&&(p.classId==1||p.classId==2||p.classId==7);
    const auto idx=p.classId-1;const auto& ratio=ratios[(p.classId==11?9:idx)*80+level-1].values;
    if(const auto* armor=activeMoltenArmor(p,c)){
        const auto rating=int32_t(int64_t(std::max(0,s.attributes[4]))*armor->spiritCritRatingPct/100);
        s.ratings[5]=std::clamp(s.ratings[5]+rating,0,1000000);
        s.incomingCritReductionPct=armor->incomingCritReductionPct;
    }
    auto& bonus=s.ratingBonus;for(size_t i=0;i<8;++i)bonus[i]=s.ratings[i]*ratio[2+i];
    s.talentWeaponHitPct=weaponHitTalentBonus(p,c,s.offHand,feral);
    // Aura54 from Dual Wield Specialization is conditional on an actually
    // usable owned offhand weapon; merely learning the talent is insufficient.
    if(s.offHand)s.talentWeaponHitPct+=std::min(100u,dualHit);
    s.defense=std::floor(bonus[0]);s.hit=bonus[4]+s.talentWeaponHitPct;s.crit=std::max(0.f,(ratio[0]+agi*ratio[1])*100+bonus[5]);s.haste=bonus[6];s.expertise=std::floor(bonus[7])*.25f;
    s.offHandCrit=s.crit;
    s.crit+=!feral && mh && mh->itemClass==2?
        localTalentWeaponCritPct(p,c,mh->itemClass,mh->subclass,mh->inventoryType):
        localTalentWeaponCritPct(p,c,UINT32_MAX,UINT32_MAX,UINT32_MAX);
    s.offHandCrit+=s.offHand?localTalentWeaponCritPct(p,c,oh->itemClass,oh->subclass,oh->inventoryType):
        localTalentWeaponCritPct(p,c,UINT32_MAX,UINT32_MAX,UINT32_MAX);
    s.crit+=localFeralCritPct(p,c);s.offHandCrit+=localFeralCritPct(p,c);
    // Aura 52 on a form boost has EquippedItemClass -1, so it applies to both
    // hands and to an unarmed form attack. Berserker Stance's 7381 is the only
    // producer among the nine modelled forms: +3%.
    {const auto formCrit=float(localFormBoostCritPct(p,c));s.crit+=formCrit;s.offHandCrit+=formCrit;}
    s.armorPenetrationPct=localFormArmorPenetrationPct(p,c);
    const auto dodgeAgility=100*ratio[1]*dodgeScale[idx]/1.15f;
    // 2.37 creature views on the character: MOD_HIT_CHANCE (aura 54; melee
    // and ranged, m_modMeleeHitChance), MOD_DODGE/PARRY/BLOCK_PERCENT (49/47/51;
    // Player::UpdateDodgePercentage adds them before the floor at zero) and a
    // disarm (aura 67: no useable main hand, so no parry - RollMeleeOutcomeAgainst).
    const auto views=localPlayerViewModifiers(p,1);
    const bool disarmed=views.disarmed;
    s.hit+=float(views.hitChancePct);
    s.dodge=std::max(0.f,100*dodgeBase[idx]+s.base[1]*dodgeAgility+localFeralDodgePct(p,c)+diminish((agi-s.base[1])*dodgeAgility+bonus[1]+s.defense*.04f,dodgeCap[idx],k[idx])+float(views.dodgePct));
    s.parry=canParry&&mh&&mh->itemClass==2&&!disarmed?std::max(0.f,5+diminish(bonus[2]+s.defense*.04f,parryCap[idx],k[idx])+float(views.parryPct)):0;
    s.block=shield?std::max(0.f,5+bonus[3]+s.defense*.04f+float(views.blockPct)):0;s.blockValue=shield?uint32_t(std::max(int64_t(0),blockValue+oh->block+str/2-10)):0;
    s.shieldBlockValue=uint32_t(std::max(int64_t(0),blockValue+(shield?int64_t(oh->block):0)+str/2-10));
    s.missBonus=diminish(s.defense*.04f,16,k[idx]);return s;
}
LocalSpellCritStats localSpellCritStats(const LocalRealmPlayer& p,const LocalWorldContent& c){
    LocalSpellCritStats result;
    if(p.classId<1||p.classId>11||p.classId==10||p.level<1||p.level>80)return result;
    const auto key=unsigned(p.classId)*100+p.level;
    const auto it=std::lower_bound(std::begin(spellCritRatios),std::end(spellCritRatios),key,
        [](const auto& a,unsigned b){return unsigned(a.clazz)*100+a.level<b;});
    const auto stats=localMeleeStats(p,c);
    if(it==std::end(spellCritRatios)||unsigned(it->clazz)*100+it->level!=key||!stats.sourceStats)return result;
    int64_t rating=0;
    for(size_t slot=0;slot<p.equipment.size();++slot)if(const auto* item=worn(p,c,slot);item&&!item->scaling){
        const auto entry=std::lower_bound(std::begin(spellCritItems),std::end(spellCritItems),item->id,
            [](const auto& a,uint32_t b){return a.id<b;});
        if(entry!=std::end(spellCritItems)&&entry->id==item->id)rating+=entry->rating;
    }
    result.sourceStats=true;result.basePct=it->base*100;
    result.intellectPct=std::max(0,stats.attributes[3])*it->intellect*100;
    result.itemRating=int32_t(std::clamp(rating,int64_t(0),int64_t(1000000)));
    if(const auto* armor=activeMoltenArmor(p,c))
        result.auraRating=int32_t(int64_t(std::max(0,stats.attributes[4]))*armor->spiritCritRatingPct/100);
    result.ratingPct=std::min(1000000,result.itemRating+result.auraRating)*it->ratingToPercent;
    result.talentPct=localTalentSpellCritPct(p,c);
    result.crit=std::clamp(result.basePct+result.intellectPct+result.ratingPct+result.talentPct,0.f,100.f);
    return result;
}
float localSpellCritFromIntellect(const LocalRealmPlayer& p,const LocalWorldContent& c){const auto s=localSpellCritStats(p,c);return s.basePct+s.intellectPct;}
float localSpellCritRatingBonus(const LocalRealmPlayer& p,const LocalWorldContent& c){return localSpellCritStats(p,c).ratingPct;}
int32_t localRangedCritRating(const LocalRealmPlayer& p,const LocalWorldContent& c){
    int64_t rating=localSpellCritStats(p,c).auraRating;
    for(size_t slot=0;slot<p.equipment.size();++slot)if(const auto* item=worn(p,c,slot);item&&!item->scaling){
        const auto entry=std::lower_bound(std::begin(spellCritItems),std::end(spellCritItems),item->id,
            [](const auto& a,uint32_t b){return a.id<b;});
        if(entry!=std::end(spellCritItems)&&entry->id==item->id)rating+=entry->rangedRating;
    }
    return int32_t(std::clamp(rating,int64_t(0),int64_t(1000000)));
}
float localRangedCritRatingBonus(const LocalRealmPlayer& p,const LocalWorldContent& c){
    const auto key=unsigned(p.classId)*100+p.level;
    const auto it=std::lower_bound(std::begin(spellCritRatios),std::end(spellCritRatios),key,
        [](const auto& a,unsigned b){return unsigned(a.clazz)*100+a.level<b;});
    return p.level>=1&&p.level<=80&&it!=std::end(spellCritRatios)&&unsigned(it->clazz)*100+it->level==key?
        localRangedCritRating(p,c)*it->rangedRatingToPercent:0;
}
float localRangedCritChance(const LocalRealmPlayer& p,const LocalWorldContent& c){
    const auto stats=localMeleeStats(p,c);
    if(!stats.sourceStats || p.level<1 || p.level>80)return 0;
    const auto& ratio=ratios[(p.classId==11?9:p.classId-1)*80+p.level-1].values;
    const auto* form=localActiveForm(p);
    const bool feral=form&&(form->form==1||form->form==5||form->form==8||form->form==16);
    const auto* weapon=feral?nullptr:worn(p,c,17);
    const auto talent=weapon&&weapon->itemClass==2?
        localTalentWeaponCritPct(p,c,weapon->itemClass,weapon->subclass,weapon->inventoryType):
        localTalentWeaponCritPct(p,c,UINT32_MAX,UINT32_MAX,UINT32_MAX);
    return std::clamp((ratio[0]+stats.attributes[1]*ratio[1])*100+localRangedCritRatingBonus(p,c)+talent+localFeralCritPct(p,c),0.f,100.f);
}
float localSpellCritChance(const LocalRealmPlayer& p,const LocalWorldContent& c,uint32_t schoolMask){
    return schoolMask&&!(schoolMask&~127u)?localSpellCritStats(p,c).crit:0;
}
float localSpellCritChance(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& d){
    const float base=localSpellCritChance(p,c,d.schoolMask);
    if(base<=0.f || d.sourceCantCrit)return 0.f;
    const auto flat=localTalentCastModifier(p,c,d,7,false);
    const auto pct=localTalentCastModifier(p,c,d,7,true);
    return std::clamp((base+float(flat))*float(100+pct)/100.f,0.f,100.f);
}
float localIncomingCritReductionPct(const LocalRealmPlayer& p,const LocalWorldContent& c){
    const auto* armor=activeMoltenArmor(p,c);return armor?float(armor->incomingCritReductionPct):0;
}
uint32_t localClassBaseMana(const LocalRealmPlayer& p){const auto* base=classPool(p);return base?base->mana:0;}
LocalRegenerationRates localRegenerationRates(const LocalRealmPlayer& p,const LocalWorldContent& c){
    LocalRegenerationRates rate;
    const auto key=unsigned(p.classId)*100+p.level;
    const auto it=std::lower_bound(std::begin(regenRatios),std::end(regenRatios),key,[](const auto& a,unsigned b){return unsigned(a.clazz)*100+a.level<b;});
    const auto stats=localMeleeStats(p,c);
    if(it==std::end(regenRatios)||unsigned(it->clazz)*100+it->level!=key||!stats.sourceStats)return rate;
    rate.sourceValues=true;
    const double spirit=std::max(0,stats.attributes[4]);
    rate.healthSpiritPerSecond=std::min(50.0,spirit)*it->healthBase+std::max(0.0,spirit-50)*it->healthMore;
    int64_t mp5=0,hp5=0;
    for(size_t slot=0;slot<p.equipment.size();++slot)if(const auto* item=worn(p,c,slot);item&&!item->scaling){
        const auto entry=std::lower_bound(std::begin(regenItems),std::end(regenItems),item->id,[](const auto& a,uint32_t b){return a.id<b;});
        if(entry!=std::end(regenItems)&&entry->id==item->id){mp5+=entry->manaPer5;hp5+=entry->healthPer5;}
    }
    rate.healthItemPerSecond=double(std::clamp(hp5,int64_t(0),int64_t(1000000)))/5;
    if(localClassBaseMana(p)){
        uint32_t interruptedSpiritPct=0;
        double statManaPerSecond=0;
        // Derive from the confirmed learned rank, not knownSpells or aura icons.
        // Learning, rank replacement and reset therefore affect host and UI
        // together without an additional saved bonus or invalidation cache.
        if(!p.dead&&validLocalTalents(p))for(auto [id,rank]:p.talents){
            const auto* talent=localTalentSpell(c,id,rank);
            if(!talent||!talent->passive||!talent->unsupportedReason.empty()||
               !(talent->allowableClasses&(1u<<(p.classId-1))))continue;
            interruptedSpiritPct=std::min(100u,interruptedSpiritPct+uint32_t(talent->passiveManaRegenInterruptPct));
            for(size_t stat=0;stat<talent->passiveManaRegenStatPct.size();++stat)
                statManaPerSecond+=double(std::max(0,stats.attributes[stat]))*talent->passiveManaRegenStatPct[stat]/500.0;
        }
        rate.manaSpiritCoefficient=spirit*it->mana;
        const double spiritManaPerSecond=std::sqrt(double(std::max(0,stats.attributes[3])))*rate.manaSpiritCoefficient;
        const double fixedManaPerSecond=double(std::clamp(mp5,int64_t(0),int64_t(1000000)))/5+statManaPerSecond;
        // Aura 219 grants stat*percent/500 mana per second in both states.
        // Aura 134 retains only the spirit component during the five-second
        // rule; it never scales equipped MP5 or a timed aura's separate credit.
        // Formula reference: AzerothCore 4e80596c, Player::UpdateManaRegen.
        rate.manaInterruptedPerSecond=std::min(1000000.0,fixedManaPerSecond+spiritManaPerSecond*interruptedSpiritPct/100.0);
        rate.manaPerSecond=std::min(1000000.0,fixedManaPerSecond+spiritManaPerSecond);
    }
    return rate;
}
uint32_t localRegenerationAuraManaPer5(const LocalRealmPlayer& p,const LocalWorldContent& c){
    if(p.dead||!p.health||!localClassBaseMana(p))return 0;
    uint64_t amount=0;
    for(const auto& a:p.statAuras)if(a.remainingMs&&a.mapId==p.mapId&&a.instanceId==p.instanceId)
        if(const auto* d=c.spell(a.spellId);d&&d->unsupportedReason.empty()&&!d->passive&&localHasTimedAura(*d)&&validLocalProc(*d)&&
           (!d->buffAbsorb||a.absorbRemaining)&&(!d->proc.charges||a.procCharges))
            amount+=localStackedAuraAmount(d->manaPer5,std::min(a.stacks,d->maxAuraStacks));
    return uint32_t(std::min(amount,uint64_t(1000000)));
}
LocalResourcePools localResourcePools(const LocalRealmPlayer& p,const LocalWorldContent& c){
    LocalResourcePools pools;const auto* base=classPool(p);const auto stats=localMeleeStats(p,c);
    int64_t flatHealth=0,flatMana=0;
    for(size_t slot=0;slot<p.equipment.size();++slot){const auto id=p.equipment[slot];const auto* item=id?c.item(id):nullptr;
        if(!item||!localEquipmentFits(item->inventoryType,item->slot,slot))continue;
        uint64_t owned=0;for(const auto& stack:p.inventory)if(stack.itemId==id)owned+=stack.count;
        if(uint64_t(std::count(p.equipment.begin(),p.equipment.begin()+slot+1,id))>owned)continue;
        if(const auto* source=localMeleeItem(id)){
            // Source Stamina is already counted through primary attributes.
            // Do not add the old catalog's Stamina*10 approximation a second time.
            if(source->scaling)continue;
            const auto it=std::lower_bound(std::begin(itemPools),std::end(itemPools),id,[](const auto& a,uint32_t b){return a.id<b;});
            if(it!=std::end(itemPools)&&it->id==id){flatHealth+=it->health;flatMana+=it->mana;}
        }else flatHealth+=item->maxHealth; // Explicit custom local item bonus.
    }
    if(base&&stats.sourceStats){
        pools.sourceValues=true;pools.baseHealth=base->health;pools.baseMana=base->mana;
        flatHealth+=base->health+localPrimaryPoolBonus(uint32_t(stats.attributes[2]),10);
        flatMana=base->mana?flatMana+base->mana+int64_t(localPrimaryPoolBonus(uint32_t(stats.attributes[3]),15)):0;
    }else{
        // A custom unsupported class/level retains a bounded legacy fallback.
        pools.baseHealth=100+uint32_t(std::max(uint8_t(1),p.level)-1)*25;
        pools.baseMana=100+uint32_t(std::max(uint8_t(1),p.level)-1)*10;
        flatHealth+=pools.baseHealth;flatMana+=pools.baseMana;
    }
    flatHealth+=uint64_t(localTalentBonus(p,c,false))+localStatAuraBonus(p,c,false);
    pools.health=uint32_t(std::clamp(flatHealth,int64_t(1),int64_t(1000000)));
    pools.mana=uint32_t(std::clamp(flatMana,int64_t(0),int64_t(1000000)));
    return pools;
}
float localMeleeRatingBonus(const LocalRealmPlayer& p,const LocalWorldContent& c,int rating){
    if(rating==9)return localRangedCritRatingBonus(p,c);
    if(rating==10)return localSpellCritRatingBonus(p,c);
    constexpr int indexes[]={1,2,3,4,5,8,17,23};
    for(size_t i=0;i<8;++i)if(indexes[i]==rating)return localMeleeStats(p,c).ratingBonus[i];return 0;
}
uint32_t localMeleeArmor(const LocalRealmPlayer& p,const LocalWorldContent& c){
    return armorFromAttributes(p,c,localMeleeStats(p,c).attributes[1]);
}
float localWeaponTalentDamageMultiplier(const LocalRealmPlayer& p,const LocalWorldContent& c,bool off) {
    const auto* form=localActiveForm(p);
    if(form&&(form->form==1||form->form==5||form->form==8||form->form==16))return 1.f;
    if(off&&!localMeleeStats(p,c).offHand)return 1.f;
    const auto* item=worn(p,c,off?16:15);
    return item&&item->itemClass==2?localTalentWeaponDamageMultiplier(p,c,item->itemClass,item->subclass,item->inventoryType):1.f;
}
LocalWeaponAmounts localWeaponAmounts(const LocalRealmPlayer& p,const LocalWorldContent& c,bool off,bool normalized,bool applyDamageModifiers){
    const auto s=localMeleeStats(p,c);LocalWeaponAmounts a;
    float secondaryLow=0,secondaryHigh=0,secondaryMagicLow=0,secondaryMagicHigh=0;
    if(off&&!s.offHand){a.active=false;a.low=a.high=0;return a;}
    const auto* form=localActiveForm(p);const bool feral=form&&(form->form==1||form->form==5||form->form==8);
    // MOD_DISARM from a creature view (2.37): Player::CanUseAttackType is
    // false for the main hand, so CalculateMinMaxDamage and
    // SetRegularAttackTime read no weapon there (unarmed range, 2 s).
    const auto* item=!off&&localPlayerDisarmed(p)?nullptr:worn(p,c,off?16:15);
    if(feral){a.seconds=form->form==1?1.f:2.5f;a.apSeconds=a.seconds;a.low=std::min(60u,unsigned(p.level))*.85f*a.seconds;a.high=std::min(60u,unsigned(p.level))*1.25f*a.seconds;}
    else if(form&&form->form==16){
        // GetWeaponForAttack(useable=true) returns null in Ghost Wolf. The
        // regular swing becomes 2s, normalized AP uses the unarmed 2.4s, and
        // no offhand, weapon school or secondary damage is available. Source
        // CalculateMinMaxDamage still retains the stored primary weapon range
        // because this form has no explicit attack-speed override in the DBC.
        a.seconds=2;a.apSeconds=normalized?2.4f:2.f;
        if(item&&item->itemClass==2&&!item->scaling&&item->damage[0]<=item->damage[1]){
            a.low=item->damage[0];a.high=item->damage[1];
        }
    }
    else if(item&&item->itemClass==2&&item->delay&&!item->scaling&&item->damage[0]<=item->damage[1]&&item->damage[2]<=item->damage[3]){
        a.low=a.high=0;a.seconds=item->delay/1000.f;
        if(item->school[0]==0){a.low=item->damage[0];a.high=item->damage[1];}
        else{a.magicLow=item->damage[0];a.magicHigh=item->damage[1];}
        if(item->school[1]==0){secondaryLow=item->damage[2];secondaryHigh=item->damage[3];}
        else{secondaryMagicLow=item->damage[2];secondaryMagicHigh=item->damage[3];}
        a.apSeconds=normalized?(item->inventoryType==17?3.3f:item->subclass==15?1.7f:2.4f):a.seconds;
    }else a.apSeconds=normalized?2.4f:a.seconds;
    a.low+=s.attackPower/14*a.apSeconds;a.high+=s.attackPower/14*a.apSeconds;
    if(off){const float factor=.5f*offhandTalentMultiplier(p,c);a.low*=factor;a.high*=factor;a.magicLow*=factor;a.magicHigh*=factor;}
    if(applyDamageModifiers){
        const float weaponFactor=!feral&&!(form&&form->form==16)&&item&&item->itemClass==2?
            localTalentWeaponDamageMultiplier(p,c,item->itemClass,item->subclass,item->inventoryType):1.f;
        const float factor=localTalentPhysicalDamageMultiplier(p,c)*localTimedDamageMultiplier(p,c,false)*weaponFactor;
        a.low*=factor;a.high*=factor;
        // Creature views on the player: MOD_DAMAGE_DONE (physical, TOTAL_VALUE
        // of UNIT_MOD_DAMAGE_MAINHAND) then MOD_DAMAGE_PERCENT_DONE (TOTAL_PCT).
        if(const auto mods=localPlayerViewModifiers(p,1);mods.damageDoneFlat||mods.damageDonePct){
            const float pct=1.f+float(std::max(-99,mods.damageDonePct))/100.f;
            a.low=std::max(0.f,(a.low+float(mods.damageDoneFlat))*pct);a.high=std::max(0.f,(a.high+float(mods.damageDoneFlat))*pct);
        }
    }
    // The source returns secondary item damage before applying offhand/AP
    // modifiers. Keep it outside both the baseline half and specialization.
    a.low+=secondaryLow;a.high+=secondaryHigh;a.magicLow+=secondaryMagicLow;a.magicHigh+=secondaryMagicHigh;
    return a;
}
uint32_t localMeleeSpecialAmount(float attackPower,uint32_t coefficientPct){
    if(!std::isfinite(attackPower)||attackPower<=0||!coefficientPct)return 0;
    return uint32_t(std::min(1000000.0,double(attackPower)*coefficientPct/100.0));
}
bool localRollMeleeSpecialBlock(const LocalRealmPlayer& p,const LocalRealmNpc& n,uint32_t roll){
    if((localNpcMeleeFlags(n.entry)&16)||!front(n.x,n.y,n.orientation,p.x,p.y))return false;
    const float chance=std::clamp(5.f+(int(p.level)-int(n.level))*.2f,0.f,100.f);
    return roll<chance*100;
}
float localMeleeAuraHastePct(const LocalRealmPlayer& p,const LocalWorldContent& c){
    if(!p.guid||p.dead||!validLocalTalents(p)||p.statAuras.size()>kLocalMaxStatAuras)return 0;
    uint32_t percent=0;
    for(const auto& aura:p.statAuras){
        const auto* d=c.spell(aura.spellId);
        if(!aura.remainingMs||aura.mapId!=p.mapId||aura.instanceId!=p.instanceId||
           aura.casterGuid!=p.guid||!d||!d->triggeredOnly||!d->unsupportedReason.empty()||
           !d->meleeHastePct||!validLocalProc(*d)||
           d->proc.effect!=LocalProcEffect::ConsumeOwnerAuraCharge||
           !aura.procCharges||aura.procCharges>d->proc.charges||aura.stacks!=1||
           aura.absorbRemaining>1000000||aura.procCooldownMs>60000||aura.manaRegenRemainder>=5000||
           aura.procAmountSnapshot>1000000||(aura.hasProcAmountSnapshot!=(aura.procAmountSnapshot!=0))||
           aura.remainingMs>d->durationMs||!localProcChildTalentReady(p,c,*d))continue;
        // Dispatch keeps consumed auras until its root ends. An unrelated
        // zero-duration tombstone must not suppress this still-active aura.
        if(std::count_if(p.statAuras.begin(),p.statAuras.end(),[&](const auto& other){
            return other.remainingMs&&other.spellId==aura.spellId;
        })!=1)continue;
        // Flurry is one current-rank aura, never additive copies or old ranks.
        percent=std::max(percent,uint32_t(d->meleeHastePct));
    }
    return float(percent);
}
float localMeleeSpeed(const LocalRealmPlayer& p,const LocalWorldContent& c,bool off){
    const auto a=localWeaponAmounts(p,c,off);if(!a.active)return 0;
    double multiplier=(1+double(localMeleeStats(p,c).haste)/100)*(1+double(localMeleeAuraHastePct(p,c))/100);
    // Unit::ApplyAttackTimePercentMod for a creature's MOD_MELEE_HASTE view:
    // a positive amount divides the attack time, a negative one multiplies it.
    if(const auto viewHaste=localPlayerViewModifiers(p,1).hastePct;viewHaste>0)multiplier*=1+double(viewHaste)/100;
    else if(viewHaste<0)multiplier/=1+double(-viewHaste)/100;
    if(!std::isfinite(a.seconds)||!std::isfinite(multiplier)||a.seconds<=0||multiplier<=0)return 0;
    return float(std::clamp(double(a.seconds)/multiplier,.2,10.0));
}
float localRescaledMeleeTimer(float remaining,float oldPeriod,float newPeriod){
    if(!std::isfinite(remaining)||remaining<=0||!std::isfinite(newPeriod)||newPeriod<=0)return 0;
    const auto boundedPeriod=std::clamp(double(newPeriod),.2,10.0);
    if(!std::isfinite(oldPeriod)||oldPeriod<=0)return float(boundedPeriod);
    // A scheduler or a weapon transition can leave more than one full period
    // remaining. Preserve that fraction too; only bound float overflow.
    return float(std::min(double(remaining)/oldPeriod*boundedPeriod,double(std::numeric_limits<float>::max())));
}
void localRescaleMeleeTimers(LocalRealmPlayer& p,float oldMainPeriod,float oldOffPeriod,const LocalWorldContent& c){
    p.meleePeriodMain=localMeleeSpeed(p,c);p.meleePeriodOff=localMeleeSpeed(p,c,true);
    p.attackTimer=localRescaledMeleeTimer(p.attackTimer,oldMainPeriod,p.meleePeriodMain);
    p.offHandTimer=localRescaledMeleeTimer(p.offHandTimer,oldOffPeriod,p.meleePeriodOff);
}
LocalMeleeOutcome localRollPlayerMelee(const LocalRealmPlayer& p,const LocalRealmNpc& n,const LocalMeleeStats& s,bool special,uint32_t roll,uint32_t criticalRoll,bool offHand,LocalMeleeSpellRules rules){
    const auto flags=localNpcMeleeFlags(n.entry);const float diff=(int(n.level)-int(p.level))*5.f;
    const float crit=offHand?s.offHandCrit:s.crit;
    float sum=0;auto take=[&](float chance){sum+=std::max(0.f,chance)*100;return roll<sum;};
    // The special-attack critical is a separate roll after the hit result
    // (Unit::SpellDoneCritChance runs in Spell::DoAllEffectOnTarget once
    // MeleeSpellHitResult has returned SPELL_MISS_NONE); ALWAYS_HIT skips the
    // hit result entirely (Unit.cpp:3321-3322) and still reaches that roll.
    const auto specialCritical=[&]{return criticalRoll<std::max(0.f,crit-diff*.04f)*100?LocalMeleeOutcome::Critical:LocalMeleeOutcome::Hit;};
    if(special&&rules.alwaysHit)return specialCritical();
    if(take(std::clamp(5+(diff>10?1+(diff-10)*.4f:diff*.1f)+(s.offHand&&!special?19:0)-s.hit,0.f,60.f)))return LocalMeleeOutcome::Miss;
    // SPELL_ATTR0_NO_ACTIVE_DEFENSE: "Same spells cannot be parry/dodge" -
    // MeleeSpellHitResult returns SPELL_MISS_NONE right after the miss roll
    // (Unit.cpp:3354-3355), before the mechanic resist, dodge, parry and block.
    if(special&&rules.noActiveDefense)return specialCritical();
    const auto it=std::lower_bound(std::begin(npcs),std::end(npcs),n.entry,[](const auto& a,uint32_t b){return a.id<b;});
    const bool boss=it!=std::end(npcs)&&it->id==n.entry&&it->rank==3;
    // 2.37: the creature's own MOD_DODGE/PARRY/BLOCK_PERCENT buffs enter
    // GetUnitDodgeChance / GetUnitParryChance / GetUnitBlockChance as
    // GetTotalAuraModifier terms.
    const float buffDodge=float(localNpcBuffTotal(n,&LocalNpcBuff::dodgePct)),buffParry=float(localNpcBuffTotal(n,&LocalNpcBuff::parryPct)),buffBlock=float(localNpcBuffTotal(n,&LocalNpcBuff::blockPct));
    if(take(flags&0x800000?0:(boss?5.85f:5.f)+buffDodge+diff*.04f-s.expertise))return LocalMeleeOutcome::Dodge;
    const bool facing=front(n.x,n.y,n.orientation,p.x,p.y);
    const float npcParry=(it!=std::end(npcs)&&it->id==n.entry?(it->rank==3?13.4f:it->type==7?5.f:0.f):0.f)+buffParry;
    if(take(!facing||(flags&4)||!(npcParry>0)?0:npcParry+diff*.04f-s.expertise))return LocalMeleeOutcome::Parry;
    // Active attacks have a separate crit roll after miss/dodge/parry, unlike white swings.
    if(special){
        // The full block of a COMPLETELY_BLOCKED spell (Unit.cpp:3475-3483):
        // GetUnitBlockChance 5 % - 0.04 per skill point, from the front, not
        // CREATURE_FLAG_EXTRA_NO_BLOCK. No accepted row reaches this arm at the
        // pin (localSpellFullyBlockable); a Block here is a nullified hit, not
        // the partial block of a white swing.
        if(rules.fullBlock&&take(!facing||(flags&16)?0:5+buffBlock+diff*.04f))return LocalMeleeOutcome::Block;
        return specialCritical();
    }
    if(take(!facing||(flags&16)?0:5+buffBlock+diff*.04f))return LocalMeleeOutcome::Block;
    if(take(n.level>p.level?std::min(40.f,10+diff):0))return LocalMeleeOutcome::Glancing;
    if(take(std::max(0.f,crit-diff*.04f)))return LocalMeleeOutcome::Critical;
    return LocalMeleeOutcome::Hit;
}
LocalMeleeOutcome localRollNpcAgainstPet(const LocalRealmNpc& n,const LocalRealmPet& pet,uint32_t roll){
    // Creature attacker against an owned creature. The victim has no defence
    // rating, no shield and - for the beast families this realm can summon -
    // no parry, so only miss, dodge, crushing and the base critical remain.
    const float diff=(int(pet.level)-int(n.level))*5.f;
    float sum=0;auto take=[&](float chance){sum+=std::max(0.f,chance)*100;return roll<sum;};
    if(take(std::clamp(5+(diff>0?diff*.04f:diff*.02f),0.f,60.f)))return LocalMeleeOutcome::Miss;
    if(take(5+diff*.04f))return LocalMeleeOutcome::Dodge;
    if(take(n.level>=pet.level+4?-diff*2-15:0))return LocalMeleeOutcome::Crushing;
    if(take(5-diff*.04f))return LocalMeleeOutcome::Critical;
    return LocalMeleeOutcome::Hit;
}
LocalMeleeOutcome localRollPetMelee(const LocalRealmPet& pet,const LocalRealmNpc& n,uint32_t roll){
    const auto flags=localNpcMeleeFlags(n.entry);const float diff=(int(n.level)-int(pet.level))*5.f;
    float sum=0;auto take=[&](float chance){sum+=std::max(0.f,chance)*100;return roll<sum;};
    // A creature attacker has neither hit rating nor expertise; the victim's
    // avoidance is the same creature table the player path reads.
    if(take(std::clamp(5+(diff>10?1+(diff-10)*.4f:diff*.1f),0.f,60.f)))return LocalMeleeOutcome::Miss;
    const auto it=std::lower_bound(std::begin(npcs),std::end(npcs),n.entry,[](const auto& a,uint32_t b){return a.id<b;});
    const bool boss=it!=std::end(npcs)&&it->id==n.entry&&it->rank==3;
    const float buffDodge=float(localNpcBuffTotal(n,&LocalNpcBuff::dodgePct)),buffParry=float(localNpcBuffTotal(n,&LocalNpcBuff::parryPct)),buffBlock=float(localNpcBuffTotal(n,&LocalNpcBuff::blockPct));
    if(take(flags&0x800000?0:(boss?5.85f:5.f)+buffDodge+diff*.04f))return LocalMeleeOutcome::Dodge;
    const bool facing=front(n.x,n.y,n.orientation,pet.x,pet.y);
    const float npcParry=(it!=std::end(npcs)&&it->id==n.entry?(it->rank==3?13.4f:it->type==7?5.f:0.f):0.f)+buffParry;
    if(take(!facing||(flags&4)||!(npcParry>0)?0:npcParry+diff*.04f))return LocalMeleeOutcome::Parry;
    if(take(!facing||(flags&16)?0:5+buffBlock+diff*.04f))return LocalMeleeOutcome::Block;
    if(take(pet.level>=n.level+4&&!(flags&32)?-diff*2-15:0))return LocalMeleeOutcome::Crushing;
    if(take(5-diff*.04f))return LocalMeleeOutcome::Critical;
    return LocalMeleeOutcome::Hit;
}
// Unit::MeleeSpellHitResult (Unit.cpp:3316-3486) for a creature's melee or
// ranged special against a player, roll in 0..10000. MeleeSpellMissChance keeps the
// victim's base miss and defense terms but never the dual-wield penalty; a
// creature has no expertise or hit auras. Crushing blows and crits do not
// exist for spells (SpellDoneCritChance returns -100 for creatures).
LocalMeleeOutcome localRollNpcMeleeSpell(const LocalRealmNpc& n,const LocalRealmPlayer& p,const LocalMeleeStats& s,
                                         const LocalSpellDefinition& d,uint32_t roll){
    if(d.sourceAlwaysHit)return LocalMeleeOutcome::Hit;
    // skillDiff = creature weapon skill - victim max skill = (n - p) * 5.
    const float diff=(int(p.level)-int(n.level))*5.f;
    // 2.37: MOD_HIT_CHANCE on the creature (m_modMeleeHitChance) lowers its
    // miss chance before the clamp (Unit::MeleeSpellMissChance).
    const float miss=d.sourceNoAttackMiss?0.f:std::clamp(5+(diff>0?diff*.04f:diff*.02f)+s.missBonus-float(localNpcBuffTotal(n,&LocalNpcBuff::hitChancePct)),0.f,60.f);
    uint32_t sum=uint32_t(miss*100.f);
    if(roll<sum)return LocalMeleeOutcome::Miss;
    if(d.sourceNoActiveDefense)return LocalMeleeOutcome::Hit;
    bool canDodge=!d.sourceNoAttackDodge,canParry=!d.sourceNoAttackParry;
    bool canBlock=d.sourceCompletelyBlocked&&!d.sourceDirectDamage;
    // Ranged attacks can only miss, be deflected (no player source) or, as a
    // COMPLETELY_BLOCKED spell, be blocked.
    if(d.sourceDamageClass==3){canDodge=false;canParry=false;}
    // From behind a player can neither dodge nor parry nor block.
    if(!front(p.x,p.y,p.orientation,n.x,n.y)){canDodge=false;canParry=false;canBlock=false;}
    // IsNonMeleeSpellCast / UNIT_STATE_CONTROLLED: a casting or stunned
    // victim has no active defense.
    const bool casting=p.castingSpellId!=0||(localPlayerControl(p)&0xdu);
    const auto take=[&](bool allowed,float pct){
        int32_t chance=allowed&&!casting?int32_t(pct*100.f)+int32_t(diff*4.f):0;
        if(chance<0)chance=0;
        sum+=uint32_t(chance);return roll<sum;
    };
    if(canDodge&&take(true,s.dodge))return LocalMeleeOutcome::Dodge;
    if(canParry&&take(s.parry>0,s.parry))return LocalMeleeOutcome::Parry;
    if(canBlock&&take(s.block>0,s.block))return LocalMeleeOutcome::Block;
    return LocalMeleeOutcome::Hit;
}
// Unit::isSpellBlocked (Unit.cpp:3262-3286): partial block of a physical
// melee special, rolled separately; the attacker's skill lead raises it.
bool localNpcMeleeSpellBlocked(const LocalRealmNpc& n,const LocalRealmPlayer& p,const LocalMeleeStats& s,
                               const LocalSpellDefinition& d,uint32_t roll){
    if(d.sourceNoActiveDefense||d.sourceAlwaysHit||s.block<=0||p.castingSpellId||(localPlayerControl(p)&0xdu))return false;
    if(!front(p.x,p.y,p.orientation,n.x,n.y))return false;
    const float chance=s.block+(int(n.level)-int(p.level))*5.f*.04f;
    return chance>0&&float(roll)<chance*100.f;
}
LocalMeleeOutcome localRollNpcMelee(const LocalRealmNpc& n,const LocalRealmPlayer& p,const LocalMeleeStats& s,uint32_t roll){
    const auto flags=localNpcMeleeFlags(n.entry);const float diff=(int(p.level)-int(n.level))*5.f;
    float sum=0;auto take=[&](float chance){sum+=std::max(0.f,chance)*100;return roll<sum;};
    if(take(std::clamp(5+(diff>0?diff*.04f:diff*.02f)+s.missBonus-float(localNpcBuffTotal(n,&LocalNpcBuff::hitChancePct)),0.f,60.f)))return LocalMeleeOutcome::Miss;
    // IsNonMeleeSpellCast / UNIT_STATE_CONTROLLED: no active defense while
    // casting or stunned.
    const bool facing=front(p.x,p.y,p.orientation,n.x,n.y)&&!p.castingSpellId&&!(localPlayerControl(p)&0xdu);
    if(take(facing?s.dodge+diff*.04f:0))return LocalMeleeOutcome::Dodge;
    if(take(facing&&s.parry>0?s.parry+diff*.04f:0))return LocalMeleeOutcome::Parry;
    if(take(facing&&s.block>0?s.block+diff*.04f:0))return LocalMeleeOutcome::Block;
    if(take(n.level>=p.level+4&&!(flags&32)?-diff*2-15:0))return LocalMeleeOutcome::Crushing;
    if(take(flags&0x20000?0:5-(diff+s.defense)*.04f-s.incomingCritReductionPct))return LocalMeleeOutcome::Critical;
    return LocalMeleeOutcome::Hit;
}
}
