from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'src/game/game_handler_local.cpp').read_text()
a=s.index('            auto& list=spellHandler_->getPlayerAurasMut();')
b=s.index('\n        }',a)
body=s[a:b]
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text('''
#include "game/local_aura_presentation.hpp"
#include "game/spell_defines.hpp"
#include <chrono>
#include <functional>
#include <cassert>
#include <iostream>
using namespace wowee::game;
struct Content{LocalSpellDefinition d;const LocalSpellDefinition* spell(uint32_t)const{return &d;}};
struct Spells{std::vector<AuraSlot> a,mirror;auto& getPlayerAurasMut(){return a;}void mirrorAurasByGuid(uint64_t,const std::vector<AuraSlot>& v){mirror=v;}};
struct UI{Spells sp;Spells* spellHandler_=&sp;unsigned events=0;std::function<void(std::string,std::vector<std::string>)> addonEventCallback_=[&](auto,auto){++events;};void sync(const LocalRealmPlayer& snapshot,const Content& content){
'''+body+'''
}};
int main(){UI ui;Content c;c.d.durationMs=10000;LocalRealmPlayer p;p.guid=7;p.level=80;p.statAuras={{123,9000,0,0}};
ui.sync(p,c);assert(ui.events==2&&ui.sp.a.size()==1&&ui.sp.a[0].spellId==123&&ui.sp.a[0].casterGuid==7&&ui.sp.a[0].maxDurationMs==10000&&ui.sp.a[0].charges==1&&ui.sp.a[0].flags&0x10);
ui.sync(p,c);assert(ui.events==2);p.statAuras[0].remainingMs=5000;ui.sync(p,c);assert(ui.events==4);p.statAuras[0].casterGuid=99;ui.sync(p,c);assert(ui.events==6&&ui.sp.a[0].casterGuid==99);p.statAuras.clear();ui.sync(p,c);assert(ui.events==8&&ui.sp.a.empty()&&ui.sp.mirror.empty());
p.healingAuras={{555,2000,3000,99}};ui.sync(p,c);assert(ui.sp.a.size()==1&&ui.sp.a[0].spellId==555&&ui.sp.a[0].casterGuid==99&&ui.sp.a[0].durationMs==2000);p.statAuras={{123,9000,0,0}};ui.sync(p,c);assert(ui.sp.a.size()==2&&ui.sp.a[0].spellId==123&&ui.sp.a[1].spellId==555);
LocalSpellDefinition heal;heal.heal=5;LocalRealmPlayer friendPlayer=p;friendPlayer.guid=8;std::vector<LocalRealmPlayer> players{p,friendPlayer};assert(localSpellCommandTarget(heal,p,8,players)==8&&localSpellCommandTarget(heal,p,999,players)==p.guid);heal.healingSelfOnly=true;assert(localSpellCommandTarget(heal,p,8,players)==p.guid);heal.heal=0;heal.damage=5;assert(localSpellCommandTarget(heal,p,999,players)==999);
std::cout<<"PASS production buff UI: identity, positive flag, duration, caster, bounded refresh events and confirmed removal\\n";}
''')
 subprocess.run(['c++','-std=c++20','-I'+str(r/'include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
