"""Run the production contextual action and cursor policy without a PS4 runtime."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
app = (root / 'src/core/application_local_ui.cpp').read_text()
backend = (root / 'src/platform/ps4/input_ps4.cpp').read_text()
framexml = (root / 'src/addons/local_framexml_input.cpp').read_text()

def body(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

primary = body(app, 'auto primaryAction = [&](bool contextual)') + ';'
mail_policy = app[app.index('    const auto* actionTarget = target();'):app.index('    if(keys && mailboxOwnsAction')]
triangle_start = app.index('if(keys && localFrameXml_.ready() && targetPad.connected &&')
triangle = app[triangle_start:app.index('toggleTarget=true;', triangle_start) + len('toggleTarget=true;')]

source = r'''
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <cassert>
#include <cstdint>
#include <iostream>
#include "ui/local_pad_focus.hpp"
#include "game/local_target_selection.hpp"
#include <vector>
struct Npc { uint64_t guid=5; bool dead=false,hostile=true; int mapId=0,instanceId=0; float x=0,y=0,z=0; };
struct Player { int mapId=0,instanceId=0; float x=0,y=0,z=0; };
struct Mailbox { uint64_t guid=99; };
namespace game { bool mailboxAvailable=true; const Mailbox* nearbyLocalMailbox(int, Player) { static Mailbox m; return mailboxAvailable?&m:nullptr; } }
struct Handler { int mail=0; void openMailbox(uint64_t id) { assert(id==99); ++mail; } } handler;
struct Realm { int attacks=0; uint64_t last=0; void attack(uint64_t id) { ++attacks; last=id; } } realm;
int interactions=0;
void dispatch(bool hostile, bool dead, bool exists, bool mail, bool contextual) {
    Npc npc; npc.hostile=hostile; npc.dead=dead;
    auto target=[&]() -> const Npc* { return exists?&npc:nullptr; };
    auto interactWith=[&](const Npc&) { ++interactions; };
    auto* gameHandler=&handler; auto* localRealm_=&realm;
    uint64_t localRealmTarget_=exists?npc.guid:0;
    int content=0; Player self;
    game::mailboxAvailable=mail;
''' + mail_policy + primary + r'''
    primaryAction(contextual);
}
namespace platform::ps4 { bool text=false,keyboard=false; bool inputTextFocus(){return text;} bool keyboardCapturesInput(){return keyboard;} }
struct Frame { bool enabled=true; bool ready()const{return enabled;} } localFrameXml_;
struct Pad { bool connected=true; unsigned pressed=1; } targetPad;
constexpr unsigned ORBIS_PAD_BUTTON_TRIANGLE=1;
bool targetPress(bool keys) { bool toggleTarget=false;
''' + triangle + r'''
return toggleTarget; }
bool s_relativeMode=false,s_lookEngaged=false,s_inWorld=true,s_cursorMode=false,s_textFocus=false,s_uiFocus=false,s_applicationKeyboardOpen=false;
unsigned s_menuOwners=0;
''' + r'''
struct LuaEngine { lua_State* L; lua_State* getState(){return L;} };
''' + body(framexml, 'bool carrying(LuaEngine* engine)') + '\n' + body(backend, 'bool inputCameraLooking()') + '\n' + body(backend, 'bool inputCursorVisible()') + r'''
int main() {
    LuaEngine engine{luaL_newstate()}; auto* L=engine.L;
    lua_pushnumber(L,42); const int base=lua_gettop(L);
    assert(!carrying(nullptr)); assert(!carrying(&engine) && lua_gettop(L)==base);
    for (const char* code:{"function GetCursorInfo() return 'macro', 30 end", "function GetCursorInfo() return 'spell', 0, 'spell', 123 end", "function GetCursorInfo() return 'money', 100 end"}) {
        assert(luaL_dostring(L,code)==0); assert(carrying(&engine) && lua_gettop(L)==base);
    }
    assert(luaL_dostring(L,"function GetCursorInfo() return nil end")==0);
    assert(!carrying(&engine) && lua_gettop(L)==base);
    assert(luaL_dostring(L,"function GetCursorInfo() return missingFunction() end")==0);
    assert(!carrying(&engine) && lua_gettop(L)==base);
    lua_close(L);
    std::cout << "PASS production carry query with real Lua: macro, iconless spell, money, empty/error cases preserve stack\n";
    dispatch(true,false,true,true,true);
    assert(realm.attacks==1 && realm.last==5 && handler.mail==0 && interactions==0);
    dispatch(false,false,true,false,true);
    assert(interactions==1 && realm.attacks==1);
    dispatch(true,true,true,false,true);
    assert(interactions==2 && realm.attacks==1);
    dispatch(false,false,true,true,true);
    dispatch(false,false,false,true,true);
    assert(handler.mail==2 && realm.attacks==1);
    dispatch(true,false,true,true,false);
    assert(handler.mail==2 && realm.attacks==2);
    std::cout << "PASS production Square dispatch: hostile beside mailbox attacks, friendly talks, corpse loots, mailbox opens contextually\n";
    assert(targetPress(true));
    assert(!targetPress(false));
    platform::ps4::text=true; assert(!targetPress(true)); platform::ps4::text=false;
    platform::ps4::keyboard=true; assert(!targetPress(true)); platform::ps4::keyboard=false;
    targetPad.connected=false; assert(!targetPress(true)); targetPad.connected=true;
    targetPad.pressed=0; assert(!targetPress(true));
    std::cout << "PASS production Triangle ownership: blocked by panels, text focus, keyboard and disconnected pad\n";
    assert(!inputCameraLooking() && !inputCursorVisible());
    s_cursorMode=true; assert(inputCursorVisible());
    s_relativeMode=true; assert(inputCameraLooking() && !inputCursorVisible());
    s_relativeMode=false; s_lookEngaged=true; assert(inputCameraLooking() && !inputCursorVisible());
    s_menuOwners=1; s_applicationKeyboardOpen=true; assert(!inputCursorVisible());
    s_lookEngaged=false; assert(!inputCameraLooking() && inputCursorVisible());
    std::cout << "PASS production cursor policy: pointer mode visible, relative/stick camera look hidden, UI restored after look\n";
    using wowee::ui::localTargetOwnsShoulders;
    assert(localTargetOwnsShoulders(5,false,false,true));
    assert(!localTargetOwnsShoulders(5,false,false,false));
    assert(!localTargetOwnsShoulders(5,true,false,true));
    assert(localTargetOwnsShoulders(0,false,true));
    std::vector<Npc> npcs(4); Player p;
    npcs[0].guid=9; npcs[0].x=2;
    npcs[1].guid=8; npcs[1].x=-2;
    npcs[2].guid=7; npcs[2].dead=true;
    npcs[3].guid=6; npcs[3].instanceId=1;
    assert(wowee::game::nearestLivingLocalTarget(p,npcs)==8);
    std::cout << "PASS target selection: nearest live same-instance target, deterministic equal-distance tie, friendly shoulders retain interaction\n";
}
'''
# Compile the production snippets, including their current signatures and predicates.
with tempfile.TemporaryDirectory(prefix='wowps-controller-p02-') as temp:
    cpp=Path(temp)/'controller.cpp'; binary=Path(temp)/'controller'
    cpp.write_text(source)
    lua=root/'extern/lua-5.1.5/src'
    c_sources=[str(path) for path in sorted(lua.glob('*.c')) if path.name not in ('lua.c','luac.c','print.c')]
    subprocess.run(['cc','-O0','-c','-I'+str(lua),*c_sources],cwd=temp,check=True)
    objects=[str(path) for path in sorted(Path(temp).glob('*.o'))]
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-I'+str(root/'include'),'-I'+str(lua),str(cpp),*objects,'-lm','-o',str(binary)], check=True)
    subprocess.run([str(binary)],check=True)
