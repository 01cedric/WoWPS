#!/usr/bin/env python3
"""Run the shipped bridge in the bundled Lua 5.1 interpreter."""
from pathlib import Path
import os,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='wowps-social-ui-0182-') as folder:
 t=Path(folder);s=(r/'include/addons/local_social_framexml_lua.hpp').read_text();(t/'bridge.lua').write_text(s.split('R"lua(',1)[1].split(')lua";',1)[0])
 for header,name in [('local_merchant_framexml_lua.hpp','merchant.lua'),('local_services_framexml_lua.hpp','services.lua')]:
  text=(r/'include/addons'/header).read_text();(t/name).write_text(text.split('R"lua(',1)[1].split(')lua";',1)[0])
 sources=[p for p in (r/'extern/lua-5.1.5/src').glob('*.c') if p.name not in {'luac.c','print.c'}]
 (t/'main.c').write_text('#include "lua.h"\n#include "lauxlib.h"\n#include "lualib.h"\n#include <stdio.h>\nint main(int argc,char** argv){lua_State* L=luaL_newstate();luaL_openlibs(L);lua_newtable(L);for(int i=2;i<argc;++i){lua_pushstring(L,argv[i]);lua_rawseti(L,-2,i-1);}lua_setglobal(L,"arg");int rc=luaL_dofile(L,argv[1]);if(rc)fprintf(stderr,"%s\\n",lua_tostring(L,-1));lua_close(L);return rc?1:0;}\n')
 subprocess.run([os.getenv('CC','cc'),'-O1','-w','-I'+str(r/'extern/lua-5.1.5/src'),str(t/'main.c'),*map(str,sources),'-lm','-ldl','-o',str(t/'lua')],check=True)
 subprocess.run([str(t/'lua'),str(r/'tools/tests/inventory_framexml_0184_test.lua'),str(t/'bridge.lua'),str(t/'merchant.lua'),str(t/'services.lua')],check=True)

