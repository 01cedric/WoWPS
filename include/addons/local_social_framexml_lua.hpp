#pragma once
namespace wowee::addons {
inline constexpr const char* kLocalSocialFrameXmlLua=R"lua(
local function state()return __WoWPSLocal end
local function social()local s=state();return s and s.social or {} end
local function trade()return social().trade or {state=0} end
local function wrap(name,fn)
    local old=rawget(_G,name)
    _G[name]=function(...)if state()then return fn(old,...)end;if type(old)=='function'then return old(...)end end
end
local function cmd(op,name,id,rev,value,bag,slot,item,count)
    return __WoWPSLocalSocialCommand(op,name or '',id or 0,rev or 0,value or 0,bag or 0,slot or 0,item or 0,count or 0)
end
local function action(op,value,bag,slot,item,count)
    local t=trade();return cmd(op,'',t.id,t.revision,value,bag,slot,item,count)
end
local function integer(v,lo,hi)return type(v)=='number' and v==math.floor(v) and v>=lo and v<=hi end
local function resolve(value)
    if value=='player' or value=='target' or (type(value)=='string' and value:match('^party[1-4]$')) then return UnitName(value) end
    return value
end
local function unit(guid)
    local s=state();if not s then return end;if guid==s.bankOwner then return 'player' end
    local n=0;for _,m in ipairs(s.party and s.party.members or {})do if m.guid~=s.bankOwner then n=n+1;if m.guid==guid then return 'party'..n end end end
end
wrap('DoReadyCheck',function()return cmd('ready_start')end)
wrap('ConfirmReadyCheck',function(_,answer)local c=social().ready or {};return cmd('ready_answer','',c.id,0,(answer and answer~=0) and 1 or 0)end)
wrap('GetReadyCheckStatus',function(_,u)local c=social().ready or {};if c.state==0 or c.state==3 then return end
    for _,m in ipairs(c.members or {})do if unit(m.guid)==u then return ({[0]='waiting','ready','notready'})[m.answer] end end
end)
wrap('GetReadyCheckTimeLeft',function()local c=social().ready or {};return c.state==1 and math.max(0,(c.remaining or 0)-(GetTime()-state().time)) or 0 end)
local selectedIgnore=0
wrap('GetNumIgnores',function()return #(social().ignores or {})end)
wrap('GetIgnoreName',function(_,i)return (social().ignores or {})[i]end)
wrap('SetSelectedIgnore',function(_,i)selectedIgnore=integer(i,1,GetNumIgnores()) and i or 0 end)
wrap('GetSelectedIgnore',function()return math.min(selectedIgnore,GetNumIgnores())end)
wrap('AddIgnore',function(_,name)name=resolve(name);return type(name)=='string' and cmd('ignore_add',name) or false end)
wrap('DelIgnore',function(_,name)name=resolve(name);return type(name)=='string' and cmd('ignore_remove',name) or false end)
wrap('AddOrDelIgnore',function(_,name)name=resolve(name);if type(name)~='string'then return false end
    for _,old in ipairs(social().ignores or {})do if old:lower()==name:lower()then return cmd('ignore_remove',old)end end;return cmd('ignore_add',name)
end)
wrap('InitiateTrade',function(_,u)local name=resolve(u or 'target');return type(name)=='string' and cmd('trade_request',name) or false end)
wrap('BeginTrade',function()return action('trade_open')end)
wrap('AcceptTradeRequest',function()return action('trade_open')end)
wrap('CancelTrade',function()return action('trade_cancel')end)
wrap('DeclineTrade',function()return action('trade_cancel')end)
wrap('AcceptTrade',function()return action('trade_accept')end)
wrap('UnacceptTrade',function()return action('trade_unaccept')end)
wrap('SetTradeMoney',function(_,amount)return integer(amount,0,1000000000) and action('trade_money',amount) or false end)
wrap('GetPlayerTradeMoney',function()return trade().ownMoney or 0 end)
wrap('GetTargetTradeMoney',function()return trade().peerMoney or 0 end)
local function itemLink(v)return v and v.id and v.id~=0 and ('|cffffffff|Hitem:'..v.id..':0:0:0:0:0:0:0|h['..v.name..']|h|r') or nil end
for _,side in ipairs({'Player','Target'})do
    local key=side=='Player' and 'ownItems' or 'peerItems'
    wrap('GetTrade'..side..'ItemInfo',function(_,slot)local v=(trade()[key] or {})[slot];if v and v.id~=0 then return v.name,v.icon,v.count,true,nil end end)
    wrap('GetTrade'..side..'ItemLink',function(_,slot)return itemLink((trade()[key] or {})[slot])end)
end
-- Picking up an offer never takes ownership out of the backpack. The drop
-- carries the exact displayed session, revision and source stack to the host.
local cursor
local previousClear=ClearCursor
function ClearCursor(...)cursor=nil;if __WoWPSLocalBankCursorIcon then __WoWPSLocalBankCursorIcon('')end;if previousClear then return previousClear(...)end end
local function held()
    if cursor then local s=state();local t=trade();local v=s and s.bags[cursor.bag]
        if not s or s.bankOwner~=cursor.owner or t.state~=2 or t.id~=cursor.trade or t.revision~=cursor.revision or not v or v.id~=cursor.id or v.count~=cursor.sourceCount then ClearCursor()end
    end;return cursor
end
local function pick(slot,count)
    ClearCursor();local s=state();local t=trade();local v=integer(slot,1,24) and s.bags[slot]
    if t.state~=2 or not v or not integer(count or v.count,1,v.count)then return false end
    cursor={owner=s.bankOwner,trade=t.id,revision=t.revision,bag=slot,id=v.id,sourceCount=v.count,count=count or v.count,link=itemLink(v)}
    if __WoWPSLocalBankCursorIcon then __WoWPSLocalBankCursorIcon(v.icon)end;return true
end
wrap('PickupContainerItem',function(old,bag,slot,...)if trade().state==2 then return bag==0 and pick(slot) or false end;if old then return old(bag,slot,...)end end)
wrap('SplitContainerItem',function(old,bag,slot,count,...)if trade().state==2 then return bag==0 and integer(count,1,65535) and pick(slot,count) or false end;if old then return old(bag,slot,count,...)end end)
wrap('GetCursorInfo',function(old,...)local c=held();if c then return 'item',c.id,c.link end;if old then return old(...)end end)
wrap('CursorHasItem',function(old,...)if held()then return true end;if old then return old(...)end;return false end)
local function click(slot)
    if not integer(slot,1,6) or trade().state~=2 then return false end
    local carried=cursor;local c=held();if carried and not c then return false end
    if c then local ok=cmd('trade_offer','',c.trade,c.revision,c.count,c.bag-1,slot-1,c.id,c.sourceCount);ClearCursor();return ok end
    return action('trade_offer',0,0,slot-1)
end
wrap('ClickTradeButton',function(_,slot)return click(slot)end)
wrap('PickupTradeItem',function(_,slot)ClearCursor();return integer(slot,1,6) and action('trade_offer',0,0,slot-1) or false end)
wrap('ClearTradeItem',function(_,slot)return integer(slot,1,6) and action('trade_offer',0,0,slot-1) or false end)
function WoWPS_LocalSocialDrop()
    if cursor then ClearCursor();return true end;return false
end
for _,api in ipairs({'PickupAction','PickupSpell','PickupSpellBookItem','PickupInventoryItem'})do
    wrap(api,function(old,...)if cursor then ClearCursor();return false end;if old then return old(...)end end)
end
local previous={};local owner
local function event(name,...)if __WoWPSLocalSocialEvent then __WoWPSLocalSocialEvent(name,...)end end
function WoWPS_LocalSocialUpdate()
    local s=state();local current=social();local old=previous
    if s and owner and owner~=s.bankOwner then old={} end
    previous=current;owner=s and s.bankOwner -- commit before event handlers can re-enter
    held()
    local c,p=current.ready or {},old.ready or {}
    if c.state==1 and (c.id~=p.id or p.state~=1)then event('READY_CHECK',c.initiator,c.remaining)end
    if c.id and c.id~=0 then for _,m in ipairs(c.members or {})do
        local before=0;if p.id==c.id then for _,v in ipairs(p.members or {})do if v.guid==m.guid then before=v.answer end end end
        if m.answer~=0 and m.answer~=before then local u=unit(m.guid);if u then event('READY_CHECK_CONFIRM',u,m.answer==1 and 1 or 0)end end
    end end
    if p.state==1 and (c.id~=p.id or c.state~=1)then
        if StaticPopup_Hide then StaticPopup_Hide('READY_CHECK')end;event('READY_CHECK_FINISHED')
    end
    if table.concat(current.ignores or {},'\n')~=table.concat(old.ignores or {},'\n')then event('IGNORELIST_UPDATE')end
    local t,q=current.trade or {},old.trade or {}
    local changed=t.id~=q.id or t.revision~=q.revision or t.state~=q.state or t.ownAccepted~=q.ownAccepted or t.peerAccepted~=q.peerAccepted
    if changed then
        if q.state==2 and (t.id~=q.id or t.state~=2)then ClearCursor();event('TRADE_CLOSED')end
        if q.state==1 and (t.id~=q.id or t.state~=1) and StaticPopup_Hide then StaticPopup_Hide('TRADE')end
        if t.state==1 and t.side==1 and (t.id~=q.id or q.state~=1)then event('TRADE_REQUEST',t.peerName)end
        if t.state==2 then
            if t.id~=q.id or q.state~=2 then if state().bankOpen and CloseBankFrame then CloseBankFrame()end;event('TRADE_SHOW')end
            event('TRADE_UPDATE');event('PLAYER_TRADE_MONEY');event('TRADE_MONEY_CHANGED')
            event('TRADE_ACCEPT_UPDATE',t.ownAccepted and 1 or 0,t.peerAccepted and 1 or 0)
        end
    end
end
)lua";
}
