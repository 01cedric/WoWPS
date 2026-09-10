#pragma once
namespace wowee::addons {
inline constexpr const char* kLocalPartyFrameXmlLua=R"lua(
local function state()return __WoWPSLocal end
local function party()local s=state();return s and s.party end
local function wrap(name,fn)
    local previous=rawget(_G,name)
    _G[name]=function(...)if state() then return fn(previous,...) end
        if type(previous)=='function' then return previous(...) end end
end
local function others()
    local out={};local p=party()
    for _,m in ipairs(p and p.members or {})do if m.guid~=state().bankOwner then out[#out+1]=m end end
    return out
end
local function member(unit)
    local p=party();if not p then return end
    if unit=='player' then for _,m in ipairs(p.members)do if m.guid==state().bankOwner then return m end end end
    local i=type(unit)=='string' and tonumber(unit:match('^party([1-4])$'))
    if i then return others()[i] end
    if unit=='target' then for _,m in ipairs(p.members)do if m.guid==p.targetGuid then return m end end end
end
local function isPartyUnit(unit)return type(unit)=='string' and unit:match('^party[1-4]$')~=nil end
local function command(action,name,token)return __WoWPSLocalPartyCommand(action,name or '',token or 0)end
local function resolveName(value)
    local m=member(value);if m then return m.name end
    if isPartyUnit(value) then return nil end
    if value=='target' then return UnitName('target') end
    return value
end
local function namedCommand(action,name)
    name=resolveName(name)
    if type(name)~='string' or name=='' then return false end
    return command(action,name)
end
wrap('InviteUnit',function(_,name)return namedCommand('invite',name)end)
wrap('AcceptGroup',function()local p=party();return p and command('accept','',p.inviteId)end)
local function decline()local p=party();return p and command('decline','',p.inviteId)end
wrap('DeclineGroup',decline);wrap('DeclineInvite',decline)
wrap('LeaveParty',function()return command('leave')end)
wrap('UninviteUnit',function(_,name)return namedCommand('remove',name)end)
wrap('PromoteToLeader',function(_,name)return namedCommand('promote',name)end)
wrap('GetNumPartyMembers',function()return #others()end)
wrap('GetRealNumPartyMembers',function()return #others()end)
wrap('GetNumSubgroupMembers',function()return #others()end)
wrap('GetNumGroupMembers',function()local p=party();return p and #p.members or 0 end)
wrap('IsInGroup',function()return #others()>0 end)
wrap('IsInRaid',function()return false end)
wrap('GetNumRaidMembers',function()return 0 end)
wrap('GetRealNumRaidMembers',function()return 0 end)
wrap('IsRaidLeader',function()return false end)
wrap('IsRaidOfficer',function()return false end)
wrap('IsPartyLeader',function()local p=party();return p and p.leader==state().bankOwner or false end)
wrap('UnitIsPartyLeader',function(_,unit)local p=party();local m=member(unit or 'player');return p and m and m.guid==p.leader or false end)
wrap('UnitIsGroupLeader',function(_,unit)return UnitIsPartyLeader(unit)end)
wrap('GetPartyLeaderIndex',function()local p=party();for i,m in ipairs(others())do if m.guid==p.leader then return i end end;return 0 end)
wrap('UnitInParty',function(_,unit)return member(unit)~=nil end)
wrap('UnitPlayerOrPetInParty',function(_,unit)return member(unit)~=nil end)
wrap('UnitInRaid',function()return nil end)
wrap('UnitPlayerOrPetInRaid',function()return false end)
wrap('GetLootMethod',function()return #others()>0 and 'roundrobin' or 'freeforall',nil,nil end)
wrap('GetLootThreshold',function()return 2 end)
local function unsupported()if UIErrorsFrame then UIErrorsFrame:AddMessage('Local parties use round-robin loot. Loot rolls, other loot modes and raids are not supported yet.',1,.2,.2)end;return false end
for _,name in ipairs({'ConvertToRaid','ConvertToParty','SetLootMethod','SetLootThreshold','SetRaidSubgroup','SwapRaidSubgroup','PromoteToAssistant','DemoteAssistant'})do wrap(name,unsupported)end
local classes={'WARRIOR','PALADIN','HUNTER','ROGUE','PRIEST','DEATHKNIGHT','SHAMAN','MAGE','WARLOCK',false,'DRUID'}
local classNames={'Warrior','Paladin','Hunter','Rogue','Priest','Death Knight','Shaman','Mage','Warlock',false,'Druid'}
local races={'Human','Orc','Dwarf','NightElf','Scourge','Tauren','Gnome','Troll',false,'BloodElf','Draenei'}
local raceNames={'Human','Orc','Dwarf','Night Elf','Undead','Tauren','Gnome','Troll',false,'Blood Elf','Draenei'}
for _,name in ipairs({'UnitName','UnitFullName','GetUnitName','UnitGUID','UnitExists','UnitHealth','UnitHealthMax','UnitMana','UnitManaMax','UnitPower','UnitPowerMax','UnitLevel','UnitClass','UnitRace','UnitIsPlayer','UnitIsConnected','UnitIsDead','UnitIsDeadOrGhost','UnitPowerType','UnitIsVisible','UnitIsGhost','UnitClassification'})do
    local api=name
    wrap(api,function(old,unit,...)
        local m=member(unit)
        if not m then
            if isPartyUnit(unit) then
                if api=='UnitHealth' or api=='UnitHealthMax' or api=='UnitMana' or api=='UnitManaMax' or api=='UnitPower' or api=='UnitPowerMax' or api=='UnitLevel' then return 0 end
                if api=='UnitExists' or api=='UnitIsPlayer' or api=='UnitIsConnected' or api=='UnitIsVisible' or api=='UnitIsDead' or api=='UnitIsDeadOrGhost' or api=='UnitIsGhost' then return false end
                return nil
            end
            if old then return old(unit,...)end;return nil
        end
        if api=='UnitName' or api=='UnitFullName' or api=='GetUnitName' then return m.name,nil end
        if api=='UnitGUID' then return m.unitGuid end
        if api=='UnitHealth' then return m.health end
        if api=='UnitHealthMax' then return m.maxHealth end
        if api=='UnitMana' or api=='UnitPower' then return m.power end
        if api=='UnitManaMax' or api=='UnitPowerMax' then return m.maxPower end
        if api=='UnitLevel' then return m.level end
        if api=='UnitClass' then return classNames[m.class],classes[m.class],m.class end
        if api=='UnitRace' then return raceNames[m.race],races[m.race],m.race end
        if api=='UnitIsDead' or api=='UnitIsDeadOrGhost' then return m.dead end
        if api=='UnitPowerType' then return m.powerType,({[0]='MANA',[1]='RAGE',[3]='ENERGY',[6]='RUNIC_POWER'})[m.powerType] end
        if api=='UnitIsVisible' then return m.sameInstance end
        if api=='UnitIsGhost' then return false end
        if api=='UnitClassification' then return 'normal' end
        return true
    end)
end
wrap('UnitInRange',function(old,unit,...)local m=member(unit);if m then return m.inRange,true end;if isPartyUnit(unit)then return false,true end;if old then return old(unit,...)end end)
wrap('UnitIsUnit',function(old,a,b)local x,y=member(a),member(b);if x and y then return x.guid==y.guid end;if isPartyUnit(a) or isPartyUnit(b)then return false end;if old then return old(a,b)end end)
wrap('UnitIsFriend',function(old,a,b)if member(a) and member(b)then return true end;if old then return old(a,b)end end)
wrap('UnitCanAttack',function(old,a,b)if member(a) and member(b)then return false end;if old then return old(a,b)end end)
wrap('TargetUnit',function(old,unit,...)local m=member(unit);if m then return command('target',m.name)end;if isPartyUnit(unit)then return false end;if old then return old(unit,...)end end)
)lua";
}
