// AssetManager-bound implementation of WoweeModelLoader::fromM2(path, am).
// Split out from wowee_model.cpp so the asset extractor can link the core
// model loader/saver without dragging the AssetManager dependency in.

#include "pipeline/wowee_model.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/m2_loader.hpp"

namespace wowee {
namespace pipeline {

// Friend bridge to the conversion helper defined in wowee_model.cpp.
WoweeModel convertM2ToWomShared(const M2Model& m2);

WoweeModel WoweeModelLoader::fromM2(const std::string& m2Path, AssetManager* am) {
    WoweeModel model;
    if (!am) return model;

    auto data = am->readFile(m2Path);
    if (data.empty()) return model;

    auto m2 = M2Loader::load(data);

    // WotLK+ M2s store header in .m2 but geometry in .skin - always merge
    // the skin file when present so we get vertices/indices/batches even
    // for M2s that already report isValid() (older expansions).
    {
        auto skinData = am->readFile(skinPathForM2(m2Path));
        if (!skinData.empty())
            M2Loader::loadSkin(skinData, m2);
    }

    if (!m2.isValid()) return model;
    return convertM2ToWomShared(m2);
}

} // namespace pipeline
} // namespace wowee
