#include "pipeline/wowee_word_filters.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'W', 'F', 'L'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wwfl";

} // namespace

const WoweeWordFilters::Entry*
WoweeWordFilters::findById(uint32_t filterId) const {
    for (const auto& e : entries)
        if (e.filterId == filterId) return &e;
    return nullptr;
}

std::vector<const WoweeWordFilters::Entry*>
WoweeWordFilters::findByKind(uint8_t filterKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.filterKind == filterKind) out.push_back(&e);
    return out;
}

bool WoweeWordFiltersLoader::save(const WoweeWordFilters& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeWordFilters::Entry& e) {
        writePOD(os, e.filterId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.pattern);
        writeStr(os, e.replacement);
        writePOD(os, e.filterKind);
        writePOD(os, e.severity);
        writePOD(os, e.caseSensitive);
        writePOD(os, e.pad0);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeWordFilters WoweeWordFiltersLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeWordFilters>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeWordFilters::Entry& e) {
        if (!readPOD(is, e.filterId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readStr(is, e.pattern) ||
            !readStr(is, e.replacement)) { return false; }
        if (!readPOD(is, e.filterKind) ||
            !readPOD(is, e.severity) ||
            !readPOD(is, e.caseSensitive) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeWordFiltersLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeWordFilters WoweeWordFiltersLoader::makeSpamRMT(
    const std::string& catalogName) {
    using F = WoweeWordFilters;
    WoweeWordFilters c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    const char* pattern, const char* repl,
                    uint8_t severity, uint8_t caseSens,
                    const char* desc) {
        F::Entry e;
        e.filterId = id; e.name = name; e.description = desc;
        e.pattern = pattern;
        e.replacement = repl;
        e.filterKind = F::GoldSeller;
        e.severity = severity;
        e.caseSensitive = caseSens;
        e.iconColorRGBA = packRgba(220, 200, 80);   // RMT yellow
        c.entries.push_back(e);
    };
    // RMT-pattern detection. All examples are PG -
    // generic gold-seller phrases without profanity.
    add(1, "WtsGold",
        "wts gold", "***",
        F::Drop, 0,
        "'wts gold' (Want To Sell) RMT solicitation. "
        "Drop the message; warn server moderators.");
    add(2, "WtbGold",
        "wtb gold", "***",
        F::Drop, 0,
        "'wtb gold' (Want To Buy) RMT solicitation.");
    add(3, "GoldTypoSubstitution",
        "g0ld", "gold",
        F::Replace, 0,
        "Common typo-substitution to bypass exact-string "
        "filters: 'g0ld' (zero instead of o). Replace "
        "with 'gold' so the message gets normalized then "
        "re-checked by other filters.");
    add(4, "BulkGoldOffer",
        "1000g for", "***",
        F::Drop, 0,
        "Common gold-seller offer pattern: '1000g for "
        "$X' or '1000g for cheap'. Match the prefix.");
    add(5, "FreeGold",
        "free gold", "***",
        F::Mute, 0,
        "'free gold' adverts - almost always RMT or "
        "phishing. Mute sender for 60s + drop message.");
    return c;
}

WoweeWordFilters WoweeWordFiltersLoader::makeAllCaps(
    const std::string& catalogName) {
    using F = WoweeWordFilters;
    WoweeWordFilters c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    const char* pattern, const char* repl,
                    uint8_t severity, uint8_t caseSens,
                    const char* desc) {
        F::Entry e;
        e.filterId = id; e.name = name; e.description = desc;
        e.pattern = pattern;
        e.replacement = repl;
        e.filterKind = F::AllCaps;
        e.severity = severity;
        e.caseSensitive = caseSens;
        e.iconColorRGBA = packRgba(220, 80, 100);   // shout red
        c.entries.push_back(e);
    };
    add(100, "AllCapsWord",
        "ANYBODY",
        "anybody",
        F::Replace, 1,
        "Single common all-caps word - replace with "
        "lowercase. Case-sensitive match (caseSens=1) so "
        "'Anybody' isn't affected.");
    add(101, "AllCapsExclamation",
        "!!!",
        "!",
        F::Replace, 0,
        "Triple-exclamation overuse. Collapse to single "
        "'!' so emphasis stays but spam-style "
        "punctuation is normalized.");
    add(102, "DollarSpam",
        "$$$",
        "***",
        F::Replace, 0,
        "Money-emphasis spam ('$$$ FOR YOU!!!' style). "
        "Replace with redaction marks.");
    return c;
}

WoweeWordFilters WoweeWordFiltersLoader::makeURLDetect(
    const std::string& catalogName) {
    using F = WoweeWordFilters;
    WoweeWordFilters c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    const char* pattern, const char* repl,
                    uint8_t severity, uint8_t caseSens,
                    const char* desc) {
        F::Entry e;
        e.filterId = id; e.name = name; e.description = desc;
        e.pattern = pattern;
        e.replacement = repl;
        e.filterKind = F::URL;
        e.severity = severity;
        e.caseSensitive = caseSens;
        e.iconColorRGBA = packRgba(140, 200, 255);   // URL blue
        c.entries.push_back(e);
    };
    add(200, "HttpUrl",
        "http://", "[link]",
        F::Replace, 0,
        "HTTP URL - replace with [link] placeholder. "
        "Server admins can decide per-channel whether "
        "to permit links via WCHN config.");
    add(201, "HttpsUrl",
        "https://", "[link]",
        F::Replace, 0,
        "HTTPS URL - same handling as HTTP.");
    add(202, "WwwShortUrl",
        "www.", "[link]",
        F::Replace, 0,
        "Bare www.example URL - common shortening when "
        "the http:// prefix is omitted. Catch-all.");
    return c;
}

} // namespace pipeline
} // namespace wowee
