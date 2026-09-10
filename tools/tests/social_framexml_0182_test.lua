local commands,events={},{}
local serverCalls=0
function InitiateTrade()serverCalls=serverCalls+1 end
function GetTime()return 100 end
function UnitName(u)return ({player='Host',party1='Guest',target='Guest'})[u]end
function ClearCursor()end
function PickupContainerItem()return 'previous-pick' end
function GetCursorInfo()return nil end
function CursorHasItem()return false end
function __WoWPSLocalBankCursorIcon(icon)_G.icon=icon end
function __WoWPSLocalSocialCommand(...)commands[#commands+1]={...};return true end
function __WoWPSLocalSocialEvent(...)events[#events+1]={...}end
function StaticPopup_Hide(name)_G.hidden=name end
__WoWPSLocal={name='Host',bankOwner='1',time=100,bags={{id=117,name='Bread',count=10,icon='BreadIcon'}},party={members={{guid='1',name='Host'},{guid='2',name='Guest'}}},social={ignores={},ready={id=7,state=1,initiator='Host',remaining=30,members={{guid='1',name='Host',answer=1},{guid='2',name='Guest',answer=0}}},trade={id=3,revision=5,state=2,side=0,peerName='Guest',ownMoney=20,peerMoney=10,ownAccepted=false,peerAccepted=false,ownItems={{id=117,name='Bread',icon='BreadIcon',count=6}},peerItems={}}}}
function __WoWPSLocalCommand(...)commands[#commands+1]={...};return true end
assert(loadfile(arg[2]))();assert(loadfile(arg[3]))();assert(loadfile(arg[1]))()
__WoWPSLocal.innkeeper=true
assert(GetNumGossipOptions()==1);local mailOption=GetGossipOptions();assert(mailOption=='I would like to check my mail.')
assert(SelectGossipOption(1) and commands[#commands][1]=='mail_open');__WoWPSLocal.innkeeper=false
print('PASS production innkeeper gossip: local mail option and mail_open dispatch')
assert(DoReadyCheck() and commands[#commands][1]=='ready_start')
assert(ConfirmReadyCheck(false) and commands[#commands][3]==7 and commands[#commands][5]==0)
assert(ConfirmReadyCheck(0) and commands[#commands][5]==0)
assert(ConfirmReadyCheck(true) and commands[#commands][5]==1)
assert(GetReadyCheckStatus('player')=='ready' and GetReadyCheckStatus('party1')=='waiting' and GetReadyCheckStatus('party4')==nil)
assert(GetReadyCheckTimeLeft()==30)
assert(AddIgnore('party1') and commands[#commands][2]=='Guest')
assert(not AddIgnore('party4'));__WoWPSLocal.social.ignores={'Guest'}
assert(GetNumIgnores()==1 and GetIgnoreName(1)=='Guest');SetSelectedIgnore(1);assert(GetSelectedIgnore()==1)
assert(AddOrDelIgnore('gUeSt') and commands[#commands][1]=='ignore_remove')
assert(InitiateTrade('party1') and commands[#commands][1]=='trade_request' and commands[#commands][2]=='Guest' and serverCalls==0)
assert(not InitiateTrade('party4'));assert(SetTradeMoney(100) and commands[#commands][5]==100)
local count=#commands;assert(not SetTradeMoney(-1) and not SetTradeMoney(0/0) and not SetTradeMoney(1.5) and not SetTradeMoney(1000000001));assert(#commands==count)
local name,texture,quantity,usable,enchant=GetTradePlayerItemInfo(1);assert(name=='Bread' and texture=='BreadIcon' and quantity==6 and usable==true and enchant==nil)
assert(GetTradePlayerItemInfo(7)==nil and GetTradeTargetItemInfo(1)==nil and GetPlayerTradeMoney()==20 and GetTargetTradeMoney()==10)
assert(SplitContainerItem(0,1,4));assert(CursorHasItem() and icon=='BreadIcon');assert(ClickTradeButton(2));local c=commands[#commands]
assert(c[1]=='trade_offer' and c[3]==3 and c[4]==5 and c[5]==4 and c[6]==0 and c[7]==1 and c[8]==117 and c[9]==10);assert(not CursorHasItem() and icon=='')
assert(PickupContainerItem(0,1));__WoWPSLocal.social.trade.revision=6;count=#commands;assert(not ClickTradeButton(1));assert(#commands==count and not CursorHasItem())
assert(PickupContainerItem(0,1));__WoWPSLocal.bags[1].count=9;assert(not CursorHasItem());assert(not ClickTradeButton(7));assert(not SplitContainerItem(0,1,0))
assert(PickupContainerItem(0,1));assert(WoWPS_LocalSocialDrop() and not CursorHasItem());assert(not WoWPS_LocalSocialDrop())
assert(PickupContainerItem(0,1));ClearCursor();assert(not CursorHasItem())
WoWPS_LocalSocialUpdate();local function seen(name)local n=0;for _,e in ipairs(events)do if e[1]==name then n=n+1 end end;return n end
assert(seen('READY_CHECK')==1 and seen('TRADE_SHOW')==1 and seen('IGNORELIST_UPDATE')==1)
local n=#events;WoWPS_LocalSocialUpdate();assert(#events==n)
-- Real publications replace snapshots; they do not mutate the previous table.
local s=__WoWPSLocal.social;s.ready={id=7,state=2,members={{guid='1',name='Host',answer=1},{guid='2',name='Guest',answer=2}}}
__WoWPSLocal.social={ready=s.ready,ignores={'Guest'},trade={id=3,revision=7,state=4}}
-- Restore old snapshot's active ready row for change detection.
s.ready={id=7,state=1,members={{guid='1',name='Host',answer=1},{guid='2',name='Guest',answer=0}}}
WoWPS_LocalSocialUpdate();assert(seen('READY_CHECK_FINISHED')==1 and seen('TRADE_CLOSED')==1)
__WoWPSLocal.social={ready={state=0},ignores={},trade={id=4,revision=1,state=1,side=1,peerName='Guest'}};WoWPS_LocalSocialUpdate();assert(seen('TRADE_REQUEST')==1)
assert(BeginTrade() and commands[#commands][1]=='trade_open');assert(CancelTrade() and commands[#commands][1]=='trade_cancel')
__WoWPSLocal.social={ready={id=8,state=1,initiator='Host',remaining=30,members={}},trade={id=5,revision=2,state=2,side=0},ignores={}};WoWPS_LocalSocialUpdate()
-- Both production cursor bridges coexist: a bank pickup reaches its original
-- payload path when there is no open trade, then ClearCursor releases both.
local oldTrade=__WoWPSLocal.social.trade
__WoWPSLocal.social.trade={state=0};__WoWPSLocal.bankOpen=true;__WoWPSLocal.bankNpc='44';__WoWPSLocal.bank={}
assert(PickupContainerItem(0,1));assert(CursorHasItem());ClearCursor();assert(not CursorHasItem())
__WoWPSLocal.bankOpen=false;__WoWPSLocal.social.trade=oldTrade
__WoWPSLocal=nil;WoWPS_LocalSocialUpdate();assert(seen('READY_CHECK_FINISHED')==2 and seen('TRADE_CLOSED')==2)
InitiateTrade('target');assert(serverCalls==1)
print('PASS production social Lua APIs: ready/ignore/trade actions, exact six-slot cursor snapshots, stale and invalid rejection, item tuple, modal events, disconnect cleanup bank cursor coexistence and server fallback')
