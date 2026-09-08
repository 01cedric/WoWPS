#include "core/helm_visual.hpp"

#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_layout.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace wowee {
namespace core {

namespace {

constexpr const char* kHeadDir = "Item\\ObjectComponents\\Head\\";

std::string stripExtension(std::string name) {
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos) name.resize(dot);
    return name;
}

} // namespace

HelmVisual resolveHelmVisual(pipeline::AssetManager& assets,
                             uint32_t itemDisplayInfoId,
                             uint8_t raceId,
                             uint8_t genderId) {
    HelmVisual out;
    if (itemDisplayInfoId == 0) return out;

    auto dbc = assets.loadDBC("ItemDisplayInfo.dbc");
    if (!dbc) return out;
    const int32_t row = dbc->findRecordById(itemDisplayInfoId);
    if (row < 0) return out;

    const auto* layout = pipeline::getActiveDBCLayout()
        ? pipeline::getActiveDBCLayout()->getLayout("ItemDisplayInfo") : nullptr;
    const uint32_t modelField = layout ? (*layout)["LeftModel"] : 1u;
    const uint32_t textureField = layout ? (*layout)["LeftModelTexture"] : 3u;

    const std::string modelName =
        stripExtension(dbc->getString(static_cast<uint32_t>(row), modelField));
    if (modelName.empty()) return out;

    const std::string suffix = raceGenderSuffix(raceId, genderId);
    if (!suffix.empty()) out.racialModelPath = kHeadDir + modelName + suffix + ".m2";
    out.baseModelPath = kHeadDir + modelName + ".m2";

    const std::string textureName = dbc->getString(static_cast<uint32_t>(row), textureField);
    if (!textureName.empty()) {
        if (!suffix.empty()) {
            const std::string racial = kHeadDir + textureName + suffix + ".blp";
            if (assets.fileExists(racial)) out.texturePath = racial;
        }
        if (out.texturePath.empty()) out.texturePath = kHeadDir + textureName + ".blp";
    }
    return out;
}


namespace {

/// Which ItemDisplayInfo columns hold HelmetGeosetVis[male] and [female].
///
/// They move with the file: 12 and 13 in the 23-field build that ships in the
/// expansion overlays, 13 and 14 in the 25-field one. Rather than keep a table
/// of indices per expansion and get it wrong the way the facial-feature columns
/// were, find them once by asking which columns actually reference the
/// visibility table.
struct VisColumns { uint32_t male = 0xFFFFFFFFu; uint32_t female = 0xFFFFFFFFu; };

VisColumns findVisColumns(pipeline::DBCFile& displayInfo, pipeline::DBCFile& visData) {
    VisColumns out;
    std::unordered_set<uint32_t> ids;
    for (uint32_t r = 0; r < visData.getRecordCount(); ++r) {
        ids.insert(visData.getUInt32(r, 0));
    }
    if (ids.empty()) return out;

    const uint32_t rows = displayInfo.getRecordCount();
    const uint32_t step = std::max(1u, rows / 400u);
    for (uint32_t c = 0; c < displayInfo.getFieldCount(); ++c) {
        uint32_t nonZero = 0, valid = 0;
        for (uint32_t r = 0; r < rows; r += step) {
            const uint32_t v = displayInfo.getUInt32(r, c);
            if (v == 0) continue;
            ++nonZero;
            if (ids.count(v)) ++valid;
        }
        // A column of ids is a column where every value present is an id.
        if (nonZero >= 8 && valid == nonZero) {
            if (out.male == 0xFFFFFFFFu) out.male = c;
            else if (out.female == 0xFFFFFFFFu) { out.female = c; break; }
        }
    }
    return out;
}

} // namespace

bool helmHidesHair(pipeline::AssetManager& assets,
                   uint32_t itemDisplayInfoId,
                   uint8_t genderId) {
    if (itemDisplayInfoId == 0) return false;

    auto displayInfo = assets.loadDBC("ItemDisplayInfo.dbc");
    auto visData = assets.loadDBC("HelmetGeosetVisData.dbc");
    if (!displayInfo || !visData) return true;  // no data: keep the old behaviour

    static VisColumns columns = findVisColumns(*displayInfo, *visData);
    const uint32_t column = (genderId == 0) ? columns.male : columns.female;
    if (column == 0xFFFFFFFFu) return true;

    const int32_t row = displayInfo->findRecordById(itemDisplayInfoId);
    if (row < 0) return true;
    const uint32_t visId = displayInfo->getUInt32(static_cast<uint32_t>(row), column);
    if (visId == 0) return false;  // hides nothing

    const int32_t visRow = visData->findRecordById(visId);
    if (visRow < 0) return false;
    // Every mask zero is the row circlets, tiaras and crowns point at.
    for (uint32_t f = 1; f < visData->getFieldCount(); ++f) {
        if (visData->getUInt32(static_cast<uint32_t>(visRow), f) != 0) return true;
    }
    return false;
}

} // namespace core
} // namespace wowee
