#pragma once
#include "local_group_rewards_fixture.hpp"
#include "game/local_world_catalog.hpp"
inline void instanceContent(LocalGameplay& game,const char* directory) {
    auto content=rewardContent();content->catalog=std::make_shared<LocalWorldCatalog>();
    std::string error;assert(content->catalog->load(directory,error));game.useContent(content);
    // Synthetic entrance geometry; destinations use the packaged catalog.
    assert(game.setAreaTriggers({{45,0,0,0,0,5},{78,0,0,0,0,5}},error));
}
inline void entrance(LocalRealmPlayer& p) {
    p.mapId=p.instanceId=0;p.x=p.y=p.z=0;p.orientation=.5f;p.portalCooldown=0;p.hasInstanceReturn=false;
}
inline uint32_t enterInstance(LocalGameplay& game,LocalRealmPlayer& p,uint32_t portal=45,bool privateMode=false) {
    entrance(p);std::string result;
    assert(game.execute(p,{LocalAction::EnterPortal,privateMode?1ULL:0ULL,portal},{&p},result));
    assert(p.instanceId && p.hasInstanceReturn && p.returnMapId==0 && p.returnOrientation==.5f);
    return p.instanceId;
}
