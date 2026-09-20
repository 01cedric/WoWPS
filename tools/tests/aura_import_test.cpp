#include "game/local_spell_import.hpp"
#include "game/local_aura_presentation.hpp"
#include <cassert>
#include <cstring>
#include <memory>
#include <iostream>
using namespace wowee::game;
static uint32_t bits(float f){uint32_t n;std::memcpy(&n,&f,4);return n;}
static std::unique_ptr<wowee::pipeline::DBCFile> dbc(std::vector<std::vector<uint32_t>> rows){
 std::vector<uint8_t> data{'W','D','B','C'};auto put=[&](uint32_t v){for(int i=0;i<4;++i)data.push_back(uint8_t(v>>(i*8)));};
 put(rows.size());put(rows[0].size());put(rows[0].size()*4);put(1);for(auto& row:rows)for(auto v:row)put(v);data.push_back(0);
 auto d=std::make_unique<wowee::pipeline::DBCFile>();assert(d->load(data));return d;
}
int main(){
 std::vector<uint32_t> row(234),range(40);row[0]=99001;row[28]=row[40]=row[46]=1;row[68]=UINT32_MAX;row[71]=6;row[86]=21;row[80]=6;row[95]=8;row[98]=250;range[0]=1;range[3]=range[4]=bits(30);
 auto ranges=dbc({range}),casts=dbc({{1,0,0,0}}),durations=dbc({{1,1000,0,0}});detail::ClientSpellTables tables;tables.ranges=ranges.get();tables.casts=casts.get();tables.durations=durations.get();
 detail::ClientSpellTables::buildIndex(ranges.get(),tables.rangeIndex);detail::ClientSpellTables::buildIndex(casts.get(),tables.castIndex);detail::ClientSpellTables::buildIndex(durations.get(),tables.durationIndex);
 for(unsigned count:{0u,1u,3u,255u,256u,UINT32_MAX}){
  row[49]=count;auto spells=dbc({row});tables.spells=spells.get();LocalSpellDefinition d;
  const bool ok=detail::decodeClientSpell(tables,0,d);assert(ok==(count<=255));if(ok)assert(d.maxAuraStacks==std::max(1u,count));
 }
 row[49]=3;
 for(unsigned column:{34u,36u,116u}){row[column]=1;auto spells=dbc({row});tables.spells=spells.get();LocalSpellDefinition d;assert(!detail::decodeClientSpell(tables,0,d));row[column]=0;}
 row[72]=3;auto spells=dbc({row});tables.spells=spells.get();LocalSpellDefinition d;assert(!detail::decodeClientSpell(tables,0,d));
 LocalRealmPlayer p;p.healingAuras={{1,100,100,2,0}};assert(!validLocalHealingAuraViews(p));p.healingAuras[0].stacks=255;assert(validLocalHealingAuraViews(p));
 std::cout<<"PASS periodic stack import: default, 1/3/255 boundaries, overflow rejection, proc/charge/trigger/mixed-effect guards and zero view rejection\n";
}
