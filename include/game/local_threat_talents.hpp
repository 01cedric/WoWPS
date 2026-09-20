#pragma once
#include "game/local_talents.hpp"
#include "game/local_spell_equipment.hpp"
#include "game/local_spell_threat.hpp"
namespace wowee::game {
// ThreatManager::CalculateModifiedThreat (ThreatManager.cpp:708-762): the
// spell_threat.pctMod , the op-2 SPELLMOD_THREAT, then the aura-10
// school multipliers, which this build carries as the form's threatPercent and
// the negative-only passive school talents.
inline uint64_t localTalentThreat(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition* spell,uint64_t amount) {
    amount=localSpellThreatPct(spell,amount);
    if(const auto* f=localActiveForm(p))amount=amount*f->threatPercent/100;
    if(p.classId>=1&&p.classId<=11)for(auto [id,rank]:p.talents) {
        const auto* talent=localTalentSpell(c,id,rank);
        if(!talent||!talent->passive||!talent->unsupportedReason.empty()||!(talent->allowableClasses&(1u<<(p.classId-1)))||
           !(talent->passiveSchoolThreatMask&(spell?spell->schoolMask:1u))||talent->passiveSchoolThreatPercent>=0||talent->passiveSchoolThreatPercent< -100||
           !localTalentPrerequisitesReady(p,c,*talent)||!localSpellEquipmentReady(p,c,*talent))continue;
        amount=amount*uint32_t(100+talent->passiveSchoolThreatPercent)/100;
    }
    return spell?amount*uint32_t(100+localTalentCastModifier(p,c,*spell,2,true))/100:amount;
}
}
