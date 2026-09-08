#pragma once
#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>

namespace wowee::rendering::preview {
inline bool finite(const glm::vec3& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
// Shrink only the avatar about its authored stand mark. The scene camera and
// backdrop never move. A yaw-independent XY envelope prevents breathing while
// rotating the character; bind-pose bounds avoid scale oscillation during idle.
inline float fitScale(glm::vec3 eye, glm::vec3 target, glm::vec3 stand,
                      glm::vec3 lo, glm::vec3 hi, float verticalFov, float aspect) {
    if (!finite(eye)||!finite(target)||!finite(stand)||!finite(lo)||!finite(hi)||
        !std::isfinite(verticalFov)||verticalFov<=0||verticalFov>=3.1f||
        !std::isfinite(aspect)||aspect<=0||hi.z<=lo.z) return 1;
    glm::vec3 f=target-eye;
    if (glm::dot(f,f)<1e-8f) return 1;
    f=glm::normalize(f); glm::vec3 right=glm::cross(f,glm::vec3(0,0,1));
    if (glm::dot(right,right)<1e-8f) return 1;
    right=glm::normalize(right); const glm::vec3 up=glm::cross(right,f);
    const double ty=std::tan(verticalFov*.5),tx=ty*aspect;
    const float rx=std::max(std::abs(lo.x),std::abs(hi.x));
    const float ry=std::max(std::abs(lo.y),std::abs(hi.y));
    const float radius=std::hypot(rx,ry);
    const float top=hi.z+(hi.z-lo.z)*.07f; // idle/horns margin
    auto fits=[&](float s) {
        for(float x:{-radius,radius}) for(float y:{-radius,radius}) for(float z:{lo.z,top}) {
            const glm::vec3 v=stand+glm::vec3(x,y,z)*s-eye;
            const double depth=glm::dot(v,f);
            if(depth<=.05) return false;
            const double nx=glm::dot(v,right)/(depth*tx),ny=glm::dot(v,up)/(depth*ty);
            if(std::abs(nx)>.53 || ny>.80 || ny<-.98) return false;
        }
        return true;
    };
    if(fits(1)) return 1;
    if(!fits(.02f)) return 1; // a broken/offscreen stand is not a size problem
    float low=.02f,high=1;
    for(int i=0;i<24;++i) { float mid=(low+high)*.5f; if(fits(mid)) low=mid; else high=mid; }
    return low;
}
} // namespace wowee::rendering::preview
