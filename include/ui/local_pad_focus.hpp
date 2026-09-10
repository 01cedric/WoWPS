#pragma once
#include <cstddef>
#include <cstdint>
namespace wowee::ui {
// Selection is independent of the unit GUID. Edge presses are supplied by the
// pad backend, so a held button never repeats a spell, menu click or target toggle.
struct LocalPadFocus {
    enum class Lane : uint8_t { None, Actions, Menus };
    Lane lane=Lane::None;
    uint32_t widget=0;
    void select(Lane next,uint32_t id){lane=next;widget=id;}
    void clear(){lane=Lane::None;widget=0;}
    template<class Range> static uint32_t step(const Range& ids,uint32_t current,int direction){
        if(ids.empty() || !direction)return current;
        std::size_t at=ids.size();
        for(std::size_t i=0;i<ids.size();++i)if(ids[i]==current){at=i;break;}
        if(at==ids.size())return direction>0?ids.front():ids.back();
        return ids[(at+ids.size()+(direction>0?1:-1))%ids.size()];
    }
};
inline bool localTargetOwnsShoulders(uint64_t target,bool panelOpen,bool trianglePressed,bool released=false,bool nonCombat=false){return trianglePressed || (target && nonCombat && !panelOpen && !released);}
inline uint64_t toggledLocalTarget(uint64_t current,uint64_t nearest){return current?0:nearest;}
} // namespace wowee::ui
