#include "addons/local_auction_framexml_lua.hpp"
#include <cstdio>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    lua_State* L=luaL_newstate();luaL_openlibs(L);
    lua_pushstring(L,argv[2]);lua_setglobal(L,"retailRoot");
    lua_pushstring(L,wowee::addons::kLocalAuctionLoadLua);lua_setglobal(L,"productionLoad");
    lua_pushstring(L,wowee::addons::kLocalAuctionShowLua);lua_setglobal(L,"productionShow");
    if(luaL_dofile(L,argv[1])) {
        std::fprintf(stderr,"FAIL auction Lua: %s\n",lua_tostring(L,-1));lua_close(L);return 1;
    }
    lua_close(L);return 0;
}
