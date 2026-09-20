#!/usr/bin/env python3
"""Exercise actual orphan-mask cleanup with live aliases and two frame fences."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'src/rendering/terrain_renderer.cpp').read_text()
start=source.index('void TerrainRenderer::cleanupUnusedAlphaTextures() {')
end=source.index('\nvoid TerrainRenderer::clear()',start)
body=source[start:end]
fixture=r'''
#include "rendering/terrain_alpha_cache.hpp"
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
using namespace wowee::rendering;
std::set<unsigned> destroyed;
struct VkTexture {unsigned id; void destroy(int,int){assert(destroyed.insert(id).second);} };
struct Context {
 std::vector<std::function<void()>> deferred;
 unsigned fences=0;bool reject=false;bool pendingUpload=false,openBatch=false;
 void pollUploadBatches(){} // completion remains externally controlled
 bool uploadsIdle() const {return !pendingUpload && !openBatch;}
 int getDevice(){return 1;}int getAllocator(){return 2;}
 template<class F>void deferAfterAllFrameFences(F f){if(reject)throw std::bad_alloc();deferred.emplace_back(f);}
 void retireFrame(unsigned slot){fences|=1u<<slot;if(fences==3){for(auto& f:deferred)f();deferred.clear();fences=0;}}
};
struct Chunk{VkTexture* baseTexture=nullptr;VkTexture* layerTextures[3]={};VkTexture* alphaTextures[3]={};};
struct TerrainRenderer {
 struct TextureCacheEntry{std::unique_ptr<VkTexture> texture;size_t approxBytes=4096;};
 Context* vkCtx;
 std::vector<Chunk> chunks;
 std::unordered_map<std::string,TextureCacheEntry> textureCache;
 TerrainAlphaCache<VkTexture> alphaReuseCache_;
 size_t textureCacheBytes_=0;
 VkTexture* add(std::string key,unsigned id){
  auto t=std::make_unique<VkTexture>();t->id=id;auto* raw=t.get();
  textureCache.emplace(key,TextureCacheEntry{std::move(t),4096});textureCacheBytes_+=4096;return raw;
 }
 void cleanupUnusedAlphaTextures();
};
BODY
int main(){
 Context context;TerrainRenderer renderer{&context};
 auto* shared=renderer.add("__alpha_1",1);
 auto* failedPartial=renderer.add("__alpha_2",2);
 renderer.add("__alpha_3",3); // departed chunk still referenced by a submitted frame
 auto* preparedDiffuse=renderer.add("world/pending.blp",4);
 TerrainAlphaCache<VkTexture>::Mask liveMask{},deadMask{};deadMask[0]=1;
 assert(TerrainAlphaCache<VkTexture>::slot(liveMask)!=TerrainAlphaCache<VkTexture>::slot(deadMask));
 renderer.alphaReuseCache_.remember(liveMask,shared);renderer.alphaReuseCache_.remember(deadMask,failedPartial);
 renderer.chunks.resize(2);renderer.chunks[0].alphaTextures[0]=shared;renderer.chunks[1].alphaTextures[1]=shared;
 renderer.chunks.erase(renderer.chunks.begin()); // one tile leaves, neighbor still uses mask
 // A fresh upload is not protected by older frame fences. No ownership
 // or lookup entry may retire until the independent upload gate completes.
 context.pendingUpload=true;
 renderer.cleanupUnusedAlphaTextures();
 assert(renderer.textureCache.size()==4 && context.deferred.empty());
 assert(renderer.alphaReuseCache_.find(deadMask)==failedPartial);
 context.retireFrame(0);context.retireFrame(1);assert(destroyed.empty());
 context.pendingUpload=false;context.openBatch=true;
 renderer.cleanupUnusedAlphaTextures();assert(context.deferred.empty());
 context.openBatch=false;
 renderer.cleanupUnusedAlphaTextures();
 assert(renderer.textureCache.size()==2 && renderer.textureCacheBytes_==8192);
 assert(renderer.textureCache.at("world/pending.blp").texture.get()==preparedDiffuse);
 assert(renderer.alphaReuseCache_.find(liveMask)==shared);
 assert(renderer.alphaReuseCache_.find(deadMask)==nullptr);
 assert(destroyed.empty() && context.deferred.size()==1);
 context.retireFrame(0);assert(destroyed.empty());
 context.retireFrame(1);assert(destroyed==std::set<unsigned>({2,3}));
 // Queue rejection must retain GPU ownership, accounting and the lookup.
 auto* retry=renderer.add("__alpha_5",5);renderer.alphaReuseCache_.remember(deadMask,retry);
 context.reject=true;bool rejected=false;
 try{renderer.cleanupUnusedAlphaTextures();}catch(const std::bad_alloc&){rejected=true;}
 assert(rejected && renderer.textureCache.size()==3 && renderer.textureCacheBytes_==12288);
 assert(renderer.alphaReuseCache_.find(deadMask)==retry);
 context.reject=false;
 renderer.chunks.clear();renderer.cleanupUnusedAlphaTextures();
 assert(renderer.textureCache.size()==1 && renderer.textureCacheBytes_==4096);
 assert(renderer.alphaReuseCache_.find(liveMask)==nullptr);
 assert(renderer.alphaReuseCache_.find(deadMask)==nullptr);
 context.retireFrame(1);assert(destroyed.size()==2);
 context.retireFrame(0);assert(destroyed==std::set<unsigned>({1,2,3,5}));
 // No bound chunks is legal for cancelled partial finalization. Diffuse
 // preload ownership is deliberately independent and must survive cleanup.
 renderer.cleanupUnusedAlphaTextures();assert(renderer.textureCache.size()==1);
 assert(!destroyed.count(4));
 std::cout<<"PASS actual terrain alpha retirement: shared survivors, failed partial tile, aliases, upload completion, both fences, queue rollback, diffuse preload\n";
}
'''.replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='wowps-alpha-retire-') as tmp:
    cpp=Path(tmp)/'test.cpp';exe=Path(tmp)/'test';cpp.write_text(fixture)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2','-I'+str(root/'include'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
