#pragma once

/**
 * character_paths.hpp - the folder a race's art lives in, and the texture paths
 * built from it.
 *
 * "Character\NightElf\Female\NightElfFemaleSkin00_00.blp" is assembled from a
 * race, a sex, and a naming convention. The convention is not quite mechanical -
 * the Undead are filed under Scourge, and the two-word races have no space - so
 * it is a table, and the table was written out four times: once for the local
 * player, once for every other player, once for the voice profile, and once
 * again in reverse in the portrait, which recovers the folder by slicing it back
 * out of the model path.
 *
 * A missing race in one copy does not raise anything. It falls through to the
 * default, and a Draenei quietly wears a human's skin.
 */

#include <cstdint>
#include <string>

namespace wowee {
namespace core {

/// The folder under Character\ that holds a race's models and textures.
///
/// Undead art is filed under Scourge, which is the one entry nobody remembers.
/// An unknown race id answers "Human", because a wrong body is more useful than
/// no body while something upstream is wrong.
inline const char* raceModelFolder(uint32_t raceId) {
    switch (raceId) {
        case 1:  return "Human";
        case 2:  return "Orc";
        case 3:  return "Dwarf";
        case 4:  return "NightElf";
        case 5:  return "Scourge";   // Undead
        case 6:  return "Tauren";
        case 7:  return "Gnome";
        case 8:  return "Troll";
        case 10: return "BloodElf";
        case 11: return "Draenei";
        default: return "Human";
    }
}

/// The sub-folder under the race folder. Sex 1 is female everywhere in the DBCs.
inline const char* sexModelFolder(uint32_t sexId) {
    return sexId == 1 ? "Female" : "Male";
}

/// The prefix the art files themselves carry: "NightElfFemale".
inline std::string characterArtPrefix(uint32_t raceId, uint32_t sexId) {
    return std::string(raceModelFolder(raceId)) + sexModelFolder(sexId);
}

/// The body texture a character falls back to when CharSections names none.
inline std::string defaultBodySkinPath(uint32_t raceId, uint32_t sexId) {
    return std::string("Character\\") + raceModelFolder(raceId) + "\\" +
           sexModelFolder(sexId) + "\\" + characterArtPrefix(raceId, sexId) +
           "Skin00_00.blp";
}

/// The underwear texture a character falls back to when CharSections names none.
inline std::string defaultPelvisPath(uint32_t raceId, uint32_t sexId) {
    return std::string("Character\\") + raceModelFolder(raceId) + "\\" +
           sexModelFolder(sexId) + "\\" + characterArtPrefix(raceId, sexId) +
           "NakedPelvisSkin00_00.blp";
}

/// The four appearance choices packed into PLAYER_BYTES.
///
/// Which byte is which is not guessable and was unpacked by hand wherever it was
/// needed. Reading face where skin was meant does not fail - it draws a face.
struct AppearanceBytes {
    uint8_t skinId = 0;
    uint8_t faceId = 0;
    uint8_t hairStyleId = 0;
    uint8_t hairColorId = 0;
};

inline AppearanceBytes unpackAppearanceBytes(uint32_t bytes) {
    AppearanceBytes a;
    a.skinId      = static_cast<uint8_t>(bytes & 0xFF);
    a.faceId      = static_cast<uint8_t>((bytes >> 8) & 0xFF);
    a.hairStyleId = static_cast<uint8_t>((bytes >> 16) & 0xFF);
    a.hairColorId = static_cast<uint8_t>((bytes >> 24) & 0xFF);
    return a;
}

}  // namespace core
}  // namespace wowee
