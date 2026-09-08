#pragma once
#include "rendering/m2_track_sampler.hpp"
#include <glm/gtc/quaternion.hpp>
#include <cmath>
namespace wowee::rendering {
struct M2UvTransform {
    glm::vec2 offset{0.f};
    glm::vec4 linear{1.f,0.f,0.f,1.f};
};
inline M2UvTransform sampleM2Uv(const pipeline::M2TextureTransform* transform,int sequence,
                              float time,float globalTime,const std::vector<uint32_t>& durations) {
    M2UvTransform result;
    if(!transform)return result;
    const auto t=m2_track::sampleVec3(transform->translation,sequence,time,globalTime,durations,glm::vec3(0));
    const auto scale=m2_track::sampleVec3(transform->scale,sequence,time,globalTime,durations,glm::vec3(1));
    const auto q=m2_track::sampleQuat(transform->rotation,sequence,time,globalTime,durations);
    const auto r=glm::mat3_cast(q);
    result.linear={r[0][0]*scale.x,r[0][1]*scale.x,r[1][0]*scale.y,r[1][1]*scale.y};
    result.offset=glm::vec2(t)+glm::vec2(.5f)-glm::vec2(
        result.linear.x+result.linear.z,result.linear.y+result.linear.w)*.5f;
    for(unsigned i=0;i<4;++i)if(!std::isfinite(result.linear[i]))return {};
    if(!std::isfinite(result.offset.x)||!std::isfinite(result.offset.y))return {};
    return result;
}
}
