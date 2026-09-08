#include "addons/local_merchant_framexml_lua.hpp"
#include <cstdio>
#include <cstdlib>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
int main(int argc,char** argv) {
    if(argc!=3){std::fputs("Usage: merchant_framexml_test fixture.lua retail-directory\n",stderr);return 2;}
    lua_State* L=luaL_newstate();luaL_openlibs(L);
    lua_pushstring(L,argv[2]);lua_setglobal(L,"retailRoot");
    if(luaL_dostring(L,"UnitName=function() return 'Online' end;UseContainerItem=function()return 'online-use' end") ||
       luaL_dostring(L,wowee::addons::kLocalMerchantFrameXmlLua) || luaL_dofile(L,argv[1])) {
        std::fprintf(stderr,"FAIL merchant Lua: %s\n",lua_tostring(L,-1));lua_close(L);return 1;
    }
    lua_close(L);return 0;
}
