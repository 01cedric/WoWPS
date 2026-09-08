#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace wowee::game {
// The Acherus teleport helpers in the bundled creature spawn catalog. Helper
// origins lie below the walking floor; the client resolves the actual WMO floor
// on arrival. Keep each pair on its own map, including the phased start map.
struct LocalScriptedPortal {
    uint32_t id, destination, mapId, helperEntry;
    float x, y, helperZ, orientation;
    const char* name;
    float arrivalZ() const { return helperZ + 5.5f; }
    bool contains(uint32_t map, float px, float py, float pz) const {
        const float dx=px-x, dy=py-y;
        return map==mapId && std::isfinite(px) && std::isfinite(py) && std::isfinite(pz)
            && dx*dx+dy*dy<=3.5f*3.5f && pz>=helperZ+1.f && pz<=helperZ+10.f;
    }
};
inline constexpr std::array<LocalScriptedPortal,4> LocalAcherusPortals{{
    {0x80000001u,0x80000002u,609,29580,2389.99f,-5640.93f,378.228f,.488692f,"Heart of Acherus"},
    {0x80000002u,0x80000001u,609,29581,2383.65f,-5645.24f,420.901f,3.59538f,"Hall of Command"},
    {0x80000003u,0x80000004u,0,29588,2390.f,-5640.98f,378.218f,3.12414f,"Heart of Acherus"},
    {0x80000004u,0x80000003u,0,29589,2383.7f,-5645.17f,421.903f,5.63741f,"Hall of Command"}
}};
inline const LocalScriptedPortal* localScriptedPortal(uint32_t id) {
    for (const auto& portal:LocalAcherusPortals) if(portal.id==id)return &portal;
    return nullptr;
}
}
