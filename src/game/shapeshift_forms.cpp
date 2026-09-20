#include "game/shapeshift_forms.hpp"

#include "game/local_forms.hpp"
#include "game/protocol_constants.hpp"

namespace wowee::game {
namespace {

// The forms the 3.3.5a client itself knows, in the order it has always shown
// them. Two different consumers read this list and they need different things
// from it, which is why it is not simply `kLocalForms`:
//
//   - the local realm's stance bar (src/addons/local_framexml.cpp) filters it by
//     what the importer accepted, so a form this realm does not model never gets
//     a button;
//   - the connected-server path (lua_unit_api.cpp, lua_spell_api.cpp,
//     ui/game_screen.cpp) does not, because on a real server Moonkin Form,
//     Tree of Life, the flight forms, Stealth and Shadowform are real forms that
//     really do set UNIT_FIELD_BYTES_2's form byte.
//
// What the implementation fixed is not the membership of that second group - it is that this
// table and the realm's own disagreed. Before the implementation it had 16 entries and **no
// shaman at all**, so Ghost Wolf - a fully accepted, fully implemented form -
// had no button on any bar, while the realm's table has had it since the reference.
// `formsAgreeWithRealm` below now refuses at compile time to let the two drift:
// every entry whose spell the realm models must name the realm's class and the
// realm's form id.
//
// The three death-knight "presences" are gone rather than reconciled, because
// they were not a disagreement but wrong data. 48266 / 48263 / 48265 carry no
// SPELL_AURA_MOD_SHAPESHIFT effect at all - their first importer rejections are
// auras 79, 142 and 138 - so they never set the form byte on any server; and the
// form ids 32 / 33 / 34 they were given name Spirit of Redemption and two
// records that do not exist, SpellShapeshiftForm.dbc having exactly 32.
// the source audit section 5.3 has the measurement.
struct BarEntry {
    uint32_t spellId;
    uint8_t classId;
    uint8_t formId;     ///< The spell's own SPELL_AURA_MOD_SHAPESHIFT misc value.
    const char* name;
    const char* icon;
};

constexpr BarEntry kBar[] = {
    {SPELL_BATTLE_STANCE,    1,  17, "Battle Stance",     "Interface\\Icons\\Ability_Warrior_OffensiveStance"},
    {SPELL_DEFENSIVE_STANCE, 1,  18, "Defensive Stance",  "Interface\\Icons\\Ability_Warrior_DefensiveStance"},
    {SPELL_BERSERKER_STANCE, 1,  19, "Berserker Stance",  "Interface\\Icons\\Ability_Racial_Avatar"},
    {SPELL_STEALTH,          4,  30, "Stealth",           "Interface\\Icons\\Ability_Stealth"},
    {SPELL_SHADOWFORM,       5,  28, "Shadowform",        "Interface\\Icons\\Spell_Shadow_Shadowform"},
    {SPELL_GHOST_WOLF,       7,  16, "Ghost Wolf",        "Interface\\Icons\\Spell_Nature_SpiritWolf"},
    {SPELL_BEAR_FORM,        11,  5, "Bear Form",         "Interface\\Icons\\Ability_Racial_BearForm"},
    {SPELL_AQUATIC_FORM,     11,  4, "Aquatic Form",      "Interface\\Icons\\Ability_Druid_AquaticForm"},
    {SPELL_CAT_FORM,         11,  1, "Cat Form",          "Interface\\Icons\\Ability_Druid_CatForm"},
    {SPELL_TRAVEL_FORM,      11,  3, "Travel Form",       "Interface\\Icons\\Ability_Druid_TravelForm"},
    {SPELL_MOONKIN_FORM,     11, 31, "Moonkin Form",      "Interface\\Icons\\Spell_Nature_ForceOfNature"},
    {SPELL_TREE_OF_LIFE,     11,  2, "Tree of Life",      "Interface\\Icons\\Ability_Druid_TreeofLife"},
    {SPELL_FLIGHT_FORM,      11, 29, "Flight Form",       "Interface\\Icons\\Ability_Druid_FlightForm"},
    {SPELL_SWIFT_FLIGHT,     11, 27, "Swift Flight Form", "Interface\\Icons\\Ability_Druid_FlightForm"},
};

/// Every entry the realm models must agree with the realm about who owns it and
/// which form id it sets. This is the check that was missing: the two tables
/// were written out separately and drifted.
constexpr bool formsAgreeWithRealm() {
    for (const BarEntry& entry : kBar)
        for (const LocalFormProfile& profile : kLocalForms)
            if (profile.spell == entry.spellId &&
                (profile.clazz != entry.classId || profile.form != entry.formId)) return false;
    return true;
}
static_assert(formsAgreeWithRealm(), "a bar entry disagrees with kLocalForms about its class or form id");

/// And every form the realm models must be reachable from the bar. Dire Bear is
/// the one deliberate exception: it replaces Bear Form on the same button rather
/// than adding one, so a druid who has learned it has two spells for one slot.
constexpr bool realmFormsAreOnTheBar() {
    for (const LocalFormProfile& profile : kLocalForms) {
        if (profile.spell == SPELL_DIRE_BEAR_FORM) continue;
        bool found = false;
        for (const BarEntry& entry : kBar) if (entry.spellId == profile.spell) found = true;
        if (!found) return false;
    }
    return true;
}
static_assert(realmFormsAreOnTheBar(), "a form the realm models has no bar entry");

}  // namespace

std::vector<ShapeshiftForm> allShapeshiftForms(uint8_t classId) {
    std::vector<ShapeshiftForm> out;
    for (const BarEntry& entry : kBar)
        if (entry.classId == classId) out.push_back({entry.spellId, entry.formId, entry.name, entry.icon});
    return out;
}

}  // namespace wowee::game
