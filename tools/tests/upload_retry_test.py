"""Run the production synchronous spawn/retry branch with injected upload failures."""
from pathlib import Path
import subprocess, tempfile
p=Path(__file__).resolve().parents[2]
s=(p/'src/core/entity_spawner_processing.cpp').read_text()
a=s.index('        // Cached WMO or M2 - spawn synchronously (cheap)',s.index('void EntitySpawner::processGameObjectSpawnQueue()'))
b=s.index('\n    }\n}',a)
branch=s[a:b]
a=s.index('        if (const auto retry = gameObjectUploadRetryAt_.find(s.displayId);',s.index('void EntitySpawner::processGameObjectSpawnQueue()'))
b=s.index('        // Check if this is an uncached WMO',a)
ready=s[a:b]
code=r'''
#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <vector>
#include <cassert>
#include <new>
#include <iostream>
#define LOG_WARNING(...) ((void)0)
struct Spawn{unsigned guid,entry,displayId;float x=0,y=0,z=0,orientation=0,scale=1;};
std::deque<Spawn> pendingGameObjectSpawns_;
std::map<unsigned,std::chrono::steady_clock::time_point> gameObjectUploadRetryAt_;
std::vector<unsigned> attempts;
int failure=0;
void spawnOnlineGameObject(unsigned guid,unsigned,unsigned display,float,float,float,float,float){
 attempts.push_back(guid);
 if(guid==1 && failure==1) gameObjectUploadRetryAt_[display]=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 if(guid==1 && failure==2) throw std::bad_alloc();
}
void frame(){size_t remaining=pendingGameObjectSpawns_.size();
 while(!pendingGameObjectSpawns_.empty() && remaining--){auto& s=pendingGameObjectSpawns_.front();
'''+ready+branch+r'''
}}
int main(){
 for(int mode:{1,2}){
  attempts.clear();gameObjectUploadRetryAt_.clear();pendingGameObjectSpawns_={{1,142075,1907},{2,1,3}};failure=mode;
  frame();assert((attempts==std::vector<unsigned>{1,2}));assert(pendingGameObjectSpawns_.size()==1);
  assert(pendingGameObjectSpawns_.front().guid==1);frame();assert(attempts.size()==2);
  gameObjectUploadRetryAt_[1907]=std::chrono::steady_clock::now()-std::chrono::seconds(1);
  failure=0;frame();assert((attempts==std::vector<unsigned>{1,2,1}));
  assert(pendingGameObjectSpawns_.empty() && gameObjectUploadRetryAt_.empty());
 }
 std::cout<<"PASS production spawn retries: failed upload and bad_alloc retain request, yield to other objects, delay retries and recover\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 t=Path(tmp);(t/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror',str(t/'test.cpp'),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True)
