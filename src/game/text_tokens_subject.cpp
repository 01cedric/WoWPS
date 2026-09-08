#include "game/text_tokens.hpp"

#include "game/game_handler.hpp"

namespace wowee {
namespace game {

// The half that needs a client. Kept out of text_tokens.cpp so the substitution
// itself links against nothing and can be exercised on its own.

TextSubject textSubjectFor(GameHandler& gameHandler) {
    TextSubject subject;
    if (const auto* character = gameHandler.getActiveCharacter()) {
        subject.gender = character->gender;
        if (!character->name.empty()) subject.name = character->name;
        subject.className = getClassName(character->characterClass);
        subject.raceName = getRaceName(character->race);
    }
    return subject;
}

std::string resolveTextTokens(const std::string& text, GameHandler& gameHandler) {
    return resolveTextTokens(text, textSubjectFor(gameHandler));
}

}  // namespace game
}  // namespace wowee
