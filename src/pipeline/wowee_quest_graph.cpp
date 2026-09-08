#include "pipeline/wowee_quest_graph.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'Q', 'G', 'R'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wqgr";



} // namespace

const WoweeQuestGraph::Entry*
WoweeQuestGraph::findById(uint32_t questId) const {
    for (const auto& e : entries)
        if (e.questId == questId) return &e;
    return nullptr;
}

std::vector<const WoweeQuestGraph::Entry*>
WoweeQuestGraph::findUnlocksFrom(uint32_t questId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        for (uint32_t p : e.prevQuestIds) {
            if (p == questId) { out.push_back(&e); break; }
        }
    }
    return out;
}

std::vector<const WoweeQuestGraph::Entry*>
WoweeQuestGraph::findByZone(uint32_t zoneId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.zoneId == zoneId) out.push_back(&e);
    return out;
}

bool WoweeQuestGraphLoader::save(const WoweeQuestGraph& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeQuestGraph::Entry& e) {
        writePOD(os, e.questId);
        writeStr(os, e.name);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxLevel);
        writePOD(os, e.questType);
        writePOD(os, e.factionAccess);
        writePOD(os, e.classRestriction);
        writePOD(os, e.raceRestriction);
        writePOD(os, e.zoneId);
        writePOD(os, e.chainHeadHint);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writeU32Vec(os, e.prevQuestIds);
        writeU32Vec(os, e.followupQuestIds);
                       });
}

WoweeQuestGraph WoweeQuestGraphLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeQuestGraph>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeQuestGraph::Entry& e) {
        if (!readPOD(is, e.questId)) { return false; }
        if (!readStr(is, e.name)) { return false; }
        if (!readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxLevel) ||
            !readPOD(is, e.questType) ||
            !readPOD(is, e.factionAccess) ||
            !readPOD(is, e.classRestriction) ||
            !readPOD(is, e.raceRestriction) ||
            !readPOD(is, e.zoneId) ||
            !readPOD(is, e.chainHeadHint) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1)) { return false; }
        if (!readU32Vec(is, e.prevQuestIds) ||
            !readU32Vec(is, e.followupQuestIds)) { return false; }
                                  return true;
                              });
}

bool WoweeQuestGraphLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

namespace {

WoweeQuestGraph::Entry makeQuest(uint32_t qid, const char* name,
                                    uint8_t minL, uint8_t maxL,
                                    uint8_t qtype, uint8_t faction,
                                    uint32_t zoneId,
                                    uint8_t chainHead,
                                    std::vector<uint32_t> prev,
                                    std::vector<uint32_t> followups) {
    WoweeQuestGraph::Entry e;
    e.questId = qid; e.name = name;
    e.minLevel = minL; e.maxLevel = maxL;
    e.questType = qtype;
    e.factionAccess = faction;
    e.zoneId = zoneId;
    e.chainHeadHint = chainHead;
    e.prevQuestIds = std::move(prev);
    e.followupQuestIds = std::move(followups);
    return e;
}

} // namespace

WoweeQuestGraph WoweeQuestGraphLoader::makeStarterChain(
    const std::string& catalogName) {
    using G = WoweeQuestGraph;
    WoweeQuestGraph c;
    c.name = catalogName;
    // Northshire Abbey starter chain (zoneId=12):
    // Q1 = chain head (no prereqs, hints next).
    // Q2..Q5 form linear progression.
    c.entries.push_back(makeQuest(
        100, "A Threat Within", 1, 5,
        G::Normal, G::Alliance, 12, 1,
        {}, {101}));
    c.entries.push_back(makeQuest(
        101, "Wolves Across the Border", 2, 5,
        G::Normal, G::Alliance, 12, 0,
        {100}, {102}));
    c.entries.push_back(makeQuest(
        102, "Kobold Camp Cleanup", 3, 6,
        G::Normal, G::Alliance, 12, 0,
        {101}, {103}));
    c.entries.push_back(makeQuest(
        103, "Investigate Echo Ridge", 4, 7,
        G::Normal, G::Alliance, 12, 0,
        {102}, {104}));
    c.entries.push_back(makeQuest(
        104, "Report to Goldshire", 5, 8,
        G::Normal, G::Alliance, 12, 0,
        {103}, {}));   // last in chain, no
                        //  followups
    return c;
}

WoweeQuestGraph WoweeQuestGraphLoader::makeBranchedChain(
    const std::string& catalogName) {
    using G = WoweeQuestGraph;
    WoweeQuestGraph c;
    c.name = catalogName;
    // Demonstrates DAG semantics - Q1 unlocks both
    // Q2a and Q2b; both prereq Q3:
    //   Q1 -> Q2a -> Q3
    //   Q1 -> Q2b -> Q3
    // Q3 has TWO prereqs (must complete BOTH branches).
    c.entries.push_back(makeQuest(
        200, "Discover the Crystal", 10, 14,
        G::Group, G::Both, 47, 1,
        {}, {201, 202}));
    c.entries.push_back(makeQuest(
        201, "The Frost Branch", 11, 15,
        G::Normal, G::Both, 47, 0,
        {200}, {203}));
    c.entries.push_back(makeQuest(
        202, "The Fire Branch", 11, 15,
        G::Normal, G::Both, 47, 0,
        {200}, {203}));
    c.entries.push_back(makeQuest(
        203, "Forge the Amulet", 12, 16,
        G::Group, G::Both, 47, 0,
        {201, 202}, {}));   // requires BOTH 201
                              //  AND 202
    return c;
}

WoweeQuestGraph WoweeQuestGraphLoader::makeDailies(
    const std::string& catalogName) {
    using G = WoweeQuestGraph;
    WoweeQuestGraph c;
    c.name = catalogName;
    // Standalone daily quests - no prereqs, no
    // followups. chainHeadHint=1 since each is its
    // own root.
    c.entries.push_back(makeQuest(
        300, "Daily: Mana Cell Disposal", 50, 0,
        G::Daily, G::Both, 100, 1,
        {}, {}));
    c.entries.push_back(makeQuest(
        301, "Daily: Felblood Sample", 50, 0,
        G::Daily, G::Both, 100, 1,
        {}, {}));
    c.entries.push_back(makeQuest(
        302, "Daily: Crystal Restoration", 50, 0,
        G::Daily, G::Both, 100, 1,
        {}, {}));
    return c;
}

} // namespace pipeline
} // namespace wowee
