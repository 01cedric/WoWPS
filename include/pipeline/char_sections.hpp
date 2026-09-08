#pragma once

/**
 * char_sections.hpp - reading a character's textures out of CharSections.dbc.
 *
 * One scan of that table, used by everything that draws a character: the player,
 * an NPC, another player, and the portrait.
 *
 * It was written three times before this - once in appearance_composer for the
 * player, once in entity_spawner for NPCs, once in character_preview for the
 * portrait - and the three did not agree. Only one of them read the skin row's
 * second texture, so ears and eyelashes were unbound everywhere else. Only one
 * had a fallback for a face the table does not carry. Every fault found in this
 * area had to be fixed two or three times, and each time one was missed the
 * report came back as "correct in the portrait, wrong in the world".
 *
 * The scan is here. What a caller does with the paths - composite them, hand
 * them to a model, put them in a portrait - stays with the caller.
 */

#include <cstdint>
#include <string>
#include <vector>

#include "pipeline/dbc_layout.hpp"

namespace wowee {
namespace pipeline {

class DBCFile;
struct M2Model;

/// Which character these textures are for.
struct CharacterAppearance {
    uint32_t raceId = 0;
    uint32_t sexId = 0;      ///< 0 male, 1 female
    uint8_t skinId = 0;
    uint8_t faceId = 0;
    uint8_t hairStyleId = 0;
    uint8_t hairColorId = 0;
};

/// The textures CharSections names for that character.
struct CharacterSectionTextures {
    std::string bodySkin;
    /// The skin row's SECOND texture. Blank in the tables the game shipped and
    /// filled in by an HD one, where it is the art a model asks for as texture
    /// type 8 - the ears, the eyes, the mouth, the eyelashes.
    std::string skinExtra;
    std::string faceLower;
    std::string faceUpper;
    std::string hair;
    std::vector<std::string> underwear;

    /// True when the exact (face, skin) pair was found rather than approximated.
    bool exactFace = false;
    /// True when a face was found at all, exact or not.
    bool haveFace = false;
    bool haveHair = false;
};

/// Scan CharSections.dbc for one character's textures.
///
/// `keepUnderwear` is asked for each underwear path before it is kept - the
/// tables name art that is not always present, and a caller that can check the
/// filesystem should. Pass nothing to keep them all.
CharacterSectionTextures resolveCharacterSections(
    const DBCFile* charSections,
    const CharSectionsFields& fields,
    const CharacterAppearance& who,
    bool (*keepUnderwear)(const std::string&, void*) = nullptr,
    void* keepUnderwearContext = nullptr);

/// Put the resolved textures into a character model's runtime texture slots.
///
/// A non-zero texture type means the client supplies the art - that is what the
/// type is for, and only type 0 carries a filename that means anything. So a
/// name found in one of these slots is not authoritative and must not be
/// treated as one: a model in the wild has 'Ohren' baked into its Skin Extra
/// slot, and every copy of this loop that filled "only if empty" left that name
/// in place, resolved it to nothing, and drew the ears and eyelashes from a
/// blank.
///
/// `raceFolderName` is used only for the last-resort hair path.
void applyCharacterTextures(M2Model& model,
                            const CharacterSectionTextures& textures,
                            const std::string& raceFolderName);

}  // namespace pipeline
}  // namespace wowee
