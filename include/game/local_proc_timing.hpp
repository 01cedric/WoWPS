#pragma once
#include "game/local_proc_rules.hpp"
#include "game/local_melee.hpp"
#include "game/local_forms.hpp"

namespace wowee::game {
// Source: Player::InitDataForForm/SetRegularAttackTime and Unit::GetAttackTime.
// No haste multiplier, normalized AP speed, owner speed, or NPC speed belongs
// in this value. Caller resolves the original aura caster in the owner's map.
inline LocalProcCasterTiming localProcTimingForCaster(const LocalRealmPlayer* caster,
                                                      const LocalWorldContent& content,
                                                      const LocalCombatEvent& event) {
    if(!caster)return {};
    const auto* form=localActiveForm(*caster);
    const bool ranged=event.attackType==LocalCombatAttackType::Ranged;
    if(form&&(form->form==1||form->form==5||form->form==8))
        return {true,ranged?2000u:form->form==1?1000u:2500u};
    if(!ranged) {
        const auto weapon=localWeaponAmounts(*caster,content,event.offHand,false,false);
        // SetRegularAttackTime retains an unarmed 2s offhand timer even when
        // there is no usable offhand attack. Incoming PPM still queries it.
        return {true,weapon.active?uint32_t(std::clamp(weapon.seconds*1000.f,1.f,60000.f)):2000u};
    }
    if(form&&form->form==16)return {true,2000}; // Ghost Wolf: no usable weapons.
    const size_t slot=localEquipmentIndex(LocalEquipmentSlot::Ranged);
    const auto id=caster->equipment[slot];
    const auto* definition=content.item(id);
    const auto* weapon=localMeleeItem(id);
    if(!definition||!weapon||weapon->itemClass!=2||weapon->scaling||!weapon->delay||
       !localEquipmentFits(definition->inventoryType,definition->slot,slot))return {true,2000};
    uint64_t copies=0;
    for(const auto& stack:caster->inventory)if(stack.itemId==id)copies+=stack.count;
    if(uint64_t(std::count(caster->equipment.begin(),caster->equipment.begin()+slot+1,id))>copies)
        return {true,2000};
    return {true,std::min(weapon->delay,60000u)};
}
}
