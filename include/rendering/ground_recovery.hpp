#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace wowee::rendering {
// A recent supported foot must actually cross a floor at the destination
// column. Looking below an unrelated ADT or merely dropping off a ledge does
// not establish penetration. A generous motion cap rejects external warps.
inline bool crossedConfirmedGroundPlane(glm::vec3 previous,glm::vec3 current,
                                       glm::vec3 checkpoint,float floor) {
    if (!std::isfinite(floor)) return false;
    for(unsigned i=0;i<3;++i)
        if(!std::isfinite(previous[i])||!std::isfinite(current[i])||!std::isfinite(checkpoint[i])) return false;
    return glm::length(previous-checkpoint)<=0.5f &&
           glm::length(glm::vec2(current-previous))<=4.0f &&
           std::abs(floor-checkpoint.z)<=0.35f && previous.z>=floor-0.15f &&
           current.z<floor-0.35f;
}

// A checkpoint is earned by continuous real support, not a spawn Z, a floor
// somewhere below an airborne player, or a seam's synthetic adhesion height.
class GroundRecovery {
    uint32_t map_ = UINT32_MAX;
    bool enabled_ = false, ghost_ = false, valid_ = false;
    glm::vec3 safe_{};
    float supportedTime_ = 0, fallingTime_ = 0;
    unsigned supportedFrames_ = 0;
    static bool finite(glm::vec3 p) {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
    }
public:
    void reset() { valid_=false;supportedTime_=fallingTime_=0;supportedFrames_=0; }
    void setContext(uint32_t map,bool enabled,bool ghost) {
        if(map!=map_ || enabled!=enabled_ || ghost!=ghost_) reset();
        map_=map;enabled_=enabled && map!=UINT32_MAX;ghost_=ghost;
    }
    bool enabled() const {return enabled_;}
    bool hasPosition() const {return valid_;}
    const glm::vec3& position() const {return safe_;}
    float fallingTime() const {return fallingTime_;}
    void observe(glm::vec3 feet,float dt,bool supported,bool falling,bool suppressed) {
        if(!enabled_ || suppressed || !finite(feet)) {reset();return;}
        // A discontinuous relocation must not retain a checkpoint from the
        // old area, even if a caller bypassed the normal teleport/reset API.
        if(valid_ && glm::length(glm::vec2(feet-safe_))>128.f) reset();
        dt=std::isfinite(dt)?std::clamp(dt,0.f,0.25f):0.f;
        if(supported) {
            fallingTime_=0;supportedTime_+=dt;++supportedFrames_;
            if(supportedTime_>=0.35f && supportedFrames_>=3) {safe_=feet;valid_=true;}
        } else {
            supportedTime_=0;supportedFrames_=0;
            fallingTime_=falling?fallingTime_+dt:0.f;
        }
    }
    bool due(glm::vec3 feet) const {
        return enabled_&&valid_&&finite(feet)&&fallingTime_>=1.5f&&
               safe_.z-feet.z>=12.f&&glm::length(glm::vec2(feet-safe_))<=128.f;
    }
};
}
