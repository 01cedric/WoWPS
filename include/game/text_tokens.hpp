#pragma once

/**
 * text_tokens.hpp - the $-tokens WoW's text is written with.
 *
 * Quest text, gossip, item text and what an NPC says all arrive from the server
 * with the player left as a blank: "$N, I need your help" and "$gsir:madam;".
 * The client is what fills them in, which is why the same quest reads
 * differently to two people.
 *
 * This lives in the game layer rather than beside the chat window because the
 * chat handler has to resolve a monster's line before it reaches the interface,
 * and it cannot reach into the UI to do it.
 */

#include <string>

#include "game/character.hpp"

namespace wowee {
namespace game {

class GameHandler;

/// Who the text is being written for. Everything resolveTextTokens needs to
/// know about the reader, and nothing else - so the substitution itself can be
/// exercised without a client attached to it.
struct TextSubject {
    std::string name = "Adventurer";
    std::string className = "Adventurer";
    std::string raceName = "Unknown";
    Gender gender = Gender::NONBINARY;
};

/// The substitution itself.
std::string resolveTextTokens(const std::string& text, const TextSubject& subject);

/// The logged-in character, as the substitution sees them.
TextSubject textSubjectFor(GameHandler& gameHandler);

/// Fill in the $-tokens in a piece of server text for the logged-in character.
///
/// $n/$N name, $c/$C class, $r/$R race, $b/$B a line break, $p/$o/$s/$S the
/// pronouns, and $gmale:female; - or male:female:neutral; - choosing by gender.
/// A `|n` is a line break too. Text with no '$' in it comes straight back.
std::string resolveTextTokens(const std::string& text, GameHandler& gameHandler);

}  // namespace game
}  // namespace wowee
