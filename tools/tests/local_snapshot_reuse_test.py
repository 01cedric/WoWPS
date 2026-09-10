#!/usr/bin/env python3
"""Run the real local presentation-copy methods under allocation denial.

Gameplay authority is a controlled fixture; no network/GPU execution is claimed.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/game/local_realm.cpp').read_text()
def function(start):
    a = source.index(start)
    # The first method-level closing brace excludes neighbouring methods added
    # by later releases. Nested blocks have deeper indentation.
    b = source.index('\n    }\n', a) + len('\n    }\n')
    return source[a:b]
methods = function('    const std::vector<LocalRealmPlayer*>& activePlayers()')
methods += function('    void refreshPlayers()')
fixture = r'''
#include "core/retained_guid_set.hpp"
#include "game/local_gameplay.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
static bool denyAllocations=false;
static size_t allocations=0;
void* operator new(size_t size) {
    if(denyAllocations) throw std::bad_alloc();
    ++allocations;
    if(auto* p=std::malloc(size?size:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
using namespace wowee::game;
struct Fixture {
    // Party authority is covered by the LAN lifecycle suite. This fixture
    // measures only presentation-copy allocation behaviour.
    void syncParty() {}
    struct Peer{uint64_t guid;};
    struct Saved{LocalRealmPlayer player;};
    struct Game {
        std::vector<LocalRealmNpc> rows;
        const auto& npcs() const{return rows;}
        bool canAttack(const LocalRealmPlayer&,const LocalRealmNpc& n)const{return n.guid%2;}
        bool isAggressive(const LocalRealmPlayer&,const LocalRealmNpc& n)const{return n.guid%3==0;}
    } gameplay;
    LocalRealmPlayer self;
    std::vector<LocalRealmPlayer> players,botPlayers;
    std::vector<LocalRealmPlayer*> activePlayerScratch;
    std::vector<LocalRealmNpc> npcView;
    std::vector<Peer> peers;
    std::vector<Saved> saved;
    Saved* findSaved(uint64_t guid){for(auto& s:saved)if(s.player.guid==guid)return &s;return nullptr;}
    const Saved* findSaved(uint64_t guid)const{for(auto& s:saved)if(s.player.guid==guid)return &s;return nullptr;}
METHODS
};
int main(){
    Fixture f;f.self.guid=1;f.self.name=std::string(40,'s');f.self.money=500;
    f.self.inventory={{117,3}};f.self.knownSpells={1,2,3};
    f.self.quests={{23,LocalQuestStatus::Active,{2,3}}};
    f.self.completedQuestIds.resize(1024,42);
    auto peer=f.self;peer.guid=2;f.saved.push_back({peer});f.peers.push_back({2});
    auto bot=f.self;bot.guid=3;f.botPlayers.push_back(bot);
    for(uint64_t i=0;i<128;++i){LocalRealmNpc n;n.guid=100+i;n.mapId=f.self.mapId;n.name=std::string(48,'n');f.gameplay.rows.push_back(n);}
    f.refreshPlayers();f.activePlayers();
    LocalRealmPlayer snapshot=f.self;
    const auto before=allocations;
    denyAllocations=true;
    for(unsigned frame=0;frame<500;++frame){
        f.self.health=frame;f.gameplay.rows[7].health=frame+1;
        f.refreshPlayers();const auto& active=f.activePlayers();snapshot=f.self;
        assert(active.size()==3&&active[0]==&f.self&&active[1]==&f.saved[0].player);
        assert(f.players[0].health==frame&&snapshot.health==frame);
        assert(f.npcView[7].health==frame+1&&f.npcView[7].hostile);
        assert(f.players[0].money==500&&f.players[0].inventory[0].count==3);
    }
    denyAllocations=false;assert(allocations==before);
    std::cout<<"PASS actual refreshPlayers/activePlayers and snapshot: 500 warmed frames, zero allocations, unchanged authority and current presentation values\n";
    // A bigger roster may still allocate. Failed view growth cannot change
    // authority inventory/gold or retain pointers into temporary player copies.
    f.players.shrink_to_fit();auto newPeer=peer;newPeer.guid=4;
    f.saved.push_back({newPeer});f.peers.push_back({4});
    bool failed=false;denyAllocations=true;
    try{f.refreshPlayers();}catch(const std::bad_alloc&){failed=true;}
    denyAllocations=false;assert(failed&&f.self.money==500&&f.self.inventory[0].count==3);
    f.refreshPlayers();assert(f.players.size()==4);
    f.gameplay.rows.resize(5);f.peers.clear();f.botPlayers.clear();f.refreshPlayers();
    assert(f.players.size()==1&&f.npcView.size()==5&&f.activePlayers().size()==1);
    std::cout<<"PASS roster growth failure preserves authority; retry and departed rows converge\n";
    wowee::core::RetainedGuidSet remote,npcs,transports,scratch;
    auto cycle=[&]{
        for(auto* destination:{&remote,&npcs,&transports}){
            scratch.clear();for(uint64_t i=1;i<=128;++i)scratch.insert(i);
            scratch.insert(64);assert(scratch.size()==128);destination->swap(scratch);
            assert(destination->count(1)&&destination->count(128)&&!destination->count(129));
        }
    };
    for(unsigned i=0;i<4;++i)cycle();const auto guidBefore=allocations;
    denyAllocations=true;for(unsigned i=0;i<1000;++i)cycle();denyAllocations=false;
    assert(guidBefore==allocations);remote.release();assert(remote.size()==0);
    std::cout<<"PASS retained actor rosters: 1000 warmed frames, zero allocations, deduplication/membership/session release\n";
}
'''.replace('METHODS', methods)
directory = Path(tempfile.mkdtemp(prefix='wowps-snapshots-'))
cpp = directory / 'fixture.cpp'
cpp.write_text(fixture)
binary = directory / 'fixture'
command = [os.environ.get('CXX', 'g++'), '-std=c++20', '-O1', '-g',
           '-I'+str(root/'include'), '-I'+str(root/'extern/glm'),
           str(cpp), '-o', str(binary)]
if os.environ.get('SANITIZE','1') == '1':
    command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
subprocess.run(command, check=True)
env = dict(os.environ)
env.setdefault('ASAN_OPTIONS','detect_leaks=0')
subprocess.run([str(binary)], check=True, env=env)
print('Test executable:', binary)
