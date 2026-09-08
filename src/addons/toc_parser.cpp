#include "addons/toc_parser.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace wowee::addons {

std::string TocFile::getTitle() const {
    auto it = directives.find("Title");
    return (it != directives.end()) ? it->second : addonName;
}
bool TocFile::isLoadOnDemand() const {
    auto it = directives.find("LoadOnDemand");
    return (it != directives.end()) && it->second == "1";
}

/// The spelling FrameXML uses for a Blizzard addon, given the one on disk.
///
/// An addon's name is its folder's name, and this install's folders are lower
/// case - the extraction flattened them. Loading copes: every lookup here folds
/// case before matching. Announcing does not, because the announcement goes to
/// Lua and Lua's == is case-sensitive:
///
///     if ( name == "Blizzard_GlyphUI" and IsAddOnLoaded("Blizzard_TalentUI") ...
///
/// That is GlyphFrame_OnEvent, and inside it is the only place the glyph panel
/// is ever parented to the talent frame, sized to it, and its close button
/// raised above it. Fired as "blizzard_glyphui" the test simply failed, so the
/// panel stayed a child of UIParent in the top-left corner of the screen with
/// no close button on it - which is exactly what it looked like.
///
/// Every addon that does its setup on ADDON_LOADED and names itself has the
/// same problem, so this is fixed once, here, where the name is decided.
///
/// The spellings are FrameXML's own, taken from the files that compare against
/// them rather than guessed: Blizzard_BarberShopUI capitalises the S and
/// Blizzard_GMChatUI capitalises both letters of GM, and a guess would have had
/// either wrong.
static std::string canonicalAddonName(const std::string& onDisk) {
    static const char* kKnown[] = {
        "Blizzard_AchievementUI",   "Blizzard_ArenaUI",
        "Blizzard_AuctionUI",       "Blizzard_BarberShopUI",
        "Blizzard_BattlefieldMinimap", "Blizzard_BindingUI",
        "Blizzard_Calendar",        "Blizzard_CombatLog",
        "Blizzard_CombatText",      "Blizzard_DebugTools",
        "Blizzard_GlyphUI",         "Blizzard_GMChatUI",
        "Blizzard_GMSurveyUI",      "Blizzard_GuildBankUI",
        "Blizzard_InspectUI",       "Blizzard_ItemSocketingUI",
        "Blizzard_MacroUI",         "Blizzard_RaidUI",
        "Blizzard_TalentUI",        "Blizzard_TimeManager",
        "Blizzard_TokenUI",         "Blizzard_TradeSkillUI",
        "Blizzard_TrainerUI",
    };
    auto fold = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return s;
    };
    const std::string key = fold(onDisk);
    for (const char* known : kKnown) {
        if (fold(known) == key) return known;
    }
    // Anything else keeps the name it has. A third-party addon's folder is
    // already spelled the way its own files expect.
    return onDisk;
}

static std::vector<std::string> parseVarList(const std::string& val) {
    std::vector<std::string> result;
    size_t pos = 0;
    while (pos <= val.size()) {
        size_t comma = val.find(',', pos);
        std::string name = (comma != std::string::npos) ? val.substr(pos, comma - pos) : val.substr(pos);
        size_t start = name.find_first_not_of(" \t");
        size_t end = name.find_last_not_of(" \t");
        if (start != std::string::npos)
            result.push_back(name.substr(start, end - start + 1));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return result;
}

std::vector<std::string> TocFile::getSavedVariables() const {
    auto it = directives.find("SavedVariables");
    return (it != directives.end()) ? parseVarList(it->second) : std::vector<std::string>{};
}

std::vector<std::string> TocFile::getSavedVariablesPerCharacter() const {
    auto it = directives.find("SavedVariablesPerCharacter");
    return (it != directives.end()) ? parseVarList(it->second) : std::vector<std::string>{};
}

std::optional<TocFile> parseTocFile(const std::string& tocPath) {
    std::ifstream f(tocPath);
    if (!f.is_open()) return std::nullopt;

    std::stringstream text; text << f.rdbuf();
    return parseTocText(text.str(), tocPath);
}

TocFile parseTocText(const std::string& text, const std::string& tocPath) {
    std::istringstream f(text);
    TocFile toc;
    toc.basePath = tocPath;
    // Strip filename to get directory
    size_t lastSlash = tocPath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        toc.basePath = tocPath.substr(0, lastSlash);
        toc.addonName = tocPath.substr(lastSlash + 1);
    }
    // Strip .toc extension from addon name
    size_t dotPos = toc.addonName.rfind(".toc");
    if (dotPos != std::string::npos) toc.addonName.resize(dotPos);
    toc.addonName = canonicalAddonName(toc.addonName);

    std::string line;
    while (std::getline(f, line)) {
        // Strip trailing CR (Windows line endings)
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Skip empty lines
        if (line.empty()) continue;

        // ## directives
        if (line.size() >= 3 && line[0] == '#' && line[1] == '#') {
            std::string directive = line.substr(2);
            size_t colon = directive.find(':');
            if (colon != std::string::npos) {
                std::string key = directive.substr(0, colon);
                std::string val = directive.substr(colon + 1);
                // Trim whitespace
                auto trim = [](std::string& s) {
                    size_t start = s.find_first_not_of(" \t");
                    size_t end = s.find_last_not_of(" \t");
                    s = (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
                };
                trim(key);
                trim(val);
                if (!key.empty()) toc.directives[key] = val;
            }
            continue;
        }

        // Single # comment
        if (line[0] == '#') continue;

        // Whitespace-only line
        size_t firstNonSpace = line.find_first_not_of(" \t");
        if (firstNonSpace == std::string::npos) continue;

        // File entry - normalize backslashes to forward slashes
        std::string filename = line.substr(firstNonSpace);
        size_t lastNonSpace = filename.find_last_not_of(" \t");
        if (lastNonSpace != std::string::npos) filename.resize(lastNonSpace + 1);
        std::replace(filename.begin(), filename.end(), '\\', '/');
        toc.files.push_back(std::move(filename));
    }

    return toc;
}

} // namespace wowee::addons
