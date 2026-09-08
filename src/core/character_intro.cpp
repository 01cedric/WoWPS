#include "core/character_intro.hpp"
#include "core/coordinates.hpp"
#include "rendering/glue_camera.hpp"
#include "rendering/cinematic_camera.hpp"
#include <algorithm>
#include <cmath>
namespace wowee::core {
namespace {
glm::vec3 toWorld(const pipeline::CharacterIntroShot& shot,const glm::vec3& local){
    // Equivalent to TrinityCore's CinematicCamera origin/facing transform:
    // rotate the model-space XY vector, translate by the DBC origin, then use
    // the application's existing server -> canonical conversion.
    const float c=std::cos(shot.originFacing),s=std::sin(shot.originFacing);
    return coords::serverToCanonical(shot.originServer+glm::vec3(local.x*c-local.y*s,local.x*s+local.y*c,local.z));
}
glm::vec3 sampleVector(const pipeline::M2AnimationTrack& track,float ms){
    if(track.sequences.empty())return {};
    return rendering::cinematic::stabilizedPosition(track,ms);
}
float sampleRoll(const pipeline::M2AnimationTrack& track,float ms){
    if(track.sequences.empty())return 0;
    const auto& k=track.sequences.front();
    return rendering::cinematic::sample(track,ms,k.floatValues,k.spline().floatInTangents,k.spline().floatOutTangents,0.0f);
}
}
bool CharacterIntro::start(pipeline::CharacterIntroPlan plan){
    cancel();if(plan.shots.empty() || plan.shots.size()>8 || !plan.durationMs)return false;
    uint64_t sum=0;for(const auto& shot:plan.shots){if(!shot.durationMs)return false;sum+=shot.durationMs;}
    if(sum!=plan.durationMs || sum>600000)return false;
    plan_=std::move(plan);active_=true;paused_=true;
    if(!sample(0)){cancel();return false;}return true;
}
void CharacterIntro::cancel(){plan_={};elapsedMs_=0;active_=paused_=finished_=failed_=false;}
void CharacterIntro::advance(float deltaSeconds,bool terrainReady){
    if(!active_)return;paused_=!terrainReady;if(paused_ || !std::isfinite(deltaSeconds) || deltaSeconds<=0)return;
    elapsedMs_=std::min(double(plan_.durationMs),elapsedMs_+double(deltaSeconds)*1000.0);
    if(elapsedMs_>=plan_.durationMs){
        active_=false;paused_=false;finished_=true;
    }else if(!sample(elapsedMs_)){
        // Return view ownership, but never persist this as a completed intro.
        active_=false;paused_=false;finished_=false;failed_=true;
    }
}
std::optional<CharacterIntroFrame> CharacterIntro::frame()const{return active_?sample(elapsedMs_):std::nullopt;}
std::optional<CharacterIntroFrame> CharacterIntro::peekAhead(uint32_t ms)const{
    return active_?sample(std::min(double(plan_.durationMs),elapsedMs_+ms)):std::nullopt;
}
std::optional<CharacterIntroFrame> CharacterIntro::sample(double time)const{
    if(plan_.shots.empty())return std::nullopt;
    time=std::clamp(time,0.0,double(plan_.durationMs));double local=time;size_t index=0;
    while(index+1<plan_.shots.size() && local>=plan_.shots[index].durationMs){local-=plan_.shots[index].durationMs;++index;}
    const auto& shot=plan_.shots[index];const auto& camera=shot.camera;
    CharacterIntroFrame frame;frame.mapId=plan_.mapId;frame.sequenceId=plan_.sequenceId;frame.cameraId=shot.cameraId;
    frame.shotIndex=index;frame.shotTimeMs=static_cast<uint32_t>(std::min(local,double(shot.durationMs)));frame.sequenceTimeMs=static_cast<uint32_t>(time);
    frame.canonicalPosition=toWorld(shot,camera.positionBase+sampleVector(camera.positions,static_cast<float>(local)));
    frame.canonicalTarget=toWorld(shot,camera.targetBase+sampleVector(camera.targets,static_cast<float>(local)));
    frame.rollRadians=sampleRoll(camera.roll,static_cast<float>(local));frame.diagonalFovRadians=camera.fov;
    const glm::vec3 direction=frame.canonicalTarget-frame.canonicalPosition;
    const float distance=glm::length(direction);
    if(!rendering::glue::finite(frame.canonicalPosition) || !rendering::glue::finite(frame.canonicalTarget) ||
       !rendering::glue::finite(direction) || !std::isfinite(distance) || distance<0.0001f ||
       !std::isfinite(frame.rollRadians) || !std::isfinite(frame.diagonalFovRadians) ||
       frame.diagonalFovRadians<=0.0f || frame.diagonalFovRadians>=3.1415927f)return std::nullopt;
    return frame;
}
} // namespace wowee::core
