#!/usr/bin/env python3
"""Compile the actual M2 retirement body against upload/fence ownership mocks."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'src/rendering/m2_renderer_instance.cpp').read_text()
a=s.index('void M2Renderer::cleanupUnusedModels(')
b=s.index('\nstd::vector<uint32_t> M2Renderer::drainReapedModelIds()',a)
body=s[a:b]
fixture=r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#define WOWEE_PS4
#define LOG_INFO(...) ((void)0)
std::set<int> destroyed;
struct VkTexture {int id;~VkTexture(){destroyed.insert(id);}};
struct Context {bool pending=true,open=false;unsigned polls=0,fences=0;
 std::vector<std::function<void()>> callbacks;
 void pollUploadBatches(){++polls;}
 bool uploadsIdle()const{return !pending&&!open;}
 template<class F>void deferAfterAllFrameFences(F f){callbacks.push_back(f);}
 void signal(unsigned slot){fences|=1u<<slot;if(fences==3){for(auto& f:callbacks)f();callbacks.clear();fences=0;}}
};
struct M2Renderer {
 struct Batch {VkTexture* texture=nullptr;};
 struct M2ModelGPU {std::vector<Batch> batches;std::vector<VkTexture*> particleTextures,ribbonTextures;};
 struct Instance {uint32_t modelId;};
 struct TextureCacheEntry {std::unique_ptr<VkTexture> texture;size_t approxBytes=4096;};
 struct Key {VkTexture* texture;bool operator<(const Key& k) const {return std::less<VkTexture*>{}(texture,k.texture);}};
 Context* vkCtx_;std::vector<Instance> instances;
 std::unordered_map<uint32_t,M2ModelGPU> models;
 std::unordered_set<uint32_t> pinnedModelIds_;
 std::unordered_map<uint32_t,std::chrono::steady_clock::time_point> modelUnusedSince_;
 std::vector<uint32_t> reapedModelIds_;
 std::unordered_map<std::string,TextureCacheEntry> textureCache;
 std::unordered_map<VkTexture*,int> texturePropsByPtr_;
 std::map<Key,int> particleGroups_;
 size_t textureCacheBytes_=0;unsigned destroyedModels=0;
 void destroyModelGPU(M2ModelGPU&){++destroyedModels;}
 VkTexture* texture(int id){auto p=std::make_unique<VkTexture>();p->id=id;auto* raw=p.get();
  textureCache.emplace(std::to_string(id),TextureCacheEntry{std::move(p),4096});textureCacheBytes_+=4096;
  texturePropsByPtr_[raw]=1;particleGroups_[{raw}]=1;return raw;}
 void cleanupUnusedModels(const std::unordered_set<uint32_t>& preparing);
};
BODY
int main(){
 Context ctx;M2Renderer w{&ctx};
 const auto expired=std::chrono::steady_clock::now()-std::chrono::seconds(10);
 for(unsigned id=1;id<=7;++id){auto* t=w.texture(id);if(id==7)continue;
  M2Renderer::M2ModelGPU m;
  if(id==5)m.particleTextures.push_back(t);
  else if(id==6)m.ribbonTextures.push_back(t);
  else m.batches.push_back({t});
  w.models.emplace(id,std::move(m));w.modelUnusedSince_[id]=expired;}
 w.instances={{1},{5},{6}};w.pinnedModelIds_.insert(3);
 std::unordered_set<uint32_t> preparing{4};
 w.cleanupUnusedModels(preparing);
 assert(ctx.polls==1 && w.models.size()==6 && w.textureCache.size()==7);
 assert(ctx.callbacks.empty() && !w.destroyedModels && w.modelUnusedSince_.size()==6);
 ctx.pending=false;ctx.open=true;w.cleanupUnusedModels(preparing);
 assert(w.textureCache.size()==7 && ctx.callbacks.empty());
 ctx.open=false;w.cleanupUnusedModels(preparing);
 assert(w.models.size()==5 && !w.models.count(2));
 assert(w.reapedModelIds_==std::vector<uint32_t>({2}));
 assert(w.textureCache.size()==5 && w.textureCacheBytes_==5*4096);
 assert(w.texturePropsByPtr_.size()==5 && w.particleGroups_.size()==5);
 assert(w.modelUnusedSince_.empty());
 assert(!w.destroyedModels && destroyed.empty());
 ctx.signal(0);assert(!w.destroyedModels && destroyed.empty());ctx.signal(1);
 assert(w.destroyedModels==1 && destroyed==std::set<int>({2,7}));
 w.vkCtx_=nullptr;w.cleanupUnusedModels(preparing);
 std::cout<<"PASS actual M2 cleanup: pending/open uploads preserve owners; active/pinned/preparing/particle/ribbon owners retained; retired model and textures await both frame fences\n";
}
'''.replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='wowps-m2-retire-') as tmp:
 cpp=Path(tmp)/'test.cpp';exe=Path(tmp)/'test';cpp.write_text(fixture)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
