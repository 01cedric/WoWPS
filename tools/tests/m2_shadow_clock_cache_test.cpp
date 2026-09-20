#include "rendering/m2_shadow.hpp"
#include "rendering/m2_shadow_cpu.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <vector>
using namespace wowee::rendering;
struct Instance { int currentSequenceIndex=0; float animTime=0, globalSequenceTime=0; };
uint32_t bits(float f) {uint32_t b;std::memcpy(&b,&f,4);return b;}
float fromBits(uint32_t b) {float f;std::memcpy(&f,&b,4);return f;}
bool same(const M2UvTransform&a,const M2UvTransform&b) {
 for(unsigned i=0;i<4;++i)if(bits(a.linear[i])!=bits(b.linear[i]))return false;
 for(unsigned i=0;i<2;++i)if(bits(a.offset[i])!=bits(b.offset[i]))return false;
 return true;
}
struct Model {
 struct Batch {uint16_t textureAnimIndex=0;}; std::vector<Batch>batches{{0}};
 bool hasTextureAnimation=true,isLavaModel=false;
 std::vector<uint16_t>textureTransformLookup{0};
 std::vector<wowee::pipeline::M2TextureTransform>textureTransforms{1};
 std::vector<uint32_t>globalSequenceDurations{1000};
 Model(){auto&t=textureTransforms[0];t.translation.interpolationType=1;t.translation.sequences.resize(2);
 for(auto&s:t.translation.sequences)for(unsigned i=0;i<64;++i){s.timestamps.push_back(i*20);s.vec3Values.push_back({float(i),float(i%7),0});}
 t.scale.interpolationType=1;t.scale.sequences=t.translation.sequences;
 }
};
struct Batch {uint32_t materialBatch=0,maskMode=1;};
struct Run {size_t start,count;M2UvTransform uv;};
struct PreviousCache {
 bool valid=false;Instance last;M2UvTransform uv;unsigned evaluations=0;
 template<class F>M2UvTransform sample(const Instance&i,F f){
 if(valid&&last.currentSequenceIndex==i.currentSequenceIndex&&bits(last.animTime)==bits(i.animTime)&&bits(last.globalSequenceTime)==bits(i.globalSequenceTime))return uv;
 ++evaluations;uv=f();last=i;valid=true;return uv;
 }
};
volatile float consumed=0;
template<class Cache>double benchmark(const std::vector<Instance>&is,Model&m,unsigned&evals){
 const auto start=std::chrono::steady_clock::now();float total=0;evals=0;
 for(unsigned repeat=0;repeat<100;++repeat){Cache cache;for(const auto&i:is){auto uv=cache.sample(i,[&]{++evals;return sampleM2ShadowUv(m,Batch{},i,7.f);});total+=uv.offset.x;}}
 consumed=total;return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
int main(){
 // Key fidelity, including signed zero, adjacent float values, signed sequence,
 // and NaN payloads. Synthetic output avoids feeding nonfinite clocks into tracks.
 std::vector<Instance> special;
 for(int seq:{-1,0,1})for(uint32_t t:{0u,0x80000000u,1u,0x3f800000u,0x3f800001u,0x7fc00001u,0x7fc00002u})
 for(uint32_t g:{0u,0x80000000u,1u,0x7fc00003u})special.push_back({seq,fromBits(t),fromBits(g)});
 M2ShadowUvClockCache exact;
 auto derive=[](const Instance&i){M2UvTransform uv;uv.linear={i.animTime,i.globalSequenceTime,float(i.currentSequenceIndex),1};return uv;};
 for(unsigned r=0;r<20;++r)for(const auto&i:special)assert(same(exact.sample(i,[&]{return derive(i);}),derive(i)));
 // >64 unique inputs force replacement, then old keys are revisited.
 for(unsigned r=0;r<4;++r)for(unsigned n=0;n<2000;++n){Instance i{int(n%3),float(n%1000),float(n)};assert(same(exact.sample(i,[&]{return derive(i);}),derive(i)));}
 assert(exact.samples()+exact.reused()==special.size()*20+8000);
 // Every material/pass receives its own cache; different sampler context must
 // never inherit a previous material's values even for identical input clocks.
 for(unsigned context=0;context<10;++context){M2ShadowUvClockCache cache;Instance i;auto f=[&]{M2UvTransform uv;uv.offset.x=float(context);return uv;};assert(same(cache.sample(i,f),f()));assert(cache.samples()==1);}
 Model model;
 for(unsigned mode=0;mode<4;++mode){
 model.textureTransforms[0].translation.globalSequence=mode==1?0:-1;model.isLavaModel=mode==2;model.textureTransformLookup[0]=mode==3?100:0;
 std::mt19937 rng(272);std::vector<Instance>is;
 for(unsigned n=0;n<12000;++n){unsigned k=rng()%100;is.push_back({int(k%2),float(k*7),float(k*11)});}
 M2ShadowUvClockCache cache;std::vector<Run>ref,got;
 forEachM2ShadowUvRun(0,is.size(),true,[&](size_t n){return sampleM2ShadowUv(model,Batch{},is[n],7);},[&](size_t a,size_t b,const auto&uv){ref.push_back({a,b,uv});});
 forEachM2ShadowUvRun(0,is.size(),true,[&](size_t n){return cache.sample(is[n],[&]{return sampleM2ShadowUv(model,Batch{},is[n],7);});},[&](size_t a,size_t b,const auto&uv){got.push_back({a,b,uv});});
 assert(ref.size()==got.size());for(size_t n=0;n<ref.size();++n){assert(ref[n].start==got[n].start&&ref[n].count==got[n].count&&same(ref[n].uv,got[n].uv));}
 }
 std::cout<<"PASS exact keys, collisions/evictions, context isolation and 48000 production UV samples with complete draw-run equivalence\n";
 model=Model{};
 for(unsigned mode=0;mode<3;++mode){std::vector<Instance>is;
 for(unsigned n=0;n<12000;++n){unsigned k=mode==0?n/20:mode==1?n%16:n;is.push_back({int(k%2),float(k),float(k*3)});}
 unsigned oldCount,newCount;const auto oldMs=benchmark<PreviousCache>(is,model,oldCount);const auto newMs=benchmark<M2ShadowUvClockCache>(is,model,newCount);
 std::cout<<"Synthetic "<<(mode==0?"adjacent20":mode==1?"interleaved16":"unique")<<": oldMs="<<oldMs<<" newMs="<<newMs<<" oldEvaluations="<<oldCount<<" newEvaluations="<<newCount<<" (host CPU, not console FPS)\n";
 if(mode==1)assert(newCount<oldCount/2);
 }
}
