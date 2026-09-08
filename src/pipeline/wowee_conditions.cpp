#include "pipeline/wowee_conditions.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'P', 'C', 'D'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wpcd";

} // namespace

const WoweeCondition::Entry*
WoweeCondition::findById(uint32_t conditionId) const {
    for (const auto& e : entries) if (e.conditionId == conditionId) return &e;
    return nullptr;
}

const char* WoweeCondition::kindName(uint8_t k) {
    switch (k) {
        case AlwaysTrue:     return "true";
        case AlwaysFalse:    return "false";
        case QuestCompleted: return "quest-done";
        case QuestActive:    return "quest-active";
        case HasItem:        return "has-item";
        case HasSpell:       return "has-spell";
        case MinLevel:       return "min-level";
        case MaxLevel:       return "max-level";
        case ClassMatch:     return "class";
        case RaceMatch:      return "race";
        case FactionRep:     return "rep";
        case HasAchievement: return "achievement";
        case TeamSize:       return "team-size";
        case GuildLevel:     return "guild-level";
        case EventActive:    return "event";
        case AreaId:         return "area";
        case HasTitle:       return "title";
        default:             return "unknown";
    }
}

const char* WoweeCondition::aggregatorName(uint8_t a) {
    switch (a) {
        case And: return "and";
        case Or:  return "or";
        default:  return "unknown";
    }
}

bool WoweeConditionLoader::save(const WoweeCondition& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeCondition::Entry& e) {
        writePOD(os, e.conditionId);
        writePOD(os, e.groupId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.kind);
        writePOD(os, e.aggregator);
        writePOD(os, e.negated);
        writePadding(os, 1);
        writePOD(os, e.targetId);
        writePOD(os, e.minValue);
        writePOD(os, e.maxValue);
                       });
}

WoweeCondition WoweeConditionLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeCondition>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeCondition::Entry& e) {
        if (!readPOD(is, e.conditionId) ||
            !readPOD(is, e.groupId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.kind) ||
            !readPOD(is, e.aggregator) ||
            !readPOD(is, e.negated)) { return false; }
        if (!skipPadding(is, 1)) { return false; }
        if (!readPOD(is, e.targetId) ||
            !readPOD(is, e.minValue) ||
            !readPOD(is, e.maxValue)) { return false; }
                                  return true;
                              });
}

bool WoweeConditionLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeCondition WoweeConditionLoader::makeStarter(const std::string& catalogName) {
    WoweeCondition c;
    c.name = catalogName;
    {
        WoweeCondition::Entry e;
        e.conditionId = 1; e.name = "Bandit Trouble done";
        e.description = "Player has completed the Bandit Trouble quest.";
        e.kind = WoweeCondition::QuestCompleted;
        e.targetId = 1;     // matches WQT.makeStarter quest 1
        c.entries.push_back(e);
    }
    {
        WoweeCondition::Entry e;
        e.conditionId = 2; e.name = "Has Healing Potion";
        e.description = "Player has at least 1 healing potion in bags.";
        e.kind = WoweeCondition::HasItem;
        e.targetId = 3;     // WIT.makeStarter healing potion
        e.minValue = 1;
        c.entries.push_back(e);
    }
    {
        WoweeCondition::Entry e;
        e.conditionId = 3; e.name = "Level 60+";
        e.description = "Player is level 60 or higher.";
        e.kind = WoweeCondition::MinLevel;
        e.minValue = 60;
        c.entries.push_back(e);
    }
    {
        WoweeCondition::Entry e;
        e.conditionId = 4; e.name = "Is Mage";
        e.description = "Player class is Mage.";
        e.kind = WoweeCondition::ClassMatch;
        e.targetId = 8;     // WCHC mage class id
        c.entries.push_back(e);
    }
    return c;
}

WoweeCondition WoweeConditionLoader::makeGated(const std::string& catalogName) {
    WoweeCondition c;
    c.name = catalogName;
    // Group 100: Alliance AND Mage AND level >= 60.
    {
        WoweeCondition::Entry e;
        e.conditionId = 100; e.groupId = 100;
        e.name = "Alliance race";
        e.kind = WoweeCondition::RaceMatch;
        e.aggregator = WoweeCondition::And;
        // raceMask bits for Alliance races (Human/Dwarf/NightElf/Gnome).
        e.targetId = (1u << 1) | (1u << 3) | (1u << 4) | (1u << 7);
        c.entries.push_back(e);
    }
    {
        WoweeCondition::Entry e;
        e.conditionId = 101; e.groupId = 100;
        e.name = "Mage class";
        e.kind = WoweeCondition::ClassMatch;
        e.aggregator = WoweeCondition::And;
        e.targetId = 8;
        c.entries.push_back(e);
    }
    {
        WoweeCondition::Entry e;
        e.conditionId = 102; e.groupId = 100;
        e.name = "Level 60+";
        e.kind = WoweeCondition::MinLevel;
        e.aggregator = WoweeCondition::And;
        e.minValue = 60;
        c.entries.push_back(e);
    }
    // Group 200: completed quest 1 OR completed quest 2.
    {
        WoweeCondition::Entry e;
        e.conditionId = 200; e.groupId = 200;
        e.name = "Did quest 1";
        e.kind = WoweeCondition::QuestCompleted;
        e.aggregator = WoweeCondition::Or;
        e.targetId = 1;
        c.entries.push_back(e);
    }
    {
        WoweeCondition::Entry e;
        e.conditionId = 201; e.groupId = 200;
        e.name = "Did quest 100";
        e.kind = WoweeCondition::QuestCompleted;
        e.aggregator = WoweeCondition::Or;
        e.targetId = 100;
        c.entries.push_back(e);
    }
    return c;
}

WoweeCondition WoweeConditionLoader::makeEvent(const std::string& catalogName) {
    WoweeCondition c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t eventId,
                    const char* desc) {
        WoweeCondition::Entry e;
        e.conditionId = id; e.name = name; e.description = desc;
        e.kind = WoweeCondition::EventActive;
        e.targetId = eventId;
        c.entries.push_back(e);
    };
    // eventIds 100/101/103 match WSEA.makeYearly (Hallow's End,
    // Brewfest, Winter's Veil).
    add(300, "Hallow's End active",  100, "Hallow's End event is currently running.");
    add(301, "Brewfest active",      101, "Brewfest event is currently running.");
    add(302, "Winter's Veil active", 103, "Winter's Veil event is currently running.");
    return c;
}

} // namespace pipeline
} // namespace wowee
