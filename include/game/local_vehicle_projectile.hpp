#pragma once
#include "game/local_gameplay.hpp"
#include <limits>

namespace wowee::game {
inline constexpr float kLocalVehiclePi=3.14159265358979323846f;
inline bool validLocalVehicleProjectileView(const LocalVehicleProjectile& p) {
    return p.id && p.spellId && p.ownerGuid && p.sourceGuid && p.mapId<=10000 && p.instanceId<=65535 &&
        (p.sourceGuid&0xffff000000000000ULL)==0xf130000000000000ULL && uint32_t((p.sourceGuid>>32)&0xffff)==p.instanceId &&
        p.remainingMs && p.remainingMs<=10000 &&
        std::isfinite(p.x) && std::abs(p.x)<=100000 && std::isfinite(p.y) && std::abs(p.y)<=100000 &&
        std::isfinite(p.z) && std::abs(p.z)<=20000 &&
        std::isfinite(p.vx) && std::abs(p.vx)<=120 && std::isfinite(p.vy) && std::abs(p.vy)<=120 &&
        std::isfinite(p.vz) && std::abs(p.vz)<=420 && std::isfinite(p.gravity) && p.gravity>=0 && p.gravity<=30;
}
// First contact with a stationary sphere over a whole simulated segment.
// Infinity denotes a miss. A zero-length segment inside a sphere hits at zero.
inline float localVehicleSegmentSphere(float ax,float ay,float az,float bx,float by,float bz,
                                        float x,float y,float z,float radius) {
    const double dx=double(bx)-ax,dy=double(by)-ay,dz=double(bz)-az;
    const double ox=double(ax)-x,oy=double(ay)-y,oz=double(az)-z;
    const double c=ox*ox+oy*oy+oz*oz-double(radius)*radius;
    if(c<=0)return 0;
    const double a=dx*dx+dy*dy+dz*dz,b=ox*dx+oy*dy+oz*dz;
    if(a<=1e-15 || b>=0)return std::numeric_limits<float>::infinity();
    const double discriminant=b*b-a*c;
    if(discriminant<0)return std::numeric_limits<float>::infinity();
    const double t=(-b-std::sqrt(discriminant))/a;
    return t>=0 && t<=1?float(t):std::numeric_limits<float>::infinity();
}
} // namespace wowee::game
