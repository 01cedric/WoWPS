#!/usr/bin/env python3
"""Compile production social dispatch and the socketless chat update prefix."""
from pathlib import Path
import os,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
def body(path,signature):
 s=(r/path).read_text();start=s.index(signature);brace=s.index('{',start);n=1;end=brace+1
 # Bodies selected here have balanced braces even inside their string literals.
 while n:
  n+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
with tempfile.TemporaryDirectory(prefix='wowps-social-client-0182-') as folder:
 t=Path(folder)
 (t/'dispatch.inc').write_text(body('src/addons/local_framexml.cpp','int LocalFrameXml::socialCommand('))
 (t/'pump.inc').write_text(body('src/game/game_handler.cpp','void GameHandler::pumpLocalSocial('))
 s=(r/'src/game/game_handler.cpp').read_text();start=s.index('void GameHandler::update(float');end=s.index('    updateNetworking();',start)
 (t/'update.inc').write_text(s[start:end]+'    ++networkUpdates;\n}')
 (t/'test.cpp').write_text(r'''
#include "game/local_social.hpp"
#include "game/local_chat_client.hpp"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <iostream>
#include <cassert>
#include <functional>
#define LOG_ERROR(...) ((void)0)
namespace wowee::game {
struct Realm {
 bool online=true;unsigned calls=0;LocalAction action{};uint64_t value=0;uint32_t id=0,revision=0,bag=0,slot=0,item=0;uint16_t count=0;bool answer=false;std::string ignored;
 std::vector<LocalChatLine> inbox;
 bool ready()const{return online;}
 bool startReadyCheck(){++calls;action=LocalAction::ReadyStart;return true;}
 bool answerReadyCheck(uint32_t i,bool a){++calls;action=LocalAction::ReadyAnswer;id=i;answer=a;return true;}
 bool changeIgnore(const std::string& n,bool add){++calls;ignored=add?n:"";return true;}
 uint64_t partyPlayerByName(const std::string& n){return n=="Guest"?0xF123456789ABCDEFull:0;}
 bool tradeAction(LocalAction a,uint32_t i,uint32_t r,uint64_t v=0,uint32_t b=0,uint32_t s=0,uint32_t it=0,uint16_t c=0){++calls;action=a;id=i;revision=r;value=v;bag=b;slot=s;item=it;count=c;return true;}
 auto takeChatMessages(){auto out=std::move(inbox);inbox.clear();return out;}
};
struct Chat {std::vector<MessageChatData> lines;void addLocalChatMessage(const MessageChatData& m){lines.push_back(m);}};
struct GameHandler {
 struct Inventory {void pumpLocalMail(){}};Inventory* inventoryHandler_=nullptr;
 Realm realm;bool localExploration_=true;Chat chat;Chat* chatHandler_=&chat;
 std::function<Realm*()> localAuctionRealm_=[this]{return &realm;};int* socket=nullptr;int networkUpdates=0,dismissals=0;std::string whisper;
 void pumpLocalSocial();void update(float);void dismissReadyCheck(){++dismissals;}
 std::string& lastWhisperSenderRef(){return whisper;}
};
#include "pump.inc"
#include "update.inc"
}
namespace wowee::addons {
struct LocalFrameXml {
 std::function<game::Realm*()> realm_;game::GameHandler* handler_=nullptr;float timer_=1;int publications=0;
 void publish(){++publications;}static int socialCommand(lua_State*);
};
#include "dispatch.inc"
}
int main(){
 using namespace wowee;game::GameHandler h;addons::LocalFrameXml bridge;bridge.realm_=[&]{return &h.realm;};bridge.handler_=&h;
 auto* L=luaL_newstate();luaL_openlibs(L);lua_pushlightuserdata(L,&bridge);lua_pushcclosure(L,addons::LocalFrameXml::socialCommand,1);lua_setglobal(L,"send");
 auto run=[&](const char* text){if(luaL_dostring(L,text)){std::cerr<<lua_tostring(L,-1)<<"\n";assert(false);}};
 run("assert(send('trade_request','Guest'))");assert(h.realm.value==0xF123456789ABCDEFULL);
 run("assert(send('trade_offer','',7,10,3,23,5,117,9))");assert(h.realm.id==7 && h.realm.revision==10 && h.realm.value==3 && h.realm.bag==23 && h.realm.slot==5 && h.realm.item==117 && h.realm.count==9);
 const auto calls=h.realm.calls;
 run("assert(not send('trade_offer','',1,1,1,0,0,117,65536));assert(not send('trade_offer','',1,1,0/0));assert(not send('trade_offer','',1,1,1.5));assert(not send('trade_offer','',1,-1));assert(not send('trade_offer','',4294967296));assert(not send('unknown'));assert(not send('ready_answer','',1,0,2))");assert(h.realm.calls==calls);
 run("assert(send('ready_answer','',8,0,1))");assert(h.realm.answer && h.realm.id==8 && h.dismissals==1);
 h.realm.online=false;run("assert(not send('trade_request','Guest'))");h.realm.online=true;
 lua_close(L);std::cout<<"PASS production C social bridge: exact 64-bit name resolution, displayed revisions/stack bounds, nonfinite and fractional rejection, ready response and stopped-realm gate\n";
 h.realm.inbox.push_back({game::LocalChatChannel::Whisper,2,"Guest","Host","hello"});h.update(.1f);
 assert(h.chat.lines.size()==1 && h.whisper=="Guest" && h.networkUpdates==0 && h.realm.inbox.empty());h.pumpLocalSocial();assert(h.chat.lines.size()==1);
 h.realm.inbox.push_back({game::LocalChatChannel::Say,2,"Guest","","queued"});h.localExploration_=false;h.update(.1f);assert(h.chat.lines.size()==1 && h.realm.inbox.size()==1);
 h.localExploration_=true;h.realm.online=false;h.update(.1f);assert(h.chat.lines.size()==1);
 std::cout<<"PASS production socketless update: local chat delivered before external-socket guard, whisper reply identity, drain exactly once and local/connection gates\n";
}
''')
 sources=[p for p in (r/'extern/lua-5.1.5/src').glob('*.c') if p.name not in {'lua.c','luac.c','print.c'}]
 subprocess.run([os.getenv('CC','cc'),'-O1','-w','-c',*map(str,sources)],cwd=t,check=True)
 command=[os.getenv('CXX','c++'),'-std=c++20','-O1','-g','-I'+str(r/'include'),'-I'+str(r/'extern/glm'),'-I'+str(r/'extern/lua-5.1.5/src'),str(t/'test.cpp'),*map(str,t.glob('*.o')),'-lm','-ldl','-o',str(t/'test')]
 if os.getenv('SANITIZE')=='1':command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run(command,check=True);subprocess.run([str(t/'test')],check=True)
