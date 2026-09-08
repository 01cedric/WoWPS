-- Actual unmodified3.3.5 auction script; widgets/API are host fixtures. This
-- exercises the production load/show snippets and retail event/button code.
UIPanelWindows={};MoneyTypeInfo={};StaticPopupDialogs={}
BUYOUT_PRICE='Buyout';OPTIONAL='optional';BROWSE_SEARCH_TEXT='Search'
local methods={}
function methods:GetID()return self.id end
function methods:SetText(text)self.text=text end
function methods:SetTexture(texture)self.texture=texture end
function methods:Show()self.shown=true end
function methods:Hide()self.shown=false end
function methods:IsShown()return self.shown end
function methods:RegisterEvent(event)self.events[event]=true end
function methods:Disable()self.disabled=true end
function methods:ClearFocus()end
function methods:SetValue(value)self.value=value end
function methods:SetMinMaxValues(a,b)self.low=a;self.high=b end
function methods:SetFormattedText(...)self.formatted={...}end
local function widget(name,id)local f=setmetatable({id=id,events={}}, {__index=methods});_G[name]=f;return f end
local ownerQueries,bidQueries=0,0
local sounds,showCalls={},0
function GetOwnerAuctionItems()ownerQueries=ownerQueries+1 end
function GetBidderAuctionItems()bidQueries=bidQueries+1 end
function PanelTemplates_SetNumTabs(f,n)f.numTabs=n end
function PanelTemplates_SetTab(f,n)f.selectedTab=n end
function MoneyInputFrame_SetPreviousFocus()end
function MoneyInputFrame_SetNextFocus()end
function MoneyInputFrame_ClearFocus()end
function FauxScrollFrame_SetOffset(f,offset)f.offset=offset end
function PlaySound(s)sounds[#sounds+1]=s end
function SetPortraitTexture(t,unit)t.unit=unit end
function SetAuctionsTabShowing(flag)assert(type(flag)=='boolean');auctionTab=flag end
function HideUIPanel(f)f:Hide()end
function ShowUIPanel(f)
 showCalls=showCalls+1;f:Show();AuctionFrame_OnShow(f)
end
local calls={}
function PlaceAuctionBid(kind,index,amount)calls.bid={kind,index,amount}end
function CancelAuction(index)calls.cancel=index end
function GetSelectedAuctionItem()return 2 end
function CloseAuctionHouse()calls.close=true end
function GetAuctionSellItemInfo()return 'Linen','Interface\\Icons\\INV_Fabric_Linen_01' end
local loads=0
function LoadAddOn(name)
 assert(name=='Blizzard_AuctionUI');loads=loads+1
 assert(loadfile(retailRoot..'/Blizzard_AuctionUI.lua'))()
 widget('AuctionFrame')
 for _,name in ipairs({'AuctionFrameBrowse','AuctionFrameBid','AuctionFrameAuctions',
  'AuctionsBuyoutText','AuctionsStackSizeEntry','AuctionsNumStacksEntry','BuyoutPriceCopper',
  'BrowseBidPrice','BrowseMaxLevel','BrowseName','BidBidPrice','BidBidPriceCopper','BidBidPriceGold',
  'StartPrice','StartPriceGold','StartPriceCopper','BuyoutPrice','BuyoutPriceGold',
  'BrowseScrollFrame','BidScrollFrame','AuctionsScrollFrame','BrowsePrevPageButton','BrowseNextPageButton',
  'AuctionPortraitTexture','BrowseNoResultsText','AuctionFrameTopLeft','AuctionFrameTop','AuctionFrameTopRight',
  'AuctionFrameBotLeft','AuctionFrameBot','AuctionFrameBotRight','AuctionsCreateAuctionButton',
  'AuctionsBlockFrame','AuctionProgressBar','AuctionProgressBarText','AuctionProgressBarIcon','AuctionProgressFrame'}) do widget(name)end
 for i=1,3 do widget('AuctionFrameTab'..i,i)end
 AuctionFrame_OnLoad(AuctionFrame)
 return true
end
assert(not rawget(_G,'AuctionFrame'))
assert(loadstring(productionLoad))()
assert(loads==1 and AuctionFrame.numTabs==3 and not AuctionFrame:IsShown())
-- Handler result data may be populated now; showing uses the original helper,
-- original OnShow, original portrait and original tab textures.
assert(loadstring(productionShow))()
assert(AuctionFrame:IsShown() and AuctionFrameBrowse:IsShown() and AuctionFrame.type=='list')
assert(AuctionPortraitTexture.unit=='npc' and not auctionTab)
assert(AuctionFrameTopLeft.texture=='Interface\\AuctionFrame\\UI-AuctionFrame-Browse-TopLeft')
assert(ownerQueries==2 and bidQueries==2 and not calls.close)
assert(loadstring(productionShow))();assert(showCalls==1)
AuctionFrameTab_OnClick(AuctionFrameTab2)
assert(AuctionFrameBid:IsShown() and not AuctionFrameBrowse:IsShown() and AuctionFrame.type=='bidder')
AuctionFrameTab_OnClick(AuctionFrameTab3)
assert(AuctionFrameAuctions:IsShown() and auctionTab)
AuctionFrame.type='list';AuctionFrame.buyoutPrice=3210
StaticPopupDialogs.BUYOUT_AUCTION.OnAccept({})
assert(calls.bid[1]=='list' and calls.bid[2]==2 and calls.bid[3]==3210)
StaticPopupDialogs.CANCEL_AUCTION.OnAccept({});assert(calls.cancel==2)
AuctionFrameAuctions_OnEvent(AuctionFrameAuctions,'AUCTION_MULTISELL_START',4)
--3.3.5 handler uses global arg1/arg2; LuaEngine dispatch supplies those globals.
arg1=4;arg2=4
AuctionFrameAuctions_OnEvent(AuctionFrameAuctions,'AUCTION_MULTISELL_UPDATE',4,4)
assert(not AuctionsBlockFrame:IsShown() and AuctionProgressFrame.fadeOut)
AuctionFrame_Hide();assert(not AuctionFrame:IsShown())
LoadAddOn=function()return false,'MISSING'end
assert(not pcall(assert(loadstring(productionLoad))))
print('PASS original AuctionUI: demand load before show, native tabs/textures, portrait, buyout/cancel callbacks, close, load failure')
