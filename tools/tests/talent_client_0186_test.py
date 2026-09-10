from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'src/game/spell_handler.cpp').read_text()
sync=s[s.index('void SpellHandler::syncLocalTalents'):s.index('void SpellHandler::learnTalent')]
a=s.index('void SpellHandler::learnTalent');learn=s[a:s.index('    if (owner_.getState()',a)]+'}\n'
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.cpp').write_text('''
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>
#include <cassert>
#include <iostream>
struct Realm{uint32_t talent=0,rank=99;unsigned calls=0;bool ok=true;bool learnTalent(uint32_t t,uint32_t r){talent=t;rank=r;++calls;return ok;}std::string actionStatus(){return "rejected";}};
struct Owner{Realm realm;unsigned events=0,errors=0;Realm* localServiceRealm(){return &realm;}void raiseUiError(const std::string&){++errors;}void fireAddonEvent(const std::string&,const std::vector<std::string>&){++events;}};
struct SpellHandler{Owner owner_;uint8_t activeTalentSpec_=0,unspentTalentPoints_[2]={};std::unordered_map<uint32_t,uint8_t> learnedTalents_[2];void learnTalent(uint32_t,uint32_t);void syncLocalTalents(const std::vector<std::pair<uint32_t,uint8_t>>&,uint8_t);};
'''+sync+learn+'''
int main(){SpellHandler ui;ui.syncLocalTalents({},80);assert(ui.unspentTalentPoints_[0]==71 && ui.owner_.events==2);ui.syncLocalTalents({},80);assert(ui.owner_.events==2);
ui.learnTalent(10,1);assert(ui.owner_.realm.calls==1 && ui.owner_.realm.rank==0);ui.learnTalent(10,5);assert(ui.owner_.realm.rank==4);ui.learnTalent(10,0);ui.learnTalent(10,6);assert(ui.owner_.realm.calls==2 && ui.owner_.errors==2);
ui.syncLocalTalents({{10,5},{20,1}},80);assert(ui.unspentTalentPoints_[0]==65 && ui.learnedTalents_[0][10]==5);ui.owner_.realm.ok=false;ui.learnTalent(20,2);assert(ui.owner_.errors==3 && ui.unspentTalentPoints_[0]==65);ui.syncLocalTalents({},80);assert(ui.learnedTalents_[0].empty() && ui.unspentTalentPoints_[0]==71);
std::cout<<"PASS production talent UI bridge: one-based UI ranks converted once, invalid ranks rejected, confirmed points/ranks/events, no optimistic update on failure and reset refresh\\n";}
''')
 subprocess.run(['c++','-std=c++20',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
