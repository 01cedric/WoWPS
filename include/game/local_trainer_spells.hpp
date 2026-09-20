#pragma once
#include <algorithm>
#include <cstdint>
namespace wowee::game {
// P05 / the implementation importer defect (the source audit
// section 7 item 6): the reference builds a spellbook from `trainer_spell`
// (ObjectMgr::LoadTrainers), `playercreateinfo_spell_custom`, Talent.dbc and
// spell_learn_spell - never from the client's SkillLineAbility.dbc, which this
// importer reads. The distinct spells any trainer offers at the pin, 3,592
// rows, emitted by tools/local_realm/import_spell_threat_tables.py with the
// provenance in tools/local_realm/spell_threat_provenance.json. Only
// membership is read: the importer retires a rank of a spell_ranks chain that
// no trainer offers any member of and whose first rank the client itself never
// marks learnable (local_spell_import.hpp, the untrained-chain rule).
inline constexpr uint32_t kLocalTrainerSpells[]={
#include "game/local_trainer_spells_generated.inc"
};
inline bool localTrainerOffers(uint32_t id) {
    return std::binary_search(std::begin(kLocalTrainerSpells),std::end(kLocalTrainerSpells),id);
}
}
