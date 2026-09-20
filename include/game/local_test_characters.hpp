#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace wowee::game {

/// One character the test-character seeder creates. `level` is the level the
/// character is created AT, not a level it is raised to afterwards:
/// LocalGameplay::initializePlayer derives the spellbook, the resource type and
/// the health/mana pools from it, so a forced level has to arrive before that
/// derivation rather than be written over the top of it.
struct LocalTestCharacterSpec {
    uint8_t slot = 0, race = 1, classId = 1, gender = 0, level = 1;
    std::string name;
};

/// Ten level-80 characters, one per playable class, each a different race.
///
/// The pairs are NOT remembered; they are the client's own. CharBaseInfo.dbc
/// (62 two-byte race/class rows) and the shipped world catalog's `starts()`
/// table (62 rows) agree exactly, and LocalGameplay::validCharacterOptions
/// encodes the same 62, so every pair below is admitted by all three. Races are
/// all different, which the character screen needs to tell the ten apart, and
/// the split happens to be five Alliance and five Horde.
///
///   Warrior  1  Human     1      Death Knight  6  Blood Elf 10
///   Paladin  2  Dwarf     3      Shaman        7  Draenei   11
///   Hunter   3  Night Elf 4      Mage          8  Undead     5
///   Rogue    4  Gnome     7      Warlock       9  Orc        2
///   Priest   5  Troll     8      Druid        11  Tauren     6
///
/// Druid is the binding constraint: only Night Elf and Tauren may be one.
/// Warlock cannot be a Dwarf, a Night Elf, a Troll, a Tauren or a Draenei, and
/// Blood Elf cannot be a Warrior, which together fix the remainder once Druid
/// takes Tauren and Shaman takes Draenei.
inline constexpr uint8_t kLocalTestCharacterLevel = 80;
inline constexpr size_t kLocalTestCharacterCount = 10;

/// The table itself, as plain data so the suite and the console read one copy.
struct LocalTestCharacterRow { uint8_t slot, race, classId; const char* name; };
inline constexpr LocalTestCharacterRow kLocalTestCharacters[kLocalTestCharacterCount] = {
    {0,  1,  1, "HumanWarrior"},
    {1,  3,  2, "DwarfPaladin"},
    {2,  4,  3, "NightelfHunter"},
    {3,  7,  4, "GnomeRogue"},
    {4,  8,  5, "TrollPriest"},
    {5, 10,  6, "BloodelfDK"},
    {6, 11,  7, "DraeneiShaman"},
    {7,  5,  8, "UndeadMage"},
    {8,  2,  9, "OrcWarlock"},
    {9,  6, 11, "TaurenDruid"},
};

}  // namespace wowee::game
