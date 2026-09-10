#pragma once
namespace wowee::addons {
inline constexpr const char* kLocalServicesFrameXmlLua=R"lua(
local function state() return __WoWPSLocal end
local function wrap(name,fn)
    local previous=rawget(_G,name)
    _G[name]=function(...) if state() then return fn(previous,...) end
        if type(previous)=='function' then return previous(...) end end
end
local function cmd(name,id,count,...) return __WoWPSLocalCommand(name,id or 0,count or 0,...) end
local function item(b,i) local s=state();local bag=b==0 and s.bags or b==-1 and s.bank;return bag and bag[i] or nil end
local function link(v) return v and ('|cffffffff|Hitem:'..v.id..':0:0:0:0:0:0:0|h['..v.name..']|h|r') end
-- A bank pickup reserves no item and changes no owner data. The authority
-- commits the entire move on drop; cancelling can never delete an item.
local cursor
local previousPickup=rawget(_G,'PickupContainerItem')
local function composingMail() return SendMailFrame and SendMailFrame.IsShown and SendMailFrame:IsShown() end
local function bagManagement()
    local s=state();return s and not composingMail() and not (s.merchant and s.merchant.open) and not (s.social and s.social.trade and s.social.trade.state==2)
end
local function clearBankCursor()
    cursor=nil
    if __WoWPSLocalBankCursorIcon then __WoWPSLocalBankCursorIcon('') end
end
local function held()
    local s=state()
    if cursor then
        local v=s and item(cursor.bag,cursor.slot)
        if not s or (cursor.bank and (not s.bankOpen or s.bankNpc~=cursor.npc)) or s.bankOwner~=cursor.owner or
           not v or v.id~=cursor.id or v.count~=cursor.sourceCount then clearBankCursor() end
    end
    return cursor
end
local previousClear=ClearCursor
function ClearCursor(...) clearBankCursor();if previousClear then return previousClear(...) end end
wrap('GetCursorInfo',function(old,...)
    local c=held();if c then return 'item',c.id,c.link end
    if old then return old(...) end
end)
wrap('CursorHasItem',function(old,...)if held() then return true end;if old then return old(...) end;return false end)
local function transfer(b,i,count)
    if not state().bankOpen or (b~=0 and b~=-1) then return false end
    local v=item(b,i);if not v then return false end
    if b==-1 then return cmd('bank_withdraw',i,count or v.count,v.id,v.count) end
    return cmd('bank_deposit_from_slot',i,count or v.count,v.id,v.count)
end
local function pickBank(i,count)
    if not state().bankOpen or type(i)~='number' or i%1~=0 or i<1 or i>28 then return false end
    local c=held();local v=item(-1,i)
    if c then
        if c.bag==-1 and c.slot==i then ClearCursor();return true end
        local amount=c.count
        if v and v.id==c.id then amount=math.min(amount,(v.maxStack or 65535)-v.count) end
        if amount<=0 then return false end
        local ok=cmd(c.bag==0 and 'bank_deposit_slot' or 'bank_move',c.slot,amount,i,c.id,v and v.id or 0,c.sourceCount,v and v.count or 0)
        if ok then ClearCursor() end
        return ok
    end
    if not v then return false end
    count=count or v.count
    if type(count)~='number' or count%1~=0 or count<1 or count>v.count then return false end
    ClearCursor()
    cursor={bag=-1,slot=i,id=v.id,count=count,sourceCount=v.count,link=link(v),owner=state().bankOwner,npc=state().bankNpc,bank=true}
    if __WoWPSLocalBankCursorIcon then __WoWPSLocalBankCursorIcon(v.icon) end
    return true
end
local function dropInBag()
    local c=held();if not c or c.bag~=-1 then return false end
    local ok=cmd('bank_withdraw',c.slot,c.count,c.id,c.sourceCount)
    if ok then ClearCursor() end
    return ok
end
local function pickBag(i,count)
    if not bagManagement() or type(i)~='number' or i%1~=0 or i<1 or i>24 then return false end
    local c=held()
    if c then
        if c.bag==0 and c.slot==i then ClearCursor();return true end
        local v=item(0,i);local amount=c.count
        if v and v.id==c.id then amount=math.min(amount,(v.maxStack or 65535)-v.count) end
        if amount<=0 then return false end
        local ok=cmd(c.bag==-1 and 'bank_withdraw_slot' or 'bag_move',c.slot,amount,i,c.id,v and v.id or 0,c.sourceCount,v and v.count or 0)
        if ok then ClearCursor() end;return ok
    end
    local v=item(0,i);if not v then return false end
    count=count or v.count
    if type(count)~='number' or count%1~=0 or count<1 or count>v.count then return false end
    ClearCursor()
    cursor={bag=0,slot=i,id=v.id,count=count,sourceCount=v.count,link=link(v),owner=state().bankOwner,npc=state().bankNpc,bank=state().bankOpen}
    if __WoWPSLocalBankCursorIcon then __WoWPSLocalBankCursorIcon(v.icon) end
    return true
end
wrap('GetContainerNumSlots',function(_,b)return b==0 and 24 or b==-1 and 28 or 0 end)
wrap('GetContainerNumFreeSlots',function(_,b)
    if b==0 then local used=0;for _,v in pairs(state().bags)do if v then used=used+1 end end;return math.max(0,24-used),0 end
    local free=0;if b==-1 then for i=1,28 do if not state().bank[i] then free=free+1 end end end
    return free,0
end)
wrap('GetContainerItemInfo',function(_,b,i)local v=item(b,i);if v then local c=held();return v.icon,v.count,c and c.bag==b and c.slot==i or false,1,false,false,link(v) end end)
wrap('GetContainerItemLink',function(_,b,i)return link(item(b,i))end)
wrap('GetContainerItemID',function(_,b,i)local v=item(b,i);return v and v.id end)
wrap('UseContainerItem',function(old,b,i,...)if b==0 and composingMail() then ClearCursor();return cmd('mail_attach',i) end;if state().bankOpen and (b==0 or b==-1) then ClearCursor();return transfer(b,i) end;if old then return old(b,i,...) end end)
wrap('PickupContainerItem',function(old,b,i)if b==-1 and state().bankOpen then return pickBank(i) end;if b==0 and bagManagement() then return pickBag(i) end;if old then return old(b,i) end end)
wrap('SplitContainerItem',function(old,b,i,count)if b==-1 and state().bankOpen then ClearCursor();return pickBank(i,count) end;if b==0 and bagManagement() then ClearCursor();return pickBag(i,count) end;if old then return old(b,i,count) end end)
wrap('ClickSendMailItem',function(old,i,...)local c=held();if c then if c.bag==0 and c.count==c.sourceCount and composingMail() then local ok=cmd('mail_attach',c.slot);if ok then ClearCursor() end;return ok end;return false end;if old then return old(i,...) end end)
wrap('AutoStoreBankItem',function(_,b,i)if held() then return dropInBag() end;return transfer(b,i)end)
wrap('AutoBankItem',function(_,b,i)return transfer(b,i)end)
wrap('CloseBankFrame',function()ClearCursor();return cmd('bank_close')end)
wrap('GetNumBankSlots',function()return 0,true end)
wrap('GetBankSlotCost',function()return 0 end)
wrap('PurchaseSlot',function()if UIErrorsFrame then UIErrorsFrame:AddMessage('Bank bag slots are not available in this local realm.',1,.2,.2)end end)
for _,name in ipairs({'GetInventoryItemTexture','GetInventoryItemCount','GetInventoryItemLink','GetInventoryItemID'}) do
    local api=name
    wrap(api,function(old,unit,slot,...)
        if unit=='player' and type(slot)=='number' and slot>=40 and slot<=67 then
            local v=state().bank[slot-39]
            if api=='GetInventoryItemTexture' then return v and v.icon end
            if api=='GetInventoryItemLink' then return link(v) end
            if api=='GetInventoryItemID' then return v and v.id end
            return v and v.count or 0
        end
        if old then return old(unit,slot,...)end
    end)
end
wrap('BankButtonIDToInvSlotID',function(_,i,isBag)return i+(isBag and 67 or 39) end)
wrap('PickupInventoryItem',function(old,i,...)
    if state().bankOpen and type(i)=='number' and i>=40 and i<=67 then return pickBank(i-39) end
    if held() then ClearCursor();return false end
    if old then return old(i,...) end
end)
wrap('UseInventoryItem',function(old,i,...)
    if state().bankOpen and type(i)=='number' and i>=40 and i<=67 then ClearCursor();return transfer(-1,i-39) end
    if held() then ClearCursor();return false end
    if old then return old(i,...) end
end)
wrap('IsInventoryItemLocked',function(old,i,...)
    if type(i)=='number' and i>=40 and i<=67 then local c=held();return c and c.bag==-1 and c.slot==i-39 or false end
    if old then return old(i,...) end
end)
-- Switching cursor mode cancels a local bank reservation before the original
-- spell/action API can replace its icon. Dropping bank goods on action bars or
-- equipment does not fabricate an inventory item or issue connected packets.
for _,name in ipairs({'PickupSpell','PickupSpellBookItem','PickupAction','PickupMacro','PickupItem','PickupMerchantItem','PickupBagFromSlot','PlaceAction','EquipCursorItem'}) do
    local api=name
    wrap(name,function(old,...)
        if api=='PlaceAction' then local c=held();if c and c.bag==0 and not c.bank and c.count==c.sourceCount and type(previousPickup)=='function' then local slot=c.slot;ClearCursor();previousPickup(0,slot);if old then return old(...) end;return false end end
        if held() then ClearCursor();return false end
        if old then return old(...) end
    end)
end
wrap('GetItemCount',function(_,id,includeBank)
    id=tonumber(id) or tonumber(tostring(id):match('item:(%d+)'));local total=0
    for _,v in pairs(state().bags)do if v.id==id then total=total+v.count end end
    if includeBank then for _,v in pairs(state().bank)do if v.id==id then total=total+v.count end end end
    return total
end)
function WoWPS_LocalBankActivate(name)
    if not state() then return false end
    local b=_G[name];if not b then return false end
    if name:match('^BankFrameItem%d+$') and state().bankOpen then pickBank(b:GetID());return true end
    if name:match('^ContainerFrame%d+Item%d+$')then local parent=b:GetParent();if parent and parent:GetID()==0 then
        if composingMail() then cmd('mail_attach',b:GetID());return true end
        if bagManagement() then pickBag(b:GetID());return true end
    end end
    return false
end
local function options()
    local s=state();local t={};local m=s.merchant
    if m and (m.vendor or m.repair)then t[#t+1]={'I would like to browse your goods.','vendor','merchant_open',0}end
    if s.flightMaster then
        if not s.taxiKnown then t[#t+1]={'Discover this flight point.','taxi','taxi_discover',0} end
        for _,f in ipairs(s.flights or {})do t[#t+1]={'Fly to '..f.name..' ('..f.cost..' copper).','taxi','taxi_fly',f.id}end
    end
    if s.banker then t[#t+1]={'I would like to check my bank.','banker','bank_open',0}end
    if s.classTrainer then t[#t+1]={'Reset my talents (free local testing).','trainer','talents_reset',0}end
    if s.innkeeper then t[#t+1]={'I would like to check my mail.','chat','mail_open',0}end
    if s.trainer then
        t[#t+1]={'I would like some training.','trainer','trainer_open',0}
        for _,skill in ipairs(s.professions)do if skill.id==s.trainerSkill then t[#t+1]={'Practice '..skill.name..'.','trainer','craft_open',skill.id}end end
    end
    return t
end
wrap('GetGossipOptions',function()local t={}for _,v in ipairs(options())do t[#t+1]=v[1];t[#t+1]=v[2]end;return unpack(t)end)
wrap('GetNumGossipOptions',function()return #options()end)
wrap('SelectGossipOption',function(_,i)local v=options()[i];if v then return cmd(v[3],v[4])end end)
local skillSelection,skillOwner
local function skillRows()
    local s=state();local rows={};for _,p in ipairs(s.professions or {})do rows[#rows+1]=p end
    if (s.ridingSkill or 0)>0 then rows[#rows+1]={id=762,name='Riding',rank=s.ridingSkill,max=s.ridingSkill,primary=false}end
    return rows
end
wrap('UnitOnTaxi',function(_,unit)return unit=='player' and state().onTaxi==true end)
local function skillRow(i)
    if type(i)~='number' or i%1~=0 or i<1 then return nil end
    return skillRows()[i]
end
local function selectedSkill()
    local owner=state().bankOwner
    if owner~=skillOwner then skillOwner=owner;skillSelection=nil end
    local rows=skillRows()
    if skillSelection==nil and rows[1] then skillSelection=rows[1].id end
    for i,s in ipairs(rows)do if s.id==skillSelection then return i end end
    return 0
end
wrap('GetNumSkillLines',function()return #skillRows() end)
wrap('GetSkillLineInfo',function(_,i)
    local s=skillRow(i)
    -- SkillFrame OnLoad performs arithmetic even with no selected skill.
    -- Match the native API's empty row; nil costs suppress training branches
    -- (0 is true in Lua). Preserve the thirteenth description result too.
    if not s then return '',false,true,0,0,0,0,false,nil,nil,0,0,'' end
    return s.name,false,true,s.rank,0,0,s.max,s.primary==true,nil,nil,0,0,''
end)
wrap('GetSelectedSkill',function()return selectedSkill() end)
wrap('SetSelectedSkill',function(_,i)
    skillOwner=state().bankOwner
    local s=skillRow(i);skillSelection=s and s.id or false
end)
wrap('AbandonSkill',function(_,i)
    local s=skillRow(i==nil and selectedSkill() or i)
    if s and s.primary then return cmd('unlearn_profession',s.id)end
end)
wrap('CollapseSkillHeader',function()end);wrap('ExpandSkillHeader',function()end)
local trainerSelection=1
local function service(i)return state().training[i or trainerSelection]end
wrap('GetNumTrainerServices',function()return #state().training end)
wrap('GetTrainerServiceInfo',function(_,i)local s=service(i);if s then return s.name,'',s.available==false and 'unavailable' or 'available',false end end)
wrap('GetTrainerServiceCost',function(_,i)local s=service(i);return s and s.cost or 0 end)
wrap('GetTrainerServiceLevelReq',function(_,i)local s=service(i);return s and s.level or 0 end)
wrap('GetTrainerServiceSkillReq',function(_,i)local s=service(i);if s and s.skill and s.skill>0 then return s.skillName,s.skill,s.skillMet==true end end)
wrap('GetTrainerServiceAbilityReq',function()return nil end)
wrap('GetTrainerServiceDescription',function()return 'Learn this class ability, profession rank or recipe.' end)
wrap('GetTrainerServiceIcon',function()return 'Interface\\Icons\\INV_Misc_Book_09' end)
wrap('GetTrainerSelectionIndex',function()return trainerSelection end)
wrap('SelectTrainerService',function(_,i)trainerSelection=i end)
wrap('BuyTrainerService',function(_,i)local s=service(i);if s and s.available~=false then return cmd(s.action,s.id)end end)
wrap('CloseTrainer',function()return cmd('trainer_close')end)
wrap('GetTrainerGreetingText',function()return state().greeting or ''end)
wrap('IsTradeskillTrainer',function()local s=state();if s.tradeTrainer~=nil then return s.tradeTrainer end;return s.trainer end)
wrap('GetTrainerServiceTypeFilter',function()return 1 end)
wrap('SetTrainerServiceTypeFilter',function()end)
local recipeSelection=1
local function recipe(i)return state().recipes[i or recipeSelection]end
local function openProfession(name)
    for _,s in ipairs(state().professions)do if s.name:lower()==tostring(name):lower() or s.id==tonumber(name)then recipeSelection=1;return cmd('craft_open',s.id)end end
    return false
end
wrap('OpenTradeSkill',function(_,name)return openProfession(name)end)
wrap('CastSpellByName',function(old,name,...)
    if openProfession(name)then return true end
    if old then return old(name,...)end
end)
wrap('GetNumTradeSkills',function()return #state().recipes end)
wrap('GetTradeSkillInfo',function(_,i)local r=recipe(i);if r then return r.name,r.chance==1000 and 'optimal' or r.chance>500 and 'medium' or r.chance>0 and 'easy' or 'trivial',r.available,false,nil,1 end end)
wrap('GetTradeSkillIcon',function(_,i)local r=recipe(i);return r and r.icon end)
wrap('GetTradeSkillItemLink',function(_,i)local r=recipe(i);return r and ('item:'..r.itemId)end)
wrap('GetTradeSkillNumMade',function(_,i)local r=recipe(i);return r and r.made or 0,r and r.made or 0 end)
wrap('GetTradeSkillNumReagents',function(_,i)local r=recipe(i);return r and #r.reagents or 0 end)
wrap('GetTradeSkillReagentInfo',function(_,i,n)local r=recipe(i);local v=r and r.reagents[n];if v then return v.name,v.icon,v.need,v.have end end)
wrap('GetTradeSkillReagentItemLink',function(_,i,n)local r=recipe(i);local v=r and r.reagents[n];return v and ('item:'..v.id)end)
wrap('GetTradeSkillLine',function()for _,s in ipairs(state().professions)do if s.id==state().craftSkill then return s.name,s.rank,s.max end end;return '',0,0 end)
wrap('GetTradeSkillSelectionIndex',function()return recipeSelection end)
wrap('SelectTradeSkill',function(_,i)recipeSelection=i end)
wrap('DoTradeSkill',function(_,i,count)local r=recipe(i);if r and (not r.unavailable or r.unavailable=='') then return cmd('craft',r.id,count or 1)end end)
wrap('CloseTradeSkill',function()return cmd('craft_close')end)
wrap('StopTradeSkillRepeat',function()end)
wrap('GetTradeSkillCooldown',function()return nil end)
wrap('GetTradeSkillTools',function(_,i)local r=recipe(i);local values={};for _,tool in ipairs(r and r.tools or {})do values[#values+1]=tool.name;values[#values+1]=tool.have==true end;return unpack(values) end)
wrap('GetTradeSkillDescription',function(_,i)local r=recipe(i);return r and r.unavailable or '' end)
wrap('IsTradeSkillLinked',function()return false end)
wrap('GetTradeSkillSubClasses',function()return 'All' end)
wrap('GetTradeSkillInvSlots',function()return 'All' end)
wrap('GetTradeSkillSubClassFilter',function()return 0 end)
wrap('GetTradeSkillInvSlotFilter',function()return 0 end)
wrap('SetTradeSkillSubClassFilter',function()end);wrap('SetTradeSkillInvSlotFilter',function()end)
)lua";
}
