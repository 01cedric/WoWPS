#!/usr/bin/env python3
"""Compile the actual WMO visible-range submission body against a draw recorder."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'src/rendering/wmo_renderer.cpp').read_text()
start = source.index('                uint32_t pendingFirst = 0, pendingCount = 0;', source.index('void WMORenderer::render('))
end = source.index('                flushRange();\n            }', start) + len('                flushRange();')
body = source[start:end]
fixture = r'''
#include "rendering/ordered_triangle_range.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>
#include <vector>
using namespace wowee::rendering;
struct Range { uint32_t firstIndex,indexCount; bool visible; };
struct Batch { std::vector<Range> draws; };
std::vector<Range> recorded;
void vkCmdDrawIndexed(int,uint32_t count,uint32_t instances,uint32_t first,int base,uint32_t instance) {
 assert(instances==1 && base==0 && instance==0);
 recorded.push_back({first,count,true});
}
uint32_t submit(const Batch& mb) {
 int cmd=0; uint32_t lastDrawCalls=0,rangesCoalesced=0;
 const auto rangeVisible=[](const Range& dr){return dr.indexCount && dr.visible;};
 const auto firstVisible=std::find_if(mb.draws.begin(),mb.draws.end(),rangeVisible);
 if(firstVisible==mb.draws.end())return 0;
BODY
 assert(lastDrawCalls==recorded.size());
 return rangesCoalesced;
}
std::vector<uint32_t> expand(const std::vector<Range>& ranges) {
 std::vector<uint32_t> out;
 for(auto r:ranges)if(r.visible)for(uint32_t i=0;i<r.indexCount;++i)out.push_back(r.firstIndex+i);
 return out;
}
int main(){
 std::mt19937 rng(276);unsigned merged=0;
 for(unsigned trial=0;trial<20000;++trial){
  Batch mb;uint32_t end=0;
  for(unsigned r=0;r<30;++r){
   uint32_t first=(rng()%3)?end:rng()%100;
   uint32_t count=(rng()%9)*3;
   if(rng()%20==0)++count; // incomplete triangles must never merge
   mb.draws.push_back({first,count,rng()%4!=0});end=first+count;
  }
  recorded.clear();merged+=submit(mb);
  assert(expand(mb.draws)==expand(recorded));
 }
 // Duplicates remain duplicated, out-of-order indices retain draw order.
 Batch explicitCase{{{3,6,true},{9,6,true},{3,6,true},{15,3,false},{9,3,true}}};
 recorded.clear();submit(explicitCase);assert(expand(explicitCase.draws)==expand(recorded));
 assert(merged>10000);
 std::cout<<"PASS actual WMO ordered submission cases=20001 packetsCoalesced="<<merged<<"\n";
}
'''.replace('BODY', body)
with tempfile.TemporaryDirectory(prefix='wowps-wmo-ranges-') as tmp:
    cpp = Path(tmp) / 'test.cpp'
    exe = Path(tmp) / 'test'
    cpp.write_text(fixture)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-I'+str(root/'include'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
