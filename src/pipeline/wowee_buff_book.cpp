#include "pipeline/wowee_buff_book.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_set>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'B', 'A', 'B'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wbab";

} // namespace

const WoweeBuffBook::Entry*
WoweeBuffBook::findById(uint32_t buffId) const {
    for (const auto& e : entries)
        if (e.buffId == buffId) return &e;
    return nullptr;
}

std::vector<const WoweeBuffBook::Entry*>
WoweeBuffBook::walkChainBackToRoot(uint32_t buffId) const {
    std::vector<const Entry*> out;
    std::unordered_set<uint32_t> visited;
    const Entry* cur = findById(buffId);
    while (cur != nullptr && visited.insert(cur->buffId).second) {
        out.push_back(cur);
        if (cur->previousRankId == 0) break;
        cur = findById(cur->previousRankId);
    }
    // Reverse so output flows root → tip.
    for (size_t a = 0, b = out.size(); a + 1 < b; ++a, --b) {
        std::swap(out[a], out[b - 1]);
    }
    return out;
}

const WoweeBuffBook::Entry*
WoweeBuffBook::findChainTip(uint32_t buffId) const {
    const Entry* cur = findById(buffId);
    std::unordered_set<uint32_t> visited;
    while (cur != nullptr && cur->nextRankId != 0 &&
           visited.insert(cur->buffId).second) {
        cur = findById(cur->nextRankId);
    }
    return cur;
}

bool WoweeBuffBookLoader::save(const WoweeBuffBook& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeBuffBook::Entry& e) {
        writePOD(os, e.buffId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.spellId);
        writePOD(os, e.castClassMask);
        writePOD(os, e.targetTypeMask);
        writePOD(os, e.statBonusKind);
        writePOD(os, e.rank);
        writePOD(os, e.maxStackCount);
        writePOD(os, e.statBonusAmount);
        writePOD(os, e.duration);
        writePOD(os, e.previousRankId);
        writePOD(os, e.nextRankId);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeBuffBook WoweeBuffBookLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeBuffBook>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeBuffBook::Entry& e) {
        if (!readPOD(is, e.buffId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.spellId) ||
            !readPOD(is, e.castClassMask) ||
            !readPOD(is, e.targetTypeMask) ||
            !readPOD(is, e.statBonusKind) ||
            !readPOD(is, e.rank) ||
            !readPOD(is, e.maxStackCount) ||
            !readPOD(is, e.statBonusAmount) ||
            !readPOD(is, e.duration) ||
            !readPOD(is, e.previousRankId) ||
            !readPOD(is, e.nextRankId) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeBuffBookLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeBuffBook WoweeBuffBookLoader::makeMage(
    const std::string& catalogName) {
    using B = WoweeBuffBook;
    WoweeBuffBook c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t spellId, uint8_t rank,
                    int32_t statAmount, uint32_t prevId,
                    uint32_t nextId, const char* desc) {
        B::Entry e;
        e.buffId = id; e.name = name; e.description = desc;
        e.spellId = spellId;
        e.castClassMask = 128;        // Mage
        e.targetTypeMask = B::TargetSelf | B::TargetParty;
        e.statBonusKind = B::Intellect;
        e.rank = rank;
        e.maxStackCount = 1;
        e.statBonusAmount = statAmount;
        e.duration = 1800;            // 30 minutes
        e.previousRankId = prevId;
        e.nextRankId = nextId;
        e.iconColorRGBA = packRgba(140, 200, 255);   // mage blue
        c.entries.push_back(e);
    };
    // Arcane Intellect rank chain - spell IDs from
    // Spell.dbc 3.3.5a; intellect bonus per rank.
    add(1, "ArcaneIntellect_R1", 1459, 1,  3, 0, 2,
        "Arcane Intellect Rank 1 - +3 Intellect, "
        "30 min party-wide. Trained at level 8.");
    add(2, "ArcaneIntellect_R2", 1460, 2,  7, 1, 3,
        "Arcane Intellect Rank 2 - +7 Intellect. "
        "Trained at level 22.");
    add(3, "ArcaneIntellect_R3", 1461, 3, 15, 2, 4,
        "Arcane Intellect Rank 3 - +15 Intellect. "
        "Trained at level 36.");
    add(4, "ArcaneIntellect_R4", 10157, 4, 25, 3, 0,
        "Arcane Intellect Rank 4 - +25 Intellect. "
        "Trained at level 50. (Max rank in this preset; "
        "real WoTLK has higher ranks via Brilliance "
        "variant.)");
    return c;
}

WoweeBuffBook WoweeBuffBookLoader::makeDruid(
    const std::string& catalogName) {
    using B = WoweeBuffBook;
    WoweeBuffBook c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t spellId, uint8_t rank,
                    int32_t statAmount, uint32_t prevId,
                    uint32_t nextId, const char* desc) {
        B::Entry e;
        e.buffId = id; e.name = name; e.description = desc;
        e.spellId = spellId;
        e.castClassMask = 1024;        // Druid
        e.targetTypeMask = B::TargetSelf |
                            B::TargetParty |
                            B::TargetFriendly;
        e.statBonusKind = B::AllStats;
        e.rank = rank;
        e.maxStackCount = 1;
        e.statBonusAmount = statAmount;
        e.duration = 1800;
        e.previousRankId = prevId;
        e.nextRankId = nextId;
        e.iconColorRGBA = packRgba(255, 125, 10);   // druid orange
        c.entries.push_back(e);
    };
    add(100, "MarkOfTheWild_R1", 1126,  1,  3, 0, 101,
        "Mark of the Wild Rank 1 - +3 to all stats. "
        "Trained at level 1.");
    add(101, "MarkOfTheWild_R2", 5232,  2,  6, 100, 102,
        "Mark of the Wild Rank 2 - +6 to all stats. "
        "Trained at level 10.");
    add(102, "MarkOfTheWild_R3", 6756,  3, 10, 101, 103,
        "Mark of the Wild Rank 3 - +10 to all stats. "
        "Trained at level 20.");
    add(103, "MarkOfTheWild_R4", 5234,  4, 14, 102, 104,
        "Mark of the Wild Rank 4 - +14 to all stats. "
        "Trained at level 30.");
    add(104, "MarkOfTheWild_R5", 8907,  5, 18, 103, 0,
        "Mark of the Wild Rank 5 - +18 to all stats. "
        "Trained at level 40. (Top rank in this preset.)");
    return c;
}

WoweeBuffBook WoweeBuffBookLoader::makeRaidMax(
    const std::string& catalogName) {
    using B = WoweeBuffBook;
    WoweeBuffBook c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t spellId, uint32_t classMask,
                    uint8_t targetMask, uint8_t statKind,
                    uint8_t rank, int32_t statAmount,
                    uint32_t duration, uint32_t color,
                    const char* desc) {
        B::Entry e;
        e.buffId = id; e.name = name; e.description = desc;
        e.spellId = spellId;
        e.castClassMask = classMask;
        e.targetTypeMask = targetMask;
        e.statBonusKind = statKind;
        e.rank = rank;
        e.maxStackCount = 1;
        e.statBonusAmount = statAmount;
        e.duration = duration;
        // No rank chain - these are max-rank standalone
        // entries pulled from each class's top buff.
        e.previousRankId = 0;
        e.nextRankId = 0;
        e.iconColorRGBA = color;
        c.entries.push_back(e);
    };
    add(200, "MarkOfTheWild_Max", 26990, 1024,
        B::TargetSelf | B::TargetRaid, B::AllStats,
        7, 35, 1800,
        packRgba(255, 125, 10),
        "Druid raid buff - Mark of the Wild rank 7, "
        "+35 to all stats, 30min, raid-wide.");
    add(201, "PowerWordFortitude_Max", 25389, 16,
        B::TargetSelf | B::TargetRaid, B::Stamina,
        8, 79, 1800,
        packRgba(255, 255, 255),
        "Priest raid buff - Prayer of Fortitude rank 4, "
        "+79 Stamina, 60min, raid-wide.");
    add(202, "ArcaneIntellect_Max", 27126, 128,
        B::TargetSelf | B::TargetRaid, B::Intellect,
        6, 60, 1800,
        packRgba(140, 200, 255),
        "Mage raid buff - Arcane Brilliance rank 2, "
        "+60 Intellect, 60min, raid-wide.");
    add(203, "BlessingOfKings", 25898, 2,
        B::TargetSelf | B::TargetRaid, B::AllStats,
        1, 10, 1800,
        packRgba(220, 220, 100),
        "Paladin raid buff - Greater Blessing of Kings, "
        "+10% to all stats, 60min, raid-wide.");
    add(204, "BattleShout_Max", 47436, 1,
        B::TargetSelf | B::TargetParty, B::AttackPower,
        9, 553, 120,
        packRgba(220, 60, 60),
        "Warrior raid buff - Battle Shout rank 9, "
        "+553 Attack Power, 2min, party-wide.");
    add(205, "TrueshotAura_Max", 19506, 4,
        B::TargetSelf | B::TargetRaid, B::Other,
        3, 0, 0,
        packRgba(170, 210, 100),
        "Hunter raid buff - Trueshot Aura, +10% AP "
        "for all party/raid (until cancel). statKind="
        "Other because it's a percentage modifier, not a "
        "flat stat.");
    return c;
}

} // namespace pipeline
} // namespace wowee
