#include "ui/unit_portrait.hpp"
#include "network/packet.hpp"
#include <glm/glm.hpp>
#include <array>
#include <functional>
#include <map>
#include <vector>
#include <stdexcept>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <new>
static void require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
namespace wowee::pipeline {
struct TestAttachment{int id;glm::vec3 position;};
struct TestCamera{int type=0;glm::vec3 positionBase{1,0,2},targetBase{0,0,1.5f},positions{0},targets{0};float fov=.6f;};
struct M2Model{std::vector<TestAttachment> attachments;std::vector<TestCamera> cameras;std::vector<int> globalSequenceDurations;};
}
namespace wowee::rendering {
namespace glue {inline glm::vec3 samplePosition(glm::vec3 v,int,int,int,const std::vector<int>&){return v;}}
inline bool isFiniteVec3(glm::vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
struct CharacterPreview {
 int releases=0; bool loaded=true;void releaseCharacterModel(){++releases;loaded=false;}
 bool hasAuthoredPortrait_=false;float modelBoundMinZ_=0,modelBoundMaxZ_=2,portraitFocusZ_=0,portraitDistance_=0,portraitFov_=0;
 glm::vec3 portraitCameraPosition_{0},portraitCameraTarget_{0};
 void readPortraitFraming(const pipeline::M2Model& model);
};
#define LOG_INFO(...) ((void)0)
#include "portrait_reader_source.inc"
}
#define LOG_WARNING(...) ((void)0)
namespace wowee::ui {
#include "portrait_rebuild_source.inc"
}
using namespace wowee;
namespace {
enum class RuneType {Blood,Unholy,Frost,Death};
struct Rune {RuneType type=RuneType::Blood;bool ready=true;float readyFraction=1;bool operator==(const Rune&)const=default;};
enum class Opcode {SMSG_CONVERT_RUNE,SMSG_RESYNC_RUNES};
struct Handler {
 std::array<Rune,6> playerRunes_{};
 std::map<Opcode,std::function<void(network::Packet&)>> dispatchTable_;
 std::vector<std::array<Rune,6>> events;
 void fireRuneUpdate(uint32_t){events.push_back(playerRunes_);}
 void fireAddonEvent(const std::string&,const std::vector<std::string>&){events.push_back(playerRunes_);}
 void install(){
#include "rune_handlers_source.inc"
 }
};
network::Packet sync(uint32_t count,const std::vector<uint8_t>& bytes){network::Packet p;p.writeUInt32(count);for(auto b:bytes)p.writeUInt8(b);return p;}
}
int main(){
 ui::PortraitModel model;model.boundMaxZ=2.6f;model.headZ=1.5f;model.headForward=.3f;
 auto f=ui::portraitFraming(model);require(f.source==ui::PortraitFraming::Source::HeadAttachment && f.focusZ>1.5f && f.focusZ<2.f,"authored head framing retained");
 model.hasPortraitCamera=true;model.cameraTargetZ=1.65f;model.cameraDistance=1.2f;model.cameraFovRadians=.6f;
 f=ui::portraitFraming(model);require(f.source==ui::PortraitFraming::Source::PortraitCamera && f.distance==1.2f,"valid camera retained");
 for(float invalid:{0.f,-1.f,3.14159265f,6.4f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
  model.cameraFovRadians=invalid;f=ui::portraitFraming(model);
  require(f.source!=ui::PortraitFraming::Source::PortraitCamera && std::isfinite(f.fovDegrees) && f.fovDegrees>0 && f.fovDegrees<180,"invalid projection falls back");
 }
 std::puts("PASS portrait camera: valid authored framing retained, invalid FOV falls back");
 model.hasPortraitCamera=false;
 for(float invalid:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
  model.boundMinZ=invalid;model.boundMaxZ=invalid;model.headForward=invalid;
  f=ui::portraitFraming(model);require(std::isfinite(f.focusZ)&&std::isfinite(f.distance)&&f.distance>0,"invalid bounds produce finite camera");
  model.boundMinZ=0;model.boundMaxZ=2.6f;
  f=ui::portraitFraming(model);require(std::isfinite(f.distance)&&f.distance>0,"invalid head offset ignored");
 }
 std::puts("PASS portrait bounds: invalid extent and head offset cannot poison projection");
 pipeline::M2Model m;pipeline::TestCamera cam;
 cam.positions.x=std::numeric_limits<float>::infinity();m.cameras.push_back(cam);
 rendering::CharacterPreview reader;reader.readPortraitFraming(m);
 require(!reader.hasAuthoredPortrait_ && std::isfinite(reader.portraitDistance_),"sampled infinite camera falls back");
 m.cameras.push_back(pipeline::TestCamera{});reader.readPortraitFraming(m);
 require(reader.hasAuthoredPortrait_ && rendering::isFiniteVec3(reader.portraitCameraPosition_),"later valid authored camera selected");
 m.cameras.resize(1);m.cameras[0].positions={0,0,0};m.cameras[0].targetBase=m.cameras[0].positionBase;
 reader.readPortraitFraming(m);require(!reader.hasAuthoredPortrait_,"coincident eye and target rejected");
 std::puts("PASS portrait reader: sampled nonfinite/degenerate camera rejected, later valid camera selected");
 rendering::CharacterPreview preview;
 require(ui::rebuildPortrait(preview,[]{return true;}) && preview.releases==0,"successful rebuild retained");
 require(!ui::rebuildPortrait(preview,[]{return false;}) && preview.releases==1 && !preview.loaded,"ordinary failure releases partial model");
 require(!ui::rebuildPortrait(preview,[]()->bool{throw std::bad_alloc();}) && preview.releases==2,"allocation failure releases partial model");
 bool propagated=false;try{ui::rebuildPortrait(preview,[]()->bool{throw std::runtime_error("unexpected");});}catch(const std::runtime_error&){propagated=true;}
 require(propagated,"unrelated exceptions not hidden");
 std::puts("PASS portrait rebuild: success, normal failure, allocation rollback and unexpected exception");
 Handler h;h.install();const auto initial=h.playerRunes_;
 for(auto p:{sync(6,{0,0,1,128}),sync(7,{}),sync(0xffffffff,{}),sync(2,{0,0,4,255})}){
  h.dispatchTable_[Opcode::SMSG_RESYNC_RUNES](p);
  require(h.playerRunes_==initial && h.events.empty(),"invalid rune packet makes no partial mutation/event");
 }
 auto p=sync(6,{0,0,1,51,2,102,3,153,0,204,2,255});h.dispatchTable_[Opcode::SMSG_RESYNC_RUNES](p);
 require(h.events.size()==6 && !h.playerRunes_[0].ready && h.playerRunes_[5].ready,"six rune states committed");
 for(const auto& state:h.events)require(state==h.playerRunes_,"callbacks see entire committed state");
 std::puts("PASS rune resync: truncated/oversized/invalid-type packets inert, callbacks see atomic snapshot");
 h.events.clear();const auto before=h.playerRunes_;
 network::Packet bad;bad.writeUInt8(2);bad.writeUInt8(255);h.dispatchTable_[Opcode::SMSG_CONVERT_RUNE](bad);
 require(h.events.empty()&&h.playerRunes_==before,"invalid conversion not masked into death rune");
 network::Packet good;good.writeUInt8(2);good.writeUInt8(3);h.dispatchTable_[Opcode::SMSG_CONVERT_RUNE](good);
 require(h.events.size()==1&&h.playerRunes_[2].type==RuneType::Death,"valid death conversion preserved");
 std::puts("PASS rune conversion: rejects invalid type, preserves valid conversion notification");
}
