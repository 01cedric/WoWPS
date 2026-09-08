#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace wowee {
namespace pipeline {

struct CustomZoneInfo {
    std::string name;
    std::string author;
    std::string description;
    std::string directory;
    uint32_t mapId = 9000;
    std::vector<std::pair<int, int>> tiles;
    bool hasCreatures = false;
    bool hasQuests = false;
};

class CustomZoneDiscovery {
public:
    // Scan directories for custom zones (checks for zone.json files)
    static std::vector<CustomZoneInfo> scan(const std::vector<std::string>& searchPaths);

    // Scan a single directory
    static std::vector<CustomZoneInfo> scanDirectory(const std::string& path);
};

} // namespace pipeline
} // namespace wowee
