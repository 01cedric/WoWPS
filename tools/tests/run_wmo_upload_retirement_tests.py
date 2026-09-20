#!/usr/bin/env python3
"""Actual WMO retirement body: pending upload/open batch must keep all owners."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'src/rendering/wmo_renderer.cpp').read_text()
a=s.index('void WMORenderer::cleanupUnusedModels(')
b=s.index('\nuint32_t WMORenderer::createInstance(',a)
body=s[a:b]
fixture=r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#define LOG_INFO(...) ((void)0)
namespace core {struct Logger {static Logger& getInstance(){static Logger l;return l;}
 template<class...T>void info(T&&...){} };}
std::set<int> destroyed;
struct VkTexture {int id;~VkTexture(){destroyed.insert(id);}};
struct Context {bool pending=true,open=false;unsigned polls=0,fences=0;
 std::vector<std::function<void()>> callbacks;
 void pollUploadBatches(){++polls;}
 bool uploadsIdle()const{return !pending&&!open;}
 template<class F>void deferAfterAllFrameFences(F f){callbacks.push_back(f);}
 void signal(unsigned slot){fences|=1u<<slot;if(fences==3){for(auto& f:callbacks)f();callbacks.clear();fences=0;}}
};
struct WMORenderer {
 struct Batch {VkTexture* texture=nullptr;VkTexture* normalHeightMap=nullptr;};
 struct Group {std::vector<Batch> mergedBatches;};
 struct Model {bool terrainManaged=true;std::vector<Group> groups;std::vector<VkTexture*> textures;};
 struct Instance {uint32_t modelId;};
 struct TextureCacheEntry {std::unique_ptr<VkTexture> texture,normalHeightMap;size_t approxBytes=4096;};
 Context* vkCtx_;std::vector<Instance> instances;
 std::unordered_map<uint32_t,Model> loadedModels,loadingModels_;
 std::unordered_map<std::string,TextureCacheEntry> textureCache;
 size_t textureCacheBytes_=0;unsigned unloaded=0,destroyedGroups=0;
 void destroyGroupGPU(Group&,bool defer){assert(defer);++destroyedGroups;}
 void unloadModel(uint32_t id){++unloaded;loadedModels.erase(id);}
 VkTexture* texture(int id){auto p=std::make_unique<VkTexture>();p->id=id;auto* raw=p.get();
  textureCache.emplace(std::to_string(id),TextureCacheEntry{std::move(p),nullptr,4096});textureCacheBytes_+=4096;return raw;}
 void cleanupUnusedModels(const std::unordered_set<uint32_t>& pendingModelIds);
};
BODY
int main(){
 Context ctx;WMORenderer w{&ctx};
 for(unsigned id=1;id<=6;++id){auto* t=w.texture(id);WMORenderer::Model m;
  m.groups.push_back({{{t,nullptr}}});m.textures.push_back(t);
  if(id==3||id==6){m.terrainManaged=id==3;w.loadingModels_.emplace(id,std::move(m));}
  else if(id!=5)w.loadedModels.emplace(id,std::move(m));}
 w.instances.push_back({1});std::unordered_set<uint32_t> preparing{4};
 w.cleanupUnusedModels(preparing);
 assert(ctx.polls==1 && w.loadedModels.size()==3 && w.loadingModels_.size()==2);
 assert(w.textureCache.size()==6 && ctx.callbacks.empty() && !w.unloaded && !w.destroyedGroups);
 ctx.pending=false;ctx.open=true;w.cleanupUnusedModels(preparing);
 assert(w.textureCache.size()==6 && ctx.callbacks.empty());
 ctx.open=false;w.cleanupUnusedModels(preparing);
 assert(w.loadedModels.size()==2 && w.loadedModels.count(1)&&w.loadedModels.count(4));
 assert(w.loadingModels_.size()==1&&w.loadingModels_.count(6)); // transport preparation owner survives
 assert(w.textureCache.size()==3&&w.textureCacheBytes_==3*4096);
 assert(w.unloaded==1&&w.destroyedGroups==1&&destroyed.empty());
 ctx.signal(0);assert(destroyed.empty());ctx.signal(1);
 assert(destroyed==std::set<int>({2,3,5}));
 std::cout<<"PASS actual WMO cleanup: pending/open upload owners retained, active/preparing/transport textures retained, orphans await both frame fences\n";
}
'''.replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='wowps-wmo-retire-') as tmp:
 cpp=Path(tmp)/'test.cpp';exe=Path(tmp)/'test';cpp.write_text(fixture)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
