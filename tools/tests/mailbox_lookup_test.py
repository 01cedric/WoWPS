"""Inject unavailable/empty/replaced DBCs into the production GO lookup lifecycle."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'src/core/entity_spawner.cpp').read_text()
def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
code = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#define LOG_INFO(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
namespace pipeline {
struct Layout { unsigned operator[](const char* key) const { return std::string(key)=="ID" ? 4u : 5u; } };
struct Layouts { Layout layout; const Layout* getLayout(const char*) const { return &layout; } };
Layouts layouts;
const Layouts* getActiveDBCLayout() { return &layouts; }
std::string modelPathToM2(const std::string& path) { return path; }
}
struct DBC {
    std::vector<std::pair<unsigned, std::string>> rows;
    bool isLoaded() const { return true; }
    unsigned getRecordCount() const { return rows.size(); }
    unsigned getUInt32(unsigned row,unsigned field) const { assert(field==4); return rows.at(row).first; }
    std::string getString(unsigned row,unsigned field) const { assert(field==5); return rows.at(row).second; }
};
struct Assets {
    bool ready=false;
    unsigned reads=0;
    std::shared_ptr<DBC> dbc;
    bool isInitialized() const { return ready; }
    std::shared_ptr<DBC> loadDBC(const char*) { ++reads; return dbc; }
};
struct EntitySpawner {
    Assets* assetManager_=nullptr;
    std::unordered_map<uint32_t,std::string> gameObjectDisplayIdToPath_;
    bool gameObjectLookupsBuilt_=false;
    uint32_t localMailboxDisplayId_=0,gameObjectLookupAttempts_=0;
    std::chrono::steady_clock::time_point gameObjectLookupRetryAt_{};
    void invalidateGameObjectDisplayLookups();
    void buildGameObjectDisplayLookups();
    uint32_t localMailboxDisplayId();
    std::string getGameObjectModelPathForDisplayId(uint32_t) const;
};
'''
for signature in ['void EntitySpawner::invalidateGameObjectDisplayLookups(',
                  'uint32_t EntitySpawner::localMailboxDisplayId(',
                  'void EntitySpawner::buildGameObjectDisplayLookups(',
                  'std::string EntitySpawner::getGameObjectModelPathForDisplayId(']:
    code += function(signature) + '\n'
code += r'''
int main() {
    Assets assets; EntitySpawner s; s.assetManager_=&assets;
    assert(s.localMailboxDisplayId()==0 && assets.reads==0);
    assets.ready=true;
    assert(s.localMailboxDisplayId()==0 && !s.gameObjectLookupsBuilt_ && assets.reads==1);
    for(int i=0;i<100;++i) assert(s.localMailboxDisplayId()==0);
    assert(assets.reads==1); // unavailable data cannot trigger a scan each frame
    assets.dbc=std::make_shared<DBC>();
    s.gameObjectLookupRetryAt_={};
    assert(s.localMailboxDisplayId()==0 && !s.gameObjectLookupsBuilt_ && assets.reads==2);
    assets.dbc->rows={{8,"World/PostBoxHuman.m2"},{2,"World/PostBoxHuman.m2"},{4,"Chest.m2"}};
    s.gameObjectLookupRetryAt_={};
    assert(s.localMailboxDisplayId()==2 && s.gameObjectLookupsBuilt_);
    assert(s.getGameObjectModelPathForDisplayId(2)=="World/PostBoxHuman.m2");
    assert(s.gameObjectLookupAttempts_==3 && assets.reads==3);
    for(int i=0;i<100;++i) assert(s.localMailboxDisplayId()==2);
    assert(assets.reads==3);
    assets.dbc->rows={{22,"Different/PostBoxHuman.m2"}};
    s.invalidateGameObjectDisplayLookups();
    assert(s.getGameObjectModelPathForDisplayId(2).empty());
    assert(s.localMailboxDisplayId()==22 && assets.reads==4);
    assets.dbc->rows={{9,"Chest.m2"}};
    s.invalidateGameObjectDisplayLookups();
    assert(s.localMailboxDisplayId()==0 && s.gameObjectLookupsBuilt_);
    for(int i=0;i<100;++i) assert(s.localMailboxDisplayId()==0);
    assert(assets.reads==5); // valid data without the mailbox is cached, not rescanned
    std::cout << "PASS production GO metadata: unavailable/empty retry, cooldown, shared layout, replacement reset and absent mailbox caching\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
