#include "pipeline/wowee_holidays.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'H', 'O', 'L'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".whol";

} // namespace

const WoweeHoliday::Entry*
WoweeHoliday::findById(uint32_t holidayId) const {
    for (const auto& e : entries) if (e.holidayId == holidayId) return &e;
    return nullptr;
}

const char* WoweeHoliday::holidayKindName(uint8_t k) {
    switch (k) {
        case Seasonal: return "seasonal";
        case Weekly:   return "weekly";
        case Daily:    return "daily";
        case WorldPvp: return "world-pvp";
        case OneShot:  return "one-shot";
        case Special:  return "special";
        default:       return "unknown";
    }
}

const char* WoweeHoliday::recurrenceName(uint8_t r) {
    switch (r) {
        case Annual:      return "annual";
        case Monthly:     return "monthly";
        case WeeklyRecur: return "weekly";
        case OneTime:     return "one-time";
        default:          return "unknown";
    }
}

bool WoweeHolidayLoader::save(const WoweeHoliday& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeHoliday::Entry& e) {
        writePOD(os, e.holidayId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.holidayKind);
        writePOD(os, e.recurrence);
        writePOD(os, e.startMonth);
        writePOD(os, e.startDay);
        writePOD(os, e.durationHours);
        writePadding(os, 2);
        writePOD(os, e.holidayQuestId);
        writePOD(os, e.bossCreatureId);
        writePOD(os, e.itemRewardId);
        writePOD(os, e.areaIdGate);
        writePOD(os, e.mapIdGate);
                       });
}

WoweeHoliday WoweeHolidayLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeHoliday>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeHoliday::Entry& e) {
        if (!readPOD(is, e.holidayId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) { return false; }
        if (!readPOD(is, e.holidayKind) ||
            !readPOD(is, e.recurrence) ||
            !readPOD(is, e.startMonth) ||
            !readPOD(is, e.startDay) ||
            !readPOD(is, e.durationHours)) { return false; }
        if (!skipPadding(is, 2)) { return false; }
        if (!readPOD(is, e.holidayQuestId) ||
            !readPOD(is, e.bossCreatureId) ||
            !readPOD(is, e.itemRewardId) ||
            !readPOD(is, e.areaIdGate) ||
            !readPOD(is, e.mapIdGate)) { return false; }
                                  return true;
                              });
}

bool WoweeHolidayLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeHoliday WoweeHolidayLoader::makeStarter(const std::string& catalogName) {
    WoweeHoliday c;
    c.name = catalogName;
    {
        WoweeHoliday::Entry e;
        e.holidayId = 1; e.name = "Hallow's End";
        e.description = "Trick-or-treat through cities, then defeat "
                         "the Headless Horseman in Scarlet Monastery.";
        e.iconPath = "Interface/Calendar/Holidays/Calendar_HallowsEnd.blp";
        e.holidayKind = WoweeHoliday::Seasonal;
        e.recurrence = WoweeHoliday::Annual;
        e.startMonth = 10; e.startDay = 18;     // Oct 18 - Nov 1
        e.durationHours = 14 * 24;
        e.bossCreatureId = 23682;               // Headless Horseman
        e.itemRewardId = 33226;                 // Tricky Treat
        e.holidayQuestId = 11131;               // intro quest
        c.entries.push_back(e);
    }
    {
        WoweeHoliday::Entry e;
        e.holidayId = 2; e.name = "Brewfest";
        e.description = "Sample brews, ride a barrel, and defeat "
                         "Coren Direbrew in Blackrock Depths.";
        e.iconPath = "Interface/Calendar/Holidays/Calendar_Brewfest.blp";
        e.holidayKind = WoweeHoliday::Seasonal;
        e.recurrence = WoweeHoliday::Annual;
        e.startMonth = 9; e.startDay = 20;      // Sep 20 - Oct 6
        e.durationHours = 16 * 24;
        e.bossCreatureId = 23872;               // Coren Direbrew
        e.itemRewardId = 37829;                 // Brewfest Prize Token
        e.holidayQuestId = 11293;
        c.entries.push_back(e);
    }
    {
        WoweeHoliday::Entry e;
        e.holidayId = 3; e.name = "Feast of Winter Veil";
        e.description = "Decorate Ironforge / Orgrimmar, exchange "
                         "gifts, and rescue Metzen the Reindeer.";
        e.iconPath = "Interface/Calendar/Holidays/Calendar_WinterVeil.blp";
        e.holidayKind = WoweeHoliday::Seasonal;
        e.recurrence = WoweeHoliday::Annual;
        e.startMonth = 12; e.startDay = 15;     // Dec 15 - Jan 2
        e.durationHours = 19 * 24;
        e.itemRewardId = 21525;                 // Green Winter Hat
        e.holidayQuestId = 7062;
        c.entries.push_back(e);
    }
    return c;
}

WoweeHoliday WoweeHolidayLoader::makeWeekly(const std::string& catalogName) {
    WoweeHoliday c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t bgZone,
                    uint32_t questId, const char* desc) {
        WoweeHoliday::Entry e;
        e.holidayId = id; e.name = name; e.description = desc;
        e.iconPath = "Interface/Icons/Inv_Misc_PvpTrinket_05.blp";
        e.holidayKind = WoweeHoliday::Weekly;
        e.recurrence = WoweeHoliday::WeeklyRecur;
        // Start any Friday at 00:00 - duration 4 days (Fri-Mon).
        e.startMonth = 0; e.startDay = 0;
        e.durationHours = 4 * 24;
        e.areaIdGate = bgZone;
        e.holidayQuestId = questId;
        c.entries.push_back(e);
    };
    add(100, "Call to Arms: Warsong Gulch",     3277, 11335,
        "Bonus honor and reputation in Warsong Gulch this weekend.");
    add(101, "Call to Arms: Arathi Basin",      3358, 11336,
        "Bonus honor and reputation in Arathi Basin this weekend.");
    add(102, "Call to Arms: Eye of the Storm",  3820, 11337,
        "Bonus honor and reputation in Eye of the Storm this weekend.");
    return c;
}

WoweeHoliday WoweeHolidayLoader::makeSpecial(const std::string& catalogName) {
    WoweeHoliday c;
    c.name = catalogName;
    {
        WoweeHoliday::Entry e;
        e.holidayId = 200; e.name = "Wintergrasp Battle";
        e.description = "World-PvP siege of Wintergrasp Fortress - "
                         "control rewards Stone Keeper's Shards.";
        e.iconPath = "Interface/Icons/Inv_Misc_PvpTrinket_03.blp";
        e.holidayKind = WoweeHoliday::WorldPvp;
        e.recurrence = WoweeHoliday::WeeklyRecur;
        // 2.5 hours, every 3 hours.
        e.startMonth = 0; e.startDay = 0;
        e.durationHours = 3;
        e.itemRewardId = 43228;                 // Stone Keeper's Shard
        e.mapIdGate = 571;                      // Northrend
        e.areaIdGate = 4197;                    // Wintergrasp
        c.entries.push_back(e);
    }
    {
        WoweeHoliday::Entry e;
        e.holidayId = 201; e.name = "Lunar Festival";
        e.description = "Honor the Elders across Azeroth, "
                         "collect Coins of Ancestry, and earn "
                         "Festive Lanterns.";
        e.iconPath = "Interface/Calendar/Holidays/Calendar_LunarFestival.blp";
        e.holidayKind = WoweeHoliday::Seasonal;
        e.recurrence = WoweeHoliday::Annual;
        e.startMonth = 1; e.startDay = 26;      // late Jan - early Feb
        e.durationHours = 16 * 24;
        e.itemRewardId = 21100;                 // Coin of Ancestry
        e.holidayQuestId = 8867;
        c.entries.push_back(e);
    }
    {
        WoweeHoliday::Entry e;
        e.holidayId = 202; e.name = "Children's Week";
        e.description = "Escort an orphan around Azeroth and earn "
                         "rare non-combat pets and a tabard.";
        e.iconPath = "Interface/Calendar/Holidays/Calendar_ChildrensWeek.blp";
        e.holidayKind = WoweeHoliday::Special;
        e.recurrence = WoweeHoliday::Annual;
        e.startMonth = 5; e.startDay = 1;       // first week of May
        e.durationHours = 7 * 24;
        e.itemRewardId = 23007;                 // Curmudgeon's Payoff
        e.holidayQuestId = 10942;
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
