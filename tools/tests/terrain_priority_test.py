#!/usr/bin/env python3
"""Execute production queue admission and post-ground priority with fake tile records."""
from pathlib import Path
import os,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'src/rendering/terrain_manager.cpp').read_text();h=(root/'include/rendering/terrain_manager.hpp').read_text()
coord=h[h.index('struct TileCoord {'):h.index('// One MODF placement')]
enqueue=s[s.index('bool TerrainManager::enqueueTile('):s.index('std::shared_ptr<PendingTile> TerrainManager::prepareTile(')]
commit=s[s.index('        // Now safe to remove from pendingTiles'):s.index('        LOG_DEBUG("  Finalized tile')]
code=r'''
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <cassert>
#include <cstdio>
'''+coord+r'''
struct TerrainTile { bool objectsIncomplete=true;std::chrono::steady_clock::time_point nextObjectRetry{}; };
struct TerrainManager {
    std::unordered_map<TileCoord,std::unique_ptr<TerrainTile>,TileCoord::Hash> loadedTiles;
    std::unordered_map<TileCoord,bool,TileCoord::Hash> pendingTiles;
    std::unordered_set<TileCoord,TileCoord::Hash> failedTiles,objectRepairRequests_;
    std::deque<TileCoord> loadQueue;
    std::mutex queueMutex;std::condition_variable queueCV;
    TileCoord currentTile{1,1};
    bool enqueueTile(int,int,bool);
    void commitGround(TileCoord coord,bool objectsOnly=false) {
        struct Pending { bool objectsOnly; } storage{objectsOnly};auto* pending=&storage;
'''+commit+r'''
    }
};
'''+enqueue+r'''
int main() {
    TerrainManager manager;const TileCoord close{1,1},far{4,4},intro{8,8};
    assert(manager.enqueueTile(4,4,false));assert(manager.enqueueTile(1,1,false));
    assert(manager.enqueueTile(1,1,true));assert(manager.loadQueue.front()==close&&manager.loadQueue.size()==2);
    manager.loadQueue.pop_front();manager.loadedTiles[close]=std::make_unique<TerrainTile>();
    manager.commitGround(close);assert(manager.loadQueue.front()==close&&manager.objectRepairRequests_.count(close));
    // Scene-critical object pass preempts distant ground immediately.
    manager.loadQueue.pop_front();manager.pendingTiles.erase(close);manager.objectRepairRequests_.erase(close);
    manager.loadedTiles[intro]=std::make_unique<TerrainTile>();
    assert(manager.enqueueTile(8,8,true));assert(manager.loadQueue.front()==intro&&manager.objectRepairRequests_.count(intro));
    assert(manager.enqueueTile(8,8,true));assert(manager.loadQueue.size()==2); // no duplicate active job
    manager.loadQueue.pop_front();manager.pendingTiles.erase(intro);manager.objectRepairRequests_.erase(intro);
    assert(manager.enqueueTile(8,8,true));assert(manager.loadQueue.front()==far); // actual failed-repair retry delay retained
    manager.commitGround(intro,true);assert(manager.loadQueue.front()==far); // repair completion does not requeue itself
    assert(!manager.enqueueTile(-1,2,true));
    puts("PASS production terrain queues: critical ground promotion, immediate current-tile objects, intro scene priority, duplicate suppression, failed-repair backoff, no repair self-loop");
}
'''
with tempfile.TemporaryDirectory(prefix='wowps-terrain-priority-') as tmp:
    path=Path(tmp)/'test.cpp';path.write_text(code);exe=Path(tmp)/'test'
    subprocess.run(shlex.split(os.environ.get('CXX','g++'))+['-std=c++20','-O1','-g','-DWOWEE_PS4','-fsanitize=address,undefined',str(path),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
