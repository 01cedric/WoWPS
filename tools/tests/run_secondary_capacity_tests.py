#!/usr/bin/env python3
"""Run actual renderer secondary submission list under address/UB sanitizers."""
from pathlib import Path
import subprocess,tempfile,os
r=Path(__file__).resolve().parents[2]
s=(r/'src/rendering/renderer.cpp').read_text();h=(r/'include/rendering/renderer.hpp').read_text()
a=s.index('        VkCommandBuffer validCmds[');b=s.index('vkCmdExecuteCommands(currentCmd, numCmds, validCmds);',a)+len('vkCmdExecuteCommands(currentCmd, numCmds, validCmds);')
body=s[a:b]
constants='\n'.join(line for line in h.splitlines() if 'static constexpr uint32_t SEC_' in line or 'static constexpr uint32_t NUM_SECONDARIES' in line)
cpp='''#include <cassert>
#include <cstdint>
using VkCommandBuffer=int;
'''+constants+'''
int secondaryCmds_[NUM_SECONDARIES][1];
void vkCmdExecuteCommands(int,uint32_t n,const int* p){assert(n<=7);assert(p[0]==SEC_SKY);assert(p[n-1]==SEC_POST);for(uint32_t i=1;i<n;++i)assert(p[i]>p[i-1]);}
void run(int mask){
 bool terrainRenderer=mask&1,camera=mask&2,terrainEnabled=mask&4,skipTerrain=mask&8,wmoRenderer=mask&16,skipWMO=mask&32,m2Renderer=mask&64,skipM2=mask&128;
 int frameIdx=0,currentCmd=0;
'''+body+'''
}
int main(){for(uint32_t i=0;i<NUM_SECONDARIES;++i)secondaryCmds_[i][0]=i;for(int m=0;m<256;++m)run(m);}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.cpp').write_text(cpp)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
 # Negative control: the old six-entry array must fail on the same list.
 (p/'old.cpp').write_text(cpp.replace('validCmds[NUM_SECONDARIES]', 'validCmds[6]'))
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'old.cpp'),'-o',str(p/'old')],check=True)
 old=subprocess.run([str(p/'old')],capture_output=True,text=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
 assert old.returncode != 0 and ('stack-buffer-overflow' in old.stderr or 'index 6 out of bounds' in old.stderr), old.stderr
print('PASS: actual secondary submission list, 256 branch combinations incl all 7 entries, old six-entry negative control fails, ASan/UBSan (LSan disabled for ptrace).')
