from pathlib import Path
import re,subprocess,tempfile,ast,os
r=Path(__file__).resolve().parents[2]
s=(r/'src/addons/lua_action_api.cpp').read_text()
def function(name):
 start=s.index('static int '+name+'(') if name!='clearCursorItem' else s.index('static void clearCursorItem(lua_State* L) {')
 a=s.index('{',start);level=1;i=a+1
 while level:
  if s[i]=='{':level+=1
  elif s[i]=='}':level-=1
  i+=1
 return s[start:i]
with tempfile.TemporaryDirectory() as d:
 p=Path(d);p.joinpath('test.cpp').write_text('''
#include <array>
#include <string>
#include <cstdint>
#include <cassert>
#include <iostream>
namespace game { struct ActionBarSlot {enum Type{EMPTY,SPELL,ITEM,MACRO};Type type=EMPTY;uint32_t id=0;bool isEmpty()const{return type==EMPTY||!id;}};namespace slots {constexpr int kCursorNoSource=-99;}}
struct lua_State {int slot=1;};
int luaL_checknumber(lua_State* L,int){return L->slot;}
enum class CursorType{NONE,SPELL,ITEM,MACRO,ACTION,MONEY};
CursorType s_cursorType=CursorType::NONE;uint32_t s_cursorId=0;int s_cursorSlot=0,s_cursorBag=0,s_cursorSplit=0;uint64_t s_cursorMoney=0;
std::string icon;struct Held{int n=0;};Held held;Held& cursorItemSlot(){return held;}
namespace wowee::ui {void frameXmlSetCursorItem(const std::string& s){icon=s;}}
void setCursorType(lua_State*,CursorType t){s_cursorType=t;if(t==CursorType::NONE){assert(icon.empty()&&s_cursorId==0&&s_cursorSlot==0);}}
struct Info{int displayInfoId=1;};
struct Handler {std::array<game::ActionBarSlot,12> bar{};int changes=0;
const auto& getActionBar(){return bar;}void setActionBarSlot(int i,game::ActionBarSlot::Type t,uint32_t id){bar[i]={t,id};++changes;}
std::string getSpellIconPath(uint32_t){return "spell";}const Info* getItemInfo(uint32_t){static Info i;return &i;}std::string getItemIconPath(int){return "item";}
}handler;
Handler* getGameHandler(lua_State*){return &handler;}
bool localRealmSpell(lua_State*,int,uint32_t&,std::string&){return false;}
'''+function('clearCursorItem')+'\n'+function('lua_PickupAction')+'\n'+function('lua_PlaceAction')+'''
int main(){lua_State L;handler.bar[0]={game::ActionBarSlot::SPELL,10};lua_PickupAction(&L);assert(handler.bar[0].isEmpty()&&s_cursorId==10&&icon=="spell");
L.slot=2;handler.bar[1]={game::ActionBarSlot::SPELL,20};lua_PlaceAction(&L);assert(handler.bar[1].id==10&&s_cursorType==CursorType::NONE&&icon.empty());
L.slot=2;lua_PickupAction(&L);assert(handler.bar[1].isEmpty());clearCursorItem(&L);assert(icon.empty()&&handler.bar[1].isEmpty());
handler.bar[2]={game::ActionBarSlot::MACRO,30};L.slot=3;lua_PickupAction(&L);assert(s_cursorType==CursorType::MACRO&&s_cursorId==30);L.slot=4;lua_PlaceAction(&L);assert(handler.bar[3].type==game::ActionBarSlot::MACRO&&handler.bar[3].id==30);
L.slot=4;lua_PickupAction(&L);L.slot=0;lua_PlaceAction(&L);assert(s_cursorId==30);clearCursorItem(&L);
std::cout<<"PASS production cursor APIs: remove, replace, clear callback state, macro move and invalid destination\\n";}
''')
 subprocess.run(['c++','-std=c++20',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
 text=(r/'src/addons/local_framexml_input.cpp').read_text();a=text.index('case PadPickupTarget::Kind::Action:');b=text.index('case PadPickupTarget::Kind::None:',a)
 code=''.join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"',text[a:b]))
 p.joinpath('test.lua').write_text('''local slot=4
local b={action=slot}
local carried=false
local picks,puts=0,0
GetCursorInfo=function() if carried then return 'spell',123 end end
PickupAction=function(i) assert(i==4);picks=picks+1;carried=true end
PlaceAction=function(i) assert(i==4);puts=puts+1;carried=false end
local function press() '''+code+''' end
press();assert(picks==1 and puts==0 and carried)
press();assert(picks==1 and puts==1 and not carried)
print('PASS production controller Lua: pick up empty cursor, replace carried spell and clear')
''');subprocess.run([os.environ.get('LUA','lua5.1'),str(p/'test.lua')],check=True)
