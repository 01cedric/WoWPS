#pragma once

/// The iconId -> icon path table from SpellIcon.dbc.
///
/// Five places built this: the spellbook, the talent tree, the HUD, the
/// achievement list in the window manager, and the background load in
/// Application. Each had its own copy of the layout lookup, the two column
/// numbers and the same guard against empty rows.
///
/// The columns matter more than the count of copies. A DBCFile asked for a
/// column the file does not have, or the wrong one, does not fail: it answers
/// zero or an empty string for every row, forever. Icons then do not appear
/// and nothing says why, so the fallbacks are named here and pinned by
/// test_spell_icon_columns against the shipped file.

#include <cstdint>
#include <string>
#include <unordered_map>

namespace wowee::pipeline {

class AssetManager;

/// SpellIcon.dbc is two columns: the icon id and its path under Interface.
/// Used when no expansion layout is active, which is every path that runs
/// before one is chosen.
inline constexpr uint32_t kSpellIconIdColumn = 0;
inline constexpr uint32_t kSpellIconPathColumn = 1;

/// Fill `out` with iconId -> path. Rows with no path or a zero id are
/// skipped; existing entries are overwritten. Does nothing if the file is
/// missing or the asset manager is not up yet, so a caller may retry.
void loadSpellIconPaths(AssetManager* assetManager,
                        std::unordered_map<uint32_t, std::string>& out);

}  // namespace wowee::pipeline
