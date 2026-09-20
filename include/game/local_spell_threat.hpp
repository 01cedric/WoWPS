#pragma once
#include "game/local_gameplay.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace wowee::game {
// P05 shared combat rules : the reference's spell_threat table,
// transcribed from AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33.
// the source audit section 3.1 and 8.1 (P05-6).
//
// spell_threat(entry, flatMod, pctMod, apPctMod): 106 rows at the pin
// (tools/local_realm/import_spell_threat_tables.py, provenance in
// tools/local_realm/spell_threat_provenance.json). SpellMgr::LoadSpellThreats
// keys the row by spell id and SpellMgr::GetSpellThreatEntry
// (SpellMgr.cpp:963-976) falls back to the FIRST RANK of the spell's chain, so
// a row on rank 1 (Searing Pain 5676, Frost Shock 8056, Retribution Aura 7294)
// speaks for every rank. Spell::HandleThreatSpells (Spell.cpp:5771-5780) adds
// `flatMod + apPctMod x AP` once per cast in place of SpellLevel;
// ThreatManager::CalculateModifiedThreat (ThreatManager.cpp:708-716)
// multiplies every damage or heal threat of the spell by `pctMod`.
struct LocalSpellThreatRow { uint32_t entry=0; int32_t flatMod=0; float pctMod=1.f,apPctMod=0.f; };
inline constexpr LocalSpellThreatRow kLocalSpellThreat[]={
#include "game/local_spell_threat_generated.inc"
};
/// The table row for `id`, else for its first rank, else null - the shape of
/// SpellMgr::GetSpellThreatEntry. `firstRank` is the definition's own
/// firstRankSpell (SpellInfo::GetFirstRankSpell), 0 or the id itself when the
/// spell is unranked.
inline const LocalSpellThreatRow* localSpellThreatRow(uint32_t id,uint32_t firstRank) {
    const auto find=[](uint32_t key)->const LocalSpellThreatRow* {
        const auto* end=std::end(kLocalSpellThreat);
        const auto* it=std::lower_bound(std::begin(kLocalSpellThreat),end,key,
            [](const LocalSpellThreatRow& r,uint32_t k){return r.entry<k;});
        return it!=end&&it->entry==key?it:nullptr;
    };
    if(const auto* row=find(id))return row;
    return firstRank&&firstRank!=id?find(firstRank):nullptr;
}
inline const LocalSpellThreatRow* localSpellThreatRow(const LocalSpellDefinition& d) {
    return localSpellThreatRow(d.id,d.firstRankSpell);
}
/// The initial threat of one cast before the split over its targets
/// (Spell::HandleThreatSpells, Spell.cpp:5764-5780): nothing for NO_THREAT
/// or SUPPRESS_TARGET_PROCS; the table's flat and attack-power terms where a
/// row exists; else SpellLevel unless the reference's computed
/// SPELL_ATTR0_CU_NO_INITIAL_THREAT. Whole threat units, not thousandths.
inline float localSpellInitialThreat(const LocalSpellDefinition& d,float attackPower) {
    if(!d.clientSpell||d.sourceNoThreat||d.sourceSuppressTargetProcs)return 0.f;
    if(const auto* row=localSpellThreatRow(d)) {
        float threat=0.f;
        if(row->apPctMod!=0.f&&std::isfinite(attackPower)&&attackPower>0)threat+=row->apPctMod*attackPower;
        return threat+float(row->flatMod);
    }
    return d.sourceNoInitialThreat?0.f:float(d.spellLevel);
}
/// ThreatManager::CalculateModifiedThreat's spell_threat.pctMod term.
inline uint64_t localSpellThreatPct(const LocalSpellDefinition* d,uint64_t amount) {
    if(!d||!d->clientSpell)return amount;
    const auto* row=localSpellThreatRow(*d);
    if(!row||row->pctMod==1.f)return amount;
    return uint64_t(double(amount)*double(row->pctMod));
}
}
