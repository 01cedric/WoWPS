#!/usr/bin/env python3
"""Exercise the production notification and argument conversion with retail Lua.

Usage: python3 character_points_framexml_test.py /path/to/3.3.5/ChatFrame.lua
The original script remains external and is not redistributed in the project.
"""
from pathlib import Path
import hashlib
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
retail = Path(sys.argv[1]).read_text()
start = retail.index('function ChatFrame_SystemEventHandler(')
end = retail.index('\nfunction ', start + 1)
bridge = (root / 'src/addons/local_framexml.cpp').read_text()
start_bridge = bridge.index('    if(dirty&Change::Professions)')
end_bridge = bridge.index('    if((dirty&', start_bridge)
engine = (root / 'src/addons/lua_engine.cpp').read_text()
start_arg = engine.index('void pushEventArg(')
end_arg = engine.index('\n}  // namespace', start_arg)
print('Retail ChatFrame.lua SHA256:', hashlib.sha256(Path(sys.argv[1]).read_bytes()).hexdigest(), flush=True)
with tempfile.TemporaryDirectory(prefix='wowps-character-points-') as folder:
    temp = Path(folder)
    (temp / 'retail.lua').write_text(retail[start:end])
    (temp / 'notification.inc').write_text(bridge[start_bridge:end_bridge])
    (temp / 'argument.inc').write_text(engine[start_arg:end_arg])
    (temp / 'test.cpp').write_text(r'''
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "game/local_ui_changes.hpp"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
namespace wowee::game { inline constexpr const char* kEventNil="\x01"; }
#include "argument.inc"
struct Engine {
    lua_State* L;
    unsigned calls=0;
    bool ok=true;
    void fireEvent(const std::string& event,const std::vector<std::string>& args={}) {
        ++calls;
        if(event=="SKILL_LINES_CHANGED") { assert(args.empty());return; }
        assert(event=="CHARACTER_POINTS_CHANGED");
        lua_getglobal(L,"ChatFrame_SystemEventHandler");
        lua_getglobal(L,"frame");lua_pushstring(L,event.c_str());
        for(const auto& arg:args)pushEventArg(L,arg);
        if(lua_pcall(L,2+args.size(),1,0)) {
            std::cerr<<"FAIL production "<<event<<" argc="<<args.size()<<": "<<lua_tostring(L,-1)<<"\n";
            ok=false;
        } else { assert(lua_toboolean(L,-1)); }
        lua_pop(L,1);
    }
};
void notify(Engine* engine_,uint32_t dirty) {
    using Change=wowee::game::LocalUiChanges;
#include "notification.inc"
}
int main(int argc,char** argv) {
    assert(argc==2);
    auto* L=luaL_newstate();luaL_openlibs(L);
    assert(luaL_dostring(L,"messages=0;ChatTypeInfo={SYSTEM={r=1,g=1,b=1,id=1}};frame={AddMessage=function()messages=messages+1 end};format=string.format;LEVEL_UP_SKILL_POINTS='%d';UnitCharacterPoints=function()return 0,5 end")==0);
    assert(luaL_dofile(L,argv[1])==0);
    Engine engine{L};
    // No notification for unchanged state; initial publication and later
    // profession changes use the same real production branch.
    notify(&engine,0);assert(engine.calls==0);
    notify(&engine,wowee::game::LocalUiChanges::Professions);
    if(!engine.ok){lua_close(L);return 1;}
    assert(engine.calls==2);
    for(int i=0;i<20;++i)notify(&engine,wowee::game::LocalUiChanges::Professions);
    assert(engine.ok && engine.calls==42);
    assert(luaL_dostring(L,"assert(messages==0)")==0);
    std::cout<<"PASS production character-points notifications: initial/repeated refresh reaches retail ChatFrame without nil comparison or invented gain\n";
    engine.fireEvent("CHARACTER_POINTS_CHANGED",{"0","1"});
    assert(engine.ok && luaL_dostring(L,"assert(messages==1)")==0);
    lua_close(L);
    std::cout<<"PASS retail positive skill-point branch still displays a real gain; numeric conversion is active\n";
}
''')
    lua = root / 'extern/lua-5.1.5/src'
    sources = [p for p in lua.glob('*.c') if p.name not in {'lua.c', 'luac.c', 'print.c'}]
    subprocess.run([os.getenv('CC', 'cc'), '-O1', '-w', '-c', *map(str, sources)], cwd=temp, check=True)
    subprocess.run([os.getenv('CXX', 'c++'), '-std=c++20', '-O1', '-I'+str(root/'include'), '-I'+str(lua), str(temp/'test.cpp'), *map(str,temp.glob('*.o')), '-lm', '-ldl', '-o', str(temp/'test')], check=True)
    subprocess.run([str(temp/'test'), str(temp/'retail.lua')], check=True)
