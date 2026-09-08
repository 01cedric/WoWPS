#pragma once

// Which maps the client believes are instances, read from the player's own
// Map.dbc.
//
// The world catalog marks a destination as leading to an instance from
// AzerothCore's instance_template, which is server data and only covers the
// maps that dump happened to carry. Map.dbc is the client's own answer to the
// same question and covers every map the client can load: InstanceType says
// whether a map is a party dungeon, a raid, a battleground or an arena, and
// MaxPlayers says how many it holds. Reading it is what makes a dungeon or raid
// entrance usable whenever the client knows about it, rather than only when the
// catalog does.
//
// Nothing here invents a map. A map the client does not describe does not
// appear, and a destination with no entrance trigger in AreaTrigger.dbc is
// still not offered - the trigger is what the player has to stand in.

#include "game/local_gameplay.hpp"
#include "pipeline/dbc_loader.hpp"

#include <string>
#include <vector>

namespace wowee::game {

/// 3.3.5a (12340) Map.dbc column defaults. The application overrides them from
/// the active DBC layout where it has one; these are only used when it does
/// not, and the record width is checked before any of them is read.
inline constexpr uint32_t kMapDbcFieldId = 0;
inline constexpr uint32_t kMapDbcFieldDirectory = 1;
inline constexpr uint32_t kMapDbcFieldInstanceType = 2;
inline constexpr uint32_t kMapDbcFieldName = 5;
inline constexpr uint32_t kMapDbcFieldExpansion = 63;
inline constexpr uint32_t kMapDbcFieldMaxPlayers = 65;

/// Read Map.dbc into the rows the local realm needs. Returns an empty vector
/// for a table this build cannot read, which simply leaves the realm relying on
/// the catalog exactly as it did before.
inline std::vector<LocalMapDefinition> importClientMaps(
    const pipeline::DBCFile* maps,
    uint32_t idField = kMapDbcFieldId,
    uint32_t instanceTypeField = kMapDbcFieldInstanceType,
    uint32_t nameField = kMapDbcFieldName,
    uint32_t directoryField = kMapDbcFieldDirectory,
    uint32_t expansionField = kMapDbcFieldExpansion,
    uint32_t maxPlayersField = kMapDbcFieldMaxPlayers) {
    std::vector<LocalMapDefinition> rows;
    if (!maps || !maps->isLoaded()) return rows;
    const uint32_t fields = maps->getFieldCount();
    // The three fields every decision below depends on. The rest are optional
    // presentation: a narrower table still yields usable instance maps.
    if (fields <= instanceTypeField) return rows;
    rows.reserve(maps->getRecordCount());
    for (uint32_t row = 0; row < maps->getRecordCount(); ++row) {
        LocalMapDefinition map;
        map.id = maps->getUInt32(row, idField);
        if (map.id > 10000) continue;
        map.instanceType = maps->getUInt32(row, instanceTypeField);
        // Anything the client does not classify as dungeon, raid, battleground
        // or arena is the open world; a nonsense value is treated as such
        // rather than trusted into an instance.
        if (map.instanceType > 4) map.instanceType = 0;
        if (fields > maxPlayersField) {
            const auto players = maps->getUInt32(row, maxPlayersField);
            map.maxPlayers = players <= 100 ? players : 0;
        }
        if (fields > expansionField) {
            const auto expansion = maps->getUInt32(row, expansionField);
            map.expansion = expansion <= 8 ? expansion : 0;
        }
        if (fields > nameField) map.name = maps->getString(row, nameField);
        if (map.name.empty() && fields > directoryField) map.name = maps->getString(row, directoryField);
        if (map.name.size() > 96) map.name.clear();
        rows.push_back(std::move(map));
    }
    return rows;
}

} // namespace wowee::game
