#include "ui/unit_portrait.hpp"
#include "game/equipment_hash.hpp"
#include "core/logger.hpp"
#include "game/game_handler.hpp"
#include "rendering/character_preview.hpp"
#include "rendering/renderer.hpp"
#include <algorithm>
#include <cmath>
#include <new>
namespace wowee::ui {
namespace {
template <class Build>
bool rebuildPortrait(rendering::CharacterPreview& preview, Build&& build) {
    try {
        if (build()) return true;
        // Normal load failures can leave partially prepared model resources,
        // just like allocation failures. Retire them before the delayed retry.
        preview.releaseCharacterModel();
        return false;
    } catch (const std::bad_alloc&) {
        // A UI preview owns its model independently of the running world.
        // Roll back that model/attachments, retaining the render target until
        // the normal fence-safe UI teardown. No partially prepared portrait
        // becomes visible and a later update retries after streaming reclaims.
        preview.releaseCharacterModel();
        LOG_WARNING("[PORTRAIT_MEMORY] model preparation rolled back; retrying after memory reclamation");
        return false;
    }
}
}
UnitPortrait::UnitPortrait()=default;
UnitPortrait::~UnitPortrait()=default;
bool UnitPortrait::ensure(pipeline::AssetManager* assets,rendering::Renderer* renderer,float dt){
    if(!assets || !renderer)return false;
    const float elapsed=std::isfinite(dt)?std::clamp(dt,0.f,.25f):0.f;
    retryRemaining_=std::max(0.f,retryRemaining_-elapsed);
    if(retryRemaining_>0)return false;
    if(preview_)return true;
    try {
    preview_=std::make_unique<rendering::CharacterPreview>();
    // Every face source is square; MicroButtonPortrait crops that square.
    // Full-body views retain their actual panel aspect ratio.
    const int w=std::clamp(targetWidth_,64,800),h=std::clamp(targetHeight_,64,800);
    initialized_=preview_->initialize(assets,framing_==Framing::Face?std::max(w,h):w,
                                           framing_==Framing::Face?std::max(w,h):h);
    if(!initialized_){preview_.reset();valid_=false;retryRemaining_=1.f;return false;}
    renderer->registerPreview(preview_.get());registered_=true;valid_=false;
    return true;
    } catch (const std::bad_alloc&) {
        // Initial FBO/pipeline setup also owns allocations; it happens before
        // this preview records any drawing, so partial setup can be destroyed.
        if (preview_) renderer->unregisterPreview(preview_.get());
        preview_.reset();registered_=initialized_=valid_=false;retryRemaining_=1.f;
        LOG_WARNING("[PORTRAIT_MEMORY] preview initialization deferred");
        return false;
    }
}
void UnitPortrait::advance(float dt,bool changed){
    if(!preview_ || !valid_)return;
    pendingDelta_+=std::isfinite(dt)?std::clamp(dt,0.f,.25f):0.f;
    // Every portrait is an offscreen Vulkan pass. Player, target, focus, pet
    // and party portraits used to become due on the same 30 Hz frame, making
    // addonWidgets wait for several passes at once. Ten updates per second is
    // still fluid at portrait size and cuts this GPU/CPU work by two thirds.
    constexpr float kFaceStep = 1.f / 10.f;
    constexpr float kModelStep = 1.f / 20.f;
    const float step = framing_ == Framing::Face ? kFaceStep : kModelStep;
    if(!changed && pendingDelta_<step)return;
    preview_->update(std::min(pendingDelta_,.1f));pendingDelta_=0;
    preview_->render();preview_->requestComposite();
}
void UnitPortrait::frame(){
    // The preview keeps only the authored camera/head measurements from the M2.
    if(framing_==Framing::Face)preview_->setPortraitFraming();else preview_->resetView();
}
void UnitPortrait::failed(){valid_=false;retryRemaining_=1.f;pendingDelta_=0;}
void UnitPortrait::update(game::GameHandler& game,pipeline::AssetManager* assets,
                           rendering::Renderer* renderer,float dt){
    const game::Character* self=nullptr;
    for(const auto& ch:game.getCharacters())if(ch.guid==game.getPlayerGuid()){self=&ch;break;}
    if(!self){valid_=false;return;}
    if(!ensure(assets,renderer,dt))return;
    const auto hash=game::hashEquipmentAppearance(self->equipment);
    const bool changed=!valid_ || loadedGuid_!=self->guid || loadedAppearance_!=self->appearanceBytes ||
        loadedFacialFeatures_!=self->facialFeatures || loadedEquipHash_!=hash ||
        loadedRace_!=uint8_t(self->race) || loadedGender_!=uint8_t(self->gender) ||
        loadedFemaleModel_!=self->useFemaleModel || !loadedCreaturePath_.empty();
    if(changed){
        preview_->setTransparentBackground(true);
        const auto bytes=self->appearanceBytes;
        if(!rebuildPortrait(*preview_, [&] {
            if(!preview_->loadCharacter(self->race,self->gender,bytes&255,(bytes>>8)&255,
                                       (bytes>>16)&255,(bytes>>24)&255,self->facialFeatures,self->useFemaleModel))return false;
            preview_->applyEquipment(self->equipment);frame();return true;
        })){
            failed();return;
        }
        valid_=true;loadedCreaturePath_.clear();loadedSkins_.clear();loadedBake_.clear();pendingBake_.clear();
        loadedGuid_=self->guid;loadedAppearance_=bytes;loadedFacialFeatures_=self->facialFeatures;
        loadedRace_=uint8_t(self->race);loadedGender_=uint8_t(self->gender);loadedFemaleModel_=self->useFemaleModel;
        loadedEquipHash_=hash;LOG_INFO("UnitPortrait: player rebuilt guid=",loadedGuid_);
    }
    advance(dt,changed);
}
bool UnitPortrait::updatePlayer(uint8_t race,uint8_t gender,uint32_t bytes,uint8_t facial,
        const std::vector<game::EquipmentItem>& equipment,pipeline::AssetManager* assets,
        rendering::Renderer* renderer,float dt){
    if(!ensure(assets,renderer,dt))return false;
    const auto hash=game::hashEquipmentAppearance(equipment);
    const bool changed=!valid_ || loadedAppearance_!=bytes || loadedFacialFeatures_!=facial ||
        loadedRace_!=race || loadedGender_!=gender || loadedEquipHash_!=hash ||
        loadedBake_!=pendingBake_ || !loadedCreaturePath_.empty();
    if(changed){
        preview_->setTransparentBackground(true);
        if(!rebuildPortrait(*preview_, [&] {
            if(!preview_->loadCharacter(static_cast<game::Race>(race),static_cast<game::Gender>(gender),
                    bytes&255,(bytes>>8)&255,(bytes>>16)&255,(bytes>>24)&255,facial,gender==1))return false;
            if(!equipment.empty())preview_->applyEquipment(equipment);
            if(!pendingBake_.empty())preview_->setBakedSkin(pendingBake_);
            frame();return true;
        })){
            failed();return false;
        }
        valid_=true;loadedGuid_=0;loadedCreaturePath_.clear();loadedSkins_.clear();
        loadedRace_=race;loadedGender_=gender;loadedAppearance_=bytes;loadedFacialFeatures_=facial;
        loadedEquipHash_=hash;loadedBake_=pendingBake_;
        LOG_INFO("UnitPortrait: humanoid rebuilt race=",int(race)," appearance=",bytes);
    }
    advance(dt,changed);return valid_ && preview_->isModelLoaded();
}
bool UnitPortrait::updateCreature(const std::string& path,
        const std::vector<std::pair<uint32_t,std::string>>& skins,pipeline::AssetManager* assets,
        rendering::Renderer* renderer,float dt){
    if(path.empty()){valid_=false;return false;}
    if(!ensure(assets,renderer,dt))return false;
    // Two creatures can share an M2 but have different display skins.
    const bool changed=!valid_ || loadedCreaturePath_!=path || loadedSkins_!=skins;
    if(changed){
        preview_->setTransparentBackground(true);
        if(!rebuildPortrait(*preview_, [&] {
            if(!preview_->loadCreature(path,skins))return false;
            frame();return true;
        })){failed();return false;}
        valid_=true;loadedCreaturePath_=path;loadedSkins_=skins;
        loadedGuid_=0;loadedAppearance_=0;loadedFacialFeatures_=0;loadedEquipHash_=0;
        loadedBake_.clear();pendingBake_.clear();loadedRace_=loadedGender_=255;
        LOG_INFO("UnitPortrait: creature rebuilt ",path);
    }
    advance(dt,changed);return valid_ && preview_->isModelLoaded();
}
uint64_t UnitPortrait::textureId() const {
    return preview_ && valid_?reinterpret_cast<uint64_t>(preview_->getTextureId()):0;
}
void UnitPortrait::rotate(float yawDelta){if(preview_ && yawDelta!=0)preview_->rotate(yawDelta);}
void UnitPortrait::shutdown(rendering::Renderer* renderer){
    if(preview_ && registered_ && renderer)renderer->unregisterPreview(preview_.get());
    registered_=false;preview_.reset();initialized_=valid_=false;retryRemaining_=pendingDelta_=0;
    loadedGuid_=loadedEquipHash_=0;loadedAppearance_=0;loadedFacialFeatures_=0;
    loadedRace_=loadedGender_=255;loadedFemaleModel_=false;
    loadedCreaturePath_.clear();loadedSkins_.clear();pendingBake_.clear();loadedBake_.clear();
}
} // namespace wowee::ui
