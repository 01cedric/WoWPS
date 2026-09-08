#include "game/text_tokens.hpp"

#include "game/character.hpp"

#include <string>
#include <vector>

namespace wowee {
namespace game {

std::string resolveTextTokens(const std::string& text, const TextSubject& subject) {
    // Fast path: the vast majority of chat messages contain no $ tokens, so
    // skip the gender/character lookup, the vector allocations below, and the
    // unconditional string copy. Returning `text` still constructs the result,
    // but the caller's by-value receiver was going to do that either way.
    if (text.find('$') == std::string::npos) {
        return text;
    }

    const Gender gender = subject.gender;
    const std::string& playerName = subject.name;
    const Pronouns pronouns = Pronouns::forGender(gender);

    std::string result = text;

    // Helper to trim whitespace
    auto trimStr = [](std::string& s) {
        const char* ws = " \t\n\r";
        size_t start = s.find_first_not_of(ws);
        if (start == std::string::npos) { s.clear(); return; }
        size_t end = s.find_last_not_of(ws);
        s = s.substr(start, end - start + 1);
    };

    // Replace $g/$G placeholders first.
    size_t pos = 0;
    while ((pos = result.find('$', pos)) != std::string::npos) {
        if (pos + 1 >= result.length()) break;
        char marker = result[pos + 1];
        if (marker != 'g' && marker != 'G') { pos++; continue; }

        size_t endPos = result.find(';', pos);
        if (endPos == std::string::npos) { pos += 2; continue; }

        std::string placeholder = result.substr(pos + 2, endPos - pos - 2);

        // Split by colons
        std::vector<std::string> parts;
        size_t start = 0;
        size_t colonPos;
        while ((colonPos = placeholder.find(':', start)) != std::string::npos) {
            std::string part = placeholder.substr(start, colonPos - start);
            trimStr(part);
            parts.push_back(part);
            start = colonPos + 1;
        }
        // Add the last part
        std::string lastPart = placeholder.substr(start);
        trimStr(lastPart);
        parts.push_back(lastPart);

        // Select appropriate text based on gender
        std::string replacement;
        if (parts.size() >= 3) {
            switch (gender) {
                case Gender::MALE:      replacement = parts[0]; break;
                case Gender::FEMALE:    replacement = parts[1]; break;
                case Gender::NONBINARY: replacement = parts[2]; break;
            }
        } else if (parts.size() >= 2) {
            switch (gender) {
                case Gender::MALE:   replacement = parts[0]; break;
                case Gender::FEMALE: replacement = parts[1]; break;
                case Gender::NONBINARY:
                    replacement = parts[0].length() <= parts[1].length() ? parts[0] : parts[1];
                    break;
            }
        } else {
            pos = endPos + 1;
            continue;
        }

        result.replace(pos, endPos - pos + 1, replacement);
        pos += replacement.length();
    }

    const std::string& className = subject.className;
    const std::string& raceName = subject.raceName;

    // Replace simple placeholders.
    // $n/$N = player name, $c/$C = class name, $r/$R = race name
    // $p = subject pronoun (he/she/they)
    // $o = object pronoun (him/her/them)
    // $s = possessive adjective (his/her/their)
    // $S = possessive pronoun (his/hers/theirs)
    // $b/$B = line break
    pos = 0;
    while ((pos = result.find('$', pos)) != std::string::npos) {
        if (pos + 1 >= result.length()) break;

        char code = result[pos + 1];
        std::string replacement;
        switch (code) {
            case 'n': case 'N': replacement = playerName; break;
            case 'c': case 'C': replacement = className; break;
            case 'r': case 'R': replacement = raceName; break;
            case 'p': replacement = pronouns.subject; break;
            case 'o': replacement = pronouns.object; break;
            case 's': replacement = pronouns.possessive; break;
            case 'S': replacement = pronouns.possessiveP; break;
            case 'b': case 'B': replacement = "\n"; break;
            case 'g': case 'G': pos++; continue;
            default: pos++; continue;
        }

        result.replace(pos, 2, replacement);
        pos += replacement.length();
    }

    // WoW markup linebreak token.
    pos = 0;
    while ((pos = result.find("|n", pos)) != std::string::npos) {
        result.replace(pos, 2, "\n");
        pos += 1;
    }
    pos = 0;
    while ((pos = result.find("|N", pos)) != std::string::npos) {
        result.replace(pos, 2, "\n");
        pos += 1;
    }

    return result;
}

}  // namespace game
}  // namespace wowee
