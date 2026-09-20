assert(RetrieveCorpse()=='online-reclaim' and RepopMe()=='online-release')
local last
function __WoWPSLocalCommand(name,id,choice)last=name;return true end
__WoWPSLocal={dead=true,ghost=false,bankOwner='1',party={members={}}}
assert(UnitIsDead('player') and not UnitIsGhost('player') and UnitIsDeadOrGhost('player'))
RepopMe();assert(last=='respawn')
__WoWPSLocal.ghost=true
assert(not UnitIsDead('player') and UnitIsGhost('player') and UnitIsDeadOrGhost('player'))
RetrieveCorpse();assert(last=='reclaimcorpse')
assert(UnitIsDead('target') and not UnitIsGhost('target'))
local own={guid='1',dead=true,ghost=true};local remote={guid='2',dead=true,ghost=true}
__WoWPSLocal.party.members={own,remote}
assert(UnitIsGhost('player') and not UnitIsDead('player') and UnitIsDeadOrGhost('player'))
assert(UnitIsGhost('party1') and not UnitIsDead('party1') and UnitIsDeadOrGhost('party1'))
own.ghost=false;assert(UnitIsDead('player') and not UnitIsGhost('player'))
own.dead=false;__WoWPSLocal.dead=false;__WoWPSLocal.ghost=false
assert(not UnitIsDead('player') and not UnitIsGhost('player') and not UnitIsDeadOrGhost('player'))
__WoWPSLocal=nil
assert(RetrieveCorpse()=='online-reclaim' and RepopMe()=='online-release' and UnitIsDead('target'))
print('PASS production FrameXML death/release/ghost/reclaim command routing, local and party ghost states, connected-server fallbacks')
