#pragma once
namespace wowee::addons {
inline constexpr const char* kLocalFrameXmlLua=R"lua(
local function state() return __WoWPSLocal end
local function wrap(name,fn)
    local online=_G[name]
    _G[name]=function(...) if state() then return fn(...) end if online then return online(...) end end
end
local function cmd(name,id,choice) return __WoWPSLocalCommand(name,id or 0,choice or 0) end
local logSelection,abandon,watch=1,0,{}
local function quest(id) return state().quests[id or state().selected] end
local function logQuest(index) return quest(state().log[index or logSelection] or 0) end
local function text(s) return (s or ''):gsub('%$N',function() return state().name end):gsub('%$B','\n') end
local function objectives(q)
    local out={} for _,o in ipairs(q and q.objectives or {}) do out[#out+1]=o.text..': '..o.done..' / '..o.count end
    return table.concat(out,'\n')
end
local function offered(active)
    local out={} for _,q in pairs(state().quests) do if q.offered and q.active==active then out[#out+1]=q end end
    table.sort(out,function(a,b)return a.id<b.id end) return out
end
wrap('GetMoney',function()return state().money end)
wrap('UnitXP',function()return state().xp end)
wrap('UnitXPMax',function()return state().xpMax end)
local unitName=UnitName
UnitName=function(unit) if state() and unit=='npc' then return state().npcName end return unitName(unit) end
wrap('GetGossipText',function()return state().greeting or '' end)
wrap('GetGreetingText',function()return state().greeting or '' end)
wrap('GetGossipOptions',function()end)
wrap('GetNumGossipOptions',function()return 0 end)
wrap('GetNumGossipAvailableQuests',function()return #offered(false) end)
wrap('GetNumGossipActiveQuests',function()return #offered(true) end)
wrap('GetGossipAvailableQuests',function()
    local t={} for _,q in ipairs(offered(false)) do
        t[#t+1]=q.title;t[#t+1]=q.level;t[#t+1]=false;t[#t+1]=false;t[#t+1]=false
    end return unpack(t)
end)
wrap('GetGossipActiveQuests',function()
    local t={} for _,q in ipairs(offered(true)) do
        t[#t+1]=q.title;t[#t+1]=q.level;t[#t+1]=false;t[#t+1]=q.complete
    end return unpack(t)
end)
wrap('SelectGossipAvailableQuest',function(i)local q=offered(false)[i];if q then cmd('detail',q.id)end end)
wrap('SelectGossipActiveQuest',function(i)local q=offered(true)[i];if q then cmd('progress',q.id)end end)
-- These are OnHide notifications, not a user declining a quest. OnHide
-- may be deferred by the widget runtime until after the next panel opened.
wrap('CloseGossip',function()cmd('close_gossip')end)
wrap('CloseQuest',function()cmd('close_quest')end)
wrap('DeclineQuest',function()cmd('close')end)
wrap('AcceptQuest',function()cmd('accept',state().selected)end)
wrap('CompleteQuest',function()cmd('reward',state().selected)end)
wrap('GetQuestReward',function(choice)
    local q=quest();local count=q and q.choices and #q.choices or 0
    choice=choice or 0
    if type(choice)~='number' or choice~=choice or choice%1~=0 or
       (count>0 and (choice<1 or choice>count)) or (count==0 and choice~=0) then return false end
    return cmd('turnin',state().selected,choice)
end)
wrap('IsQuestCompletable',function()local q=quest();return q and q.complete or false end)
wrap('GetTitleText',function()local q=quest();return q and q.title or '' end)
wrap('GetQuestText',function()local q=quest();return text(q and q.description) end)
wrap('GetObjectiveText',function()return objectives(quest())end)
wrap('GetProgressText',function()return objectives(quest())end)
wrap('GetRewardText',function()local q=quest();return text(q and q.description)end)
wrap('GetRewardMoney',function()local q=quest();return q and q.money or 0 end)
wrap('GetRewardXP',function()local q=quest();return q and q.xp or 0 end)
local function fixed(q) return q and (q.rewards or (q.reward and {q.reward})) or {} end
local function choices(q) return q and q.choices or {} end
wrap('GetNumQuestRewards',function()return #fixed(quest()) end)
wrap('GetNumQuestChoices',function()return #choices(quest()) end)
-- The question mark is the fallback for an item whose display entry names no
-- artwork, not the answer for every item: the bridge publishes the real path.
local function icon(v) local path=v and v.icon;if path and path~='' then return path end return 'Interface\\Icons\\INV_Misc_QuestionMark' end
local function reward(r) if r then return r.name,icon(r),r.count,1,true end end
local function rewardAt(q,kind,i)
    if kind=='reward' then return fixed(q)[i] elseif kind=='choice' then return choices(q)[i] end
end
wrap('GetQuestItemInfo',function(kind,i)return reward(rewardAt(quest(),kind,i))end)
wrap('GetQuestItemLink',function(kind,i)local r=rewardAt(quest(),kind,i);if r then return 'item:'..r.id end end)
for _,n in ipairs({'GetNumQuestItems','GetQuestMoneyToGet','GetRewardHonor','GetRewardTalents','GetRewardArenaPoints',
    'GetQuestLogRequiredMoney','GetQuestLogRewardHonor','GetQuestLogRewardTalents','GetQuestLogRewardArenaPoints','GetNumQuestLogRewardFactions'}) do wrap(n,function()return 0 end)end
for _,n in ipairs({'GetRewardSpell','GetQuestRewardSpell','GetQuestRewardTitle','GetQuestLogRewardSpell','GetQuestLogRewardTitle'}) do wrap(n,function()end)end
wrap('GetNumQuestLogEntries',function()return #state().log,#state().log end)
wrap('GetQuestLogTitle',function(i)local q=logQuest(i);if q then return q.title,q.level,nil,0,false,false,q.complete and 1 or nil,false,q.id,false end end)
wrap('GetQuestLogQuestText',function()local q=logQuest();return text(q and q.description),objectives(q)end)
wrap('GetQuestLogSelection',function()return logSelection end)
wrap('SelectQuestLogEntry',function(i)logSelection=math.max(1,math.min(i or 1,#state().log))end)
wrap('GetNumQuestLeaderBoards',function(i)local q=logQuest(i);return q and #q.objectives or 0 end)
wrap('GetQuestLogLeaderBoard',function(i,index)local q=logQuest(index);local o=q and q.objectives[i];if o then return o.text..': '..o.done..' / '..o.count,o.type,o.done>=o.count end end)
wrap('GetQuestLogRewardMoney',function()local q=logQuest();return q and q.money or 0 end)
wrap('GetQuestLogRewardXP',function()local q=logQuest();return q and q.xp or 0 end)
wrap('GetNumQuestLogRewards',function()return #fixed(logQuest()) end)
wrap('GetQuestLogRewardInfo',function(i)return reward(fixed(logQuest())[i])end)
wrap('GetNumQuestLogChoices',function()return #choices(logQuest()) end)
wrap('GetQuestLogChoiceInfo',function(i)return reward(choices(logQuest())[i])end)
wrap('GetQuestLogItemLink',function(kind,i)local r=rewardAt(logQuest(),kind,i);if r then return 'item:'..r.id end end)
wrap('GetQuestLogPushable',function()return false end)
wrap('SetAbandonQuest',function()local q=logQuest();abandon=q and q.id or 0 end)
wrap('GetAbandonQuestName',function()local q=quest(abandon);return q and q.title or '' end)
wrap('AbandonQuest',function()if abandon~=0 then cmd('abandon',abandon)end end)
local function watches()local t={}for i,id in ipairs(state().log)do if watch[id]then t[#t+1]=i end end return t end
local function refreshMapQuests()
    if not CURRENT_MAP_QUESTS then return end
    for id in pairs(CURRENT_MAP_QUESTS) do CURRENT_MAP_QUESTS[id]=nil end
    -- The local log has no server POI/zone headers. Include active quests so
    -- the original remote-zone filter cannot silently discard the whole log.
    for i,id in ipairs(state().log) do CURRENT_MAP_QUESTS[id]=i end
end
function WoWPS_RefreshLocalQuestTracking()
    if not state() then return end
    local active={}
    for _,id in ipairs(state().log) do
        active[id]=true
        -- false means explicitly untracked by the player, not a new quest.
        if watch[id]==nil then watch[id]=true end
    end
    for id in pairs(watch) do if not active[id] then watch[id]=nil end end
    refreshMapQuests()
end
local function updateWatches()
    refreshMapQuests()
    if WatchFrame_Update and WatchFrame then WatchFrame_Update(WatchFrame) end
end
wrap('AddQuestWatch',function(i)local q=logQuest(i);if q then watch[q.id]=true;updateWatches() end end)
wrap('RemoveQuestWatch',function(i)local q=logQuest(i);if q then watch[q.id]=false;updateWatches() end end)
wrap('GetNumQuestWatches',function()return #watches()end)
wrap('GetQuestIndexForWatch',function(i)return watches()[i]end)
wrap('GetQuestWatchIndex',function(i)for n,row in ipairs(watches())do if row==i then return n end end end)
wrap('IsQuestWatched',function(i)local q=logQuest(i);return q and watch[q.id] or false end)
wrap('GetQuestLogCompletionText',function()return 'Return to the quest giver to complete this quest.' end)
wrap('GetQuestLogSpecialItemInfo',function()end)
wrap('GetQuestLogSpecialItemCooldown',function()return 0,0,0 end)
wrap('GetQuestLink',function(i)local q=logQuest(i);if q then return '|cffffff00|Hquest:'..q.id..':'..q.level..'|h['..q.title..']|h|r' end end)
WoWPS_RefreshLocalQuestTracking()
wrap('GetContainerNumSlots',function(b)return b==0 and 24 or 0 end)
wrap('GetContainerNumFreeSlots',function(b)local used=0;for _,v in pairs(state().bags)do if v then used=used+1 end end;return b==0 and math.max(0,24-used) or 0,0 end)
local function bag(b,i)return b==0 and state().bags[i] or nil end
wrap('GetContainerItemInfo',function(b,i)local v=bag(b,i);if v then return icon(v),v.count,false,1,false,false,'item:'..v.id end end)
wrap('GetContainerItemLink',function(b,i)local v=bag(b,i);if v then return 'item:'..v.id end end)
wrap('GetContainerItemID',function(b,i)local v=bag(b,i);return v and v.id end)
wrap('GetContainerItemCooldown',function()return 0,0,1 end)
wrap('UseContainerItem',function(b,i)local v=bag(b,i);if v then cmd(v.equip~=0 and 'equip' or 'use',v.id)end end)
-- Retail functions are defined by later FrameXML files. Bind these hooks
-- only after the source load, never to an auto-created missing-API stub.
local mapHook,bagHook
function WoWPS_InstallLocalFrameXmlHooks()
    local originalMapQuests=rawget(_G,'WatchFrame_GetCurrentMapQuests')
    if type(originalMapQuests)=='function' and originalMapQuests~=mapHook then
        mapHook=function(...)
            if state() then return refreshMapQuests() end
            return originalMapQuests(...)
        end
        WatchFrame_GetCurrentMapQuests=mapHook
    end
local originalGenerateBag=rawget(_G,'ContainerFrame_GenerateFrame')
if type(originalGenerateBag)=='function' and originalGenerateBag~=bagHook then
    bagHook=function(frame,size,id)
        if frame.wowpsBackpackRows then
            for _,region in ipairs(frame.wowpsBackpackRows) do region:Hide() end
            -- A ContainerFrame is reused for different bags. Restore the
            -- authored footer/money anchors before retail generates another.
            local name=frame:GetName()
            local footer=_G[name..'BackgroundBottom']
            if footer then
                footer:ClearAllPoints();footer:SetPoint('TOP',_G[name..'BackgroundMiddle1'],'BOTTOM',0,0)
                footer:SetHeight(10);footer:SetTexCoord(0,1,.330078125,.349609375)
            end
            local money=_G[name..'MoneyFrame']
            if money then money:ClearAllPoints();money:SetPoint('TOPRIGHT',frame,'TOPRIGHT',-2,-216) end
        end
        originalGenerateBag(frame,size,id)
        if not state() or id~=0 or size<=16 then return end
        local name=frame:GetName()
        local rows=math.ceil(size/4)
        local extra=math.max(0,rows-4)*41
        frame:SetHeight((BACKPACK_HEIGHT or 240)+extra)
        local path='Interface\\ContainerFrame\\UI-BackpackBackground'
        -- This texture already contains four 41-pixel slot rows. Stretching
        -- those pixels creates another grid underneath the real item buttons.
        -- Keep the header/footer and repeat full rows at their authored pitch.
        -- Its canvas is 256 wide while ContainerFrame is 192: retain retail's
        -- TOPRIGHT anchor, including the transparent left margin.
        local function slice(region,height,top,bottom,relative,relativePoint)
            if not region then return end
            region:SetTexture(path);region:SetHeight(height);region:SetWidth(256)
            region:SetTexCoord(0,1,top/256,bottom/256);region:ClearAllPoints()
            region:SetPoint('TOPRIGHT',relative,relativePoint,0,0);region:Show()
        end
        slice(_G[name..'BackgroundTop'],48,0,48,frame,'TOPRIGHT')
        for _,suffix in ipairs({'BackgroundMiddle1','BackgroundMiddle2'}) do
            local region=_G[name..suffix];if region then region:Hide() end
        end
        local previous=_G[name..'BackgroundTop']
        frame.wowpsBackpackRows=frame.wowpsBackpackRows or {}
        for row=1,rows do
            local region=frame.wowpsBackpackRows[row]
            if not region then
                region=frame:CreateTexture(name..'WoWPSBackpackRow'..row,'ARTWORK')
                frame.wowpsBackpackRows[row]=region
            end
            slice(region,41,48,89,previous,'BOTTOMRIGHT')
            previous=region
        end
        for row=rows+1,#frame.wowpsBackpackRows do frame.wowpsBackpackRows[row]:Hide() end
        slice(_G[name..'BackgroundBottom'],44,212,256,previous,'BOTTOMRIGHT')
        local first=_G[name..'Item1']
        if first then first:ClearAllPoints();first:SetPoint('BOTTOMRIGHT',frame,'TOPRIGHT',-12,-208-extra) end
        local money=_G[name..'MoneyFrame']
        if money then money:ClearAllPoints();money:SetPoint('TOPRIGHT',frame,'TOPRIGHT',-2,-216-extra) end
        if updateContainerFrameAnchors then updateContainerFrameAnchors() end
    end
    ContainerFrame_GenerateFrame=bagHook
end
    WoWPS_RefreshLocalQuestTracking()
    return mapHook~=nil and bagHook~=nil
end
wrap('StartAttack',function()cmd('attack')end)
wrap('AttackTarget',function()cmd('attack')end)
wrap('StopAttack',function()cmd('stop')end)
wrap('SpellStopCasting',function()cmd('cancelcast')end)
wrap('Logout',function()cmd('logout')end)
wrap('RepopMe',function()cmd('respawn')end)
wrap('RetrieveCorpse',function()cmd('respawn')end)
local function spell(id,book)
    if book then return state().spells[id]end
    for _,s in ipairs(state().spells)do if s.id==id or s.name==id then return s end end
end
-- Use compact authority metadata, never reload the full Spell.dbc merely to
-- construct the companion tab after learning one local mount.
local function mounts()
    local out={};for _,s in ipairs(state().spells)do if s.mountDisplay and s.mountDisplay>0 then out[#out+1]=s end end
    table.sort(out,function(a,b)return a.name<b.name end);return out
end
wrap('GetNumCompanions',function(kind)return kind=='MOUNT' and #mounts() or 0 end)
wrap('GetCompanionInfo',function(kind,index)
    local s=kind=='MOUNT' and mounts()[index];if s then return s.mountDisplay,s.name,s.id,s.icon,state().mountedSpell==s.id end
end)
wrap('GetCompanionCooldown',function()return 0,0,1 end)
wrap('CallCompanion',function(kind,index)local s=kind=='MOUNT' and mounts()[index];if s then cmd('cast',s.id)end end)
wrap('DismissCompanion',function(kind)if kind=='MOUNT' then cmd('dismount')end end)
wrap('Dismount',function()cmd('dismount')end)
wrap('IsMounted',function()return (state().mountedSpell or 0)~=0 end)
wrap('GetNumSpellTabs',function()return 1 end)
wrap('GetSpellTabInfo',function(i)if i==1 then local n=#state().spells;return 'Spells','Interface\\Icons\\INV_Misc_Book_09',0,n,0,n end return '','',0,0,0,0 end)
wrap('GetSpellName',function(i)local s=spell(i,true);if s then return s.name,'' end end)
wrap('GetSpellTexture',function(i,b)local s=spell(i,b);return s and s.icon end)
wrap('GetSpellCooldown',function(i,b)local s=spell(i,b);local d=s and s.cooldown or 0;return d>0 and state().time or 0,d,1 end)
wrap('IsUsableSpell',function(i,b)local s=spell(i,b);return s and s.usable or false,false end)
wrap('IsPassiveSpell',function()return false end)
wrap('CastSpell',function(i)local s=spell(i,true);if s then cmd('cast',s.id)end end)
wrap('CastSpellByID',function(id)cmd('cast',id)end)
wrap('CastSpellByName',function(name)local s=spell(name);if s then cmd('cast',s.id)end end)
-- The action bar.
--
-- This was an identity mapping: slot two was always the first known spell, and
-- so on down the list. Nothing could be moved by it - dragging a spell to
-- another slot left it in the old one as well, because the old one was not a
-- slot at all, it was a view of the spell list. The client keeps a real
-- per-slot bar, the one PickupAction and PlaceAction write and
-- saveCharacterConfig persists, so that is the store read here; the spell list
-- is only its starting arrangement.
--
-- wrapAction hands each wrapper the implementation it replaced. A slot holding
-- an item or a macro is not this realm's to describe, and answering about it
-- from the spell list would be inventing a spell - the client's own answer is
-- the right one.
local function wrapAction(name,fn)
    local online=_G[name]
    _G[name]=function(...) if state() then return fn(online,...) end if online then return online(...) end end
end
local onlineHasAction,onlineGetActionInfo=HasAction,GetActionInfo
local function rawSlot(i)
    if not onlineHasAction or not onlineGetActionInfo then return end
    if type(i)~='number' or not onlineHasAction(i) then return end
    return onlineGetActionInfo(i)
end
local seeded=false
local function seedBar()
    if seeded then return end
    -- Nothing to seed into without the client's bar and the two calls that
    -- write it: a headless bridge keeps the old list mapping instead, which is
    -- the only arrangement it can have.
    if not (onlineHasAction and onlineGetActionInfo and PickupSpellBookItem and PlaceAction and ClearCursor) then return end
    seeded=true -- before the placements: each fires a redraw that asks again
    -- Only a bar with nothing in it. One that was loaded back from the
    -- character's config, or that the player has already arranged, is theirs.
    for i=1,math.max(12,#state().spells+1) do if rawSlot(i) then return end end
    -- Through the same two calls the player's own drag uses, so there is one
    -- way a spell reaches a slot rather than two that can disagree.
    for i in ipairs(state().spells) do PickupSpellBookItem(i,'spell');PlaceAction(i+1) end
    ClearCursor()
end
local function barSlot(i) seedBar() return rawSlot(i) end
local function action(i)
    local kind,id=barSlot(i)
    if kind=='spell' then return spell(id) end
    if kind then return end
    if seeded or type(i)~='number' or i<2 then return end
    return state().spells[i-1]
end
-- Slot one holds no spell: melee and interact are a client action, and there is
-- nothing in the spellbook to place there. It stays the default for that slot
-- while the slot is empty and stands aside the moment anything is dropped on it.
local function meleeSlot(i) return i==1 and barSlot(1)==nil end
wrapAction('HasAction',function(online,i)
    if barSlot(i) then return true end
    return action(i)~=nil or meleeSlot(i)
end)
wrapAction('GetActionTexture',function(online,i)
    local s=action(i);if s then return s.icon end
    if barSlot(i) and online then return online(i) end
    if meleeSlot(i) then return 'Interface\\Icons\\Ability_MeleeDamage' end
end)
wrapAction('GetActionInfo',function(online,i)
    local s=action(i);if s then return 'spell',s.id end
    if barSlot(i) and online then return online(i) end
    if meleeSlot(i) then return 'macro',1 end
end)
wrapAction('GetActionText',function(online,i)
    if barSlot(i) and not action(i) and online then return online(i) end
    return ''
end)
wrapAction('GetActionCount',function(online,i)
    if barSlot(i) and not action(i) and online then return online(i) end
    return 0
end)
wrap('IsConsumableAction',function()return false end)
wrap('IsCurrentAction',function()return false end)
wrap('IsAutoRepeatAction',function()return false end)
wrapAction('IsUsableAction',function(online,i)
    local s=action(i)
    if not s and barSlot(i) and online then return online(i) end
    return not state().dead and (meleeSlot(i) or (s and s.usable)) or false,false
end)
wrap('GetActionCooldown',function(i)local s=action(i);local d=s and s.cooldown or 0;return d>0 and state().time or 0,d,1 end)
wrapAction('UseAction',function(online,i)
    local s=action(i)
    if s then cmd('cast',s.id)
    elseif meleeSlot(i) then cmd('interact_or_attack')
    elseif barSlot(i) and online then online(i) end
end)
-- Now, rather than only when something first asks about a slot: the pad can
-- pick an action up before anything has drawn one, and an unseeded bar would
-- answer that the slot is empty. Skipped with no snapshot yet - there is no
-- spell list to seed from - and the lazy call above still covers that case.
if state() then seedBar() end

)lua";
}
