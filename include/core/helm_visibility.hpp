#pragma once

#include "pipeline/dbc_loader.hpp"
#include "pipeline/dbc_layout.hpp"
#include <array>
#include <cstdint>
#include <unordered_set>

namespace wowee::core {

// HelmetGeosetVisData's first five masks select hair, the three facial
// feature groups and ears independently. A mask applies to a race, not to
// every wearer of the item. Missing tables/rows do not hide body geometry.
struct HelmVisibility {
    std::array<bool, 5> hide{};
};

inline HelmVisibility resolveHelmVisibility(const pipeline::DBCFile* displayInfo,
    const pipeline::DBCFile* visibility, const pipeline::DBCFieldMap* layout,
    uint32_t displayId, uint8_t raceId, uint8_t modelSex) {
    HelmVisibility result;
    if (!displayInfo || !visibility || !displayInfo->isLoaded() || !visibility->isLoaded() ||
        displayId == 0 || raceId == 0 || raceId >= 32 || modelSex > 1 ||
        visibility->getFieldCount() < 6) return result;

    // Stock 3.3.5: 25 words, two icons, visibility columns 13/14.
    // Vanilla/early TBC: 23/24 words, one icon, columns 12/13.
    // Use the actual binary shape; an expansion's stale JSON can describe
    // a different base DBC. No ID-frequency heuristic or process-wide cache.
    uint32_t column = 0xffffffffu;
    switch (displayInfo->getFieldCount()) {
        case 23:
        case 24: column = 12u + modelSex; break;
        case 25: column = 13u + modelSex; break;
        default:
            if (layout) column = layout->tryField(modelSex == 0
                ? "HelmetGeosetVisID_0" : "HelmetGeosetVisID_1");
            break;
    }
    if (column >= displayInfo->getFieldCount()) return result;
    const int32_t displayRow = displayInfo->findRecordById(displayId);
    if (displayRow < 0) return result;
    const uint32_t visibilityId = displayInfo->getUInt32(displayRow, column);
    if (visibilityId == 0) return result;
    const int32_t visibilityRow = visibility->findRecordById(visibilityId);
    if (visibilityRow < 0) return result;

    // This table uses the race ID as the bit position (Human = bit 1), unlike
    // the race-eligibility masks used by items/spells. See M2_DbcHelmetHideMask:
    // https://github.com/corepunch/open-realm/blob/main/games/world-of-warcraft/renderer/m2/r_dbc.c
    const uint32_t raceBit = uint32_t{1} << raceId;
    for (uint32_t mask = 0; mask < result.hide.size(); ++mask) {
        result.hide[mask] = (visibility->getUInt32(visibilityRow, mask + 1) & raceBit) != 0;
    }
    return result;
}

inline void applyHelmVisibility(const HelmVisibility& visibility,
    std::unordered_set<uint16_t>& selected,
    const std::unordered_set<uint16_t>& modelGeosets) {
    constexpr std::array<uint16_t, 5> groups{0, 1, 2, 3, 7};
    for (size_t feature = 0; feature < groups.size(); ++feature) {
        if (!visibility.hide[feature]) continue;
        for (auto it = selected.begin(); it != selected.end();) {
            // Submesh zero is the body, even though hair shares group zero.
            if (*it != 0 && *it / 100 == groups[feature]) it = selected.erase(it);
            else ++it;
        }
        // A covering helm uses the bald scalp where the model has one.
        if (feature == 0 && modelGeosets.count(1) != 0) selected.insert(1);
    }
}

} // namespace wowee::core
