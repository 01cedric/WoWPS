local calls,native={},{}
function __WoWPSLocalCommand(...) calls[#calls+1]={...};return true end
function ClearCursor()end
function PickupContainerItem(...)native[#native+1]={'pickup',...}end
function PlaceAction(...)native[#native+1]={'action',...};return true end
function UseContainerItem(...)native[#native+1]={'use',...};return true end
function GetCursorInfo()return nil end
function CursorHasItem()return false end
function __WoWPSLocalBankCursorIcon(icon)_G.cursorIcon=icon end
__WoWPSLocal={bankOwner='1',bankNpc='10',bankOpen=false,bags={[1]={id=117,name='Bread',icon='bread',count=10,maxStack=20},[6]={id=118,name='Potion',icon='potion',count=4,maxStack=20},[24]={id=900001,name='Helmet',icon='helm',count=1,maxStack=1}},bank={[1]={id=117,name='Bread',icon='bread',count=18,maxStack=20}},professions={}}
assert(loadfile(arg[3]))()
assert(GetContainerNumFreeSlots(0)==21 and GetContainerNumSlots(0)==24)
assert(PickupContainerItem(0,6) and CursorHasItem());assert(PickupContainerItem(0,14));local c=calls[#calls]
assert(c[1]=='bag_move' and c[2]==6 and c[3]==4 and c[4]==14 and c[5]==118 and c[6]==0 and c[7]==4 and c[8]==0)
assert(not CursorHasItem());assert(SplitContainerItem(0,1,3));assert(PickupContainerItem(0,15));assert(calls[#calls][3]==3)
assert(not SplitContainerItem(0,1,0) and not SplitContainerItem(0,1,1.5) and not PickupContainerItem(0,25))
assert(PickupContainerItem(0,6));__WoWPSLocal.bags[6].count=3;assert(not CursorHasItem());local n=#calls;assert(not PickupContainerItem(0,14) and #calls==n)
print('PASS production bag Lua: sparse free count, physical destinations, split snapshots and stale/invalid cursor rejection')
__WoWPSLocal.bankOpen=true
assert(UseContainerItem(0,6));local quick=calls[#calls];assert(quick[1]=='bank_deposit_from_slot' and quick[2]==6 and quick[3]==3 and quick[4]==118 and quick[5]==3)
assert(PickupContainerItem(-1,1));assert(PickupContainerItem(0,1));c=calls[#calls]
assert(c[1]=='bank_withdraw_slot' and c[2]==1 and c[3]==10 and c[4]==1 and c[7]==18 and c[8]==10)
assert(PickupContainerItem(0,24));assert(PickupContainerItem(-1,28));assert(calls[#calls][1]=='bank_deposit_slot' and calls[#calls][2]==24)
assert(PickupContainerItem(-1,1));__WoWPSLocal.bankOpen=false;assert(not CursorHasItem())
print('PASS production bank Lua: chosen backpack cell, merge capacity, cross-bank placement and close cleanup')
assert(PickupContainerItem(0,24));assert(PlaceAction(2));assert(native[#native-1][1]=='pickup' and native[#native-1][3]==24 and native[#native][1]=='action')
assert(SplitContainerItem(0,1,2));n=#native;assert(not PlaceAction(2));assert(#native==n and not CursorHasItem())
local bagButton={GetID=function()return 24 end,GetParent=function()return {GetID=function()return 0 end}end};ContainerFrame1Item1=bagButton
assert(WoWPS_LocalBankActivate('ContainerFrame1Item1'));assert(CursorHasItem());ClearCursor()
__WoWPSLocal.merchant={open=true};assert(not WoWPS_LocalBankActivate('ContainerFrame1Item1'));__WoWPSLocal.merchant=nil
print('PASS production controller/action bar: bag pickup/drop, native full-stack action handoff, split refusal and merchant priority')
SendMailFrame={IsShown=function()return true end}
n=#native;assert(UseContainerItem(0,24));assert(calls[#calls][1]=='mail_attach' and calls[#calls][2]==24 and #native==n)
assert(WoWPS_LocalBankActivate('ContainerFrame1Item1'));assert(calls[#calls][1]=='mail_attach')
SendMailFrame=nil;assert(PickupContainerItem(0,6));SendMailFrame={IsShown=function()return true end};assert(ClickSendMailItem(1));assert(calls[#calls][1]=='mail_attach' and calls[#calls][2]==6 and not CursorHasItem())
__WoWPSLocal=nil;UseContainerItem(0,1);assert(native[#native][1]=='use')
print('PASS production mail attachment routing: use/controller/drop attach instead of consuming, connected fallback retained')
