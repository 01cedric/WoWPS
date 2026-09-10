#include "pipeline/m2_asset_loader.hpp"

#include <algorithm>
#include <cstdio>

#include "pipeline/asset_manager.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/char_sections.hpp"
#include "core/logger.hpp"

namespace wowee {
namespace pipeline {

std::shared_ptr<const M2Model> loadCharacterPreviewModel(AssetManager& assets,
    const std::string& path, const CharacterSectionTextures& textures, const std::string& raceFolder) {
    PreviewModelCache::Key key{path,raceFolder,textures.bodySkin,textures.skinExtra,
        textures.faceLower,textures.faceUpper,textures.hair};
    key.insert(key.end(),textures.underwear.begin(),textures.underwear.end());
    uint64_t generation=0;
    if (auto model=assets.previewModels().find(key,generation)) {
        LOG_INFO("[PREVIEW_MODEL] reused immutable model path=",path);
        return model;
    }
    auto data=assets.readFile(path);
    if (data.empty()) return {};
    auto model=std::make_shared<M2Model>(M2Loader::load(data));
    if (model->name.empty()) model->name=path;
    if (model->version>=264) {
        auto skin=assets.readFile(skinPathForM2(path));
        if (!skin.empty()) M2Loader::loadSkin(skin,*model);
    }
    if (!model->isValid()) return {};
    applyCharacterTextures(*model,textures,raceFolder);
    loadExternalAnimations(assets,path,data,*model,{0}); // Stand and its variations.
    assets.previewModels().remember(key,generation,model);
    LOG_INFO("[PREVIEW_MODEL] prepared model path=",path," vertices=",model->vertices.size());
    return model;
}

bool loadM2WithSkin(AssetManager& assets, const std::string& m2Path, M2Model& outModel) {
    auto m2Data = assets.readFile(m2Path);
    if (m2Data.empty()) return false;

    outModel = M2Loader::load(m2Data);
    // Everything downstream identifies a model by its name, and a model that
    // does not carry one is identified by nothing.
    if (outModel.name.empty()) outModel.name = m2Path;

    if (outModel.version >= 264) {
        auto skinData = assets.readFile(skinPathForM2(m2Path));
        if (!skinData.empty()) M2Loader::loadSkin(skinData, outModel);
    }
    return outModel.isValid();
}

std::string animPathForM2(const std::string& m2Path, uint32_t animId, uint32_t variationIndex) {
    const std::string base = skinPathForM2(m2Path);
    // skinPathForM2 answers "<stem>00.skin"; the stem is what is wanted here.
    const std::string stem = base.substr(0, base.size() - 7);
    char tail[32];
    std::snprintf(tail, sizeof(tail), "%04u-%02u.anim", animId, variationIndex);
    return stem + tail;
}

void loadExternalAnimations(AssetManager& assets, const std::string& m2Path,
                            const std::vector<uint8_t>& m2Data, M2Model& model,
                            std::initializer_list<uint32_t> wantedAnimIds) {
    for (uint32_t si = 0; si < model.sequences.size(); si++) {
        // 0x20 means the keyframes are inside the M2 already.
        if (model.sequences[si].flags & 0x20) continue;
        const uint32_t animId = model.sequences[si].id;
        if (wantedAnimIds.size() > 0 &&
            std::find(wantedAnimIds.begin(), wantedAnimIds.end(), animId) == wantedAnimIds.end()) {
            continue;
        }
        auto animData = assets.readFileOptional(
            animPathForM2(m2Path, animId, model.sequences[si].variationIndex));
        if (!animData.empty()) M2Loader::loadAnimFile(m2Data, animData, si, model);
    }
}

}  // namespace pipeline
}  // namespace wowee
