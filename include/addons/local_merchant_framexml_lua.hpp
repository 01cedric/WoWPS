#pragma once
namespace wowee::addons {
inline constexpr const char* kLocalMerchantFrameXmlLua=R"lua(
-- The original MerchantFrame owns layout, paging, prices and confirmation
-- dialogs. These APIs supply the selected local merchant's authoritative
-- stock snapshot and forward commands; no synthetic inventory is generated.
local function state() return __WoWPSLocal end
local function merchant() local s=state();return s and s.merchant end
local function wrap(name, fn)
    local online=rawget(_G,name)
    _G[name]=function(...) if state() then return fn(...) end
        if type(online)=='function' then return online(...) end end
end
local function item(i) local m=merchant();return m and m.items and m.items[i] end
local function command(name,id,count) return __WoWPSLocalCommand(name,id or 0,count or 0) end
local unitName=UnitName
UnitName=function(unit,...)
    if state() and type(unit)=='string' and unit:lower()=='npc' then return state().npcName end
    return unitName(unit,...)
end
wrap('GetGossipOptions',function()
    local m=merchant();if m and (m.vendor or m.repair) then
        return (m.vendor and 'I would like to browse your goods.' or 'I need repairs.'),'vendor'
    end
end)
wrap('GetNumGossipOptions',function()local m=merchant();return m and (m.vendor or m.repair) and 1 or 0 end)
wrap('SelectGossipOption',function(i)if i==1 then return command('merchant_open')end end)
wrap('GetMerchantNumItems',function()local m=merchant();return m and m.open and #m.items or 0 end)
wrap('GetMerchantItemInfo',function(i)local v=item(i);if v then
    return v.name,v.icon,v.price,v.bundle,v.remaining,true,nil end end)
wrap('GetMerchantItemLink',function(i)local v=item(i);if v then
    return '|cffffffff|Hitem:'..v.id..':0:0:0:0:0:0:0|h['..v.name..']|h|r' end end)
wrap('GetMerchantItemMaxStack',function(i)local v=item(i);return v and math.max(1,math.floor(v.stack/v.bundle)) or 1 end)
wrap('GetMerchantItemCostInfo',function()return 0,0,0 end)
wrap('GetMerchantItemCostItem',function()end)
wrap('BuyMerchantItem',function(i,count)
    local v=item(i);count=count or 1
    if not v or not merchant().open or type(count)~='number' or count~=math.floor(count) or count<1 then return false end
    local amount=count*v.bundle
    if amount>65535 then return false end
    return command('merchant_buy',v.id,amount)
end)
-- A controller click purchases one bundle. Square uses the original button's
-- right-click handler below, including its expensive-purchase confirmation.
wrap('PickupMerchantItem',function(i)
    if type(i)~='number' or i<1 or i~=math.floor(i) then return false end
    local slot=(i-1)%10+1;local b=rawget(_G,'MerchantItem'..slot..'ItemButton')
    if b and b:GetID()==i and MerchantItemButton_OnClick then
        MerchantItemButton_OnClick(b,'RightButton');return true
    end
    return false
end)
wrap('CanMerchantRepair',function()local m=merchant();return m and m.open and m.repair or false end)
wrap('GetRepairAllCost',function()return 0,false end)
wrap('RepairAllItems',function()if CanMerchantRepair() then return command('merchant_repair') end end)
wrap('InRepairMode',function()return false end)
wrap('ShowRepairCursor',function()if CanMerchantRepair() then return command('merchant_repair') end end)
wrap('HideRepairCursor',function()end)
wrap('CanGuildBankRepair',function()return false end)
wrap('GetGuildBankWithdrawMoney',function()return 0 end)
wrap('CloseMerchant',function()return command('merchant_close')end)
local function buyback(i)
    local m=merchant()
    if not m or not m.open or not m.buyback or type(i)~='number' or i%1~=0 or i<1 or i>#m.buyback then return nil end
    -- The C++ publisher already reverses the durable newest-first ledger.
    -- Keep that visible order: the last index is the newest sale.
    return m.buyback[i]
end
wrap('GetNumBuybackItems',function()local m=merchant();return m and m.open and m.buyback and #m.buyback or 0 end)
wrap('GetBuybackItemInfo',function(i)local v=buyback(i);if v then
    return v.name,v.icon,v.price,v.count,-1,true end end)
wrap('GetBuybackItemLink',function(i)local v=buyback(i);if v then
    return '|cffffffff|Hitem:'..v.itemId..':0:0:0:0:0:0:0|h['..v.name..']|h|r' end end)
wrap('BuybackItem',function(i)local v=buyback(i);if v then return command('merchant_buyback',v.id) end;return false end)
local useContainer=UseContainerItem
local function sell(bag,slot)
    local s=state();local v=s and bag==0 and s.bags[slot]
    if v then return command('merchant_sell',v.id,v.count) end
    return false
end
UseContainerItem=function(bag,slot,...)
    local m=merchant();if m and m.open then return sell(bag,slot) end
    return useContainer(bag,slot,...)
end
wrap('SellContainerItem',sell)
function WoWPS_LocalMerchantActivate(name)
    local m=merchant();if not m or not m.open then return false end
    local b=rawget(_G,name);if not b then return false end
    if name:match('^MerchantItem%d+ItemButton$') and MerchantItemButton_OnClick then
        MerchantItemButton_OnClick(b,'RightButton');return true
    end
    if name=='MerchantBuyBackItemItemButton' then
        BuybackItem(GetNumBuybackItems());return true
    end
    if name:match('^ContainerFrame%d+Item%d+$') then
        local parent=b:GetParent();if parent then UseContainerItem(parent:GetID(),b:GetID());return true end
    end
    return false
end
)lua";
}
