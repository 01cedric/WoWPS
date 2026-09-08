#include "pipeline/wowee_quest_sorts.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'Q', 'S', 'O'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wqso";

} // namespace

const WoweeQuestSort::Entry*
WoweeQuestSort::findById(uint32_t sortId) const {
    for (const auto& e : entries) if (e.sortId == sortId) return &e;
    return nullptr;
}

const char* WoweeQuestSort::sortKindName(uint8_t k) {
    switch (k) {
        case General:    return "general";
        case ClassQuest: return "class";
        case Profession: return "profession";
        case Daily:      return "daily";
        case Holiday:    return "holiday";
        case Reputation: return "reputation";
        case Dungeon:    return "dungeon";
        case Raid:       return "raid";
        case Heroic:     return "heroic";
        case Repeatable: return "repeatable";
        case PvP:        return "pvp";
        case Tournament: return "tournament";
        default:         return "unknown";
    }
}

bool WoweeQuestSortLoader::save(const WoweeQuestSort& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeQuestSort::Entry& e) {
        writePOD(os, e.sortId);
        writeStr(os, e.name);
        writeStr(os, e.displayName);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.sortKind);
        writePOD(os, e.displayPriority);
        writePOD(os, e.targetProfessionId);
        writePadding(os, 1);
        writePOD(os, e.targetClassMask);
        writePOD(os, e.targetFactionId);
                       });
}

WoweeQuestSort WoweeQuestSortLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeQuestSort>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeQuestSort::Entry& e) {
        if (!readPOD(is, e.sortId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.displayName) ||
            !readStr(is, e.description) || !readStr(is, e.iconPath)) { return false; }
        if (!readPOD(is, e.sortKind) ||
            !readPOD(is, e.displayPriority) ||
            !readPOD(is, e.targetProfessionId)) { return false; }
        if (!skipPadding(is, 1)) { return false; }
        if (!readPOD(is, e.targetClassMask) ||
            !readPOD(is, e.targetFactionId)) { return false; }
                                  return true;
                              });
}

bool WoweeQuestSortLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeQuestSort WoweeQuestSortLoader::makeStarter(
    const std::string& catalogName) {
    WoweeQuestSort c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* display,
                    uint8_t kind, uint8_t prio, const char* desc) {
        WoweeQuestSort::Entry e;
        e.sortId = id; e.name = name; e.displayName = display;
        e.description = desc;
        e.sortKind = kind;
        e.displayPriority = prio;
        e.iconPath = std::string("Interface/Icons/INV_Misc_QuestionMark_") +
                      name + ".blp";
        c.entries.push_back(e);
    };
    add(1, "General",      "General Quests",
        WoweeQuestSort::General,    0,
        "Default catch-all category for area / story quests.");
    add(2, "Daily",        "Daily Quests",
        WoweeQuestSort::Daily,      10,
        "Quests that reset every 24 hours.");
    add(3, "Repeatable",   "Repeatable Quests",
        WoweeQuestSort::Repeatable, 20,
        "Non-daily quests that can be repeated infinitely "
        "(turn-in tokens, faction repeatables).");
    return c;
}

WoweeQuestSort WoweeQuestSortLoader::makeClass(
    const std::string& catalogName) {
    WoweeQuestSort c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* className,
                    uint32_t classBit) {
        WoweeQuestSort::Entry e;
        e.sortId = id;
        e.name = std::string("Class") + className;
        e.displayName = std::string(className) + " Quests";
        e.description = std::string(className) +
                         "-only quest line (trainer / class trial).";
        e.iconPath = std::string("Interface/Icons/Class_") +
                      className + ".blp";
        e.sortKind = WoweeQuestSort::ClassQuest;
        e.displayPriority = 1;
        e.targetClassMask = classBit;
        c.entries.push_back(e);
    };
    // Class bits match WCHC.classId enum.
    add(100, "Warrior",     1u << 1);
    add(101, "Paladin",     1u << 2);
    add(102, "Hunter",      1u << 3);
    add(103, "Rogue",       1u << 4);
    add(104, "Priest",      1u << 5);
    add(105, "DeathKnight", 1u << 6);
    add(106, "Shaman",      1u << 7);
    add(107, "Mage",        1u << 8);
    add(108, "Warlock",     1u << 9);
    add(109, "Druid",       1u << 11);
    return c;
}

WoweeQuestSort WoweeQuestSortLoader::makeProfession(
    const std::string& catalogName) {
    WoweeQuestSort c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* profName,
                    uint8_t professionId) {
        WoweeQuestSort::Entry e;
        e.sortId = id;
        e.name = std::string("Prof") + profName;
        e.displayName = std::string(profName) + " Quests";
        e.description = std::string(profName) +
                         "-specific quest (recipe acquisition, "
                         "trainer reward).";
        e.iconPath = std::string("Interface/Icons/Trade_") +
                      profName + ".blp";
        e.sortKind = WoweeQuestSort::Profession;
        e.displayPriority = 2;
        e.targetProfessionId = professionId;
        c.entries.push_back(e);
    };
    // Profession IDs match WTSK.profession enum.
    add(200, "Blacksmithing",  0);
    add(201, "Tailoring",      1);
    add(202, "Engineering",    2);
    add(203, "Alchemy",        3);
    add(204, "Enchanting",     4);
    add(205, "Leatherworking", 5);
    add(206, "Jewelcrafting",  6);
    add(207, "Inscription",    7);
    return c;
}

} // namespace pipeline
} // namespace wowee
