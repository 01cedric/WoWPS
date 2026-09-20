#include "rendering/m2_shadow_snapshot.hpp"
#include "rendering/m2_shadow_lod.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>
using namespace wowee::rendering;
struct Model { float boundRadius=3,shadowVertexRadius=3; bool shadowWindFoliage=false; std::vector<int> shadowBatches{1}; };
// Match the cache pressure of production instances, which also contain matrices,
// particle/ribbon vectors, animation state, collision bounds and GPU-cull state.
struct Instance { glm::vec3 position; float scale=1; glm::mat4 modelMatrix{1}; char cold[392]{}; bool cachedIsValid=true,cachedIsSmoke=false,cachedIsInvisibleTrap=false; const Model* cachedModel; };
using Snapshot=M2ShadowSnapshot<Instance>;
volatile uint64_t sink=0;
int main(int argc,char**){std::mt19937 gen(731); std::uniform_real_distribution<float>d(-700,700);
for(unsigned n:{1000u,2000u,4000u,13000u,20000u}){
 std::vector<Model>models(71); std::vector<Instance>instances(n); std::vector<unsigned>order(n);std::iota(order.begin(),order.end(),0);
 for(unsigned i=0;i<n;++i){auto&v=instances[i];v.position={d(gen),d(gen),d(gen)};v.cachedModel=&models[i%71];v.scale=1+float(i%7);v.cachedIsSmoke=i%91==0;v.cachedIsValid=i%103!=0;}
 std::stable_sort(order.begin(),order.end(),[](unsigned a,unsigned b){return a%71<b%71;});
 Snapshot snapshot;std::vector<const Instance*>oldOut,newOut;oldOut.reserve(n*2);newOut.reserve(n*2);
 glm::mat4 light[2]={glm::mat4(1),glm::mat4(1)};light[0][0][0]=light[0][1][1]=1.f/90;light[1][0][0]=light[1][1][1]=1.f/500;for(auto&l:light){l[2][2]=1.f/2000;l[3][2]=.5f;}
 auto farReject=[](const Instance&v,unsigned pass){if(pass==0)return false;float norm=0;for(int c=0;c<3;++c)for(int r=0;r<3;++r)norm+=v.modelMatrix[c][r]*v.modelMatrix[c][r];return m2FarShadowSubtexel(pass,1.f,v.cachedModel->shadowVertexRadius,norm,v.cachedModel->shadowWindFoliage);};
 auto old=[&]{oldOut.clear();for(unsigned pass=0;pass<2;++pass)for(auto index:order){const auto&v=instances[index];if(!v.cachedIsValid||v.cachedIsSmoke||v.cachedIsInvisibleTrap||!v.cachedModel||v.cachedModel->shadowBatches.empty())continue;const auto clip=light[pass]*glm::vec4(v.position,1);float margin=(v.cachedModel->boundRadius*v.scale)/(pass?675.f:121.5f)*1.5f;if(std::abs(clip.x)>1+margin||std::abs(clip.y)>1+margin||clip.z < -margin||clip.z>1+margin)continue;if(farReject(v,pass))continue;oldOut.push_back(&v);}sink+=oldOut.size();};
 auto fresh=[&]{newOut.clear();snapshot.prepare(instances,order);for(unsigned pass=0;pass<2;++pass)for(const auto&e:snapshot.entries){if(!Snapshot::intersects(e,light[pass],pass?675.f:121.5f)||farReject(*e.instance,pass))continue;newOut.push_back(e.instance);}snapshot.finish();sink+=newOut.size();};
 for(unsigned frame=0;frame<30;++frame){instances[frame].position.x+=3;instances[frame].scale+=.1f;models[frame%models.size()].boundRadius+=.25f;instances[frame+200].cachedModel=nullptr;light[1][3][0]+=.002f;old();fresh();assert(oldOut==newOut);assert(!snapshot.active&&snapshot.entries.empty());}
 std::vector<double>ratios;for(unsigned trial=0;trial<(argc>1?1u:21u);++trial){auto time=[&](auto&&fn){auto start=std::chrono::steady_clock::now();for(int i=0;i<(argc>1?1:80);++i)fn();return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/(argc>1?1:80);};double a,b;if(trial%2){b=time(fresh);a=time(old);}else{a=time(old);b=time(fresh);}ratios.push_back(b/a);}
 std::sort(ratios.begin(),ratios.end());std::cout<<"instances="<<n<<" raw_compact_prepare_plus_two_gathers_ratio="<<ratios[ratios.size()/2]<<" min="<<ratios.front()<<" max="<<ratios.back()<<" survivors="<<newOut.size()<<" instanceBytes="<<sizeof(Instance)<<" snapshotBytes="<<sizeof(Snapshot::Entry)<<"\n";
 snapshot.release();assert(snapshot.entries.capacity()==0);
}std::cout<<"PASS exact caster IDs/order; fresh motion/model bounds; paired lifetime; release\n";
}
