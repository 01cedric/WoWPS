#!/usr/bin/env python3
"""Generate actual pinned AC game_graveyard positions; join only zone-linked IDs."""
from pathlib import Path
import hashlib,json,math
from import_azerothcore import sql_rows,PINNED_COMMIT,REPOSITORY
root=Path(__file__).resolve().parents[2]
source=Path(__file__).with_name('game_graveyard_source.sql')
links=list(sql_rows(Path(__file__).with_name('graveyard_zone_source.sql'),'graveyard_zone'))
rows=list(sql_rows(source,'game_graveyard'));byid={r['id']:r for r in rows}
assert len(byid)==len(rows)
ids=sorted({r['id'] for r in links});assert all(i in byid for i in ids)
for i in ids:
 r=byid[i];assert 0<i<2**32 and 0<=r['map']<2**32
 assert all(math.isfinite(r[k]) and abs(r[k])<=100000 for k in ('x','y','z'))
lines=['#pragma once','#include "game/local_gameplay.hpp"','#include "game/local_graveyard_links.hpp"','#include <algorithm>','',
'namespace wowee::game {',
'// Generated from pinned AC '+PINNED_COMMIT+' game_graveyard.sql.',
'// Only IDs referenced by graveyard_zone are retained. No authored/synthetic positions.',
'struct LocalGraveyardPosition { uint32_t id, mapId; float x,y,z; };',
'inline constexpr std::array<LocalGraveyardPosition, '+str(len(ids))+'> kLocalGraveyardPositions{{']
for i in ids:
 r=byid[i];lines.append('    {'+str(i)+'u, '+str(r['map'])+'u, '+', '.join(format(float(r[k]),'.9f')+'f' for k in ('x','y','z'))+'},')
lines+=['}};',
'inline std::vector<LocalGraveyardSite> buildPinnedLocalGraveyards() {',
'    constexpr uint32_t alliance=(1u<<0)|(1u<<2)|(1u<<3)|(1u<<6)|(1u<<10);',
'    constexpr uint32_t horde=(1u<<1)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9);',
'    std::vector<LocalGraveyardSite> sites; sites.reserve(kLocalGraveyardLinks.size());',
'    for (const auto& link : kLocalGraveyardLinks) {',
'        auto it=std::lower_bound(kLocalGraveyardPositions.begin(),kLocalGraveyardPositions.end(),link.id,',
'            [](const auto& p,uint32_t id){return p.id<id;});',
'        if(it==kLocalGraveyardPositions.end()||it->id!=link.id) continue;',
'        LocalGraveyardSite site; site.id=link.id;site.mapId=it->mapId;site.zoneId=link.zoneId;',
'        site.raceMask=link.team==469?alliance:link.team==67?horde:0;',
'        site.x=it->x;site.y=it->y;site.z=it->z;',
'        // Neither upstream coordinate table supplies facing; retain default orientation zero.',
'        sites.push_back(site);',
'    }',
'    return sites;',
'}','} // namespace wowee::game','']
(root/'include/game/local_graveyard_sites.hpp').write_text('\n'.join(lines))
report={'upstream':REPOSITORY,'commit':PINNED_COMMIT,'sourceURL':REPOSITORY+'/blob/'+PINNED_COMMIT+'/data/sql/base/db_world/game_graveyard.sql','sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'sourceRows':len(rows),'linkedPositions':len(ids),'joinedZoneTeamSites':len(links),'missingLinkedIds':[],'coordinates':'Unmodified upstream map/x/y/z. No client DBC required.','orientation':'Zero default: upstream has no facing field.','eligibility':'Preserve every (graveyardID,zoneId,team) link separately.'}
(root/'assets/local_realm/graveyard_sites.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
