#!/usr/bin/env python3
"""Exercise checkpoint lifecycle and actual target-validation/landing-veto bodies."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
controller=(root/'src/rendering/camera_controller.cpp').read_text()
start=controller.index('std::optional<glm::vec3> CameraController::getValidatedRecoveryPosition() const {')
end=controller.index('\nvoid CameraController::teleportTo(',start)
validate=controller[start:end]
start=controller.index('bool CameraController::crossedConfirmedSupport(')
end=controller.index('std::optional<glm::vec3> CameraController::getValidatedRecoveryPosition()',start)
crossing=controller[start:end]
wmo=(root/'src/rendering/wmo_renderer.cpp').read_text()
start=wmo.index('bool WMORenderer::hasPotentialGroundBelow(')
end=wmo.index('\nfloat WMORenderer::raycastBoundingBoxes(',start)
below=wmo[start:end]
start=controller.index('        if (!landingBelow && getValidatedRecoveryPosition() && autoUnstuckCallback_) {')
end=controller.index('\n        }\n    }\n}',start)+len('\n        }')
callback=controller[start:end]
fixture=r'''
#include "rendering/ground_recovery.hpp"
#include <cassert>
#include <iostream>
#include <functional>
#define LOG_WARNING(...) ((void)0)
#include <limits>
#include <optional>
#include <vector>
using namespace wowee::rendering;
constexpr float MIN_WALKABLE_NORMAL_WMO=0.5f,MIN_WALKABLE_NORMAL_M2=0.5f;
struct WMORenderer {
 struct Instance{std::vector<std::pair<glm::vec3,glm::vec3>> worldGroupBounds;};
 std::vector<Instance> instances;
 std::optional<float> floor;float normal=1;
 std::optional<float> getFloorHeight(float,float,float,float* nz,float,float){*nz=normal;return floor;}
 bool hasPotentialGroundBelow(const glm::vec3&)const;
};
struct M2Renderer {std::optional<float> floor;float normal=1;
 std::optional<float> getFloorHeight(float,float,float,float* nz){*nz=normal;return floor;}};
struct TerrainManager {std::optional<float> floor;bool hole=false;
 bool isHoleAt(float,float){return hole;}
 std::optional<float> getHeightAt(float,float){return floor;}};
struct CameraController {
 GroundRecovery groundRecovery_;WMORenderer* wmoRenderer=nullptr;
 M2Renderer* m2Renderer=nullptr;TerrainManager* terrainManager=nullptr;
 std::optional<glm::vec3> getValidatedRecoveryPosition()const;
 bool crossedConfirmedSupport(const glm::vec3&,const glm::vec3&)const;
 bool autoUnstuckFired_=false;
 std::function<bool()> autoUnstuckCallback_;
 glm::vec3* followTarget=nullptr;
 void attempt(glm::vec3& targetPos,bool landingBelow){
  if(autoUnstuckFired_)return;
CALLBACK
 }
};
VALIDATE
CROSSING
BELOW
static void stable(GroundRecovery& p,glm::vec3 feet) {
 for(int i=0;i<12;++i)p.observe(feet,1.f/30.f,true,false,false);
 assert(p.hasPosition());
}
int main(){
 GroundRecovery p;const glm::vec3 floor(100,200,-50);
 for(int i=0;i<100;++i)p.observe(floor,.1f,true,false,false);
 assert(!p.hasPosition()); // no authoritative context
 p.setContext(0,true,false);
 p.observe(floor,.25f,true,false,false);assert(!p.hasPosition());
 p.observe(floor,.25f,false,true,false);assert(!p.hasPosition());
 stable(p,floor);
 // A brief missed floor/normal jump never causes a return.
 for(int i=0;i<10;++i)p.observe(floor-glm::vec3(0,0,15),1.f/30.f,false,true,false);
 assert(!p.due(floor-glm::vec3(0,0,15)));
 for(int i=0;i<50;++i)p.observe(floor-glm::vec3(0,0,15),1.f/30.f,false,true,false);
 assert(p.due(floor-glm::vec3(0,0,15)));
 assert(!p.due(floor-glm::vec3(0,0,5)));
 assert(p.position()==floor); // airborne floor sightings never overwrite safe spot
 p.setContext(1,true,false);assert(!p.hasPosition()); // map transition
 stable(p,floor);p.setContext(1,true,true);assert(!p.hasPosition()); // fresh ghost phase
 stable(p,floor);p.setContext(1,false,true);assert(!p.hasPosition()); // corpse/transport/taxi
 p.setContext(1,true,true);stable(p,floor);p.observe(floor,.1f,false,true,true);
 assert(!p.hasPosition()); // swim/fly/hover/knockback/cinematic
 stable(p,floor);p.reset();assert(!p.hasPosition()); // teleport/respawn
 stable(p,floor);p.observe(floor+glm::vec3(1000,0,0),.1f,false,true,false);
 assert(!p.hasPosition()); // relocation bypassing teleport helper
 p.observe(glm::vec3(std::numeric_limits<float>::quiet_NaN()),.1f,true,false,false);
 assert(!p.hasPosition());
 // Direct swept-plane proof is independent of another floor farther below.
 assert(crossedConfirmedGroundPlane(floor,floor-glm::vec3(0,0,.8f),floor,floor.z));
 assert(!crossedConfirmedGroundPlane(floor,floor-glm::vec3(0,0,.02f),floor,floor.z));
 assert(!crossedConfirmedGroundPlane(floor,floor-glm::vec3(0,0,.8f),floor,floor.z-.8f)); // stairs
 assert(!crossedConfirmedGroundPlane(floor-glm::vec3(0,0,2),floor-glm::vec3(0,0,3),floor,floor.z)); // already under roof
 assert(!crossedConfirmedGroundPlane(floor,floor+glm::vec3(20,0,-1),floor,floor.z)); // relocation
 // Actual target validation preserves an underground WMO level, never roof ADT.
 WMORenderer w;M2Renderer m;TerrainManager t;CameraController c;
 c.wmoRenderer=&w;c.m2Renderer=&m;c.terrainManager=&t;
 c.groundRecovery_.setContext(1,true,false);stable(c.groundRecovery_,floor);
 t.floor=-100;w.floor=-50;
 assert(c.crossedConfirmedSupport(floor,floor-glm::vec3(0,0,.8f))); // failed city floor with lower ADT
 w.floor.reset();assert(!c.crossedConfirmedSupport(floor,floor-glm::vec3(0,0,.8f))); // walked off ledge
 w.floor=-50.8f;assert(!c.crossedConfirmedSupport(floor,floor-glm::vec3(0,0,.8f))); // stood on lower stair
 w.floor=-50;t.floor=200;
 assert(c.getValidatedRecoveryPosition() && std::abs(c.getValidatedRecoveryPosition()->z+49.92f)<.001f);
 w.floor.reset();assert(!c.getValidatedRecoveryPosition()); // WMO unloaded: do not teleport into air
 m.floor=-50;assert(c.getValidatedRecoveryPosition());
 m.floor=-40;assert(!c.getValidatedRecoveryPosition()); // moving lift left saved world position
 m.floor.reset();t.floor=-50;t.hole=true;assert(!c.getValidatedRecoveryPosition());
 t.hole=false;assert(c.getValidatedRecoveryPosition());
 // Any lower dungeon group vetoes void classification regardless of draw/focus.
 w.instances.push_back({{{glm::vec3(99,199,-200),glm::vec3(101,201,-190)}}});
 assert(w.hasPotentialGroundBelow(glm::vec3(100,200,-70)));
 assert(!w.hasPotentialGroundBelow(glm::vec3(100,200,-210)));
 assert(!w.hasPotentialGroundBelow(glm::vec3(110,200,-70)));
 w.instances[0].worldGroupBounds[0].first.x=std::numeric_limits<float>::quiet_NaN();
 assert(w.hasPotentialGroundBelow(glm::vec3(110,200,-70)));
 w.instances[0].worldGroupBounds[0]={glm::vec3(10),glm::vec3(-10)};
 assert(w.hasPotentialGroundBelow(glm::vec3(110,200,-70)));
 // Compile the actual callback-dispatch block: a declined authority correction
 // retries, successful correction latches, and a lower floor never dispatches.
 glm::vec3 target(100,200,-80),follow=target;c.followTarget=&follow;
 unsigned attempts=0;
 c.autoUnstuckCallback_=[&](){++attempts;return false;};
 c.attempt(target,true);assert(attempts==0);
 c.attempt(target,false);assert(attempts==1&&!c.autoUnstuckFired_);
 c.attempt(target,false);assert(attempts==2&&!c.autoUnstuckFired_);
 c.autoUnstuckCallback_=[&](){++attempts;follow=*c.getValidatedRecoveryPosition();return true;};
 c.attempt(target,false);assert(attempts==3&&c.autoUnstuckFired_&&target==follow);
 c.attempt(target,false);assert(attempts==3);
 std::cout<<"PASS ground recovery: stable support, airborne exclusion, maps/ghosts/transports/teleports, underground level, unload/lift/hole vetoes, lower dungeon floors, invalid bounds, failed callback retry/success latch, swept city floor with lower ADT, ledge/stair exclusions\n";
}
'''.replace('VALIDATE',validate).replace('BELOW',below).replace('CALLBACK',callback).replace('CROSSING',crossing)
with tempfile.TemporaryDirectory(prefix='wowps-ground-recovery-') as tmp:
 cpp=Path(tmp)/'test.cpp';exe=Path(tmp)/'test';cpp.write_text(fixture)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2','-I'+str(root/'include'),'-I'+str(root/'extern/glm'),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
