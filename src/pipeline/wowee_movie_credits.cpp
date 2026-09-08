#include "pipeline/wowee_movie_credits.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'V', 'C'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wmvc";

} // namespace

const WoweeMovieCredits::Entry*
WoweeMovieCredits::findById(uint32_t rollId) const {
    for (const auto& e : entries)
        if (e.rollId == rollId) return &e;
    return nullptr;
}

std::vector<const WoweeMovieCredits::Entry*>
WoweeMovieCredits::findByCinematic(uint32_t cinematicId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.cinematicId == cinematicId) out.push_back(&e);
    std::sort(out.begin(), out.end(),
              [](const Entry* a, const Entry* b) {
                  return a->orderHint < b->orderHint;
              });
    return out;
}

bool WoweeMovieCreditsLoader::save(const WoweeMovieCredits& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeMovieCredits::Entry& e) {
        writePOD(os, e.rollId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.cinematicId);
        writePOD(os, e.category);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.orderHint);
        writePOD(os, e.pad4);
        writePOD(os, e.pad5);
        writePOD(os, e.iconColorRGBA);
        uint32_t lineCount = static_cast<uint32_t>(
            e.lines.size());
        writePOD(os, lineCount);
        for (const auto& L : e.lines) writeStr(os, L);
                       });
}

WoweeMovieCredits WoweeMovieCreditsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeMovieCredits>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeMovieCredits::Entry& e) {
        if (!readPOD(is, e.rollId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.cinematicId) ||
            !readPOD(is, e.category) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.orderHint) ||
            !readPOD(is, e.pad4) ||
            !readPOD(is, e.pad5) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
        uint32_t lineCount = 0;
        if (!readPOD(is, lineCount)) { return false; }
        if (lineCount > 4096) { return false; }
        e.lines.resize(lineCount);
        for (uint32_t k = 0; k < lineCount; ++k) {
            if (!readStr(is, e.lines[k])) { return false; }
        }
                                  return true;
                              });
}

bool WoweeMovieCreditsLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeMovieCredits WoweeMovieCreditsLoader::makeWotLKIntro(
    const std::string& catalogName) {
    using M = WoweeMovieCredits;
    WoweeMovieCredits c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t cat,
                    uint16_t order,
                    std::vector<std::string> lines,
                    uint32_t color, const char* desc) {
        M::Entry e;
        e.rollId = id; e.name = name; e.description = desc;
        e.cinematicId = 100;        // WotLK intro cinematic
        e.category = cat;
        e.orderHint = order;
        e.lines = std::move(lines);
        e.iconColorRGBA = color;
        c.entries.push_back(e);
    };
    add(1, "WotLK_Production", M::Production, 10,
        {
            "DIRECTOR",
            "Cinematic Director (placeholder)",
            "EXECUTIVE PRODUCER",
            "Cinematic Producer (placeholder)",
            "PRODUCER",
            "Production Coordinator (placeholder)",
        },
        packRgba(220, 220, 100),
        "WotLK intro - Production block. 6 lines: 3 "
        "title + 3 name pairs.");
    add(2, "WotLK_Direction", M::Production, 20,
        {
            "ART DIRECTION",
            "Art Director (placeholder)",
            "TECHNICAL DIRECTION",
            "Tech Director (placeholder)",
        },
        packRgba(220, 200, 80),
        "WotLK intro - Direction block. 4 lines.");
    add(3, "WotLK_Music", M::Music, 30,
        {
            "ORIGINAL SCORE COMPOSED BY",
            "Russell Brower",
            "Derek Duke",
            "Glenn Stafford",
            "ADDITIONAL MUSIC",
            "Jason Hayes",
        },
        packRgba(180, 100, 240),
        "WotLK intro - Music block. The actual WoTLK "
        "score credits, 6 lines.");
    add(4, "WotLK_Voice", M::Voice, 40,
        {
            "VOICE CAST",
            "Arthas Menethil  ......  Patrick Seitz",
            "King Terenas .........  Earl Boen",
            "Narrator ............  Patrick Seitz",
        },
        packRgba(255, 220, 220),
        "WotLK intro - Voice cast block. The iconic "
        "Arthas/Terenas exchange in the cinematic.");
    add(5, "WotLK_SpecialThanks", M::Special, 90,
        {
            "SPECIAL THANKS",
            "All the players who beta-tested the expansion",
            "The Blizzard QA team",
            "Our families and loved ones",
            "FOR THE LICH KING",
        },
        packRgba(180, 220, 255),
        "WotLK intro - Special Thanks block. End of "
        "the credit roll, traditionally last.");
    return c;
}

WoweeMovieCredits WoweeMovieCreditsLoader::makeQuestCinema(
    const std::string& catalogName) {
    using M = WoweeMovieCredits;
    WoweeMovieCredits c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t cat,
                    uint16_t order,
                    std::vector<std::string> lines,
                    uint32_t color, const char* desc) {
        M::Entry e;
        e.rollId = id; e.name = name; e.description = desc;
        e.cinematicId = 200;        // generic quest cine
        e.category = cat;
        e.orderHint = order;
        e.lines = std::move(lines);
        e.iconColorRGBA = color;
        c.entries.push_back(e);
    };
    add(100, "QuestCine_Designer", M::Production, 10,
        {
            "QUEST DESIGN",
            "Quest Designer (placeholder)",
        },
        packRgba(140, 200, 255),
        "Per-quest cinematic - Designer credit. Two "
        "lines: title + name.");
    add(101, "QuestCine_Voice", M::Voice, 20,
        {
            "VOICE",
            "NPC Voice Actor (placeholder)",
        },
        packRgba(255, 220, 220),
        "Per-quest cinematic - single voice credit.");
    add(102, "QuestCine_Director", M::Production, 30,
        {
            "CINEMATIC DIRECTOR",
            "Director (placeholder)",
        },
        packRgba(220, 220, 100),
        "Per-quest cinematic - Cinematic Director "
        "credit. Always last per Blizzard convention.");
    return c;
}

WoweeMovieCredits WoweeMovieCreditsLoader::makeStarterRoll(
    const std::string& catalogName) {
    using M = WoweeMovieCredits;
    WoweeMovieCredits c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t cat,
                    uint16_t order,
                    std::vector<std::string> lines,
                    uint32_t color, const char* desc) {
        M::Entry e;
        e.rollId = id; e.name = name; e.description = desc;
        e.cinematicId = 1;        // starter / vanilla intro
        e.category = cat;
        e.orderHint = order;
        e.lines = std::move(lines);
        e.iconColorRGBA = color;
        c.entries.push_back(e);
    };
    add(200, "Starter_Production", M::Production, 10,
        { "PRODUCTION", "Producer Name", "Co-Producer" },
        packRgba(220, 220, 100),
        "Generic starter cinematic - 3-line Production "
        "block.");
    add(201, "Starter_Engineering", M::Engineering, 20,
        { "ENGINEERING", "Lead Engineer", "Pipeline Tools" },
        packRgba(140, 200, 255),
        "Generic starter cinematic - 3-line Engineering "
        "block.");
    add(202, "Starter_Art", M::Art, 30,
        { "ART", "Concept Artist", "3D Modeler", "Animator" },
        packRgba(255, 180, 100),
        "Generic starter cinematic - 4-line Art block.");
    add(203, "Starter_Special", M::Special, 90,
        { "WITH SPECIAL THANKS TO", "Our players",
          "Our families" },
        packRgba(180, 220, 255),
        "Generic starter cinematic - 3-line Special "
        "Thanks block.");
    return c;
}

} // namespace pipeline
} // namespace wowee
