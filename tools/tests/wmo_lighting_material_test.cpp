#define main previousRegressionMain
#include "wmo_lighting_interior_test.cpp"
#undef main
#include "wmo_material.generated.hpp"
#include "rendering/shadow_caster_bounds.hpp"
#include <glm/gtc/matrix_transform.hpp>
int main() {
 previousRegressionMain();
 using rendering::shadowIntersectsWorldBounds;
 // Low sun: world X is light depth, Y/Z span the shadow footprint.
 glm::mat4 light(0.0f); light[1][0]=.01f; light[2][1]=.01f;
 light[0][2]=.0005f; light[3][2]=.5f; light[3][3]=1;
 require(shadowIntersectsWorldBounds(light,vec3(-510,-5,0),vec3(-490,5,20)), "upstream caster outside old180 sphere still reaches map");
 require(!shadowIntersectsWorldBounds(light,vec3(-510,200,0),vec3(-490,220,20)), "outside light footprint excluded");
 require(!shadowIntersectsWorldBounds(light,vec3(2100,-5,0),vec3(2120,5,20)), "beyond far light depth excluded");
 require(!shadowIntersectsWorldBounds(light,vec3(-2120,-5,0),vec3(-2100,5,20)), "before near light depth excluded");
 require(shadowIntersectsWorldBounds(light,vec3(-5,99,0),vec3(5,101,20)), "straddling light edge retained");
 require(shadowIntersectsWorldBounds(light,vec3(-1500,-200,-200),vec3(1500,200,200)), "large enclosing building retained");
 // Compare the exact projected AABB with all eight corners under rotations.
 for(int i=0;i<180;++i) {
  const glm::mat4 matrix=light*glm::rotate(glm::mat4(1),float(i)*.07f,glm::normalize(vec3(1,2,3)));
  const vec3 low(float(i-90)*4,float(i%13)*17-100,-3), high=low+vec3(20,30,50);
  vec3 lo(1e10f),hi(-1e10f);
  for(int c=0;c<8;++c) {
   const vec3 corner(c&1?high.x:low.x,c&2?high.y:low.y,c&4?high.z:low.z);
   const vec3 projected=vec3(matrix*glm::vec4(corner,1));
   lo=glm::min(lo,projected);hi=glm::max(hi,projected);
  }
  const bool expected=!(hi.x < -1 || lo.x > 1 || hi.y < -1 || lo.y > 1 || hi.z < 0 || lo.z > 1);
  require(shadowIntersectsWorldBounds(matrix,low,high)==expected,"center/extents equals8 transformed corners");
 }
 std::puts("PASS: bounded WMO light-volume culling: upstream low-sun caster, edge/depth exclusions,180 eight-corner equivalence cases");
 static_assert(sizeof(WMOMaterialUBO)==80 && offsetof(WMOMaterialUBO,unfogged)==68);
 BatchKey ordinary{}, glossy{}, unfogged{};
 glossy.specular=true; unfogged.unfogged=true;
 require(!(ordinary==glossy) && !(ordinary==unfogged), "merge key preserves specular and unfogged material distinctions");
 require(ordinary==BatchKey{}, "identical diffuse material remains batchable");
 using rendering::wmoMaterialSpecularIntensity;
 require(wmoMaterialSpecularIntensity(0)==0, "ordinary diffuse walls have no invented gloss");
 require(wmoMaterialSpecularIntensity(1)==.5f && wmoMaterialSpecularIntensity(2)==.5f, "authored specular and metal keep existing approximation");
 for(unsigned shader:{3u,4u,5u,6u,255u}) require(wmoMaterialSpecularIntensity(shader)==0, "unimplemented material variants never inherit wall gloss");
 const vec3 n(0,0,1), front(0,0,1), back(0,0,-1);
 require(directionalSpecular(n,front,front,.5f,32.f)==.5f,"front highlight retained");
 require(directionalSpecular(n,back,front,.5f,32.f)==0,"back light no highlight");
 require(directionalSpecular(n,front,back,.5f,32.f)==0,"back eye/opposed half vector finite zero");
 require(directionalSpecular(n,vec3(1,0,0),front,.5f,32.f)==0,"tangent light no highlight");
 require(directionalSpecular(n,front,front,0,32.f)==0,"diffuse material zero specular");
 require(outdoorUnlitLighting(vec3(1),vec3(0))==vec3(0),"unlit exterior is not emission");
 require(outdoorUnlitLighting(vec3(1),vec3(.2f))==vec3(.2f),"unlit uses only regional ambient");
 for(int count:{0,1,2,4}) {
  pipeline::WMOModel m{};m.groups.resize(1);
  require(pipeline::WMOLoader::loadGroup(make(8,count),m,0),"partial/oversized fixture parsed");
  for(const auto& v:m.groups[0].vertices) require(v.color==glm::vec4(1),"invalid MOCV cannot partially tint geometry");
 }
 std::puts("PASS: G2 WMO material selection, directional highlight positive/negative, unlit ambient and malformed MOCV fallback");
}
