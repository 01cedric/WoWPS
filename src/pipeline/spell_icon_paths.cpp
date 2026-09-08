#include "pipeline/spell_icon_paths.hpp"

#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_layout.hpp"

namespace wowee::pipeline {

void loadSpellIconPaths(AssetManager* assetManager,
                        std::unordered_map<uint32_t, std::string>& out) {
    if (!assetManager || !assetManager->isInitialized()) return;

    auto dbc = assetManager->loadDBC("SpellIcon.dbc");
    if (!dbc || !dbc->isLoaded()) return;

    // The active layout when there is one, the shipped columns when there is
    // not. Both answer the same for every expansion that ships this file.
    const auto* layout =
        getActiveDBCLayout() ? getActiveDBCLayout()->getLayout("SpellIcon") : nullptr;
    const uint32_t idColumn = layout ? (*layout)["ID"] : kSpellIconIdColumn;
    const uint32_t pathColumn = layout ? (*layout)["Path"] : kSpellIconPathColumn;

    for (uint32_t i = 0; i < dbc->getRecordCount(); ++i) {
        const uint32_t id = dbc->getUInt32(i, idColumn);
        std::string path = dbc->getString(i, pathColumn);
        // A row with no path is a hole in the table rather than an icon.
        if (id > 0 && !path.empty()) out[id] = std::move(path);
    }
}

}  // namespace wowee::pipeline
