#pragma once

#include "game/world_packets.hpp"
#include "game/entity.hpp"
#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

namespace wowee {

// Forward declarations
namespace game {
    class GameHandler;
}

namespace ui {
namespace chat_utils {

/** Create a system-type chat message (used 15+ times throughout commands). */
inline game::MessageChatData makeSystemMessage(const std::string& text) {
    game::MessageChatData msg;
    msg.type     = game::ChatType::SYSTEM;
    msg.language = game::ChatLanguage::UNIVERSAL;
    msg.message  = text;
    return msg;
}

/** Trim leading/trailing whitespace from a string. */
inline std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

/** Convert string to lowercase (returns copy). */
inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

/**
 * Replace $g/$G gender, $n/$N name, $c/$C class, $r/$R race,
 * $p/$o/$s/$S pronoun, $b/$B linebreak, and |n linebreak placeholders.
 * Extracted from ChatPanel::replaceGenderPlaceholders (Phase 6.6).
 */
std::string replaceGenderPlaceholders(const std::string& text,
                                       game::GameHandler& gameHandler);

// getEntityDisplayName was here, as a sixth copy of game::entityDisplayName
// under a different name. Callers use that one now - it is beside the classes
// whose names it is asking about.

/// The PortBot destination a whispered word means, or "" for none.
///
/// The chat panel's whisper path and the /whisper command each had an
/// identical copy of this, so the same whisper behaved differently depending
/// on which of the two a player happened to type it through - and each carried
/// its own help line listing the aliases, which is a third copy of the same
/// vocabulary and the one that goes stale first.
std::string portBotCommandFor(const std::string& rawInput);

/// The line to print when someone asks PortBot for help. Built from the same
/// table, so an alias added is an alias mentioned.
std::string portBotHelpText();

/// Whether a whisper target is the teleport bot.
bool isPortBotTarget(const std::string& target);

} // namespace chat_utils
} // namespace ui
} // namespace wowee
