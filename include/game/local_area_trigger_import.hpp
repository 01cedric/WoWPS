#pragma once
#include "game/local_gameplay.hpp"
#include <cmath>
#include <set>
#include <string>
#include <vector>
namespace wowee::game {
// Only the active shape participates in containment. Never treat placeholder
// dimensions of a spherical DBC record as a reason to reject the whole world.
inline bool normalizeClientAreaTrigger(LocalAreaTriggerVolume& v, std::string& reason) {
    const auto bounded=[](float f,float limit) { return std::isfinite(f) && std::abs(f)<=limit; };
    if (!v.id || v.mapId>10000) { reason="identity/map"; return false; }
    if (!bounded(v.x,100000) || !bounded(v.y,100000) || !bounded(v.z,100000)) {
        reason="position"; return false;
    }
    if (!std::isfinite(v.radius)) { reason="non-finite radius"; return false; }
    if (v.radius>0) {
        if (v.radius>200000) { reason="sphere extent"; return false; }
        v.boxLength=v.boxWidth=v.boxHeight=v.boxYaw=0;
    } else {
        if (!(v.boxLength>0 && v.boxWidth>0 && v.boxHeight>0) ||
            !bounded(v.boxLength,200000) || !bounded(v.boxWidth,200000) ||
            !bounded(v.boxHeight,200000) || !std::isfinite(v.boxYaw)) {
            reason="empty/invalid box"; return false;
        }
        v.radius=0;
        v.boxYaw=std::remainder(v.boxYaw,6.2831853071795864769f);
    }
    reason.clear(); return true;
}
// Callback gets the ORIGINAL row, so logs retain malformed source values.
template<class Rejected>
std::vector<LocalAreaTriggerVolume> importClientAreaTriggers(
    const std::vector<LocalAreaTriggerVolume>& rows, Rejected rejected) {
    std::vector<LocalAreaTriggerVolume> result;
    std::set<uint32_t> ids;
    for (const auto& source:rows) {
        auto v=source; std::string reason;
        if (!normalizeClientAreaTrigger(v,reason)) { rejected(source,reason); continue; }
        if (!ids.insert(v.id).second) { rejected(source,"duplicate ID"); continue; }
        if (result.size()==16384) { rejected(source,"volume limit"); continue; }
        result.push_back(v);
    }
    return result;
}
}
