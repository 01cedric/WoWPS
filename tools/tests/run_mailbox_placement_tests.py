#!/usr/bin/env python3
"""Compile actual placement and host reach functions; verify curated provenance."""
from pathlib import Path
import json
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
def extract(path, signature):
 s=(ROOT/path).read_text(); a=s.index(signature); b=s.index('{',a)+1; depth=1
 while depth:
  depth+=(s[b]=='{')-(s[b]=='}'); b+=1
 return s[a:b]+'\n'
placement=extract('src/game/local_services.cpp','const std::vector<LocalMailboxSite>& localMailboxSites(')
nearby=extract('src/game/local_services.cpp','const LocalMailboxSite* nearbyLocalMailbox(')
host=extract('src/game/local_mail_authority.inc','bool mailReach(')
client=extract('src/game/local_realm.cpp','bool LocalRealm::mailAccess(')
assert 'innkeeper' not in host and 'innkeeper' not in client
assert 's.x+3.f' not in placement and 'query3D' not in placement
assert "'mail_open'" not in (ROOT/'include/addons/local_services_framexml_lua.hpp').read_text()
assert 'for(const auto& m:game::localMailboxSites' in (ROOT/'src/core/application_local_realm.cpp').read_text()
rows=json.loads((ROOT/'assets/local_realm/mailbox_sites.json').read_text())['sites']
assert len({r['guid'] for r in rows})==len(rows)
# The selected source rows retain original XYZ/yaw; no arithmetic offset is baked.
expected='\n'.join('check(%d,%d,%.8ff,%.8ff,%.8ff,%.8ff);'%(r['guid'],r['map'],r['position_x'],r['position_y'],r['position_z'],r['orientation']) for r in rows)
code='''#include "game/local_mailbox_sites.hpp"
#include "game/local_services.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <algorithm>
namespace wowee::game {
'''+placement+nearby+'''
struct Authority {
 struct Gameplay { LocalWorldContent data; const LocalWorldContent& content()const{return data;} } gameplay;
 bool tradeAvailable(const LocalRealmPlayer&)const{return true;}
'''+host+'''};
} // namespace wowee::game
using namespace wowee::game;
void check(uint64_t sourceGuid,uint32_t map,float x,float y,float z,float yaw) {
 auto it=std::find_if(kLocalMailboxSites.begin(),kLocalMailboxSites.end(),[&](auto& m){return m.guid==(0x0A1D000000000000ULL|sourceGuid);});
 assert(it!=kLocalMailboxSites.end());
 assert(it->mapId==map && it->x==x && it->y==y && it->z==z && it->orientation==yaw);
}
int main(){
'''+expected+'''
 Authority host;
 LocalWorldContent guest;
 LocalRealmPlayer p{};
 for(auto site:kLocalMailboxSites){
  p.mapId=site.mapId;p.x=site.x;p.y=site.y;p.z=site.z;
  assert(nearbyLocalMailbox(host.gameplay.data,p,site.guid));
  assert(nearbyLocalMailbox(guest,p,site.guid));
  assert(host.mailReach(p,site.guid));
  assert(!host.mailReach(p,0x0A1B000000000123ULL)); // retired innkeeper synthetic GUID
  p.x=site.x+5;assert(host.mailReach(p,site.guid));
  p.x=site.x+5.02f;assert(!host.mailReach(p,site.guid));
  p.x=site.x;p.z=site.z+5.02f;assert(!host.mailReach(p,site.guid));p.z=site.z;
  p.dead=true;assert(!host.mailReach(p,site.guid));p.dead=false;
  p.instanceId=1;assert(!host.mailReach(p,site.guid));p.instanceId=0;
  p.flight.active=true;assert(!host.mailReach(p,site.guid));p.flight.active=false;
  p.mapId=999;assert(!host.mailReach(p,site.guid));p.mapId=site.mapId;
  p.x+=500;assert(!host.mailReach(p,site.guid));p.x=site.x;
  assert(host.mailReach(p,site.guid)); // stream-out, map-change and return retain identity
 }
 p.mapId=0;p.x=-8943;p.y=-132;p.z=83.6f;assert(localMailboxSites(guest,p).empty());
 p.mapId=530;p.x=10345;p.y=-6362;p.z=33.4f;assert(localMailboxSites(guest,p).empty());
 p.mapId=0;p.x=0;p.y=0;p.z=0;assert(localMailboxSites(guest,p).empty());
 puts("PASS authored mailbox coordinates/yaw/IDs; identical authority and visual-site list; host/guest lookup; exact five-yard 3D range; map/instance/death/flight restrictions; stream return; retired inn GUID and synthetic starter rejection. No innkeeper gossip mail option. CPU source regression, not console doorway geometry.");
}
'''
with tempfile.TemporaryDirectory(prefix='mailboxes--') as temp:
 cpp=Path(temp)/'mail.cpp';exe=Path(temp)/'mail';cpp.write_text(code)
 subprocess.run([os.getenv('CXX','c++'),'-std=c++20','-O1','-I'+str(ROOT/'include'),'-I'+str(ROOT/'extern'),'-I'+str(ROOT/'extern/glm'),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
