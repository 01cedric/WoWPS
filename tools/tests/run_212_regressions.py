#!/usr/bin/env python3
"""Run extracted production paths with deterministic allocation/VideoOut faults."""
from pathlib import Path
import subprocess, tempfile, os, shlex
ROOT=Path(__file__).resolve().parents[2]
def extract(path,start,end):
 s=(ROOT/path).read_text();a=s.index(start);return s[a:s.index(end,a)]
def run(name,code):
 with tempfile.TemporaryDirectory() as d:
  p=Path(d);(p/'test.cpp').write_text(code)
  subprocess.run(shlex.split(os.environ.get('CXX','g++'))+['-std=c++20','-O0','-g','-I'+str(ROOT/'include'),'-I'+str(ROOT/'extern/glm'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
 print('PASS',name,flush=True)
common=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <memory>
#include <new>
#include <utility>
#define LOG_INFO(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
'''
allocator=r'''
static long failAfter=-1;
void* operator new(size_t n) {if(failAfter==0)throw std::bad_alloc();if(failAfter>0)--failAfter;if(auto p=malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{free(p);} void operator delete(void*p,size_t)noexcept{free(p);}
'''
flip=common+r'''
#define __ORBIS__ 1
#define VK_PS4_VIDEO_OUT_FLIP_TIMEOUT_US 3000000u
#define GNM_ERROR_INTERNAL_FAILURE -1
using OrbisKernelUseconds=uint32_t;using OrbisKernelEqueue=int;struct OrbisKernelEvent{};
struct OrbisVideoOutFlipStatus {int64_t flipArg;int32_t currentBuffer;};
struct Device{bool gnm_present_in_progress=true;};
struct VkPs4Swapchain{unsigned pending_flips[4]{},pending_flip_count=0,displayed_image=4,image_count=4;int64_t pending_flip_args[4]{};bool image_in_flight[4]{};void* image_fences[4]{};bool flip_submission_uncertain=true,last_present_confirmed=false;Device*device=nullptr;struct {int handle=1,flipqueue=1,last_error_code=0;}video_out;};
static uint64_t clockUs=0;static OrbisVideoOutFlipStatus status{-1,-1};static int statusError=0,waits=0;static bool advance=false;
uint64_t sceKernelGetProcessTime(){return clockUs;}
int sceVideoOutGetFlipStatus(int,OrbisVideoOutFlipStatus*out){*out=status;return statusError;}
int sceKernelWaitEqueue(int,OrbisKernelEvent*,int,int*out,OrbisKernelUseconds*t){clockUs+=*t;++waits;*out=0;if(advance)status={102,2};return -1;}
'''+extract('ps4/third_party/ps4_vulkan/source/vulkan-ps4/src/vk_ps4_swapchain.c','static void vk_ps4_retire_flip','/* Drain every outstanding flip.')+r'''
int main(){Device d;VkPs4Swapchain sc;sc.device=&d;sc.displayed_image=0;sc.pending_flip_count=2;sc.pending_flips[0]=1;sc.pending_flips[1]=2;sc.pending_flip_args[0]=101;sc.pending_flip_args[1]=102;std::fill_n(sc.image_in_flight,3,true);bool reaped=true;
assert(vk_ps4_video_out_reap_flip(&sc,false,&reaped)&&!reaped&&sc.pending_flip_count==2);
status={99,2};assert(vk_ps4_video_out_reap_flip(&sc,false,&reaped)&&!reaped); // same buffer, stale generation
status={102,2};assert(vk_ps4_video_out_reap_flip(&sc,false,&reaped)&&reaped);assert(sc.pending_flip_count==0&&!sc.image_in_flight[0]&&!sc.image_in_flight[1]&&sc.image_in_flight[2]&&sc.displayed_image==2&&!d.gnm_present_in_progress&&waits==0);
sc.pending_flip_count=1;sc.pending_flips[0]=1;sc.pending_flip_args[0]=103;status={-1,-1};clockUs=0;assert(!vk_ps4_video_out_reap_flip(&sc,true,&reaped)&&!reaped&&sc.pending_flip_count==1&&sc.image_in_flight[2]);assert(clockUs==3000000); // bounded, no buffer released
statusError=-7;assert(!vk_ps4_video_out_reap_flip(&sc,false,&reaped)&&sc.video_out.last_error_code==-7);statusError=0;
sc.pending_flips[0]=2;sc.pending_flip_args[0]=102;advance=true;clockUs=0;status={-1,-1};assert(vk_ps4_video_out_reap_flip(&sc,true,&reaped)&&reaped&&clockUs==1000);
}
'''
run('VideoOut coalesced/missing events, stale serial, timeout and ownership',flip)
instance=common+allocator+r'''
#include "rendering/spatial_grid.hpp"
using namespace wowee::rendering;
namespace core{struct Logger{static Logger&getInstance(){static Logger l;return l;}template<class...T>void error(T&&...){};};}
static void transformAABB(const glm::mat4&,glm::vec3 a,glm::vec3 b,glm::vec3&c,glm::vec3&d){c=a;d=b;}
struct Group{glm::vec3 boundingBoxMin{0},boundingBoxMax{80};};
struct ModelData{glm::vec3 boundingBoxMin{0},boundingBoxMax{80};std::vector<Group>groups;};
struct WMOInstance{uint32_t id,modelId;glm::vec3 position,rotation;float scale;glm::mat4 modelMatrix{1};glm::vec3 worldBoundsMin,worldBoundsMax;std::vector<std::pair<glm::vec3,glm::vec3>>worldGroupBounds;void updateModelMatrix(){}};
struct WMORenderer{uint32_t nextInstanceId=1;std::unordered_map<uint32_t,ModelData>loadedModels;std::vector<WMOInstance>instances;std::unordered_map<uint32_t,size_t>instanceIndexById;SpatialGrid spatialGrid;bool isModelLoaded(uint32_t i){return loadedModels.count(i);}uint32_t createInstance(uint32_t,const glm::vec3&,const glm::vec3&,float);};
'''+extract('src/rendering/wmo_renderer.cpp','uint32_t WMORenderer::createInstance(', '/// Recomputes an instance')+r'''
int main(){unsigned failures=0,successes=0;for(long allowance=0;allowance<45;++allowance){WMORenderer r;r.loadedModels[1].groups.resize(3);failAfter=allowance;bool failed=false;try{r.createInstance(1,{}, {},1);}catch(const std::bad_alloc&){failed=true;}failAfter=-1;if(failed){++failures;assert(r.instances.empty()&&r.instanceIndexById.empty());for(auto&[k,v]:r.spatialGrid)assert(v.empty());}else{++successes;assert(r.instances.size()==1&&r.instanceIndexById.size()==1);}auto id=r.createInstance(1,{}, {},1);assert(r.instanceIndexById.count(id));}assert(failures>10&&successes>0);}
'''
run('WMO instance creation fault sweep and exact retry ownership',instance)
quest=common+r'''
using FlatFieldMap=std::unordered_map<uint16_t,uint32_t>;enum class UF{PLAYER_QUEST_LOG_START};uint16_t fieldIndex(UF){return 100;}
bool isQuestSlotComplete(uint8_t,uint32_t v){return v&1;}
struct QuestLogEntry{uint32_t questId;bool complete=false;};
struct Packet{void writeUInt64(uint64_t){}};namespace network{using Packet=::Packet;}
enum class Opcode{CMSG_QUESTGIVER_STATUS_QUERY};int wireOpcode(Opcode){return 0;}
struct Sock{void send(Packet&){};};struct Parser{uint8_t questLogStride(){return 5;}};
struct Owner{Parser p;Sock s;FlatFieldMap fields;Parser*getPacketParsers(){return &p;}Sock*getSocket(){return &s;}};
struct QuestHandler{Owner owner_;std::vector<QuestLogEntry>questLog_;std::unordered_map<uint32_t,float>pendingQuestAcceptTimeouts_;std::unordered_map<uint32_t,uint64_t>pendingQuestAcceptNpcGuids_;int queries=0;bool hasQuestInLog(uint32_t q){return std::any_of(questLog_.begin(),questLog_.end(),[&](auto&e){return e.questId==q;});}void addQuestToLocalLogIfMissing(uint32_t q,const std::string&,const std::string&){questLog_.push_back({q});}void requestQuestQuery(uint32_t,bool){++queries;}void clearPendingQuestAccept(uint32_t q){pendingQuestAcceptTimeouts_.erase(q);}void applyPackedKillCountsFromFields(QuestLogEntry&){}void applyQuestStateFromFields(const FlatFieldMap&);};
'''
quest=quest.replace('struct Packet{','struct Packet{Packet(int=0){}')
quest+=extract('src/game/quest_handler.cpp','void QuestHandler::applyQuestStateFromFields(', 'void QuestHandler::applyPackedKillCountsFromFields(')+r'''
int main(){QuestHandler h;FlatFieldMap f{{100,7},{101,1}};h.applyQuestStateFromFields(f);assert(h.questLog_.size()==1&&h.questLog_[0].questId==7&&h.questLog_[0].complete&&h.queries==1);h.applyQuestStateFromFields(f);assert(h.questLog_.size()==1&&h.queries==1);h.applyQuestStateFromFields({{110,8},{111,0}});assert(h.hasQuestInLog(8)&&h.queries==2);}
'''
run('Server-confirmed quests restored without pending acceptance; idempotent updates',quest)
maptest=common+allocator+r'''
using VkDevice=int;enum{VK_FORMAT_R8G8B8A8_UNORM,VK_FILTER_LINEAR,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
struct VkContext{int getDevice(){return 1;}void finishInterruptedUploadBatch(){}};
struct VkTexture{static inline int live=0;VkTexture(){++live;}~VkTexture(){--live;}bool upload(VkContext&,const void*,int,int,int,bool){return true;}void createSampler(int,int,int,int,float){}};
namespace pipeline{struct AssetManager{struct Image{std::vector<unsigned char>data;int width=2,height=2;bool isValid(){return !data.empty();}};Image loadTexture(const std::string&){return {std::vector<unsigned char>(16)};}void trimFileCache(){}};}
struct Overlay{int tileCols=2,tileRows=1;std::string textureName="overlay";};struct Zone{std::string areaName="World";int areaID=0;std::vector<Overlay>overlays;};
struct CompositeRenderer{int compositedIdx_=-1;VkContext*vkCtx;pipeline::AssetManager*assetManager;std::chrono::steady_clock::time_point textureRetryAt_{};std::vector<std::unique_ptr<VkTexture>>zoneTextures;struct ZoneTextureSlots{VkTexture*tileTextures[12]{};bool tilesLoaded=false;struct OverlaySlots{std::vector<VkTexture*>tiles;bool tilesLoaded=false;};std::vector<OverlaySlots>overlays;};std::vector<ZoneTextureSlots>zoneTextureSlots_;void ensureTextureSlots(size_t,const std::vector<Zone>&);void loadZoneTextures(int,std::vector<Zone>&,const std::string&);void loadOverlayTextures(int,std::vector<Zone>&);};
'''+extract('src/rendering/world_map/composite_renderer.cpp','void CompositeRenderer::ensureTextureSlots(', 'bool CompositeRenderer::initialize(')+extract('src/rendering/world_map/composite_renderer.cpp','void CompositeRenderer::loadZoneTextures(', 'void CompositeRenderer::detachZoneTextures(')+r'''
int main(){for(long allowance=0;allowance<100;++allowance){VkContext v;pipeline::AssetManager a;std::vector<Zone>zones(2);zones[0].overlays.resize(1);zones[1].overlays.resize(2);CompositeRenderer r;r.vkCtx=&v;r.assetManager=&a;failAfter=allowance;r.loadZoneTextures(0,zones,"World");r.loadOverlayTextures(0,zones);failAfter=-1;for(auto&slots:r.zoneTextureSlots_)for(auto*tex:slots.tileTextures)if(tex)assert(std::any_of(r.zoneTextures.begin(),r.zoneTextures.end(),[&](auto&p){return p.get()==tex;}));r.textureRetryAt_={};r.loadZoneTextures(0,zones,"World");r.loadOverlayTextures(0,zones);assert(r.zoneTextures.size()==14&&r.zoneTextureSlots_[0].tilesLoaded&&r.zoneTextureSlots_[0].overlays[0].tilesLoaded);r.loadZoneTextures(0,zones,"World");assert(r.zoneTextures.size()==14);}assert(VkTexture::live==0);}
'''
run('World-map allocation fault sweep: partial uploads, pointer ownership and retry',maptest)
upload=common+r'''
namespace rendering{struct WMORenderer{enum class ModelLoadResult{InProgress,Complete,Failed};bool fail=true;void*bound=nullptr;void setPredecodedBLPCache(void*p){bound=p;}ModelLoadResult loadModelIncremental(int,uint32_t,float){if(fail)throw std::bad_alloc();return ModelLoadResult::Complete;}};}
struct Renderer{rendering::WMORenderer w;auto*getWMORenderer(){return &w;}};
struct EntitySpawner{struct Result{std::unique_ptr<int>wmoModel;int predecodedTextures=0;uint32_t displayId=7;std::string modelPath;};struct Pending{Result result;uint32_t modelId=1;};Renderer*renderer_;std::vector<Pending>pendingWmoUploads_;std::unordered_map<uint32_t,uint32_t>gameObjectDisplayIdWmoCache_;bool finishFail=true;unsigned finishes=0;void finishWmoSpawn(Result&,uint32_t){if(finishFail)throw std::bad_alloc();++finishes;}void processPendingWmoUploads();};
'''+extract('src/core/entity_spawner_processing.cpp','void EntitySpawner::processPendingWmoUploads()', 'void EntitySpawner::processGameObjectSpawnQueue()')+r'''
int main(){Renderer r;EntitySpawner s;s.renderer_=&r;s.pendingWmoUploads_.emplace_back();s.pendingWmoUploads_[0].result.wmoModel=std::make_unique<int>(1);try{s.processPendingWmoUploads();assert(false);}catch(const std::bad_alloc&){}assert(!r.w.bound&&s.pendingWmoUploads_.size()==1);r.w.fail=false;try{s.processPendingWmoUploads();assert(false);}catch(const std::bad_alloc&){}assert(!r.w.bound&&s.pendingWmoUploads_.size()==1);s.finishFail=false;s.processPendingWmoUploads();assert(!r.w.bound&&s.pendingWmoUploads_.empty()&&s.finishes==1);}
'''
run('Pending WMO decode binding clears on upload/finalization failures; job retained',upload)

# Execute the production automatic-continent predicate for synthetic and real IDs.
s=(ROOT/'src/rendering/world_map/world_map_facade.cpp').read_text()
a=s.index('if (zone.displayMapID != 0 &&');b=s.index(' {',a)
predicate=s[a+4:b-1]
run('World/Cosmic sentinel exclusion preserves real continent redirection',common+"struct Zone{uint32_t displayMapID;};struct Data{int currentMapId(){return 0;}};struct Context{Data data;};bool redirect(uint32_t id){Zone zone{id};Context d;return "+predicate+";}int main(){assert(!redirect(UINT32_MAX));assert(!redirect(UINT32_MAX-1));assert(!redirect(0));assert(redirect(1));assert(redirect(530));}")
