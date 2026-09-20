#include "game/local_graveyard_sites.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
using namespace wowee::game;
int main(){
 const auto sites=buildPinnedLocalGraveyards();assert(sites.size()==704);assert(kLocalGraveyardPositions.size()==582);
 std::set<std::pair<uint32_t,uint32_t>> keys;unsigned neutral=0,alliance=0,horde=0;
 for(size_t i=0;i<sites.size();++i){
  const auto&s=sites[i];const auto&l=kLocalGraveyardLinks[i];assert(s.id==l.id&&s.zoneId==l.zoneId);
  assert(keys.emplace(s.id,s.zoneId).second);assert(std::isfinite(s.x)&&std::isfinite(s.y)&&std::isfinite(s.z));
  assert(std::abs(s.x)<=100000&&std::abs(s.y)<=100000&&std::abs(s.z)<=100000);assert(s.orientation==0);
  auto p=std::find_if(kLocalGraveyardPositions.begin(),kLocalGraveyardPositions.end(),[&](auto&p){return p.id==s.id;});
  assert(p!=kLocalGraveyardPositions.end());assert(s.mapId==p->mapId&&s.x==p->x&&s.y==p->y&&s.z==p->z);
  if(l.team==0){assert(s.raceMask==0);neutral++;}
  if(l.team==469){assert(s.raceMask==1101);alliance++;}
  if(l.team==67){assert(s.raceMask==690);horde++;}
 }
 assert(neutral==573&&alliance==66&&horde==65);
 auto find=[&](unsigned id,unsigned zone)->const LocalGraveyardSite&{auto i=std::find_if(sites.begin(),sites.end(),[&](auto&s){return s.id==id&&s.zoneId==zone;});assert(i!=sites.end());return *i;};
 // Same physical site intentionally changes eligibility by death zone.
 const auto&a=find(32,14);const auto&b=find(32,1637);assert(a.raceMask==0&&b.raceMask==1101);assert(a.mapId==1&&a.x==b.x&&a.y==b.y&&a.z==b.z);
 const auto&r=find(2,44);assert(r.mapId==0&&r.x==-9194.31f&&r.y==-2313.26f&&r.z==88.8265f);
 printf("PASS 704 exact zone/team joins from 582 authored positions; neutral=573 Alliance=66 Horde=65; finite bounds, map/xyz preservation, no missing IDs, zone-specific eligibility retained\n");
}
