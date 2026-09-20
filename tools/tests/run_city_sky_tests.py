#!/usr/bin/env python3
"""Execute production sky orchestration with recording renderers (no GPU claim)."""
from pathlib import Path
import subprocess, tempfile, os
root=Path(__file__).resolve().parents[2]
s=(root/'src/rendering/sky_system.cpp').read_text()
methods=s[s.index('void SkySystem::render('):s.index('glm::vec3 SkySystem::getSunPosition')]
helper=(root/'include/rendering/sky_params_from_lighting.hpp').read_text()
policy=next(line.strip() for line in helper.splitlines() if 'params.terrestrialCelestials =' in line)
renderer=(root/'src/rendering/renderer.cpp').read_text()
for command in ('cmd','currentCmd'):
    authored=f'skyboxModelRenderer_->render({command}, perFrameSet, *camera);'
    atmosphere=f'skySystem->renderAtmosphere({command}, perFrameSet, *camera, skyParams);'
    a=renderer.index(authored); b=renderer.index(atmosphere,a)
    assert 0<b-a<220, 'Atmosphere must follow authored M2 before world geometry'
assert renderer.count('skySystem->renderAtmosphere(')==3
cpp=r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <algorithm>
namespace glm { template<class T>T min(T a,T b){return std::min(a,b);} struct vec3{}; }
using VkCommandBuffer=int; using VkDescriptorSet=int; struct Camera{};
std::vector<int> events;
struct Fake {int id; void setEnabled(bool){} void setDensity(float){} template<class...T>void render(T...){events.push_back(id);} };
struct SkyParams {bool useOriginalSkybox=false,originalSkyboxAllowsAtmosphere=false,terrestrialCelestials=false,skyboxHasStars=false; float timeOfDay=12,cloudDensity=0,fogDensity=0,gameTime=12,weatherIntensity=0;glm::vec3 directionalDir,sunColor;};
struct SkySystem {
 bool initialized_=true,debugSkyMode_=false,proceduralStarsEnabled_=false;
 Fake a{1},b{2},c{3},d{4},e{5};Fake *skybox_=&a,*starField_=&b,*celestial_=&c,*clouds_=&d,*lensFlare_=&e;
 glm::vec3 getSunPosition(const SkyParams&)const{return {};}
 void render(int,int,const Camera&,const SkyParams&);
 void renderAtmosphere(int,int,const Camera&,const SkyParams&);
};
'''+methods+r'''
int main(){
 Camera camera;
 for(uint32_t mapId:{0u,1u,571u,530u,33u,0xffffffffu}) for(bool original:{false,true}) for(bool flag:{false,true}) {
  SkySystem sky; SkyParams params; params.useOriginalSkybox=original;params.originalSkyboxAllowsAtmosphere=flag;
  POLICY
  events.clear();sky.render(0,0,camera,params);
  assert(std::find(events.begin(),events.end(),3)==events.end()); // no covered disc in underlay
  events.push_back(6); // authored sky M2
  sky.renderAtmosphere(0,0,camera,params);
  const bool terrestrial=mapId==0||mapId==1||mapId==571;
  const bool visible=!original||flag||terrestrial;
  auto disc=std::find(events.begin(),events.end(),3);
  assert((disc!=events.end())==visible);
  if(visible){assert(disc>std::find(events.begin(),events.end(),6));}
  bool flare=std::find(events.begin(),events.end(),5)!=events.end();assert(flare==(!original||flag));
  bool clouds=std::find(events.begin(),events.end(),4)!=events.end();assert(clouds==(!original||flag));
 }
}
'''.replace('POLICY',policy)
with tempfile.TemporaryDirectory() as td:
    p=Path(td);(p/'test.cpp').write_text(cpp)
    subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
print('PASS: 24 actual sky-policy cases; authored sky before celestial; clouds after celestial; 3 renderer call sites; ASan/UBSan (LSan disabled: ptrace runtime). Hardware visibility remains unverified.')
