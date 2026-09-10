-- Unmodified retail script, loaded after the production local API bridge.
assert(loadfile(retailRoot..'/MerchantFrame.lua'))()
ceil=math.ceil;floor=math.floor;MERCHANT_PAGE_NUMBER='Page %d of %d'
local methods={}
function methods:GetName()return self.name end
function methods:SetText(v)self.text=v end
function methods:SetFormattedText(f,...)self.text=string.format(f,...)end
function methods:SetTexture(v)self.texture=v end
function methods:SetID(v)self.id=v end
function methods:GetID()return self.id end
function methods:GetParent()return self.parent end
function methods:Show()self.shown=true end
function methods:Hide()self.shown=false end
function methods:IsShown()return self.shown end
function methods:IsVisible()return self.shown end
function methods:Enable()self.enabled=true end
function methods:Disable()self.enabled=false end
function methods:SetWidth(v)self.width=v end
function methods:SetHeight(v)self.height=v end
function methods:SetPoint(...)self.point={...}end
function methods:ClearAllPoints()self.point=nil end
function methods:RegisterEvent()end
function methods:RegisterForDrag()end
function methods:RegisterForClicks()end
local function widget(name)local w=setmetatable({name=name},{__index=methods});_G[name]=w;return w end
for _,name in ipairs({'MerchantFrame','MerchantNameText','MerchantPageText','MerchantFramePortrait',
 'MerchantBuyBackItem','MerchantBuyBackItemName','MerchantBuyBackItemMoneyFrame','MerchantBuyBackItemItemButton',
 'MerchantRepairText','MerchantRepairAllButton','MerchantRepairItemButton','MerchantGuildBankRepairButton',
 'MerchantRepairAllIcon','MerchantGuildBankRepairButtonIcon','MerchantFrameBottomLeftBorder','MerchantFrameBottomRightBorder',
 'MerchantNextPageButton','MerchantPrevPageButton','BuybackFrameTopLeft','BuybackFrameTopRight','BuybackFrameBotLeft','BuybackFrameBotRight'}) do widget(name)end
for i=1,12 do for _,suffix in ipairs({'','Name','ItemButton','MoneyFrame','AltCurrencyFrame'}) do widget('MerchantItem'..i..suffix)end end
function SetItemButtonCount(w,v)w.count=v end
function SetItemButtonStock(w,v)w.stock=v end
function SetItemButtonTexture(w,v)w.texture=v end
function SetItemButtonNameFrameVertexColor()end
function SetItemButtonSlotVertexColor()end
function SetItemButtonTextureVertexColor()end
function SetItemButtonNormalTextureVertexColor()end
function SetPortraitTexture(w,unit)w.unit=unit end
function MoneyFrame_Update(name,price)_G[name].price=price end
function PanelTemplates_SetNumTabs(w,n)w.numTabs=n end
function PanelTemplates_SetTab(w,n)w.selectedTab=n end
function SetDesaturation()end
function PlaySound()end
function OpenBackpack()return true end
function CloseBackpack()end
function ResetCursor()end
function StaticPopup_Hide()end
function ShowUIPanel(w)w:Show()end
function HideUIPanel(w)w:Hide()end
local popup
function StaticPopup_Show(which,link)popup={which,link}end
GameTooltip={IsOwned=function()return false end}
local commands={}
function __WoWPSLocalCommand(name,id,count)commands[#commands+1]={name,id,count};return true end
__WoWPSLocal={npcName='Kaja',bags={{id=117,count=3}},merchant={open=true,vendor=true,repair=false,items={}}}
local m=__WoWPSLocal.merchant
for i=1,11 do m.items[i]={id=100+i,name='Stock '..i,icon='Interface\\Icons\\item',price=i*5,bundle=1,remaining=-1,stack=20}end
m.items[1]={id=2516,name='Rough Arrow',icon='Interface\\Icons\\INV_Ammo_Arrow_02',price=10,bundle=200,remaining=-1,stack=1000}
m.items[2].remaining=0
MerchantFrame_OnLoad(MerchantFrame)
MerchantFrame_OnEvent(MerchantFrame,'MERCHANT_SHOW')
assert(MerchantFrame:IsShown() and MerchantNameText.text=='Kaja')
assert(MerchantItem1Name.text=='Rough Arrow' and MerchantItem1ItemButton.count==200)
assert(MerchantItem1MoneyFrame.price==10 and MerchantItem2ItemButton.stock==0)
assert(MerchantItem10ItemButton.id==10 and MerchantNextPageButton.shown and MerchantNextPageButton.enabled)
assert(not MerchantPrevPageButton.enabled and not MerchantRepairAllButton.shown)
MerchantNextPageButton_OnClick()
assert(MerchantItem1ItemButton.id==11 and MerchantItem1Name.text=='Stock 11')
assert(not MerchantItem2ItemButton.shown and not MerchantNextPageButton.enabled and MerchantPrevPageButton.enabled)
MerchantPrevPageButton_OnClick()
assert(WoWPS_LocalMerchantActivate('MerchantItem1ItemButton'))
assert(commands[#commands][1]=='merchant_buy' and commands[#commands][2]==2516 and commands[#commands][3]==200)
BuyMerchantItem(1,2);assert(commands[#commands][3]==400)
local before=#commands;BuyMerchantItem(1,0.5);BuyMerchantItem(1,-1);BuyMerchantItem(1,1000000);assert(#commands==before)
assert(GetMerchantItemMaxStack(1)==5 and not select(7,GetMerchantItemInfo(1)))
m.items[1].price=2000000;MerchantFrame_OnEvent(MerchantFrame,'MERCHANT_UPDATE')
assert(WoWPS_LocalMerchantActivate('MerchantItem1ItemButton'))
assert(popup[1]=='CONFIRM_HIGH_COST_ITEM' and #commands==before)
popup=nil;PickupMerchantItem(1);assert(popup and popup[1]=='CONFIRM_HIGH_COST_ITEM' and #commands==before)
local bag=widget('ContainerFrame1');bag.id=0
local button=widget('ContainerFrame1Item24');button.id=1;button.parent=bag
assert(WoWPS_LocalMerchantActivate('ContainerFrame1Item24'))
assert(commands[#commands][1]=='merchant_sell' and commands[#commands][2]==117 and commands[#commands][3]==3)
-- Replicated owner ledger uses stable IDs; retail's compact widget selects
-- its newest row through the last visible index.
m.buyback={{id=40,itemId=117,name='Old sale',icon='old',price=6,count=3},
 {id=44,itemId=2392,name='New sale',icon='new',price=25,count=1}}
MerchantFrame_OnEvent(MerchantFrame,'MERCHANT_UPDATE')
assert(GetNumBuybackItems()==2 and MerchantBuyBackItemName.text=='New sale' and MerchantBuyBackItemMoneyFrame.price==25)
assert(WoWPS_LocalMerchantActivate('MerchantBuyBackItemItemButton'))
assert(commands[#commands][1]=='merchant_buyback' and commands[#commands][2]==44)
MERCHANT_BUYBACK='Buyback';MerchantFrame.selectedTab=2;MerchantFrame_Update()
assert(MerchantItem1Name.text=='Old sale' and MerchantItem2Name.text=='New sale' and not MerchantItem3ItemButton.shown)
assert(WoWPS_LocalMerchantActivate('MerchantItem1ItemButton'))
assert(commands[#commands][1]=='merchant_buyback' and commands[#commands][2]==40)
assert(GetBuybackItemLink(1):find('item:117:',1,true))
local countBefore=#commands;assert(not BuybackItem(0) and not BuybackItem(13));assert(#commands==countBefore)
table.remove(m.buyback,1);MerchantFrame_OnEvent(MerchantFrame,'MERCHANT_UPDATE')
assert(MerchantItem1Name.text=='New sale' and not MerchantItem2ItemButton.shown)
assert(WoWPS_LocalMerchantActivate('MerchantItem1ItemButton') and commands[#commands][2]==44)
MerchantFrame.selectedTab=1;MerchantFrame_Update()
assert(GetNumGossipOptions()==1 and select(2,GetGossipOptions())=='vendor')
SelectGossipOption(1);assert(commands[#commands][1]=='merchant_open')
m.repair=true;MerchantFrame_OnEvent(MerchantFrame,'MERCHANT_UPDATE');assert(MerchantRepairAllButton.shown)
local price,damaged=GetRepairAllCost();assert(price==0 and not damaged)
RepairAllItems();assert(commands[#commands][1]=='merchant_repair')
MerchantFrame_OnEvent(MerchantFrame,'MERCHANT_CLOSED');assert(not MerchantFrame:IsShown())
MerchantFrame_OnHide();assert(commands[#commands][1]=='merchant_close')
m.open=false;assert(not WoWPS_LocalMerchantActivate('ContainerFrame1Item24'))
assert(UseContainerItem(0,1)=='online-use') -- delegates to earlier local bag API in the real VM
__WoWPSLocal=nil;assert(UnitName('NPC')=='Online')
print('PASS actual MerchantFrame Lua: stock/paging, bundle purchase, high-price confirmation, selected bag sale, stable-ID buyback tab/compact widget, gossip, repair, close and controller activation')
