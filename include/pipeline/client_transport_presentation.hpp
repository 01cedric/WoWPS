#pragma once

#include "pipeline/dbc_loader.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace wowee::pipeline {

// WotLK SoundEntries has ten file fields followed by ten frequency fields.
// DirectoryBase is field 23, not field 22 (the last frequency value).
inline std::string clientSoundEntryPath(const DBCFile* dbc, uint32_t row, unsigned file) {
    if (!dbc || !dbc->isLoaded() || dbc->getFieldCount() < 29 ||
        row >= dbc->getRecordCount() || file >= 10) return {};
    const auto name = dbc->getString(row, 3 + file);
    if (name.empty()) return {};
    auto directory = dbc->getString(row, 23);
    if (!directory.empty() && directory.back() != '\\' && directory.back() != '/') directory += '\\';
    directory += name;
    std::replace(directory.begin(), directory.end(), '/', '\\');
    if (directory.find("..") != std::string::npos || directory.find(':') != std::string::npos) return {};
    return directory;
}

struct ClientTravelPoint { float x = 0, y = 0; };
struct ClientTravelSpline {
    uint32_t rowId = 0, pathId = 0, legIndex = 0;
    std::array<ClientTravelPoint, 8> points{};
    unsigned count = 0;
    bool valid() const noexcept { return count >= 2 && count <= points.size(); }
};

// Legacy client layout: ID, TaxiPath, LocX[8], LocY[8], LegIndex (76 bytes).
// Refuse other layouts, absent legs and non-normalized/invalid coordinates;
// a missing authored route must not be replaced with an invented travel line.
inline ClientTravelSpline resolveClientTravelSpline(const DBCFile* dbc,
                                                     uint32_t pathId, uint32_t legIndex) {
    ClientTravelSpline out;
    if (!dbc || !dbc->isLoaded() || dbc->getFieldCount() != 19 ||
        dbc->getRecordSize() != 76 || !pathId) return out;
    for (uint32_t row = 0; row < dbc->getRecordCount(); ++row) {
        if (dbc->getUInt32(row, 1) != pathId || dbc->getUInt32(row, 18) != legIndex) continue;
        out.rowId = dbc->getUInt32(row, 0);out.pathId = pathId;out.legIndex = legIndex;
        unsigned end = 8;
        while (end && dbc->getFloat(row, 2 + end - 1) == 0 && dbc->getFloat(row, 10 + end - 1) == 0) --end;
        for (unsigned i = 0; i < end; ++i) {
            const float x = dbc->getFloat(row, 2 + i), y = dbc->getFloat(row, 10 + i);
            if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1) return {};
            if (out.count && out.points[out.count - 1].x == x && out.points[out.count - 1].y == y) continue;
            out.points[out.count++] = {x, y};
        }
        return out.valid() ? out : ClientTravelSpline{};
    }
    return {};
}

inline ClientTravelPoint sampleClientTravelSpline(const ClientTravelSpline& route, float progress) noexcept {
    if (!route.valid()) return {};
    if (!std::isfinite(progress)) progress = 0;
    progress = std::clamp(progress, 0.0f, 1.0f);
    if (progress <= 0.0f) return route.points[0];
    if (progress >= 1.0f) return route.points[route.count - 1];
    const float scaled = progress * (route.count - 1);
    const unsigned segment = std::min(static_cast<unsigned>(scaled), route.count - 2);
    const float t = scaled - segment;
    const auto& a = route.points[segment ? segment - 1 : segment];
    const auto& b = route.points[segment];
    const auto& c = route.points[segment + 1];
    const auto& d = route.points[std::min(segment + 2, route.count - 1)];
    const auto cubic = [t](float a, float b, float c, float d) {
        return std::clamp(0.5f * ((2*b) + (-a+c)*t + (2*a-5*b+4*c-d)*t*t + (-a+3*b-3*c+d)*t*t*t), 0.0f, 1.0f);
    };
    return {cubic(a.x,b.x,c.x,d.x), cubic(a.y,b.y,c.y,d.y)};
}

} // namespace wowee::pipeline
