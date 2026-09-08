#include "pipeline/wowee_learning_notifications.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'L', 'D', 'N'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wldn";

} // namespace

const WoweeLearningNotifications::Entry*
WoweeLearningNotifications::findById(uint32_t notificationId) const {
    for (const auto& e : entries)
        if (e.notificationId == notificationId) return &e;
    return nullptr;
}

std::vector<const WoweeLearningNotifications::Entry*>
WoweeLearningNotifications::findByTrigger(uint8_t triggerKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.triggerKind == triggerKind) out.push_back(&e);
    return out;
}

bool WoweeLearningNotificationsLoader::save(
    const WoweeLearningNotifications& cat,
    const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const auto& e) {
        writePOD(os, e.notificationId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.messageText);
        writePOD(os, e.triggerKind);
        writePOD(os, e.channelKind);
        writePOD(os, e.factionFilter);
        writePOD(os, e.pad0);
        writePOD(os, e.triggerValue);
        writePOD(os, e.soundId);
        writePOD(os, e.minTotalTimePlayed);
        writePOD(os, e.iconColorRGBA);
    });
}

WoweeLearningNotifications WoweeLearningNotificationsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeLearningNotifications>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeLearningNotifications::Entry& e) {
        if (!readPOD(is, e.notificationId)) { return false; }
        if (!readStr(is, e.name) ||
            !readStr(is, e.description) ||
            !readStr(is, e.messageText)) { return false; }
        if (!readPOD(is, e.triggerKind) ||
            !readPOD(is, e.channelKind) ||
            !readPOD(is, e.factionFilter) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.triggerValue) ||
            !readPOD(is, e.soundId) ||
            !readPOD(is, e.minTotalTimePlayed) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeLearningNotificationsLoader::exists(
    const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeLearningNotifications
WoweeLearningNotificationsLoader::makeLevelMilestones(
    const std::string& catalogName) {
    using L = WoweeLearningNotifications;
    WoweeLearningNotifications c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    int32_t level, uint8_t channel,
                    const char* msg, uint32_t soundId,
                    const char* desc) {
        L::Entry e;
        e.notificationId = id; e.name = name;
        e.description = desc;
        e.messageText = msg;
        e.triggerKind = L::LevelReach;
        e.triggerValue = level;
        e.channelKind = channel;
        e.factionFilter = L::Both;
        e.soundId = soundId;
        e.minTotalTimePlayed = 0;
        e.iconColorRGBA = packRgba(220, 200, 80);   // milestone gold
        c.entries.push_back(e);
    };
    add(1, "ApprenticeRidingUnlock", 20, L::Tutorial,
        "You have reached level 20! You may now train "
        "Apprentice Riding from any riding trainer in "
        "your capital city.",
        12867,
        "Level 20 milestone - first riding tier "
        "unlock. Tutorial popup with mount icon.");
    add(2, "TalentResetReminder", 30, L::SystemMsg,
        "You have reached level 30. Visit a class "
        "trainer to reset talents if you wish to try a "
        "different specialization.",
        0,
        "Level 30 reminder - silent system message, no "
        "fanfare. Soft suggestion to try respec.");
    add(3, "EpicGroundMountUnlock", 40, L::Tutorial,
        "You have reached level 40! You may now train "
        "Journeyman Riding (epic ground mount, +100% "
        "speed) and Dual Specialization.",
        12867,
        "Level 40 milestone - epic ground mount + dual "
        "spec dual unlock.");
    add(4, "FlightTrainingUnlock", 60, L::Tutorial,
        "You have reached level 60! Visit a flight "
        "trainer in Outland or Northrend to learn Expert "
        "Riding (flying mount).",
        12867,
        "Level 60 milestone - flying mount unlock.");
    add(5, "EndgameRaidContent", 80, L::RaidWarning,
        "Congratulations on reaching level 80! Endgame "
        "raid content is now available - speak to your "
        "Stormwind / Orgrimmar liaison for details.",
        12865,
        "Level 80 milestone - endgame banner. "
        "RaidWarning channel for max emphasis.");
    return c;
}

WoweeLearningNotifications
WoweeLearningNotificationsLoader::makeAccountUnlocks(
    const std::string& catalogName) {
    using L = WoweeLearningNotifications;
    WoweeLearningNotifications c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t kind, int32_t value,
                    uint8_t channel, const char* msg,
                    uint32_t timePlayed, const char* desc) {
        L::Entry e;
        e.notificationId = id; e.name = name;
        e.description = desc;
        e.messageText = msg;
        e.triggerKind = kind;
        e.triggerValue = value;
        e.channelKind = channel;
        e.factionFilter = L::Both;
        e.soundId = 0;
        e.minTotalTimePlayed = timePlayed;
        e.iconColorRGBA = packRgba(140, 200, 255);   // unlock blue
        c.entries.push_back(e);
    };
    // First-mailbox-use tutorial - fires once when player
    // acquires their first mail item, but only for newbies
    // (timePlayed < 7200 = 2 hours).
    add(100, "FirstMailReceived", L::ItemAcquired,
        17, L::Tutorial,
        "You received your first mail! Open the mailbox "
        "to retrieve attached items. Mailboxes are at "
        "every inn and capital.",
        7200,
        "First-mail tutorial - gated to total time "
        "played < 2hr to suppress for veterans.");
    add(101, "AuctionHouseAvailable", L::ZoneEntered,
        1519, L::Subtitle,
        "Welcome to Stormwind! The Auction House is "
        "located in the Trade District near the front "
        "gate.",
        0,
        "Stormwind first-entry subtitle - explains "
        "auction house location for newbies.");
    add(102, "DualSpecActivated", L::SpellLearned,
        63645, L::SystemMsg,
        "Dual Specialization is now active! Press 'N' "
        "and click the secondary spec slot to switch.",
        0,
        "Fires when player learns the dual-spec activator "
        "spell (id 63645). System message channel.");
    add(103, "TransmogVendorUnlock", L::QuestComplete,
        25000, L::Tutorial,
        "You may now visit a Transmogrification vendor "
        "to apply cosmetic appearances to your gear "
        "without changing stats.",
        0,
        "Fires when player completes the transmog intro "
        "quest (placeholder questId 25000).");
    return c;
}

WoweeLearningNotifications
WoweeLearningNotificationsLoader::makeReputation(
    const std::string& catalogName) {
    using L = WoweeLearningNotifications;
    WoweeLearningNotifications c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    int32_t standing, const char* msg,
                    const char* desc) {
        L::Entry e;
        e.notificationId = id; e.name = name;
        e.description = desc;
        e.messageText = msg;
        e.triggerKind = L::FactionStanding;
        e.triggerValue = standing;
        e.channelKind = L::SystemMsg;
        e.factionFilter = L::Both;
        e.soundId = 0;
        e.minTotalTimePlayed = 0;
        e.iconColorRGBA = packRgba(160, 220, 80);   // rep green
        c.entries.push_back(e);
    };
    // Reputation-based notifications - generic, would
    // need per-faction variants in a real deployment.
    // standing values: Honored=9000, Revered=21000,
    // Exalted=42000.
    add(200, "HonoredRepReached", 9000,
        "You have reached Honored standing with the "
        "Argent Crusade. New quests and discounted "
        "vendor prices are now available.",
        "Generic Honored milestone - placeholder for "
        "per-faction variants. 10% vendor discount kicks "
        "in at this tier.");
    add(201, "ReveredRepReached", 21000,
        "You have reached Revered standing with the "
        "Argent Crusade. Tabard and select rare items "
        "are now purchasable.",
        "Generic Revered milestone - tabard unlock "
        "tier (15% vendor discount).");
    add(202, "ExaltedRepReached", 42000,
        "You have reached Exalted standing with the "
        "Argent Crusade. Maximum reputation reward "
        "items unlocked. Achievement granted.",
        "Generic Exalted milestone - maximum rep tier. "
        "Triggers achievement system.");
    return c;
}

} // namespace pipeline
} // namespace wowee
