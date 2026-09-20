#include "addons/local_framexml_lua.hpp"
#include "addons/local_party_framexml_lua.hpp"
#include <cstdio>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
int main(int argc,char** argv){
 if(argc!=2)return 2;auto* L=luaL_newstate();luaL_openlibs(L);
 const char* setup="UnitName=function(u)return u end; UnitIsDead=function(u)return u=='target' end;UnitIsGhost=function(u)return false end;UnitIsDeadOrGhost=UnitIsDead;RepopMe=function()return 'online-release' end;RetrieveCorpse=function()return 'online-reclaim' end";
 if(luaL_dostring(L,setup)||luaL_dostring(L,wowee::addons::kLocalFrameXmlLua)||luaL_dostring(L,wowee::addons::kLocalPartyFrameXmlLua)||luaL_dofile(L,argv[1])){
  std::fprintf(stderr,"FAIL death Lua: %s\n",lua_tostring(L,-1));lua_close(L);return 1;
 }lua_close(L);
}
