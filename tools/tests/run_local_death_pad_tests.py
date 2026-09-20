#!/usr/bin/env python3
"""Execute the production ghost-controller branch against an authority fixture."""
from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[2]
s=(root/'src/addons/local_framexml_input.cpp').read_text()
a=s.index('    if(auto* realm=realm_?realm_():nullptr) {',s.index('bool LocalFrameXml::navigateBars()'))
b=s.index('    // Circle cancels',a)
branch=s[a:b]
prefix=r'''
#include <functional>
#include <cassert>
#include <cstdio>
constexpr unsigned ORBIS_PAD_BUTTON_SQUARE=1,ORBIS_PAD_BUTTON_TRIANGLE=2;
constexpr int ImGuiKey_GamepadFaceLeft=1,ImGuiKey_1=2;
struct Player {bool dead=false,ghost=false;};
struct Realm { Player player;int requests=0;bool near=false;
 Player* localPlayer(){return &player;}
 bool reclaimCorpse(){++requests;if(near){player.dead=player.ghost=false;}return near;}
} authority;
namespace platform::ps4 {struct Pad {bool connected=true;unsigned pressed=0;}pad;const Pad& padState(){return pad;}}
namespace ui {int consumed=0;void noteInterfaceConsumedKey(int key){consumed|=key;}}
struct Controls {
 std::function<Realm*()> realm_=[](){return &authority;};
 struct Focus {bool active=true;void clear(){active=false;}}padFocus_;
 int actions=0;bool map=false;
 bool padWorldMapToggle(){return false;}bool padWorldMapFrame(){return map;}
 bool navigate(){
'''
suffix=r'''
 ++actions;return false;
 }
};
int main(){
 Controls c;
 authority.player={true,true};platform::ps4::pad.pressed=ORBIS_PAD_BUTTON_SQUARE;
 assert(c.navigate());assert(authority.requests==1&&authority.player.ghost);
 assert(c.actions==0&&!c.padFocus_.active&&ui::consumed==3);
 // Near corpse: same Square reclaims; no bar/NPC/combat action also runs.
 authority.near=true;assert(c.navigate());assert(!authority.player.dead&&!authority.player.ghost);
 assert(authority.requests==2&&c.actions==0);
 // Living characters retain normal world/bar input.
 c.navigate();assert(c.actions==1&&authority.requests==2);
 // Unreleased body never reclaims or executes combat/bar input.
 authority.player={true,false};c.navigate();assert(authority.requests==2&&c.actions==1);
 // Ghost movement itself does not emit a recovery request; map stays usable.
 authority.player={true,true};platform::ps4::pad.pressed=0;c.map=true;
 assert(c.navigate()&&authority.requests==2);c.map=false;
 platform::ps4::pad.pressed=ORBIS_PAD_BUTTON_SQUARE|ORBIS_PAD_BUTTON_TRIANGLE;
 assert(!c.navigate()&&authority.requests==2);
 platform::ps4::pad.pressed=ORBIS_PAD_BUTTON_SQUARE;platform::ps4::pad.connected=false;
 assert(!c.navigate()&&authority.requests==2);
 puts("PASS production ghost Square priority, single command consumption, authority rejection/acceptance, dead-body gate, living input, map access and disconnected pad");
}
'''
with tempfile.TemporaryDirectory(prefix='death-pad-') as d:
 p=Path(d);(p/'test.cpp').write_text(prefix+branch+suffix)
 subprocess.run([os.getenv('CXX','c++'),'-std=c++20','-O1','-g','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
