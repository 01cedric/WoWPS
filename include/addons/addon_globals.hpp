#pragma once

/// Reading out of an addon's own files the globals it will define once it
/// loads. Those names must answer as absent until then: FrameXML decides
/// whether to load a panel by asking for it, and a truthy stand-in makes every
/// one of those tests read as "already loaded".
///
/// Here rather than inside the addon manager so the two parsers can be tested
/// directly. Naming one global too many is the failure that matters - that
/// name reads as absent for the whole session - so both are deliberately
/// strict, and the tests pin the cases they must not claim.

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace wowee::addons {

inline bool isIdentifier(const std::string& s) {
    if (s.empty() || (!std::isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_'))
        return false;
    for (char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    return true;
}

/// The globals one Lua file defines: functions declared at any indent, and
/// assignments written hard against the left margin.
///
/// Column zero is the whole test for an assignment, and it is deliberately
/// strict - an indented `SomeGlobal = 1` inside a function body is a global
/// too, but so is every local this cannot tell it from. Naming too few leaves
/// a global answering the no-op it answers today; naming one too many makes a
/// name FrameXML needs read as absent, which is the louder failure.
inline void collectLuaGlobals(const std::string& text, std::vector<std::string>& out) {
    size_t at = 0;
    while (at <= text.size()) {
        const size_t eol = text.find('\n', at);
        const std::string line = text.substr(at, eol == std::string::npos ? eol : eol - at);
        at = (eol == std::string::npos) ? text.size() + 1 : eol + 1;

        size_t i = line.find_first_not_of(" \t");
        if (i == std::string::npos) continue;
        // `function Name(`, `function Name:Method(`, `function Name.field(` -
        // the last two need Name to exist already, so it is the addon's too.
        // `local function` does not match, because the line starts with local.
        if (line.compare(i, 9, "function ") == 0) {
            const size_t start = i + 9;
            size_t end = start;
            while (end < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_'))
                ++end;
            const std::string name = line.substr(start, end - start);
            if (isIdentifier(name)) out.push_back(name);
            continue;
        }
        if (i != 0) continue;
        size_t end = 0;
        while (end < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_'))
            ++end;
        if (end == 0) continue;
        size_t eq = line.find_first_not_of(" \t", end);
        if (eq == std::string::npos || line[eq] != '=') continue;
        if (eq + 1 < line.size() && line[eq + 1] == '=') continue;  // a comparison
        const std::string name = line.substr(0, end);
        if (isIdentifier(name)) out.push_back(name);
    }
}

/// The frames one XML file names. Virtual elements are templates rather than
/// frames, so their name never becomes a global and listing one would make a
/// real global of the same name read as absent.
inline void collectXmlNames(const std::string& text, std::vector<std::string>& out) {
    size_t at = 0;
    while ((at = text.find("name=\"", at)) != std::string::npos) {
        const size_t start = at + 6;
        const size_t end = text.find('"', start);
        if (end == std::string::npos) break;
        const std::string name = text.substr(start, end - start);
        at = end + 1;
        if (!isIdentifier(name)) continue;  // $parent-built, so never a literal
        const size_t tag = text.rfind('<', start);
        if (tag == std::string::npos) continue;
        // The whole element, not the part before the name: virtual is written
        // on either side of it and reading only one side finds half of them.
        const size_t tagEnd = text.find('>', start);
        const std::string element =
            text.substr(tag, (tagEnd == std::string::npos ? text.size() : tagEnd) - tag);
        if (element.find("virtual=\"true\"") != std::string::npos) continue;
        out.push_back(name);
    }
}

} // namespace wowee::addons
