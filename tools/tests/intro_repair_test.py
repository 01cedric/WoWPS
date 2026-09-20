"""Exercise the production tile enqueue policy with a deterministic worker queue."""
from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parents[2]
s=(p/'src/rendering/terrain_manager.cpp').read_text();a=s.index('bool TerrainManager::enqueueTile(');b=s.index('\nstd::shared_ptr<PendingTile>',a)
code=r'''
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <cassert>
#include <iostream>
struct TileCoord {int x,y;auto operator<=>(const TileCoord&)const=default;};
struct TerrainManager {
struct Tile{bool objectsIncomplete=false;std::chrono::steady_clock::time_point nextObjectRetry{};};
std::map<TileCoord,std::unique_ptr<Tile>> loadedTiles;
std::map<TileCoord,bool> pendingTiles,failedTiles;
std::set<TileCoord> objectRepairRequests_;
std::deque<TileCoord> loadQueue;std::mutex queueMutex;std::condition_variable queueCV;
bool enqueueTile(int,int,bool,bool=false);
};
'''+s[a:b]+r'''
int main(){TerrainManager t;TileCoord c{4,5};t.loadedTiles[c]=std::make_unique<TerrainManager::Tile>();
assert(t.enqueueTile(4,5,true)&&t.loadQueue.empty());
t.loadedTiles[c]->objectsIncomplete=true;assert(t.enqueueTile(4,5,false)&&t.loadQueue.empty());
assert(t.enqueueTile(4,5,true));assert(t.loadQueue.size()==1&&t.pendingTiles.count(c)&&t.objectRepairRequests_.count(c));
assert(t.enqueueTile(4,5,true)&&t.loadQueue.size()==1);
t.loadQueue.clear();t.pendingTiles.clear();t.objectRepairRequests_.clear();
assert(t.enqueueTile(4,5,true)&&t.loadQueue.empty());
t.loadedTiles[c]->nextObjectRetry=std::chrono::steady_clock::now()-std::chrono::seconds(1);
assert(t.enqueueTile(4,5,true)&&t.loadQueue.size()==1);
assert(!t.enqueueTile(-1,5,true)&&!t.enqueueTile(64,5,true));
t.failedTiles[{6,6}]=true;assert(!t.enqueueTile(6,6,true));
std::cout<<"PASS camera tile repair: complete/ordinary tiles untouched, priority repair coalesces, retry interval and bounds enforced\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 t=Path(tmp);(t/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror',str(t/'test.cpp'),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True)
