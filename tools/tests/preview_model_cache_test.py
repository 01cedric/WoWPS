#!/usr/bin/env python3
"""Exercise production preview preparation/cache with a counted asset/parser seam.
This measures avoided reads/decodes and ownership, not GPU or PS4 timings.
"""
from pathlib import Path
import os, subprocess, tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'src/pipeline/m2_asset_loader.cpp').read_text()
body=s[s.index('std::shared_ptr<const M2Model> loadCharacterPreviewModel('):s.index('\nbool loadM2WithSkin(')]
c=(r/'src/pipeline/char_sections.cpp').read_text()
textures=c[c.index('void applyCharacterTextures('):c.index('\n}  // namespace pipeline',c.index('void applyCharacterTextures('))]
fixture=r'''
#include "pipeline/preview_model_cache.hpp"
#include <cassert>
#include <cstdio>
#include <functional>
#include <type_traits>
#define LOG_INFO(...) ((void)0)
namespace wowee::pipeline {
struct CharacterSectionTextures {
 std::string bodySkin,skinExtra,faceLower,faceUpper,hair;
 std::vector<std::string> underwear;
};
struct M2Model {
 struct Texture { unsigned type;std::string filename; };
 std::vector<Texture> textures{{1,""},{6,""},{8,"Ohren"}};
 std::vector<float> vertices=std::vector<float>(65536);
 std::string name;unsigned version=264;bool valid=true;
 bool isValid()const{return valid;}
};
struct AssetManager {
 PreviewModelCache cache;unsigned reads=0;bool missing=false;std::function<void()> duringRead;
 PreviewModelCache& previewModels(){return cache;}
 std::vector<uint8_t> readFile(const std::string&) {
  ++reads;if(duringRead){auto f=std::move(duringRead);duringRead={};f();}
  return missing?std::vector<uint8_t>{}:std::vector<uint8_t>{1};
 }
};
unsigned decodes=0,skinLoads=0,animLoads=0;
struct M2Loader {
 static M2Model load(const std::vector<uint8_t>&){++decodes;return {};}
 static void loadSkin(const std::vector<uint8_t>&,M2Model&){++skinLoads;}
};
std::string skinPathForM2(const std::string& p){return p+"00.skin";}
void loadExternalAnimations(AssetManager&,const std::string&,const std::vector<uint8_t>&,M2Model&,std::initializer_list<uint32_t> wanted){
 assert(wanted.size()==1 && *wanted.begin()==0);++animLoads;
}
TEXTURES
LOADER
}
using namespace wowee::pipeline;
int main(){
 AssetManager a;CharacterSectionTextures t;t.bodySkin="skin";t.hair="hair";
 auto face=loadCharacterPreviewModel(a,"human.m2",t,"Human");
 const auto reads=a.reads;
 auto doll=loadCharacterPreviewModel(a,"human.m2",t,"Human");
 static_assert(std::is_const_v<std::remove_reference_t<decltype(*face)>>);
 assert(face==doll && face->vertices.data()==doll->vertices.data());
 assert(a.reads==reads && decodes==1 && skinLoads==1 && animLoads==1);
 std::puts("PASS face/paperdoll share immutable vertex storage; second load performs zero reads/decodes/animation loads");
 auto other=t;other.bodySkin="other skin";
 auto skin=loadCharacterPreviewModel(a,"human.m2",other,"Human");
 assert(skin!=face && skin->textures[0].filename=="other skin" && face->textures[0].filename=="skin");
 other=t;other.hair="other hair";assert(loadCharacterPreviewModel(a,"human.m2",other,"Human")!=face);
 other=t;other.skinExtra="ears";assert(loadCharacterPreviewModel(a,"human.m2",other,"Human")->textures[2].filename=="ears");
 other=t;other.underwear={"fallback"};assert(loadCharacterPreviewModel(a,"human.m2",other,"Human")->textures[2].filename=="fallback");
 other=t;other.faceLower="face2";assert(loadCharacterPreviewModel(a,"human.m2",other,"Human")!=face);
 assert(loadCharacterPreviewModel(a,"orc.m2",t,"Orc")!=face);
 std::puts("PASS model/skin/hair/face/skin-extra/underwear identity cannot alias another appearance");
 std::weak_ptr<const M2Model> weak=face;face.reset();doll.reset();assert(weak.expired());
 const auto before=decodes;face=loadCharacterPreviewModel(a,"human.m2",t,"Human");assert(decodes==before+1);
 a.cache.clear();doll=loadCharacterPreviewModel(a,"human.m2",t,"Human");assert(face!=doll && face->isValid());
 std::puts("PASS last owner frees model; invalidation reloads while existing owners remain valid");
 AssetManager b;b.duringRead=[&]{b.cache.clear();};
 auto stale=loadCharacterPreviewModel(b,"human.m2",t,"Human");
 auto fresh=loadCharacterPreviewModel(b,"human.m2",t,"Human");assert(stale!=fresh);
 b.missing=true;assert(!loadCharacterPreviewModel(b,"missing.m2",t,"Human"));b.missing=false;
 assert(loadCharacterPreviewModel(b,"missing.m2",t,"Human"));
 std::puts("PASS source invalidation during preparation cannot publish stale entries; missing assets can retry");
 PreviewModelCache bounded;std::vector<std::shared_ptr<const M2Model>> owners;
 for(int i=0;i<65;++i){uint64_t gen;auto key=PreviewModelCache::Key{std::to_string(i)};bounded.find(key,gen);owners.push_back(std::make_shared<M2Model>());bounded.remember(key,gen,owners.back());}
 uint64_t gen;assert(!bounded.find({"64"},gen));assert(bounded.find({"0"},gen));
 owners[0].reset();bounded.remember({"64"},gen,owners.back());assert(bounded.find({"64"},gen));
 std::puts("PASS 64-entry metadata bound and expired-entry reclamation");
}
'''.replace('TEXTURES',textures).replace('LOADER',body)
with tempfile.TemporaryDirectory(prefix='wowps-preview-test-') as d:
 p=Path(d);(p/'test.cpp').write_text(fixture)
 subprocess.run([os.environ.get('CXX','g++'),'-std=c++20','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(r/'include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
 subprocess.run([str(p/'test')],check=True,env=env)
