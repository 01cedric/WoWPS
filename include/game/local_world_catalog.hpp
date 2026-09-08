#pragma once
#include "game/local_gameplay.hpp"
#include <memory>
#include <string>
#include <vector>

namespace wowee::game {
struct LocalCatalogStart {
    uint8_t race = 1, classId = 1, level = 1;
    uint32_t mapId = 0;
    float x = 0, y = 0, z = 0, orientation = 0;
};
// Destination metadata only: upstream teleport SQL does not contain the
// triggering area geometry (that lives in the user's client DBCs).
struct LocalCatalogDestination {
    uint32_t id = 0, mapId = 0;
    std::string name;
    float x = 0, y = 0, z = 0, orientation = 0;
    bool instanceMap = false;
};
struct LocalCatalogMap {
    uint32_t id = 0, spawnCount = 0;
    bool instanceMap = false;
    std::string upstreamScript;
};
struct LocalCatalogIoStats {
    uint64_t diskReads = 0, cacheHits = 0;
    size_t cacheBytes = 0;
};
class LocalWorldCatalog {
public:
    static constexpr float CellSize = 256, MaxRadius = 256;
    static constexpr size_t MaxResults = 128, MaxRecordBytes = 16384;
    LocalWorldCatalog();
    ~LocalWorldCatalog();
    LocalWorldCatalog(const LocalWorldCatalog&) = delete;
    LocalWorldCatalog& operator=(const LocalWorldCatalog&) = delete;
    bool load(const std::string& directory, std::string& error);
    bool query(uint32_t mapId, float x, float y, float radius, size_t limit,
               std::vector<LocalNpcSpawn>& result, std::string& error) const;
    bool query3D(uint32_t mapId, float x, float y, float z, float radius, size_t limit,
                 std::vector<LocalNpcSpawn>& result, std::string& error) const;
    bool npc(uint32_t id, LocalNpcDefinition& result, std::string& error) const;
    bool item(uint32_t id, LocalItemDefinition& result, std::string& error) const;
    bool quest(uint32_t id, LocalQuestDefinition& result, std::string& error) const;
    bool questsForNpc(uint32_t id, std::vector<LocalQuestDefinition>& result, std::string& error) const;
    uint32_t fingerprint() const;
    const std::vector<LocalCatalogStart>& starts() const;
    const std::vector<LocalCatalogMap>& maps() const;
    const std::vector<LocalCatalogDestination>& destinations() const;
    // Useful for regression tests and diagnostics; no full-world allocation.
    size_t lastQueryRecordsRead() const;
    LocalCatalogIoStats ioStats() const;
private:
    bool queryImpl(uint32_t mapId, float x, float y, float z, bool useHeight,
                   float radius, size_t limit, std::vector<LocalNpcSpawn>& result,
                   std::string& error) const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
