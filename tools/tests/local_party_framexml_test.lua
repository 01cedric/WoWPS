assert(GetNumPartyMembers()==7 and InviteUnit('Someone')=='online-invite-Someone')
local commands={}
function __WoWPSLocalPartyCommand(...)commands[#commands+1]={...};return true end
local function last(action,name,token)local c=commands[#commands];assert(c[1]==action and c[2]==(name or '') and c[3]==(token or 0))end
__WoWPSLocal={bankOwner='2',party={id=0,inviteId=12,inviter='Host',members={}}}
assert(GetNumPartyMembers()==0 and not IsInGroup() and not UnitExists('party1') and UnitHealth('party1')==0)
AcceptGroup();last('accept','',12);DeclineGroup();last('decline','',12);DeclineInvite('Host');last('decline','',12)
local host={guid='1',unitGuid='0x0000000000000001',name='Host',health=70,maxHealth=100,power=10,maxPower=100,powerType=1,level=5,class=1,race=1,dead=false,sameInstance=true,inRange=true}
local own={guid='2',unitGuid='0x0000000000000002',name='Guest',health=55,maxHealth=100,power=8,maxPower=100,powerType=0,level=5,class=5,race=1,dead=false,sameInstance=true,inRange=true}
local friend={guid='3',unitGuid='0x0000000000000003',name='Friend',health=0,maxHealth=80,power=0,maxPower=100,powerType=3,level=4,class=4,race=3,dead=true,sameInstance=false,inRange=false}
__WoWPSLocal.party={id=9,inviteId=0,leader='1',targetGuid='3',members={host,own,friend}}
assert(IsInGroup() and GetNumPartyMembers()==2 and GetNumGroupMembers()==3 and GetNumRaidMembers()==0 and not IsInRaid())
assert(UnitName('party1')=='Host' and UnitName('party2')=='Friend' and UnitName('player')=='Guest' and UnitName('target')=='Friend')
assert(UnitExists('party1') and UnitIsConnected('party1') and UnitIsPlayer('party1') and UnitInParty('party1'))
assert(not UnitInParty('npc') and not UnitExists('party3') and UnitName('party3')==nil)
assert(UnitHealth('party1')==70 and UnitHealthMax('party1')==100 and UnitMana('party1')==10 and UnitPowerMax('party1')==100)
assert(UnitGUID('party1')=='0x0000000000000001' and UnitIsUnit('party2','target') and not UnitIsUnit('party1','party2'))
local localized,token,id=UnitClass('party2');assert(localized=='Rogue' and token=='ROGUE' and id==4)
local race,raceToken=UnitRace('party2');assert(race=='Dwarf' and raceToken=='Dwarf')
assert(UnitIsDead('party2') and UnitIsDeadOrGhost('party2') and not UnitIsGhost('party2'))
assert(UnitPowerType('party1')==1 and UnitIsFriend('player','party1') and not UnitCanAttack('player','party1'))
local range,checked=UnitInRange('party2');assert(not range and checked and not UnitIsVisible('party2'))
assert(not IsPartyLeader() and UnitIsPartyLeader('party1') and GetPartyLeaderIndex()==1)
InviteUnit('NewPlayer');last('invite','NewPlayer');UninviteUnit('party2');last('remove','Friend')
PromoteToLeader('party1');last('promote','Host');TargetUnit('party2');last('target','Friend')
assert(TargetUnit('npc')=='online-target-npc');LeaveParty();last('leave')
assert(GetLootMethod()=='roundrobin' and GetLootThreshold()==2)
assert(not ConvertToRaid() and not SetLootMethod('group'))
__WoWPSLocal.party.members={own,host,friend};__WoWPSLocal.party.leader='2'
assert(IsPartyLeader() and GetPartyLeaderIndex()==0 and UnitName('party1')=='Host')
__WoWPSLocal.party={id=0,inviteId=0,members={}}
assert(GetLootMethod()=='freeforall')
local before=#commands
assert(not InviteUnit('party4') and not UninviteUnit('party4') and not PromoteToLeader('party4'))
assert(not InviteUnit(nil) and not UninviteUnit('') and #commands==before)
UninviteUnit('PendingPlayer');last('remove','PendingPlayer')
print('PASS party Lua withdrawal: named pending invite routes to uninvite, missing party units and empty names emit no command')
assert(not IsInGroup() and not UnitExists('party1') and UnitHealth('party1')==0)
__WoWPSLocal=nil
assert(GetNumPartyMembers()==7 and InviteUnit('Someone')=='online-invite-Someone' and UnitHealth('player')==999)
print('PASS party Lua: invite/accept/decline/leave/remove/promote/target routing; leader indices; membership, name, GUID, class, race, health, resources, death, range and empty-slot APIs; unsupported rules blocked; connected fallbacks')
