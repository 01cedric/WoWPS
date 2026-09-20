#include "rendering/celestial_lighting.hpp"
#include "rendering/zone_ambience.hpp"
#include "rendering/lighting_manager.hpp"
#include <cassert>
#include <cstdio>
using namespace wowee::rendering;
float luma(glm::vec3 c){return glm::dot(c,glm::vec3(.2126f,.7152f,.0722f));}
int main(){
 unsigned checks=0;
 for(glm::vec3 c:{glm::vec3(0),glm::vec3(.00001f),glm::vec3(.12f,.17f,.21f),glm::vec3(.8f,.6f,.2f),glm::vec3(.02f,.8f,.1f)}){
  glm::vec3 previous=celestialKeyColor(c,sunTravelDirection(0));
  for(int tick=0;tick<=86400;++tick){auto ray=sunTravelDirection(float(tick)/86400.f);auto out=celestialKeyColor(c,ray);
   assert(std::abs(luma(out)-luma(c))<1.e-6f);assert(glm::all(glm::greaterThanEqual(out,glm::vec3(0))));
   assert(glm::length(out-previous)<.003f);previous=out;++checks;
  }
  assert(glm::length(celestialKeyColor(c,sunTravelDirection(0))-celestialKeyColor(c,sunTravelDirection(1)))<1.e-6f);
 }
 for(float h:{0.f,3.f,12.f,15.f,21.f}){
  auto c=celestialKeyColor(glm::vec3(.2f),sunTravelDirection(h/24.f));
  if(h>=6&&h<=18)assert(c.r>c.g&&c.g>c.b);else assert(c.b>c.g&&c.g>c.r);
  auto cool=celestialKeyColor(glm::vec3(.2f),sunTravelDirection(h/24.f),true);assert(cool.b>cool.g&&cool.g>cool.r);
 }
 for(bool indoors:{false,true})for(float hour:{0.f,12.f})for(unsigned zone:{12u,85u,130u,10u}){
  LightingParams p;p.diffuseColor=glm::vec3(.15f);p.sunColor=glm::vec3(.15f);auto ambient=p.ambientColor;auto fog=p.fogColor;
  applyOutdoorLightingPolicy(zone,hour,indoors,false,false,true,p);
  if(hour==0||zone!=12)assert(p.diffuseColor.b>p.diffuseColor.r);else assert(p.diffuseColor.r>p.diffuseColor.b);
  if(indoors){assert(p.ambientColor==ambient);assert(p.fogColor==fog);assert(std::abs(luma(p.diffuseColor)-.15f)<1.e-6f);}
  p.diffuseColor=p.sunColor=glm::vec3(0);applyOutdoorLightingPolicy(zone,hour,indoors,false,false,true,p);assert(p.diffuseColor==glm::vec3(0));assert(p.sunColor==glm::vec3(0));
 }
 printf("PASS %u second-by-second palette/luminance/continuity checks; warm day, cool night, gloomy zones, indoor ambient/fog unchanged, zero remains zero\n",checks);
}
