#include "addons/local_merchant_framexml_lua.hpp"
#include "addons/local_services_framexml_lua.hpp"
#include <cstdio>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
int main(int argc,char**argv){
    if(argc!=2)return 2;
    auto* L=luaL_newstate();luaL_openlibs(L);
    const char* setup="UnitName=function()return 'Online' end; UseContainerItem=function()return 'online-use' end; CastSpellByName=function()return 'online-cast' end;GetContainerNumSlots=function()return 7 end";
    if(luaL_dostring(L,setup)||luaL_dostring(L,wowee::addons::kLocalMerchantFrameXmlLua)||luaL_dostring(L,wowee::addons::kLocalServicesFrameXmlLua)||luaL_dofile(L,argv[1])){
        std::fprintf(stderr,"FAIL services Lua: %s\n",lua_tostring(L,-1));lua_close(L);return 1;
    }lua_close(L);
}
