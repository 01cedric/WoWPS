#pragma once
#include "pipeline/asset_manager.hpp"
#include "pipeline/m2_loader.hpp"
#include "game/local_forms.hpp"
#include "game/local_realm.hpp"
namespace wowee::core {
struct LocalFormAsset {std::string path;std::array<std::string,3> skins{};};
inline LocalFormAsset localFormAsset(pipeline::AssetManager& assets,uint32_t display){
    LocalFormAsset out;if(!display)return out;
    auto info=assets.loadDBC("CreatureDisplayInfo.dbc");auto models=assets.loadDBC("CreatureModelData.dbc");
    if(!info||!models||!info->isLoaded()||!models->isLoaded()||info->getFieldCount()<9||models->getFieldCount()<3)return out;
    uint32_t model=0;
    for(uint32_t r=0;r<info->getRecordCount();++r)if(info->getUInt32(r,0)==display){model=info->getUInt32(r,1);for(unsigned k=0;k<3;++k)out.skins[k]=info->getString(r,6+k);break;}
    for(uint32_t r=0;r<models->getRecordCount();++r)if(models->getUInt32(r,0)==model){out.path=models->getString(r,2);break;}
    const auto ext=out.path.find_last_of('.');if(ext!=std::string::npos)out.path.replace(ext,std::string::npos,".m2");
    const auto slash=out.path.find_last_of("/\\");const auto directory=slash==std::string::npos?std::string{}:out.path.substr(0,slash+1);
    for(auto& skin:out.skins)if(!skin.empty()){if(skin.find_first_of("/\\")==std::string::npos)skin=directory+skin;if(skin.find('.')==std::string::npos)skin+=".blp";}
    return out;
}
inline void applyLocalFormTextures(pipeline::M2Model& model,const LocalFormAsset& form){
    for(auto& texture:model.textures)if(texture.type>=11&&texture.type<=13&&!form.skins[texture.type-11].empty()){
        texture.filename=form.skins[texture.type-11];texture.type=0;
    }
}
}
