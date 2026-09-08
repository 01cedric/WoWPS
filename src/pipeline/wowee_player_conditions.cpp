#include "pipeline/wowee_player_conditions.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'P', 'C', 'N'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wpcn";

} // namespace

const WoweePlayerCondition::Entry*
WoweePlayerCondition::findById(uint32_t conditionId) const {
    for (const auto& e : entries)
        if (e.conditionId == conditionId) return &e;
    return nullptr;
}

const char* WoweePlayerCondition::conditionKindName(uint8_t k) {
    switch (k) {
        case Always:         return "always";
        case Race:           return "race";
        case Class:          return "class";
        case Level:          return "level";
        case Zone:           return "zone";
        case Map:            return "map";
        case Reputation:     return "reputation";
        case AchievementWon: return "achievement";
        case QuestComplete:  return "quest-complete";
        case QuestActive:    return "quest-active";
        case SpellKnown:     return "spell-known";
        case ItemEquipped:   return "item-equipped";
        case Faction:        return "faction";
        case InCombat:       return "in-combat";
        case Mounted:        return "mounted";
        case Resting:        return "resting";
        default:             return "unknown";
    }
}

const char* WoweePlayerCondition::comparisonOpName(uint8_t o) {
    switch (o) {
        case Equal:          return "==";
        case NotEqual:       return "!=";
        case GreaterThan:    return ">";
        case GreaterOrEqual: return ">=";
        case LessThan:       return "<";
        case LessOrEqual:    return "<=";
        case InSet:          return "in-set";
        case NotInSet:       return "not-in-set";
        default:             return "?";
    }
}

const char* WoweePlayerCondition::chainOpName(uint8_t c) {
    switch (c) {
        case ChainNone: return "none";
        case ChainAnd:  return "and";
        case ChainOr:   return "or";
        case ChainNot:  return "not";
        default:        return "unknown";
    }
}

bool WoweePlayerConditionLoader::save(const WoweePlayerCondition& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweePlayerCondition::Entry& e) {
        writePOD(os, e.conditionId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.conditionKind);
        writePOD(os, e.comparisonOp);
        writePOD(os, e.chainOp);
        writePadding(os, 1);
        writePOD(os, e.targetIdA);
        writePOD(os, e.targetIdB);
        writePOD(os, e.intValueA);
        writePOD(os, e.intValueB);
        writePOD(os, e.chainNextId);
        writeStr(os, e.failMessage);
                       });
}

WoweePlayerCondition WoweePlayerConditionLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweePlayerCondition>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweePlayerCondition::Entry& e) {
        if (!readPOD(is, e.conditionId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.conditionKind) ||
            !readPOD(is, e.comparisonOp) ||
            !readPOD(is, e.chainOp)) { return false; }
        if (!skipPadding(is, 1)) { return false; }
        if (!readPOD(is, e.targetIdA) ||
            !readPOD(is, e.targetIdB) ||
            !readPOD(is, e.intValueA) ||
            !readPOD(is, e.intValueB) ||
            !readPOD(is, e.chainNextId)) { return false; }
        if (!readStr(is, e.failMessage)) { return false; }
                                  return true;
                              });
}

bool WoweePlayerConditionLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweePlayerCondition WoweePlayerConditionLoader::makeStarter(
    const std::string& catalogName) {
    WoweePlayerCondition c;
    c.name = catalogName;
    {
        WoweePlayerCondition::Entry e;
        e.conditionId = 1; e.name = "Level60Plus";
        e.description = "Player must be level 60 or higher.";
        e.conditionKind = WoweePlayerCondition::Level;
        e.comparisonOp = WoweePlayerCondition::GreaterOrEqual;
        e.intValueA = 60;
        e.failMessage = "You must be at least level 60.";
        c.entries.push_back(e);
    }
    {
        WoweePlayerCondition::Entry e;
        e.conditionId = 2; e.name = "RaceHuman";
        e.description = "Player must be Human race (raceId=1).";
        e.conditionKind = WoweePlayerCondition::Race;
        e.comparisonOp = WoweePlayerCondition::Equal;
        e.targetIdA = 1;       // WCHC raceId Human
        e.failMessage = "Only Humans may take this option.";
        c.entries.push_back(e);
    }
    {
        WoweePlayerCondition::Entry e;
        e.conditionId = 3; e.name = "ClassWarrior";
        e.description = "Player must be Warrior class (classId=1).";
        e.conditionKind = WoweePlayerCondition::Class;
        e.comparisonOp = WoweePlayerCondition::Equal;
        e.targetIdA = 1;       // WCHC classId Warrior
        e.failMessage = "Only Warriors may take this option.";
        c.entries.push_back(e);
    }
    return c;
}

WoweePlayerCondition WoweePlayerConditionLoader::makeQuestGates(
    const std::string& catalogName) {
    WoweePlayerCondition c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t op, uint32_t targetA, int32_t intA,
                    const char* desc, const char* failMsg) {
        WoweePlayerCondition::Entry e;
        e.conditionId = id; e.name = name; e.description = desc;
        e.conditionKind = kind; e.comparisonOp = op;
        e.targetIdA = targetA; e.intValueA = intA;
        e.failMessage = failMsg;
        c.entries.push_back(e);
    };
    add(100, "Quest1Complete", WoweePlayerCondition::QuestComplete,
        WoweePlayerCondition::Equal, 1, 0,
        "Player must have completed the bandit-trouble intro quest "
         "(WQT questId=1).",
        "You must complete 'Bandit Trouble' first.");
    add(101, "StormwindHonored", WoweePlayerCondition::Reputation,
        WoweePlayerCondition::GreaterOrEqual, 72, 9000,
        "Player must be at least Honored (9000) with Stormwind "
         "(WFAC factionId=72).",
        "You need at least Honored standing with Stormwind.");
    add(102, "AchHelloAzeroth", WoweePlayerCondition::AchievementWon,
        WoweePlayerCondition::Equal, 6, 0,
        "Player must have earned the 'Hello, Azeroth!' achievement "
         "(WACH achievementId=6).",
        "You haven't earned the 'Hello, Azeroth!' achievement yet.");
    add(103, "InElwynnForest", WoweePlayerCondition::Zone,
        WoweePlayerCondition::Equal, 12, 0,
        "Player must be in Elwynn Forest (WMS areaId=12).",
        "You must be in Elwynn Forest to do this.");
    return c;
}

WoweePlayerCondition WoweePlayerConditionLoader::makeComposite(
    const std::string& catalogName) {
    WoweePlayerCondition c;
    c.name = catalogName;
    // First the leaves, then the chains. Leaves get IDs
    // 200-202; the chained roots get 300-302 and reference
    // them via chainNextId + chainOp.
    auto leaf = [&](uint32_t id, const char* name, uint8_t kind,
                     uint8_t op, uint32_t targetA, int32_t intA,
                     const char* desc) {
        WoweePlayerCondition::Entry e;
        e.conditionId = id; e.name = name; e.description = desc;
        e.conditionKind = kind; e.comparisonOp = op;
        e.targetIdA = targetA; e.intValueA = intA;
        c.entries.push_back(e);
    };
    leaf(200, "Level80",        WoweePlayerCondition::Level,
        WoweePlayerCondition::GreaterOrEqual, 0, 80,
        "Leaf - level 80 or higher.");
    leaf(201, "ClassWarriorLeaf", WoweePlayerCondition::Class,
        WoweePlayerCondition::Equal, 1, 0,
        "Leaf - class is Warrior.");
    leaf(202, "AllyMember",     WoweePlayerCondition::Faction,
        WoweePlayerCondition::Equal, 469, 0,
        "Leaf - member of the Alliance "
        "(WFAC factionId=469).");
    auto chain = [&](uint32_t id, const char* name, uint8_t headKind,
                      uint8_t headOp, uint32_t headTarget,
                      int32_t headInt, uint8_t chainOp,
                      uint32_t chainNextId, const char* desc,
                      const char* failMsg) {
        WoweePlayerCondition::Entry e;
        e.conditionId = id; e.name = name; e.description = desc;
        e.conditionKind = headKind; e.comparisonOp = headOp;
        e.targetIdA = headTarget; e.intValueA = headInt;
        e.chainOp = chainOp;
        e.chainNextId = chainNextId;
        e.failMessage = failMsg;
        c.entries.push_back(e);
    };
    chain(300, "Level80AndWarrior",
        WoweePlayerCondition::Level,
        WoweePlayerCondition::GreaterOrEqual, 0, 80,
        WoweePlayerCondition::ChainAnd, 201,
        "Composite - head=Level>=80 AND tail=Warrior.",
        "Requires Warrior, level 80 or higher.");
    chain(301, "AllyOrHonored",
        WoweePlayerCondition::Reputation,
        WoweePlayerCondition::GreaterOrEqual, 72, 9000,
        WoweePlayerCondition::ChainOr, 202,
        "Composite - head=Honored Stormwind OR tail=Alliance member.",
        "Requires Alliance membership or Honored Stormwind.");
    chain(302, "NotInCombat",
        WoweePlayerCondition::Always,
        WoweePlayerCondition::Equal, 0, 0,
        WoweePlayerCondition::ChainNot, 200,
        "Composite - NOT (level 80 leaf) - sample inverted check.",
        "Cannot be used at max level.");
    return c;
}

} // namespace pipeline
} // namespace wowee
