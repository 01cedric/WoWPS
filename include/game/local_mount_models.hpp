#pragma once
#include <algorithm>
#include <cstdint>
#include <iterator>
namespace wowee::game {
struct LocalMountModel {uint32_t creature,display;};
inline constexpr LocalMountModel kLocalMountModels[]={
#include "game/local_mount_models_generated.inc"
};
inline uint32_t localMountDisplay(uint32_t creature) {
    auto it=std::lower_bound(std::begin(kLocalMountModels),std::end(kLocalMountModels),creature,
        [](const LocalMountModel& a,uint32_t id){return a.creature<id;});
    return it!=std::end(kLocalMountModels) && it->creature==creature ? it->display : 0;
}
}
