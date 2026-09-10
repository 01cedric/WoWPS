#!/usr/bin/env python3
"""Exercise production Lua send, native dispatch and CHAT_MSG event bodies."""
from pathlib import Path
import os,subprocess,tempfile
r=Path(__file__).resolve().parents[2];t=Path(tempfile.mkdtemp(prefix='wowps-chat-ui-'))
def between(file,start,end):
 s=(r/file).read_text();a=s.index(start);return s[a:s.index(end,a)]
(t/'send.inc').write_text(between('src/game/game_handler_callbacks.cpp','void GameHandler::sendChatMessage(', '\nvoid GameHandler::sendAddonMessage('))
(t/'lua.inc').write_text(between('src/addons/lua_social_api.cpp','static int lua_SendChatMessage(', '// SendAddonMessage'))
(t/'event.inc').write_text(between('src/game/chat_handler.cpp','void ChatHandler::fireChatEvent(', '\nvoid ChatHandler::addLocalChatLine('))
(t/'test.cpp').write_text(r'''
#include "game/local_chat_client.hpp"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <cassert>
#include <functional>
#include <iostream>
#include <cctype>
#define LOG_WARNING(...) ((void)0)
namespace wowee::core {double appTimeSeconds(){return 3;}}
namespace wowee::game {
const char* getChatTypeString(ChatType t){switch(t){case ChatType::WHISPER:return "WHISPER";case ChatType::WHISPER_INFORM:return "WHISPER_INFORM";case ChatType::PARTY:return "PARTY";case ChatType::YELL:return "YELL";default:return "SAY";}}
struct Realm {bool online=true;unsigned sends=0;LocalChatChannel channel{};std::string text,target;
 bool ready()const{return online;}bool sendChat(LocalChatChannel c,const std::string& s,const std::string& t){++sends;channel=c;text=s;target=t;return true;}};
struct GameHandler;
struct ChatHandler {GameHandler& owner_;unsigned connectedSends=0;
 void sendChatMessage(ChatType,const std::string&,const std::string&){++connectedSends;}
 int getChannelIndex(const std::string&)const{return 0;}
 void fireChatEvent(const MessageChatData&);
};
struct GameHandler {
 bool localExploration_=true;Realm realm;ChatHandler chat{*this};ChatHandler* chatHandler_=&chat;
 std::function<Realm*()> localAuctionRealm_=[this]{return &realm;};
 std::vector<std::string> channels;std::string error,event;std::vector<std::string> args;Character character;
 std::function<void(const std::string&,const std::vector<std::string>&)> callback=[this](const auto& e,const auto& a){event=e;args=a;};
 void sendChatMessage(ChatType,const std::string&,const std::string&);
 void addSystemChatMessage(const std::string& s){error=s;}
 const auto& getJoinedChannels(){return channels;}
 auto& addonEventCallbackRef(){return callback;}
 const Character* getActiveCharacter(){return &character;}
 uint64_t getPlayerGuid(){return 1;}
 std::string getLanguageName(uint32_t){return "";}
};
#include "send.inc"
#include "event.inc"
}
namespace wowee::addons {
using namespace wowee;
static game::GameHandler* active;
static game::GameHandler* getGameHandler(lua_State*){return active;}
#include "lua.inc"
}
int main(){
 using namespace wowee::game;GameHandler handler;wowee::addons::active=&handler;
 auto* L=luaL_newstate();luaL_openlibs(L);lua_register(L,"SendChatMessage",wowee::addons::lua_SendChatMessage);
 assert(luaL_dostring(L,"SendChatMessage('hello','party')")==0);assert(handler.realm.sends==1 && handler.realm.channel==LocalChatChannel::Party && handler.chat.connectedSends==0);
 assert(luaL_dostring(L,"SendChatMessage('secret','WHISPER',nil,'Guest')")==0);assert(handler.realm.target=="Guest");
 const auto count=handler.realm.sends;handler.realm.online=false;
 assert(luaL_dostring(L,"SendChatMessage('offline','SAY')")==0);assert(handler.realm.sends==count && !handler.error.empty() && handler.chat.connectedSends==0);
 handler.localExploration_=false;assert(luaL_dostring(L,"SendChatMessage('server','SAY')")==0);assert(handler.chat.connectedSends==1);
 lua_close(L);std::cout<<"PASS production Lua/native chat dispatch selects local authority, handles stopped realm and preserves connected-server path\n";
 auto line=localChatClientMessage({LocalChatChannel::WhisperInform,1,"Host","Guest","|Hfake|hsecret|h"});
 handler.chat.fireChatEvent(line);assert(handler.event=="CHAT_MSG_WHISPER_INFORM" && handler.args.size()==12 && handler.args[1]=="Guest" && handler.args[0]=="||Hfake||hsecret||h");
 line=localChatClientMessage({LocalChatChannel::Whisper,2,"Guest","Host","hello"});handler.chat.fireChatEvent(line);assert(handler.event=="CHAT_MSG_WHISPER" && handler.args[1]=="Guest" && handler.args[11]=="0x0000000000000002");
 for(const auto channel:{LocalChatChannel::Say,LocalChatChannel::Party,LocalChatChannel::Yell}){line=localChatClientMessage({channel,1,"Host","","hello"});handler.chat.fireChatEvent(line);assert(handler.args[0]=="hello" && handler.args[1]=="Host");}
 std::cout<<"PASS production FrameXML event arguments, whisper recipient/sender identity and escaped text\n";
}
''')
sources=[p for p in (r/'extern/lua-5.1.5/src').glob('*.c') if p.name not in {'lua.c','luac.c','print.c'}]
subprocess.run([os.getenv('CC','cc'),'-O1','-w','-c',*map(str,sources)],cwd=t,check=True)
command=[os.getenv('CXX','c++'),'-std=c++20','-O1','-g','-I'+str(r/'include'),'-I'+str(r/'extern/glm'),'-I'+str(r/'extern/lua-5.1.5/src'),str(t/'test.cpp'),*map(str,t.glob('*.o')),'-lm','-ldl','-o',str(t/'test')]
if os.getenv('SANITIZE')=='1':command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
subprocess.run(command,check=True);subprocess.run([str(t/'test')],check=True)
