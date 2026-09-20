#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_class_pools.hpp"
#include <algorithm>
namespace wowee::game {
struct LocalFormProfile {
    uint32_t spell; uint8_t form,clazz,bar; LocalResourceType power;
    uint32_t allianceModel,hordeModel; uint16_t armorPercent,threatPercent;
    uint8_t damagePercent,takenPercent,runPercent,swimPercent;
};
// Reviewed 12340 ground-form identities. Source attributes, melee/AP and base
// resource pools are derived by local_melee. Mana regeneration credit follows
// the caster pool across forms.
//
// Every numeric column below is a boost-spell amount, and as of the implementation three of
// them are checked against the client's own bytes rather than trusted:
// decodeClientFormBoost refuses the boost unless 100 + its aura-142 amount is
// this row's armorPercent, 100 + its aura-10 amount is threatPercent and
// 100 + its aura-87 amount is takenPercent, for every form the reference's
// switch casts it for. The remaining constants - damagePercent, runPercent,
// swimPercent and the two models - have no admitted carrier and stay
// transcriptions; the source audit section 2.3 lists their source
// amounts one by one.
inline constexpr LocalFormProfile kLocalForms[]={
    {2457,17,1,1,LocalResourceType::Rage,0,0,100,80,100,100,100,100},
    {71,18,1,2,LocalResourceType::Rage,0,0,100,145,95,90,100,100},
    {2458,19,1,3,LocalResourceType::Rage,0,0,100,80,100,105,100,100},
    {5487,5,11,3,LocalResourceType::Rage,2281,2289,280,145,100,100,100,100},
    {9634,8,11,3,LocalResourceType::Rage,2281,2289,470,145,100,100,100,100},
    {768,1,11,1,LocalResourceType::Energy,892,8571,100,71,100,100,100,100},
    {783,3,11,0,LocalResourceType::Mana,918,918,100,100,100,100,140,100},
    {1066,4,11,0,LocalResourceType::Mana,2428,2428,100,100,100,100,100,150},
    // SpellShapeshiftForm 16: Beast, model 4613, no alternate action bar or
    // attack-speed override. Spell 2645 supplies the 40% run-speed aura.
    {2645,16,7,0,LocalResourceType::Mana,4613,4613,100,100,100,100,140,100},
};
inline const LocalFormProfile* localFormProfile(uint32_t spell){for(const auto& f:kLocalForms)if(f.spell==spell)return &f;return nullptr;}
inline const LocalFormProfile* localFormProfileByForm(uint8_t form){for(const auto& f:kLocalForms)if(f.form==form)return &f;return nullptr;}
// The reference's per-form grant table is the switch in
// AuraEffect::HandleShapeshiftBoosts (SpellAuraEffects.cpp:1356-1425), which
// casts up to two boost spells on the player when a form is applied and removes
// them when it is lost. SpellShapeshiftForm.dbc's stanceSpell[] is NOT that
// table: 0 of the nine forms modelled here carries a non-zero entry, so the
// loop at SpellAuraEffects.cpp:2274-2287 is dead for this build.
//
// These five are the boost spells that carry at least one amount kLocalForms
// does not already reproduce, plus 21178, which is cast alongside both bear
// forms and owns the +45% threat both bear rows hard-code. 5419, 5421, 21156
// and 7376 carry only amounts kLocalForms already has exactly, and 67116 has
// its own decoder; none of those four is admitted here.
//
// `forms` is the switch's own mapping, not the DBC's Stances column: 21178 and
// 7381 carry Stances = 0 in the client data and are still cast for form 5/8 and
// form 19 respectively, which is why the mask is written out here.
struct LocalFormBoost { uint32_t spell; uint64_t forms; };
inline constexpr LocalFormBoost kLocalFormBoosts[]={
    {3025, uint64_t(1)<<0},                            // Cat Form (Passive), form 1
    {1178, uint64_t(1)<<4},                            // Bear Form (Passive), form 5
    {21178,(uint64_t(1)<<4)|(uint64_t(1)<<7)},         // Bear Form (Passive2), forms 5 and 8
    {9635, uint64_t(1)<<7},                            // Dire Bear Form (Passive), form 8
    {7381, uint64_t(1)<<18},                           // Berserker Stance Passive, form 19
};
inline const LocalFormBoost* localFormBoost(uint32_t spell){for(const auto& b:kLocalFormBoosts)if(b.spell==spell)return &b;return nullptr;}
// The three passives the reference reads on a transition to give resource BACK.
// Furor is a druid dummy read by GetDummyAuraEffect(SPELLFAMILY_DRUID, 238, 0)
// (SpellAuraEffects.cpp:2103); Stance Mastery and Tactical Mastery are found by
// walking the spellbook and the talent map for SpellFamilyName == WARRIOR with
// SpellIconID == 139 (:2197-2221). All nine rank spells carry the amount on a
// SPELL_AURA_DUMMY effect, which is the single reason all nine were rejected.
enum class LocalFormResourceKind : uint8_t { Furor=1, RetainedRage=2 };
struct LocalFormResourceTalent { uint32_t spell; LocalFormResourceKind kind; };
inline constexpr LocalFormResourceTalent kLocalFormResourceTalents[]={
    {17056,LocalFormResourceKind::Furor},{17058,LocalFormResourceKind::Furor},
    {17059,LocalFormResourceKind::Furor},{17060,LocalFormResourceKind::Furor},
    {17061,LocalFormResourceKind::Furor},
    {12295,LocalFormResourceKind::RetainedRage},{12676,LocalFormResourceKind::RetainedRage},
    {12677,LocalFormResourceKind::RetainedRage},{12678,LocalFormResourceKind::RetainedRage},
};
inline const LocalFormResourceTalent* localFormResourceTalent(uint32_t spell){
    for(const auto& e:kLocalFormResourceTalents)if(e.spell==spell)return &e;return nullptr;}
/// SpellEffectInfo::CalcValue's level term, SpellInfo.cpp:414-431: clamp the
/// caster level up to BaseLevel and down to MaxLevel when MaxLevel is set, then
/// subtract max(BaseLevel, SpellLevel). Every form-boost amount has DieSides in
/// {0, 1}, so the roll is never taken and `base` already carries the +1.
inline uint32_t localFormBoostAmount(uint32_t level,uint32_t base,float perLevel,
                                     uint32_t baseLevel,uint32_t maxLevel,uint32_t spellLevel) {
    double amount=double(base);
    if(perLevel!=0.f) {
        int64_t clamped=int64_t(level);
        if(maxLevel>0&&clamped>int64_t(maxLevel))clamped=int64_t(maxLevel);
        else if(clamped<int64_t(baseLevel))clamped=int64_t(baseLevel);
        clamped-=int64_t(std::max(baseLevel,spellLevel));
        amount+=double(clamped)*double(perLevel);
    }
    return uint32_t(std::clamp(amount,0.0,1000000.0));
}
inline const LocalFormProfile* localActiveForm(const LocalRealmPlayer& p){const auto* f=localFormProfile(p.formSpellId);return f&&f->clazz==p.classId&&!p.dead?f:nullptr;}
inline uint32_t localBaseMana(const LocalRealmPlayer& p){return localClassBaseMana(p);}
inline uint32_t localManaCapacity(const LocalRealmPlayer& p){return p.classId==11&&p.formSpellId&&p.resourceType!=LocalResourceType::Mana?p.druidManaCapacity:p.maxMana;}
inline uint32_t localAvailableMana(const LocalRealmPlayer& p){return p.resourceType==LocalResourceType::Mana?p.mana:p.druidMana;}
inline bool localSpellFormReady(const LocalRealmPlayer& p,const LocalSpellDefinition& s){
    const auto* f=localActiveForm(p);const uint64_t mask=f?uint64_t(1)<<(f->form-1):0;
    if(s.excludedForms&mask)return false;
    // No form spell carries SPELL_ATTR2_ALLOW_WHILE_NOT_SHAPESHIFTED and all nine
    // carry SPELL_ATTR0_NOT_SHAPESHIFTED, so SpellInfo::CheckShapeshift
    // (SpellInfo.cpp:1495-1497) REFUSES a direct form -> form shift. The 3.3.5a
    // client hides that by cancelling the current form before it sends the cast;
    // this short-circuit reproduces the client, which is what a player sees.
    // The exclusion mask above still applies, so Tree and Moonkin still block.
    if(s.formId)return true;
    if(s.requiredForms&mask)return true;
    // Ghost Wolf is a real shapeshift (source flags 0xd8 have no STANCE bit),
    // while warrior stances permit normal non-shapeshifted activities.
    const bool shifted=f&&(f->clazz==11||f->form==16);
    if(shifted&&(s.notShapeshifted||s.requiredForms))return false;
    return shifted||!s.requiredForms||s.allowWithoutForm;
}
/// The environment rules that both refuse a cast and evict an already active
/// form. SPELL_ATTR0_ONLY_OUTDOORS (Attributes 0x8000, Spell.cpp:5902-5904) is
/// the client's own bit and is set on exactly two of the nine form spells:
/// Travel Form 783 and Ghost Wolf 2645. previously only Ghost Wolf was gated,
/// by a hard-coded form id; Travel Form's bit was never read. The liquid and
/// instance rules below have no source attribute and remain local stand-ins.
inline bool localFormEnvironmentRetained(const LocalRealmPlayer& p,const LocalSpellDefinition& s){
    if(!s.formId)return true;
    if(s.sourceOnlyOutdoors&&(p.movementState&kLocalMovementIndoors))return false;
    if(s.formId==4)return (p.movementState&kLocalMovementInLiquid)!=0;
    if(s.formId==3)return !p.instanceId&&!(p.movementState&kLocalMovementInLiquid);
    return true;
}
inline bool localFormEnvironmentReady(const LocalRealmPlayer& p,const LocalSpellDefinition& s){
    if(!s.formId)return true;
    if(p.flight.active||p.transportEntry)return false;
    return localFormEnvironmentRetained(p,s);
}
inline bool validLocalFormState(const LocalRealmPlayer& p){
    if(p.druidMana>1000000||p.druidManaRemainder>=1000)return false;
    if(!p.formSpellId)return !p.druidMana&&!p.druidManaRemainder;
    const auto* f=localFormProfile(p.formSpellId);
    return f&&f->clazz==p.classId&&!p.dead&&p.resourceType==f->power&&
        (p.classId==11||(!p.druidMana&&!p.druidManaRemainder));
}
inline void leaveLocalForm(LocalRealmPlayer& p){
    if(p.classId==11&&p.formSpellId){
        if(p.resourceType!=LocalResourceType::Mana){p.mana=p.druidMana;p.resourceRegenRemainder=p.druidManaRemainder;}
        p.resourceType=LocalResourceType::Mana;p.maxMana=p.druidManaCapacity;p.mana=std::min(p.mana,p.maxMana);
    }
    p.formSpellId=0;p.druidMana=0;p.druidManaRemainder=0;
}
// `entryResource` is what the reference's entry rules leave in the new pool:
// Furor's energy or rage for a druid, the rage Stance Mastery and Tactical
// Mastery retain for a warrior, and zero for everything else - which is what it
// defaults to, so an entry with no talents behaves exactly as it did before
// the reference. localFormEntryResource (game/local_form_boosts.hpp) computes it; it is
// passed in rather than read here because the bear grant is a roll and this
// header has no random source.
inline void enterLocalForm(LocalRealmPlayer& p,const LocalFormProfile& f,uint32_t entryResource=0){
    if(p.classId==11){
        const auto mana=localAvailableMana(p);const auto rem=p.resourceType==LocalResourceType::Mana?p.resourceRegenRemainder:p.druidManaRemainder;
        p.druidMana=mana;p.druidManaRemainder=rem;p.resourceType=f.power;p.maxMana=f.power==LocalResourceType::Mana?p.druidManaCapacity:100;
        p.mana=f.power==LocalResourceType::Mana?mana:std::min(entryResource,p.maxMana);
        p.resourceRegenRemainder=f.power==LocalResourceType::Mana?rem:0;
    }else if(f.power==LocalResourceType::Rage){p.resourceType=LocalResourceType::Rage;p.maxMana=100;p.mana=std::min(entryResource,100u);}
    else {
        // Ghost Wolf keeps the already-paid mana pool and regeneration credit.
        // It does not use the separate druid shapeshift mana storage.
        p.resourceType=f.power;p.druidMana=p.druidManaRemainder=0;
    }
    p.formSpellId=f.spell;p.mountSpellId=0;p.attackTimer=0;
}
inline uint32_t localFormDamage(const LocalRealmPlayer& p,uint32_t amount,bool incoming=false){const auto* f=localActiveForm(p);return f?uint32_t(uint64_t(amount)*(incoming?f->takenPercent:f->damagePercent)/100):amount;}
inline uint32_t localFormEquipmentArmor(const LocalRealmPlayer& p,uint32_t armor){const auto* f=localActiveForm(p);return f?uint32_t(std::min(uint64_t(1000000),uint64_t(armor)*f->armorPercent/100)):armor;}
// The source Ghost Wolf boost 67116 has aura 305 (+100% minimum speed).
// Unit::UpdateSpeed applies this floor after the strongest slow. This helper
// expresses that order without inventing a player-snare producer.
inline float localFormRunPercent(const LocalRealmPlayer& p,uint32_t slowPercent=100){
    const auto* f=localActiveForm(p);
    const float speed=(f?f->runPercent:100)*std::min(slowPercent,100u)/100.f;
    return f&&f->form==16?std::max(100.f,speed):speed;
}
inline uint32_t localFormDisplay(const LocalRealmPlayer& p){const auto* f=localActiveForm(p);return f?(p.race==6?f->hordeModel:f->allianceModel):0;}
}
