#include "pipeline/wmo_loader.hpp"
#include "rendering/wmo_lighting.hpp"
#include "wmo_shader_math.generated.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace wowee;
void require(bool v,const char* m){if(!v){std::fprintf(stderr,"FAIL: %s\n",m);std::abort();}}
template<class T> void add(std::vector<uint8_t>& b,T v){const auto* p=reinterpret_cast<const uint8_t*>(&v);b.insert(b.end(),p,p+sizeof(T));}
void chunk(std::vector<uint8_t>& b,uint32_t tag,const std::vector<uint8_t>& body){add(b,tag);add(b,uint32_t(body.size()));b.insert(b.end(),body.begin(),body.end());}
std::vector<uint8_t> make(uint32_t flags,int colors,bool second=false){
 std::vector<uint8_t> body(68),out,vertices,indices,color;
 std::memcpy(body.data()+8,&flags,4);
 for(float x:{0.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f,0.f}) add(vertices,x);
 chunk(body,0x4d4f5654,vertices); // MOVT
 for(uint16_t i:{0,1,2}) add(indices,i);chunk(body,0x4d4f5649,indices);
 if(colors>=0){for(int i=0;i<colors;++i)add(color,uint32_t(0xff604020));chunk(body,0x4d4f4356,color);}
 if(second){color.clear();for(int i=0;i<3;++i)add(color,uint32_t(0xffffffff));chunk(body,0x4d4f4356,color);}
 chunk(out,0x4d4f4750,body);return out;
}
int main(){
 unsigned checks=0;
 require(bakedInteriorLighting(glm::vec3(0),glm::vec3(0))==glm::vec3(0),"authored black interior gets no invented ambient floor");
 require(bakedInteriorLighting(glm::vec3(.1f,.3f,.5f),glm::vec3(.2f))==glm::vec3(.2f,.3f,.5f),"authored MOCV and root ambient retained");
 require(outdoorGlassReflection(glm::vec3(0),glm::vec3(0))==glm::vec3(0),"non-emissive glass has no fixed daylight-blue at zero world light");
 const auto day=outdoorGlassReflection(glm::vec3(.4f),glm::vec3(.6f));
 const auto night=outdoorGlassReflection(glm::vec3(.04f),glm::vec3(.06f));
 require(glm::length(day*.1f-night)<.00001f,"glass follows world day/night intensity");
 for(uint32_t flags:{0u,4u,8u,0x40u,0x2000u,0x2004u,0x200cu,0x2044u,0x204cu})
 for(int colorCount:{-1,0,1,3}) {
  pipeline::WMOModel model{};model.groups.resize(1);
  require(pipeline::WMOLoader::loadGroup(make(flags,colorCount),model,0),"production parser accepts triangle");
  auto& g=model.groups[0];require(g.hasVertexColors==(colorCount==3),"actual complete first MOCV tracked");
  const bool baked=rendering::wmoUsesBakedInteriorLighting(g.flags,g.hasVertexColors);
  const bool expected=(flags==0x2000u||flags==0x2004u)&&colorCount==3;
  require(baked==expected,"exterior/exterior-lit overrides spatial indoor; absent colors use world light");
  if(colorCount<0) require(g.vertices[0].color==glm::vec4(1),"default white is retained as albedo multiplier only");
  ++checks;
 }
 pipeline::WMOModel m{};m.groups.resize(1);
 require(pipeline::WMOLoader::loadGroup(make(0x2004,3,true),m,0),"second MOCV input");
 require(m.groups[0].hasVertexColors,"first lighting channel remains valid");
 require(std::abs(m.groups[0].vertices[0].color.r-96.f/255)<.00001,"second texture-blend channel cannot replace baked RGB with white");
 require(pipeline::WMOLoader::loadGroup(make(0x2000,-1),m,0),"reload without vertex colors");
 require(!m.groups[0].hasVertexColors,"reload clears lighting metadata");
 std::printf("PASS: production WMO parser and lighting classification: %u combinations; absent/partial/complete MOCV, exterior flags, second-channel preservation, reload\n",checks);
 return 0;
}
