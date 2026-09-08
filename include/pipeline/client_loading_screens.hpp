#pragma once

#include "pipeline/dbc_loader.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace wowee::pipeline {

struct ClientLoadingArt {
    std::string path;
    bool wide = false;
};

struct ClientLoadingScreen {
    uint32_t screenId = 0;
    bool matchedMap = false;
    bool matchedScreen = false;
    std::vector<ClientLoadingArt> candidates;
};

inline ClientLoadingScreen resolveClientLoadingScreen(const DBCFile* maps,
        const DBCFile* screens, uint32_t mapId, bool widescreen) {
    ClientLoadingScreen out;
    // WotLK build 12340: raw Map.dbc has exactly 66 scalar fields. Reading
    // field 57 in a different expansion's table can select an unrelated row.
    if (maps && maps->isLoaded() && maps->getFieldCount() == 66) {
        for (uint32_t row = 0; row < maps->getRecordCount(); ++row) {
            if (maps->getUInt32(row, 0) == mapId) {
                out.screenId = maps->getUInt32(row, 57);
                out.matchedMap = true;
                break;
            }
        }
    }
    std::string path;
    bool hasWide = false;
    if (out.matchedMap && screens && screens->isLoaded() && screens->getFieldCount() == 4) {
        for (uint32_t row = 0; row < screens->getRecordCount(); ++row) {
            if (screens->getUInt32(row, 0) == out.screenId) {
                path = screens->getString(row, 2);
                hasWide = screens->getUInt32(row, 3) != 0;
                out.matchedScreen = !path.empty();
                break;
            }
        }
    }
    const auto append = [&](std::string base, bool allowWide) {
        std::replace(base.begin(), base.end(), '/', '\\');
        std::string lower = base;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.ends_with(".blp")) { base.resize(base.size() - 4); lower.resize(lower.size() - 4); }
        if (base.empty() || base.find("..") != std::string::npos || base.find(':') != std::string::npos)
            return;
        if (base.find('\\') == std::string::npos)
            base = "Interface\\Glues\\LoadingScreens\\" + base;
        const bool alreadyWide = lower.ends_with("wide");
        const auto add = [&](const std::string& candidate, bool wide) {
            for (const auto& old : out.candidates) if (old.path == candidate) return;
            out.candidates.push_back({candidate, wide});
        };
        if (alreadyWide) {
            if (widescreen) add(base + ".blp", true);
            base.resize(base.size() - 4);
            add(base + ".blp", false);
        } else {
            if (widescreen && allowWide) add(base + "Wide.blp", true);
            add(base + ".blp", false);
        }
    };
    if (!path.empty()) append(path, hasWide);
    // A missing map/row/picture selects a real generic client painting. It
    // never selects an unrelated zone or the packaged illustration.
    append("Interface\\Glues\\LoadingScreens\\LoadScreenGeneric", true);
    return out;
}

inline constexpr const char* ClientLoadingBarBorder =
    "Interface\\Glues\\LoadingBar\\Loading-BarBorder.blp";
inline constexpr const char* ClientLoadingBarFill =
    "Interface\\Glues\\LoadingBar\\Loading-BarFill.blp";

} // namespace wowee::pipeline
