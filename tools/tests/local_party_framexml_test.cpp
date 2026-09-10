#include "addons/local_party_framexml_lua.hpp"
#include <cstdio>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
int main(int argc,char** argv){
    if(argc!=2)return 2;auto* L=luaL_newstate();luaL_openlibs(L);
    const char* setup="UnitName=function(unit)return 'Online-'..unit end;GetNumPartyMembers=function()return 7 end;InviteUnit=function(name)return 'online-invite-'..name end;UnitHealth=function()return 999 end;TargetUnit=function(unit)return 'online-target-'..unit end";
    if(luaL_dostring(L,setup)||luaL_dostring(L,wowee::addons::kLocalPartyFrameXmlLua)||luaL_dofile(L,argv[1])){
        std::fprintf(stderr,"FAIL party Lua: %s\n",lua_tostring(L,-1));lua_close(L);return 1;
    }lua_close(L);
}
