"""Exercise production instance/scene checks against renderer reset and partial loads."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
def function(file, signature):
    text=(root/file).read_text(); start=text.index(signature); i=text.index('{',start); depth=1; end=i+1
    while depth:
        if text[end]=='{': depth+=1
        if text[end]=='}': depth-=1
        end+=1
    return text[start:end]
code=r'''
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <memory>
#include <iostream>
struct ModelRenderer { std::set<unsigned> live; bool hasInstance(unsigned id) const {return live.count(id);}};
struct Renderer {ModelRenderer m,w; bool mAvailable=true,wAvailable=true;
const ModelRenderer* getM2Renderer() const{return mAvailable?&m:nullptr;}
const ModelRenderer* getWMORenderer() const{return wAvailable?&w:nullptr;}};
struct EntitySpawner {struct Info{unsigned modelId,instanceId;bool isWmo;};
std::map<uint64_t,Info> gameObjectInstances_;Renderer* renderer_=nullptr;bool isGameObjectSpawned(uint64_t) const;};
struct TerrainManager {struct Tile{bool objectsIncomplete=false;};
std::map<int,std::unique_ptr<Tile>> loadedTiles;
int worldToTile(float x,float) const{return int(x);}
bool isTileSceneReadyAt(float,float)const;};
'''
code+=function('src/core/entity_spawner.cpp','bool EntitySpawner::isGameObjectSpawned(')+'\n'
code+=function('src/rendering/terrain_manager.cpp','bool TerrainManager::isTileSceneReadyAt(')+'\n'
code+=r'''
int main(){
Renderer renderer;EntitySpawner spawner;spawner.renderer_=&renderer;
assert(!spawner.isGameObjectSpawned(10));
spawner.gameObjectInstances_[10]={1,42,false};assert(!spawner.isGameObjectSpawned(10));
renderer.m.live.insert(42);assert(spawner.isGameObjectSpawned(10));
renderer.m.live.clear();assert(!spawner.isGameObjectSpawned(10));
renderer.m.live.insert(43);assert(!spawner.isGameObjectSpawned(10));
spawner.gameObjectInstances_[10].instanceId=43;assert(spawner.isGameObjectSpawned(10));
renderer.mAvailable=false;assert(!spawner.isGameObjectSpawned(10));
spawner.gameObjectInstances_[20]={2,8,true};renderer.w.live.insert(8);assert(spawner.isGameObjectSpawned(20));
renderer.w.live.clear();assert(!spawner.isGameObjectSpawned(20));
spawner.renderer_=nullptr;assert(!spawner.isGameObjectSpawned(20));
std::cout<<"PASS live M2/WMO handles: absent, present, renderer reset, respawn and renderer teardown\n";
TerrainManager terrain;assert(!terrain.isTileSceneReadyAt(1,0));
terrain.loadedTiles[1]=std::make_unique<TerrainManager::Tile>();assert(terrain.isTileSceneReadyAt(1,0));
terrain.loadedTiles[1]->objectsIncomplete=true;assert(!terrain.isTileSceneReadyAt(1,0));
terrain.loadedTiles[1]->objectsIncomplete=false;assert(terrain.isTileSceneReadyAt(1,0));
terrain.loadedTiles.clear();assert(!terrain.isTileSceneReadyAt(1,0));
std::cout<<"PASS cinematic scene readiness: missing terrain, incomplete scenery, successful repair and unload\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
