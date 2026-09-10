#include "addons/local_framexml_lua.hpp"
#include <cstdio>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
int main(int argc,char**argv){
    if(argc!=2)return 2;
    auto* L=luaL_newstate();luaL_openlibs(L);
    const char* setup="GetQuestReward=function(i)return 'online-'..tostring(i) end;GetNumQuestChoices=function()return 99 end;UnitName=function()return 'Online' end; UseContainerItem=function()return 'online-use' end; CastSpellByName=function()return 'online-cast' end;GetContainerNumSlots=function()return 7 end";
    if(luaL_dostring(L,setup)||luaL_dostring(L,wowee::addons::kLocalFrameXmlLua)||luaL_dofile(L,argv[1])){
        std::fprintf(stderr,"FAIL quest reward Lua: %s\n",lua_tostring(L,-1));lua_close(L);return 1;
    }lua_close(L);
}
