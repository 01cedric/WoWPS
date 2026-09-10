local calls={}
function __WoWPSLocalCommand(...)calls[#calls+1]={...};return true end
__WoWPSLocal={bankOwner='1',bags={},bank={},professions={},ridingSkill=75,onTaxi=true,flightMaster=true,taxiKnown=false,flights={{id=2,name='Destination',cost=25}},training={{action='train_riding',id=150,name='Journeyman Riding',cost=500000,level=40,skillName='Riding',skill=75,available=true,skillMet=true}},trainer=true,tradeTrainer=false}
assert(loadfile(arg[3]))()
assert(GetNumGossipOptions()==3)
SelectGossipOption(1);assert(calls[#calls][1]=='taxi_discover')
SelectGossipOption(2);assert(calls[#calls][1]=='taxi_fly' and calls[#calls][2]==2)
SelectGossipOption(3);assert(calls[#calls][1]=='trainer_open')
__WoWPSLocal.taxiKnown=true;assert(GetNumGossipOptions()==2)
assert(UnitOnTaxi('player') and not UnitOnTaxi('party1'))
print('PASS travel Lua: discovery and priced flight gossip, explicit action routing and player taxi state')
assert(GetNumSkillLines()==1);local name,_,_,skill,_,_,maximum=GetSkillLineInfo(1);assert(name=='Riding' and skill==75 and maximum==75)
assert(not IsTradeskillTrainer());assert(GetNumTrainerServices()==1);BuyTrainerService(1);assert(calls[#calls][1]=='train_riding' and calls[#calls][2]==150)
__WoWPSLocal=nil;assert(GetNumSkillLines()==nil)
print('PASS riding Lua: separate skill row and original trainer purchase route, non-tradeskill status and online fallback')
