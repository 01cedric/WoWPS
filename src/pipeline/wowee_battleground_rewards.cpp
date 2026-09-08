#include "pipeline/wowee_battleground_rewards.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'B', 'R', 'D'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wbrd";

} // namespace

const WoweeBattlegroundRewards::Entry*
WoweeBattlegroundRewards::findById(uint32_t rewardId) const {
    for (const auto& e : entries)
        if (e.rewardId == rewardId) return &e;
    return nullptr;
}

const WoweeBattlegroundRewards::Entry*
WoweeBattlegroundRewards::find(uint16_t bgId,
                                  uint8_t bracketIndex) const {
    for (const auto& e : entries) {
        if (e.battlegroundId == bgId &&
            e.bracketIndex == bracketIndex)
            return &e;
    }
    return nullptr;
}

std::vector<const WoweeBattlegroundRewards::Entry*>
WoweeBattlegroundRewards::findByBg(uint16_t bgId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.battlegroundId == bgId) out.push_back(&e);
    return out;
}

bool WoweeBattlegroundRewardsLoader::save(
    const WoweeBattlegroundRewards& cat,
    const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const auto& e) {
        writePOD(os, e.rewardId);
        writePOD(os, e.battlegroundId);
        writePOD(os, e.bracketIndex);
        writePOD(os, e.minPlayersToStart);
        writePOD(os, e.winHonor);
        writePOD(os, e.lossHonor);
        writePOD(os, e.markItemId);
        writePOD(os, e.winMarks);
        writePOD(os, e.lossMarks);
        writePOD(os, e.bonusItemId);
        writePOD(os, e.bonusItemCount);
        writePOD(os, e.pad0);
    });
}

WoweeBattlegroundRewards WoweeBattlegroundRewardsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeBattlegroundRewards>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeBattlegroundRewards::Entry& e) {
        if (!readPOD(is, e.rewardId) ||
            !readPOD(is, e.battlegroundId) ||
            !readPOD(is, e.bracketIndex) ||
            !readPOD(is, e.minPlayersToStart) ||
            !readPOD(is, e.winHonor) ||
            !readPOD(is, e.lossHonor) ||
            !readPOD(is, e.markItemId) ||
            !readPOD(is, e.winMarks) ||
            !readPOD(is, e.lossMarks) ||
            !readPOD(is, e.bonusItemId) ||
            !readPOD(is, e.bonusItemCount) ||
            !readPOD(is, e.pad0)) { return false; }
                                  return true;
                              });
}

bool WoweeBattlegroundRewardsLoader::exists(
    const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

namespace {

WoweeBattlegroundRewards::Entry makeStage(
    uint32_t rewardId, uint16_t bgId,
    uint8_t bracketIndex, uint8_t minPlayers,
    uint32_t winHonor, uint32_t lossHonor,
    uint32_t markItemId,
    uint16_t winMarks, uint16_t lossMarks,
    uint32_t bonusItemId = 0,
    uint16_t bonusItemCount = 0) {
    WoweeBattlegroundRewards::Entry e;
    e.rewardId = rewardId;
    e.battlegroundId = bgId;
    e.bracketIndex = bracketIndex;
    e.minPlayersToStart = minPlayers;
    e.winHonor = winHonor;
    e.lossHonor = lossHonor;
    e.markItemId = markItemId;
    e.winMarks = winMarks;
    e.lossMarks = lossMarks;
    e.bonusItemId = bonusItemId;
    e.bonusItemCount = bonusItemCount;
    return e;
}

} // namespace

WoweeBattlegroundRewards WoweeBattlegroundRewardsLoader::makeAlteracValley(
    const std::string& catalogName) {
    WoweeBattlegroundRewards c;
    c.name = catalogName;
    // AV bgId=1, requires 20/side. Brackets 5
    // (51-60) and 6 (61-69 - endgame). Mark of AV =
    // itemId 17502. Win 3 marks / loss 1 mark
    // baseline.
    c.entries.push_back(makeStage(
        1, 1, 5, 20,
        750 /* winHonor */, 250 /* lossHonor */,
        17502 /* Mark of Honor: AV */,
        3, 1));
    c.entries.push_back(makeStage(
        2, 1, 6, 20,
        1000, 350,
        17502,
        3, 1));
    return c;
}

WoweeBattlegroundRewards WoweeBattlegroundRewardsLoader::makeWarsong(
    const std::string& catalogName) {
    WoweeBattlegroundRewards c;
    c.name = catalogName;
    // WSG bgId=2, requires 10/side. All 6 brackets.
    // Mark of WSG = itemId 20558.
    c.entries.push_back(makeStage(
        10, 2, 1, 10, 50, 25, 20558, 1, 0));
    c.entries.push_back(makeStage(
        11, 2, 2, 10, 100, 50, 20558, 1, 1));
    c.entries.push_back(makeStage(
        12, 2, 3, 10, 200, 100, 20558, 2, 1));
    c.entries.push_back(makeStage(
        13, 2, 4, 10, 350, 175, 20558, 2, 1));
    c.entries.push_back(makeStage(
        14, 2, 5, 10, 500, 250, 20558, 3, 1));
    c.entries.push_back(makeStage(
        15, 2, 6, 10, 750, 375, 20558, 3, 1));
    return c;
}

WoweeBattlegroundRewards WoweeBattlegroundRewardsLoader::makeArathiBasin(
    const std::string& catalogName) {
    WoweeBattlegroundRewards c;
    c.name = catalogName;
    // AB bgId=3, requires 15/side. Brackets 2..6.
    // Mark of AB = itemId 20559. Weekly bonus quest
    // turns in 3 marks for an extra Token of the
    // Triumvirate (placeholder itemId 20560).
    c.entries.push_back(makeStage(
        20, 3, 2, 15, 80, 40, 20559, 1, 0,
        20560 /* weekly bonus */, 1));
    c.entries.push_back(makeStage(
        21, 3, 3, 15, 175, 90, 20559, 2, 1,
        20560, 1));
    c.entries.push_back(makeStage(
        22, 3, 4, 15, 300, 150, 20559, 2, 1,
        20560, 1));
    c.entries.push_back(makeStage(
        23, 3, 5, 15, 450, 225, 20559, 3, 1,
        20560, 1));
    c.entries.push_back(makeStage(
        24, 3, 6, 15, 700, 350, 20559, 3, 1,
        20560, 1));
    return c;
}

} // namespace pipeline
} // namespace wowee
