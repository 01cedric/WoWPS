#!/usr/bin/env python3
"""Compile the production enqueueTile implementation with a deterministic queue fixture."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/rendering/terrain_manager.cpp').read_text()
start = source.index('bool TerrainManager::enqueueTile(')
end = source.index('\nstd::shared_ptr<PendingTile>', start)
function = source[start:end]
harness = r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
struct TileCoord {
 int x,y;
 bool operator<(const TileCoord& b)const {return x<b.x||(x==b.x&&y<b.y);}
 bool operator==(const TileCoord& b)const{return x==b.x&&y==b.y;}
};
struct Tile { bool objectsIncomplete=true; std::chrono::steady_clock::time_point nextObjectRetry{}; };
struct TerrainManager {
 std::map<TileCoord,std::shared_ptr<Tile>> loadedTiles;
 std::set<TileCoord> failedTiles,objectRepairRequests_;
 std::map<TileCoord,bool> pendingTiles;
 std::deque<TileCoord> loadQueue;
 std::mutex queueMutex; std::condition_variable queueCV;
 bool enqueueTile(int,int,bool=false,bool=false);
};
'''
tests = r'''
int main() {
 TerrainManager t;
 const TileCoord future{20,20}, required{21,20};
 t.loadedTiles[future]=std::make_shared<Tile>();
 assert(t.enqueueTile(future.x,future.y)); // Ordinary streaming remains ground-only.
 assert(t.loadQueue.empty());
 assert(t.enqueueTile(required.x,required.y,true));
 assert(t.enqueueTile(future.x,future.y,false,true));
 assert(t.loadQueue.size()==2&&t.loadQueue.front()==required);
 assert(t.objectRepairRequests_.count(future)==1);
 for(int i=0;i<100;i++)assert(t.enqueueTile(future.x,future.y,false,true));
 assert(t.loadQueue.size()==2); // No duplicate repair per preview sample/frame.
 assert(t.enqueueTile(future.x,future.y,true));
 assert(t.loadQueue.front()==future); // Required camera outranks background work.
 t.loadQueue.pop_front(); // Worker owns repair now.
 assert(t.enqueueTile(future.x,future.y,true));
 assert(t.loadQueue.size()==1&&t.loadQueue.front()==required);
 t.pendingTiles.erase(future); // Failed repair retains existing 15-second cooldown.
 assert(t.enqueueTile(future.x,future.y,false,true));
 assert(t.loadQueue.size()==1);
 t.loadedTiles[future]->nextObjectRetry={};
 assert(t.enqueueTile(future.x,future.y,false,true));
 assert(t.loadQueue.size()==2&&t.loadQueue.back()==future);
 t.loadedTiles[future]->objectsIncomplete=false;
 t.pendingTiles.erase(future); t.loadQueue.pop_back();
 assert(t.enqueueTile(future.x,future.y,false,true)&&t.loadQueue.size()==1);
 t.failedTiles.insert({10,10});
 assert(!t.enqueueTile(10,10,false,true));
 assert(!t.enqueueTile(-1,20,false,true));
 assert(!t.enqueueTile(64,20,false,true));
}
'''
with tempfile.TemporaryDirectory() as d:
    cpp=Path(d)/'test.cpp'; binary=Path(d)/'test'
    cpp.write_text(harness+function+tests)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('PASS: production intro prefetch queue: repair, priority promotion, deduplication, active worker, cooldown, complete/failed tiles, bounds; ASan/UBSan')

# Exercise the exact application lookahead lambda with deterministic tile readiness.
application = (root / 'src/core/application_character_intro.cpp').read_text()
a = application.index('    const auto prewarm = [&]')
b = application.index('    for (const uint32_t leadMs', a)
branch = application[a:b]
fixture = r'''
#include <cassert>
#include <cmath>
#include <utility>
namespace glm {struct vec3 {float x,y,z;};}
namespace coords {std::pair<int,int> worldToTile(float x,float y){return {int(x),int(y)};}}
bool usablePosition(const glm::vec3& p){return std::isfinite(p.x)&&std::isfinite(p.y);}
struct Terrain {bool accepted=true,sceneReady=false;int requests=0;bool isTileSceneReadyAt(float,float){return sceneReady;}};
bool requestPosition(Terrain& t,const glm::vec3&,bool priority,bool repair){assert(!priority&&repair);++t.requests;return t.accepted;}
int main(){
 Terrain data;auto* terrain=&data;
 const auto activeTile=std::pair{10,10};bool preparingShot=true,entryCorridorReady=true;
''' + branch + r'''
 prewarm({10,11,0},true);assert(!entryCorridorReady&&data.requests==1);
 entryCorridorReady=true;prewarm({12,10,0},true);assert(entryCorridorReady&&data.requests==1);
 prewarm({10,10,0});assert(entryCorridorReady&&data.requests==2);
 preparingShot=false;prewarm({10,10,0},true);assert(entryCorridorReady&&data.requests==3);
 preparingShot=true;data.accepted=false;prewarm({10,10,0},true);assert(entryCorridorReady);
 data.accepted=true;data.sceneReady=true;prewarm({10,10,0},true);assert(entryCorridorReady);
 prewarm({NAN,10,0},true);assert(data.requests==5);
}
'''
with tempfile.TemporaryDirectory() as d:
    cpp=Path(d)/'corridor.cpp';binary=Path(d)/'corridor'
    cpp.write_text(fixture)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('PASS: production lookahead: entry readiness, 3x3 bounds, speculative repair, playback never gated by future-only tile, failed requests excluded, nonfinite position rejected')
