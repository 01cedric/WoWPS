assert(GetContainerNumSlots(0)==7 and UseContainerItem(0,1)=='online-use' and CastSpellByName('Fireball')=='online-cast')
-- 1.82 HF1: the PS4 SkillFrame OnLoad queries the selected row before any
-- profession exists. Exercise its arithmetic consumer, not only populated rows.
local function checkSkillDetail(index, expectedName, expectedRank)
    local name,header,expanded,rank,temp,modifier,maximum,abandonable,stepCost,rankCost,level,costType,description=GetSkillLineInfo(index)
    assert(rank+temp+modifier==expectedRank and maximum>=0, 'SkillDetailFrame arithmetic must be safe')
    assert(select('#',GetSkillLineInfo(index))==13, 'skill rows need all 13 results')
    assert(name==expectedName and not header and expanded and type(abandonable)=='boolean')
    assert(stepCost==nil and rankCost==nil, 'zero costs are truthy and show bogus training buttons')
    assert(type(level)=='number' and type(costType)=='number' and type(description)=='string')
end
__WoWPSLocal={}
checkSkillDetail(nil,'',0)
assert(GetNumSkillLines()==0 and GetSelectedSkill()==0)
__WoWPSLocal={professions={}}
checkSkillDetail(GetSelectedSkill(),'',0)
checkSkillDetail(nil,'',0)
checkSkillDetail(0,'',0)
checkSkillDetail(-1,'',0)
checkSkillDetail(1,'',0)
checkSkillDetail(0/0,'',0)
assert(GetNumSkillLines()==0 and GetSelectedSkill()==0)
__WoWPSLocal=nil
print('PASS skill startup: empty/nil/zero/out-of-range rows keep numeric arithmetic and nil training costs')
local commands={}
local rejectCommand=false
function __WoWPSLocalCommand(name,id,count,...) commands[#commands+1]={name,id,count,...};return not rejectCommand end
local cursorIcon
function __WoWPSLocalBankCursorIcon(icon) cursorIcon=icon end
local function last(name,id,count)local c=commands[#commands];assert(c[1]==name and c[2]==id and c[3]==count)end
__WoWPSLocal={bags={{id=117,count=6,name='Bread',icon='bread',equip=0}},bank={[2]={id=2392,count=3,name='Chest',icon='chest'}},bankOpen=true,banker=true,
    professions={{id=164,name='Blacksmithing',rank=23,max=75,primary=true},{id=129,name='First Aid',rank=4,max=75,primary=false}},
    trainer=true,trainerSkill=164,training={{id=164,name='Journeyman',action='train_rank',cost=500}},craftSkill=164,
    recipes={{id=300,name='Plate',chance=800,available=3,itemId=2392,made=2,icon='chest',reagents={{id=117,name='Bread',icon='bread',need=2,have=6}}}}}
assert(GetContainerNumSlots(-1)==28 and GetContainerNumFreeSlots(-1)==27 and GetContainerNumFreeSlots(0)==23)
assert(GetContainerItemID(-1,1)==nil and GetContainerItemID(-1,2)==2392)
assert(GetInventoryItemTexture('player',41)=='chest' and GetInventoryItemCount('player',41)==3)
assert(GetItemCount(2392)==0 and GetItemCount('item:2392',true)==3)
assert(UseContainerItem(0,1));last('bank_deposit',117,6)
local before=#commands
assert(PickupContainerItem(-1,2) and CursorHasItem() and GetCursorInfo()=='item' and cursorIcon=='chest')
assert(#commands==before) -- Pickup is not a transfer.
assert(PickupContainerItem(-1,1));last('bank_move',2,3)
local c=commands[#commands];assert(c[4]==1 and c[5]==2392 and c[6]==0 and c[7]==3 and c[8]==0)
assert(not CursorHasItem() and cursorIcon=='')
assert(SplitContainerItem(-1,2,1) and CursorHasItem());assert(PickupContainerItem(0,3));last('bank_withdraw',2,1)
assert(commands[#commands][4]==2392 and commands[#commands][5]==3)
assert(PickupInventoryItem(41) and CursorHasItem());ClearCursor();assert(not CursorHasItem() and cursorIcon=='')
assert(UseInventoryItem(41));last('bank_withdraw',2,3)
assert(BankButtonIDToInvSlotID(2)==41 and BankButtonIDToInvSlotID(1,true)==68)
assert(GetInventoryItemID('player',41)==2392)
assert(PickupInventoryItem(41) and IsInventoryItemLocked(41));assert(not PlaceAction(1) and not CursorHasItem())
-- A stale cursor must never submit the newly arrived stack instead.
assert(PickupInventoryItem(41));__WoWPSLocal.bank[2].count=4
before=#commands;assert(not CursorHasItem() and #commands==before);__WoWPSLocal.bank[2].count=3
-- Full/partial merge, bounded split and explicit cancel use the original APIs.
__WoWPSLocal.bank[3]={id=2392,count=19,maxStack=20,name='Chest',icon='chest'}
assert(PickupContainerItem(-1,2));assert(PickupContainerItem(-1,3));last('bank_move',2,1)
assert(commands[#commands][4]==3 and commands[#commands][6]==2392 and commands[#commands][8]==19)
assert(not SplitContainerItem(-1,2,0) and not SplitContainerItem(-1,2,4))
assert(PickupContainerItem(-1,2));before=#commands;assert(PickupContainerItem(-1,2) and not CursorHasItem() and #commands==before)
__WoWPSLocal.bank[3]=nil
assert(PickupContainerItem(-1,2));rejectCommand=true
assert(not PickupContainerItem(-1,1) and CursorHasItem())
rejectCommand=false;ClearCursor();assert(not CursorHasItem())
-- Character/NPC transitions cannot retain a prior bank reservation.
assert(PickupContainerItem(-1,2));__WoWPSLocal.bankOwner='different';assert(not CursorHasItem())
assert(PickupContainerItem(-1,2));__WoWPSLocal.bankNpc='different';assert(not CursorHasItem())
assert(__WoWPSLocal.bags[1].count==6 and __WoWPSLocal.bank[2].count==3) -- Await authority, no optimistic item mutation.
BankFrameItem2={GetID=function()return 2 end};assert(WoWPS_LocalBankActivate('BankFrameItem2') and CursorHasItem())
ContainerFrame1Item1={GetID=function()return 1 end,GetParent=function()return {GetID=function()return 0 end}end}
assert(WoWPS_LocalBankActivate('ContainerFrame1Item1'));last('bank_withdraw',2,3)
assert(WoWPS_LocalBankActivate('ContainerFrame1Item1') and CursorHasItem());assert(PickupContainerItem(-1,1));last('bank_deposit_slot',1,6)
assert(GetNumBankSlots()==0 and GetBankSlotCost()==0)
assert(PickupInventoryItem(41));CloseBankFrame();assert(not CursorHasItem());last('bank_close',0,0)
__WoWPSLocal.bankOpen=false;assert(not WoWPS_LocalBankActivate('BankFrameItem2'))
assert(GetNumGossipOptions()==3);SelectGossipOption(1);last('bank_open',0,0)
SelectGossipOption(2);last('trainer_open',0,0);SelectGossipOption(3);last('craft_open',164,0)
assert(GetNumSkillLines()==2)
local name,header,expanded,rank,_,_,maximum,abandonable=GetSkillLineInfo(1)
assert(name=='Blacksmithing' and rank==23 and maximum==75 and abandonable and not header)
SetSelectedSkill(1);AbandonSkill();last('unlearn_profession',164,0)
local before=#commands;AbandonSkill(2);assert(#commands==before)
assert(GetTrainerServiceInfo(1)=='Journeyman' and GetTrainerServiceCost(1)==500)
BuyTrainerService(1);last('train_rank',164,0)
assert(OpenTradeSkill('Blacksmithing'));last('craft_open',164,0)
assert(CastSpellByName('Blacksmithing'));last('craft_open',164,0)
assert(CastSpellByName('Fireball')=='online-cast')
assert(GetNumTradeSkills()==1 and GetTradeSkillLine()=='Blacksmithing')
local n,difficulty,available=GetTradeSkillInfo(1);assert(n=='Plate' and difficulty=='medium' and available==3)
local r,icon,need,have=GetTradeSkillReagentInfo(1,1);assert(r=='Bread' and need==2 and have==6)
assert(GetTradeSkillNumMade(1)==2 and GetTradeSkillNumReagents(1)==1)
DoTradeSkill(1,3);last('craft',300,3)
CloseTradeSkill();last('craft_close',0,0)
-- 1.74: backpack pickup/split waits for a chosen bank destination.
__WoWPSLocal.bankOpen=true
before=#commands;assert(PickupContainerItem(0,1) and CursorHasItem() and #commands==before)
local _,_,locked=GetContainerItemInfo(0,1);assert(locked)
assert(PickupContainerItem(-1,28));last('bank_deposit_slot',1,6)
local c=commands[#commands];assert(c[4]==28 and c[5]==117 and c[6]==0 and c[7]==6 and c[8]==0)
assert(not CursorHasItem() and __WoWPSLocal.bags[1].count==6)
assert(SplitContainerItem(0,1,2));assert(PickupContainerItem(-1,28));last('bank_deposit_slot',1,2)
__WoWPSLocal.bank[3]={id=117,count=19,maxStack=20,name='Bread',icon='bread'}
assert(PickupContainerItem(0,1));assert(PickupContainerItem(-1,3));last('bank_deposit_slot',1,1)
assert(commands[#commands][6]==117 and commands[#commands][8]==19)
__WoWPSLocal.bank[3]=nil
assert(PickupContainerItem(0,1));assert(PickupContainerItem(-1,2));last('bank_deposit_slot',1,6)
assert(commands[#commands][6]==2392 and commands[#commands][8]==3)
assert(PickupContainerItem(0,1));__WoWPSLocal.bags[1].count=5;assert(not CursorHasItem());__WoWPSLocal.bags[1].count=6
before=#commands;assert(PickupContainerItem(0,1));ClearCursor();assert(not CursorHasItem() and #commands==before)
assert(PickupContainerItem(0,1));rejectCommand=true;assert(not PickupContainerItem(-1,28) and CursorHasItem());rejectCommand=false;ClearCursor()
assert(PickupContainerItem(0,1));assert(PickupContainerItem(0,1) and not CursorHasItem())
assert(not SplitContainerItem(0,1,0) and not SplitContainerItem(0,1,7) and not SplitContainerItem(0,1,0/0))
__WoWPSLocal.bankOpen=false
__WoWPSLocal.merchant={open=true,items={},buyback={{id=10,itemId=117,name='Oldest',icon='bread',price=3,count=1},{id=20,itemId=2392,name='Older',icon='chest',price=10,count=2},{id=30,itemId=117,name='Newest',icon='bread',price=5,count=1}}}
assert(GetBuybackItemInfo(1)=='Oldest' and GetBuybackItemInfo(3)=='Newest')
BuybackItem(1);last('merchant_buyback',10,0)
MerchantBuyBackItemItemButton={};assert(WoWPS_LocalMerchantActivate('MerchantBuyBackItemItemButton'));last('merchant_buyback',30,0)
before=#commands;assert(not BuybackItem(0) and not BuybackItem(4) and not BuybackItem(0/0));assert(#commands==before)
__WoWPSLocal.merchant=nil
print('PASS bank/merchant Lua APIs: targeted backpack pickup/split/merge/swap, cursor locks/cancel/stale snapshots, exact destination payload and newest quick buyback')
-- 1.73: original class trainer services and their prerequisite presentation.
__WoWPSLocal.tradeTrainer=false
__WoWPSLocal.trainerSkill=0
__WoWPSLocal.training={{id=900,name='Class rank',action='learn_spell',cost=100,level=20,available=true},
    {id=164,name='Journeyman',action='train_rank',cost=10000,level=10,skillName='Blacksmithing',skill=50,available=false,skillMet=false}}
assert(not IsTradeskillTrainer())
local n,_,status=GetTrainerServiceInfo(1);assert(n=='Class rank' and status=='available' and GetTrainerServiceLevelReq(1)==20)
BuyTrainerService(1);last('learn_spell',900,0)
local skillName,required,hasReq=GetTrainerServiceSkillReq(2);assert(skillName=='Blacksmithing' and required==50 and hasReq==false)
__WoWPSLocal.training[2].skillMet=true;local _,_,met=GetTrainerServiceSkillReq(2);assert(met==true)
local _,_,status=GetTrainerServiceInfo(2);assert(status=='unavailable')
before=#commands;BuyTrainerService(2);assert(#commands==before)
__WoWPSLocal.recipes[1].tools={{name='Hammer',have=true},{name='Tool',have=false}}
local tool,owned,other,missing=GetTradeSkillTools(1);assert(tool=='Hammer' and owned==true and other=='Tool' and missing==false)
__WoWPSLocal.recipes[1].unavailable='Additional recipe effects are not implemented'
assert(GetTradeSkillDescription(1)==__WoWPSLocal.recipes[1].unavailable)
before=#commands;DoTradeSkill(1,1);assert(#commands==before)
print('PASS progression Lua APIs: class trainer purchase, unavailable rank, level/skill prerequisites, exact tool presentation and blocked-recipe description')
-- Selection follows the skill identity across snapshots, never another row.
__WoWPSLocal.bankOwner='skill-owner'
SetSelectedSkill(1);checkSkillDetail(GetSelectedSkill(),'Blacksmithing',23)
__WoWPSLocal.professions={__WoWPSLocal.professions[2],__WoWPSLocal.professions[1]}
assert(GetSelectedSkill()==2)
checkSkillDetail(GetSelectedSkill(),'Blacksmithing',23)
__WoWPSLocal.professions={__WoWPSLocal.professions[1]}
before=#commands;assert(GetSelectedSkill()==0);AbandonSkill();assert(#commands==before)
checkSkillDetail(GetSelectedSkill(),'',0)
for _,invalid in ipairs({0,-1,2,0.5,0/0,'bad',math.huge}) do
    SetSelectedSkill(invalid);assert(GetSelectedSkill()==0)
    checkSkillDetail(invalid,'',0);AbandonSkill(invalid)
end
assert(#commands==before)
SetSelectedSkill(nil);assert(GetSelectedSkill()==0)
__WoWPSLocal.bankOwner='next-owner'
__WoWPSLocal.professions={{id=171,name='Alchemy',rank=1,max=75,primary=true}}
assert(GetSelectedSkill()==1);checkSkillDetail(1,'Alchemy',1)
print('PASS skill selection: reorder/removal/invalid indices/character changes cannot retain a stale row')
__WoWPSLocal=nil;assert(GetContainerNumSlots(-1)==7 and UseContainerItem(0,1)=='online-use' and CastSpellByName('Fireball')=='online-cast')
print('PASS services Lua APIs: bank cursor/split/move/merge/cancel/stale-source/controller commands, bank slots/item queries/counts, unchanged pending snapshots, controller activation, gossip, profession confirmation callback, trainer purchase, recipe/reagent/batch APIs and connected fallbacks')
