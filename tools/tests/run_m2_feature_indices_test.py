#!/usr/bin/env python3
"""Exercise the production swap-removal index repair against a full rebuild."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'src/rendering/m2_renderer_instance.cpp').read_text()
begin = source.index('    const auto repair = [idx, oldTail]')
end = source.index('\n    repair(smokeInstanceIndices_);', begin)
repair = source[begin:end]
code = r'''
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>
int main() {
 std::mt19937 rng(275);
 for (unsigned trial=0; trial<1000; ++trial) {
  std::vector<unsigned> flags(1+rng()%1000);
  for(auto& f:flags) f=rng()%128;
  while(!flags.empty()) {
   size_t idx=rng()%flags.size(), oldTail=flags.size()-1;
   std::vector<size_t> lists[7];
   for(size_t i=0;i<flags.size();++i)for(int k=0;k<7;++k)
    if(flags[i]&(1u<<k))lists[k].push_back(i);
   // Particle list can be reordered by camera/budget priority each frame.
   std::shuffle(lists[4].begin(),lists[4].end(),rng);
   std::sort(lists[4].begin(),lists[4].end());
''' + repair + r'''
   for(auto& list:lists)repair(list);
   flags[idx]=flags.back(); flags.pop_back();
   for(int k=0;k<7;++k) {
    std::vector<size_t> expected;
    for(size_t i=0;i<flags.size();++i)if(flags[i]&(1u<<k))expected.push_back(i);
    assert(lists[k]==expected);
   }
  }
 }
 std::cout<<"PASS: production auxiliary index repair matches full rebuild across 1000 randomized unload sequences, seven categories (including ribbons and water vegetation), tail removal and reordered particles\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp)/'test.cpp'; cpp.write_text(code)
    exe = Path(tmp)/'test'
    subprocess.run(['g++','-std=c++17','-O2',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
