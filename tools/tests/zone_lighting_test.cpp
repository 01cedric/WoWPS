#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>
// Exercise production update/sample/volume code with controlled DBC-format
// profile values. This is not a substitute for original-client data evidence.
#define private public
#include "rendering/lighting_manager.hpp"
#undef private
#include "rendering/zone_ambience.hpp"
#include "rendering/light_blend.hpp"
using namespace wowee::rendering;
float luminance(glm::vec3 c) { return glm::dot(c,glm::vec3(.2126f,.7152f,.0722f)); }
float distance(const LightingParams& a,const LightingParams& b) {
 return glm::length(a.ambientColor-b.ambientColor)+glm::length(a.diffuseColor-b.diffuseColor)+glm::length(a.skyTopColor-b.skyTopColor);
}
LightingParams policy(unsigned zone,float hour,LightingParams p=LightingParams{}) {
 applyOutdoorLightingPolicy(zone,hour,false,false,false,true,p);return p;
}
LightingManager manager(bool volumes) {
 LightingManager m;m.initialized_=true;m.setFogSkyBlend(0.f);m.setFogStrength(1.f);
 if(volumes) {
  LightParamsProfile p{};p.lightParamsId=1;
  for(auto& b:p.colorBands) {b.numKeyframes=2;b.times[0]=0;b.times[1]=1440;b.colors[0]={.35f,.50f,.65f};b.colors[1]={.8f,.7f,.5f};}
  for(auto& b:p.floatBands) {b.numKeyframes=1;b.times[0]=0;b.values[0]=.3f;}
  p.floatBands[0].values[0]=12000.f;
  m.lightParamsProfiles_[1]=p;
  LightVolume v;v.lightId=1;v.mapId=0;v.innerRadius=100;v.outerRadius=200;v.lightParamsId=1;
  m.lightVolumesByMap_[0].push_back(v);
 }
 return m;
}
int main() {
 LightingParams original;
 assert(distance(original,policy(12,12))==0.f);
 assert(distance(original,policy(14,12))==0.f);
 const auto warm=policy(14,17.25f);assert(warm.diffuseColor.r/warm.diffuseColor.b>original.diffuseColor.r/original.diffuseColor.b);
 const auto tir=policy(85,12);assert(luminance(tir.diffuseColor)<=.29001f);assert(luminance(tir.ambientColor)<=.25001f);
 assert(tir.diffuseColor.b/tir.diffuseColor.r>original.diffuseColor.b/original.diffuseColor.r);
 for(unsigned zone:{12u,14u,85u,130u,10u,1637u,1519u,3483u}) {
  auto night=policy(zone,0);assert(luminance(night.diffuseColor)<=.25001f);assert(luminance(night.ambientColor)<=.22001f);
  assert(distance(policy(zone,23.9999f),policy(zone,.0001f))<.0001f);
  for(float hour:{0.f,5.f,6.f,7.f,12.f,17.f,18.f,19.f,24.f})
   assert(distance(policy(zone,hour-.0001f),policy(zone,hour+.0001f))<.001f);
 }
 LightingParams purple=original;purple.skyTopColor={.3f,.07f,.8f};auto pn=policy(3483,0,purple);assert(pn.skyTopColor.b>pn.skyTopColor.r*2.f);assert(pn.skyTopColor.r>pn.skyTopColor.g*2.f);
 LightingParams black=original;black.ambientColor=black.diffuseColor=glm::vec3(0.f);auto bn=policy(85,0,black);assert(glm::length(bn.ambientColor)+glm::length(bn.diffuseColor)==0);
 for(bool indoors:{false,true}) {
  auto unchanged=original;applyOutdoorLightingPolicy(85,0,indoors,!indoors,true,true,unchanged);assert(distance(original,unchanged)==0);
 }
 assert(resolveZoneVisualTimeHours(10,false,12.f)==12.f);
 auto a=original,b=tir;auto whole=blendLighting(a,b,lightingBlendFactor(.5f));auto split=a;
 for(int i=0;i<30;++i) split=blendLighting(split,b,lightingBlendFactor(1.f/60.f));
 assert(distance(whole,split)<.00001f);
 // Real manager path: data-bearing volume sampled at clock time, policy then
 // smoothing. Include both entry and exit, fallback, midnight and interiors.
 { auto m=manager(true);m.lightParamsProfiles_.clear();m.update({0,0,0},0,85,12,false,false,.1f);assert(m.getLightingParams().fogEnd<=460.01f);m.setTimeOfDay(-.25f);m.update({0,0,0},0,12);assert(m.getVisualTimeOfDayHours()==18.f); }
 // Two overlapping volumes keep their spatial palette variation under the
 // policy. Check both directions through the ordering boundary.
 { auto m=manager(true);auto p=m.lightParamsProfiles_[1];p.lightParamsId=2;
   for(auto& b:p.colorBands) b.colors[0]=b.colors[1]=glm::vec3(.25f,.65f,.40f);
   m.lightParamsProfiles_[2]=p;auto v=m.lightVolumesByMap_[0][0];v.lightId=2;v.lightParamsId=2;v.position.x=180;m.lightVolumesByMap_[0].push_back(v);
   LightingParams left,right;
   for(float x:{89.99f,90.01f}) {m.firstLightingSample_=true;m.update({x,0,0},0,85,12,false,false,.1f);if(x<90)left=m.getLightingParams();else right=m.getLightingParams();}
   assert(distance(left,right)<.001f);
   m.firstLightingSample_=true;m.update({20,0,0},0,85,12,false,false,.1f);left=m.getLightingParams();
   m.firstLightingSample_=true;m.update({160,0,0},0,85,12,false,false,.1f);right=m.getLightingParams();assert(distance(left,right)>.01f);
 }
 for(bool hasVolumes:{false,true}) {
  auto m=manager(hasVolumes);m.update({0,0,0},0,12,12,false,false,.1f);const auto daylight=m.getLightingParams();
  m.update({0,0,0},0,85,12,false,false,1.f/60.f);auto first=m.getLightingParams();assert(distance(first,daylight)<distance(policy(85,12,daylight),daylight));
  for(int i=0;i<180;++i)m.update({0,0,0},0,85,12,false,false,1.f/60.f);
  auto dark=m.getLightingParams();assert(luminance(dark.diffuseColor)<.30001f);assert(m.getVisualTimeOfDayHours()==12.f);
  m.update({0,0,0},0,12,12,false,false,1.f/60.f);assert(distance(m.getLightingParams(),dark)<distance(daylight,dark));
  for(int i=0;i<180;++i)m.update({0,0,0},0,12,12,false,false,1.f/60.f);
  assert(distance(m.getLightingParams(),daylight)<.0001f);
  m.firstLightingSample_=true;m.update({0,0,0},0,12,23.999f,false,false,.1f);auto before=m.getLightingParams();
  m.firstLightingSample_=true;m.update({0,0,0},0,12,.001f,false,false,.1f);assert(distance(before,m.getLightingParams())<.005f);
  m.setIndoors(true);m.firstLightingSample_=true;m.update({0,0,0},0,85,0,false,false,.1f);assert(luminance(m.getLightingParams().ambientColor)>.5f);
  if(hasVolumes) { assert(!m.activeVolumes_.empty()); m.setIndoors(false);m.firstLightingSample_=true;m.update({0,0,0},0,85,12,false,false,.1f);assert(std::abs(m.getLightingParams().fogEnd-12000.f/36.f)<.01f); }
 }
 printf("PASS zone policy: neutral noon, dry sunset, cool bounded Tirisfal, 8 zone nights, midnight/dawn/dusk continuity, palette/black preservation, indoor/underwater exclusion, timestep invariance; production manager synthetic DBC-profile volume and fallback, zone entry/exit, midnight, indoor and authored fog preservation.\n");
}
