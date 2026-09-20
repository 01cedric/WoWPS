#include "game/local_spell_import.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>

using nlohmann::json;
using wowee::pipeline::DBCFile;
namespace fs = std::filesystem;

// Developer-only executable. Calls the same importer as application_local_realm.cpp.
// Decoder acceptance is not a claim of complete spell or gameplay compatibility.
int main(int argc, char** argv) try {
    if (argc != 3) {
        std::cerr << "Usage: audit_client_spells DBC_DIRECTORY OUTPUT_JSON\n";
        return 2;
    }
    struct Spec { const char* name; uint32_t fields; bool minimum; };
    const Spec specs[] = {{"Spell",234,false},{"SpellRange",40,false},
        {"SpellCastTimes",4,false},{"SpellDuration",4,false},{"SpellIcon",2,false},
        {"SkillLineAbility",14,false},{"SkillLine",38,true},{"Talent",23,false},
        {"SpellRuneCost",5,false},{"SpellRadius",4,false},{"TalentTab",24,true}};
    std::map<std::string,std::unique_ptr<DBCFile>> tables;
    json out = {{"schemaVersion",1},{"scope","Installed client importer; not full gameplay verification"},
        {"tables",json::array()},{"problems",json::array()}};
    for (const auto& spec : specs) {
        const auto path=fs::path(argv[1])/(std::string(spec.name)+".dbc");
        json info={{"name",spec.name}};
        if (!fs::is_regular_file(path)) {
            info["status"]="missing";out["problems"].push_back(path.filename().string()+" missing");
        } else {
            const auto size=fs::file_size(path);
            if(size<20 || size>128*1024*1024) throw std::runtime_error("Invalid table size: "+path.string());
            std::ifstream file(path,std::ios::binary);
            std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
            auto table=std::make_unique<DBCFile>();
            const bool valid=bytes.size()==size && std::equal(bytes.begin(),bytes.begin()+4,"WDBC") && table->load(bytes);
            if(!valid || table->getRecordSize()!=table->getFieldCount()*4 ||
               (spec.minimum ? table->getFieldCount()<spec.fields : table->getFieldCount()!=spec.fields)) {
                info["status"]="incompatible";out["problems"].push_back(path.filename().string()+" incompatible");
            } else {
                info["status"]="loaded";info["records"]=table->getRecordCount();
                info["fields"]=table->getFieldCount();tables[spec.name]=std::move(table);
            }
        }
        out["tables"].push_back(std::move(info));
    }
    out["inputComplete"]=out["problems"].empty();
    if(out["inputComplete"]==true) {
        const auto get=[&](const char* name){return tables.at(name).get();};
        auto imported=wowee::game::importClientStarterSpells(get("Spell"),get("SpellRange"),
            get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SkillLineAbility"),
            get("SkillLine"),get("Talent"),get("SpellRuneCost"),get("SpellRadius"));
        wowee::game::detail::importClientTalents(imported,get("Talent"),get("TalentTab"),get("Spell"),
            get("SpellRange"),get("SpellCastTimes"),get("SpellDuration"),get("SpellIcon"),get("SpellRuneCost"),get("SpellRadius"));
        out["diagnostic"]=imported.diagnostic;
        out["importAudit"]=json::array();
        for(const auto& row:imported.audit) out["importAudit"].push_back({{"spellId",row.id},
            {"classMask",row.classes},{"talent",row.talent},{"firstResult",row.status}});
        out["retainedDefinitions"]=json::array();
        for(const auto& d:imported.spells) out["retainedDefinitions"].push_back({{"spellId",d.id},
            {"classMask",d.allowableClasses},{"talentId",d.talentId},{"talentRank",d.talentRank},
            {"talentTab",d.talentTab},{"talentRow",d.talentRow},{"talentPrerequisites",d.talentPrerequisites},{"talentPrerequisiteRanks",d.talentPrerequisiteRanks},
            {"decoderAccepted",d.unsupportedReason.empty()},{"maxAuraStacks",d.maxAuraStacks},
            {"npcOnly",d.npcOnly},{"sourceProjectileSpeed",d.sourceProjectileSpeed},{"sourceCantReflect",d.sourceCantReflect},{"sourceAlwaysHit",d.sourceAlwaysHit},
            {"wardProfile",d.wardProfile},{"moltenShieldsChancePct",d.moltenShieldsChancePct},
            {"comboProfile",d.comboProfile},{"comboGain",d.comboGain},{"comboFinisher",d.comboFinisher},
            {"spiritCritRatingPct",d.spiritCritRatingPct},{"incomingCritReductionPct",d.incomingCritReductionPct},{"mageArmorGroup",d.mageArmorGroup},{"sourceDamageClass",d.sourceDamageClass},{"sourceNotAProc",d.sourceNotAProc},{"sourceDoNotConsumeResources",d.sourceDoNotConsumeResources},{"sourceIgnoreCasterModifiers",d.sourceIgnoreCasterModifiers},{"rangedAutoProfile",d.rangedAutoProfile},{"sourceCantCrit",d.sourceCantCrit},{"procCanCrit",d.procCanCrit},{"meleeHastePct",d.meleeHastePct},{"procParentTalentId",d.procParentTalentId},{"meleeSpecialProfile",d.meleeSpecialProfile},{"triggeredAuraSpellId",d.triggeredAuraSpellId},{"triggeredOnly",d.triggeredOnly},{"weaponDamage",d.weaponDamage},{"normalizedWeapon",d.normalizedWeapon},{"weaponPercent",d.weaponPercent},
            {"requiresBehind",d.requiresBehind},{"directPerCombo",d.directPerCombo},{"periodicPerCombo",d.periodicPerCombo},{"extraEnergyMultiplier",d.extraEnergyMultiplier},
            {"periodicDamageBase",d.periodicDamage},{"periodicDamageMax",d.periodicDamageMax},{"periodicDamagePerLevel",d.periodicDamagePerLevel},{"formId",d.formId},{"requiredForms",d.requiredForms},{"excludedForms",d.excludedForms},{"notShapeshifted",d.notShapeshifted},{"allowWithoutForm",d.allowWithoutForm},{"requiredItemClass",d.requiredItemClass},{"requiredItemSubclasses",d.requiredItemSubclasses},{"requiredInventoryTypes",d.requiredInventoryTypes},{"requiresMainHand",d.requiresMainHand},{"requiresOffHand",d.requiresOffHand},{"schoolMask",d.schoolMask},{"directIgnoresArmor",d.directIgnoresArmor},{"periodicIgnoresArmor",d.periodicIgnoresArmor},{"passiveSchoolThreatPercent",d.passiveSchoolThreatPercent},{"passiveSchoolThreatMask",d.passiveSchoolThreatMask},{"areaRadius",d.areaRadius},{"chainTargets",d.chainTargets},{"chainMultiplierPermille",d.chainMultiplierPermille},{"chainRadius",d.chainRadius},{"procFamily",d.proc.spellFamily},{"procFamilyFlags",d.proc.spellFamilyFlags},
            {"procAmount",d.proc.amount},{"procResourceType",d.proc.resourceType},{"procFlags",d.proc.flags},
            {"procChance",d.proc.chance},{"procPpm",d.proc.ppm},{"procHitMask",d.proc.hitMask},
            {"procSpellTypeMask",d.proc.spellTypeMask},{"procPhaseMask",d.proc.phaseMask},{"procAllowTriggered",d.proc.allowTriggered},
            {"procRequiredForms",d.proc.requiredForms},{"procPushbackPercent",d.proc.pushbackPercent},{"procChild",d.proc.spellId},{"procCharges",d.proc.charges},{"procCooldownMs",d.proc.cooldownMs},
            {"spellFamily",d.spellFamily},{"spellFamilyFlags",d.spellFamilyFlags},
            {"passiveArmorAttackPowerDivisor",d.passiveArmorAttackPowerDivisor},{"passiveOffhandDamagePct",d.passiveOffhandDamagePct},{"passiveWeaponHitPct",d.passiveWeaponHitPct},
            {"passiveEquipmentArmorPct",d.passiveEquipmentArmorPct},{"passiveFeralCritPct",d.passiveFeralCritPct},{"passiveFeralDodgePct",d.passiveFeralDodgePct},{"passiveCatRunPct",d.passiveCatRunPct},
            {"directEffectSlot",d.directEffectSlot},{"periodicEffectSlot",d.periodicEffectSlot},
            {"stormstrikeProfile",d.stormstrikeProfile},{"stormstrikeManaChancePct",d.stormstrikeManaChancePct},
            {"passiveIntellectAttackPowerPct",d.passiveIntellectAttackPowerPct},{"passiveDualWieldHitPct",d.passiveDualWieldHitPct},
            {"passiveCanParry",d.passiveCanParry},{"passiveCanDualWield",d.passiveCanDualWield},
            {"passivePhysicalDamagePct",d.passivePhysicalDamagePct},{"physicalDamageDonePct",d.physicalDamageDonePct},{"damageTakenPct",d.damageTakenPct},{"periodicHealMaxHealthPct",d.periodicHealMaxHealthPct},{"arcaneBlastProfile",d.arcaneBlastProfile},
            {"passiveMeleeCritPct",d.passiveMeleeCritPct},{"passiveSpellCritPct",d.passiveSpellCritPct},{"passiveTotalStatPct",d.passiveTotalStatPct},
            {"sourceRawCastTimeMs",d.sourceRawCastTimeMs},{"omenProcEligible",d.omenProcEligible},{"clearcastingProfile",d.clearcastingProfile},{"chargedCostPct",d.chargedCostPct},{"chargedCostMask",d.chargedCostMask},{"passivePushbackPct",d.passivePushbackPct},{"pushbackSpellMask",d.pushbackSpellMask},
            {"cooldownMs",d.cooldownMs},{"cooldownCategory",d.cooldownCategory},{"categoryCooldownMs",d.categoryCooldownMs},{"noCategoryCooldownMods",d.noCategoryCooldownMods},
            {"snarePercent",d.snarePercent},{"snareZeroHealingMarker",d.snareZeroHealingMarker},
            {"castTimeMs",d.castTimeMs},{"interruptFlags",d.interruptFlags},{"noPushback",d.noPushback},
            {"manaPer5",d.manaPer5},{"manaPerAbsorbMilli",d.manaPerAbsorbMilli},
            {"passiveManaRegenInterruptPct",d.passiveManaRegenInterruptPct},{"passiveManaRegenStatPct",d.passiveManaRegenStatPct},
            {"shieldCapacity",d.buffAbsorb},{"reactionKind",uint32_t(d.proc.effect)},
            {"nextRank",d.supercededBySpell},{"firstRejection",d.unsupportedReason}});
        for(size_t i=0;i<imported.spells.size();++i) {
            const auto& p=imported.spells[i].secondaryProc;
            out["retainedDefinitions"][i]["procSecondary"]={{"effect",uint32_t(p.effect)},{"child",p.spellId},
                {"amount",p.amount},{"resourceType",p.resourceType},{"requiredForms",p.requiredForms},
                {"flags",p.flags},{"chance",p.chance},{"hitMask",p.hitMask},{"phaseMask",p.phaseMask},
                {"spellTypeMask",p.spellTypeMask},{"triggerSpellFamily",p.triggerSpellFamily},
                {"triggerSpellFamilyFlags",p.triggerSpellFamilyFlags},{"allowTriggered",p.allowTriggered}};
            auto& mods=out["retainedDefinitions"][i]["castModifiers"];mods=json::array();
            for(const auto& m:imported.spells[i].passiveCastModifiers)if(m.active)
                mods.push_back({{"operation",m.operation},{"percentage",m.percentage},{"amount",m.amount},{"mask",m.mask}});
        }
        // Every installed spell is listed, including spells not selected by the class importer.
        // Raw metadata deliberately carries no supported/unsupported inference.
        const auto* spells=get("Spell");out["sourceSpells"]=json::array();
        for(uint32_t row=0;row<spells->getRecordCount();++row) {
            const auto u=[&](uint32_t c){return spells->getUInt32(row,c);};
            json effects=json::array();
            for(uint32_t e=0;e<3;++e) if(u(71+e))effects.push_back({{"slot",e},{"effect",u(71+e)},
                {"aura",u(95+e)},{"targetA",u(86+e)},{"targetB",u(89+e)},{"triggerSpell",u(116+e)}});
            out["sourceSpells"].push_back({{"spellId",u(0)},{"name",spells->getString(row,136)},
                {"effects",effects},{"interruptFlags",u(wowee::game::spell335::InterruptFlags)},
                {"procFlags",u(wowee::game::spell335::ProcFlags)},{"procChance",u(wowee::game::spell335::ProcChance)},
                {"procCharges",u(wowee::game::spell335::ProcCharges)},{"stackLimit",u(wowee::game::spell335::StackAmount)}});
        }
        out["sourceTalentRanks"]=json::array();const auto* talents=get("Talent");
        for(uint32_t row=0;row<talents->getRecordCount();++row)
            for(uint32_t rank=1;rank<=5;++rank)if(const auto spell=talents->getUInt32(row,3+rank))
                out["sourceTalentRanks"].push_back({{"talentId",talents->getUInt32(row,0)},
                    {"tabId",talents->getUInt32(row,1)},{"rank",rank},{"spellId",spell}});
        out["limitations"]={"firstResult contains only the first reported blocker, not every missing mechanic",
            "Audit rows may repeat a spell selected both as a starter and a talent; use IDs and rank identity",
            "Source rows omitted by the importer remain unassessed, not implemented",
            "No automatic overall completion percentage; no console, LAN or gameplay verification"};
    } else {
        out["importAudit"]=nullptr;out["sourceSpells"]=nullptr;out["sourceTalentRanks"]=nullptr;
    }
    std::ofstream file(argv[2]);file << out.dump(2) << '\n';file.close();
    if(!file)throw std::runtime_error("Could not write output");
    return out["inputComplete"]==true ? 0 : 3;
} catch(const std::exception& e) { std::cerr << e.what() << '\n';return 2; }
