assert(GetQuestReward(3)=='online-3' and GetNumQuestChoices()==99)
local calls={}
__WoWPSLocalCommand=function(action,id,choice)calls[#calls+1]={action,id,choice};return true end
local q={id=1,title='Synthetic reward quest',level=1,complete=true,active=true,objectives={},rewards={},choices={}}
for i=1,4 do q.rewards[i]={id=100+i,name='Fixed '..i,count=i,icon='Icon'..i} end
for i=1,6 do q.choices[i]={id=200+i,name='Choice '..i,count=i,icon='ChoiceIcon'..i} end
__WoWPSLocal={name='Tester',selected=1,quests={[1]=q},log={1},bags={},spells={}}
assert(GetNumQuestRewards()==4 and GetNumQuestChoices()==6)
assert(GetNumQuestLogRewards()==4 and GetNumQuestLogChoices()==6)
for i=1,6 do
 local name,icon,count=GetQuestItemInfo('choice',i)
 assert(name=='Choice '..i and icon=='ChoiceIcon'..i and count==i)
 assert(GetQuestItemLink('choice',i)=='item:'..(200+i))
 assert(GetQuestLogChoiceInfo(i)==name and GetQuestLogItemLink('choice',i)=='item:'..(200+i))
end
for i=1,4 do assert(GetQuestLogRewardInfo(i)=='Fixed '..i and GetQuestItemInfo('reward',i)=='Fixed '..i)end
assert(GetQuestItemInfo('choice',7)==nil and GetQuestItemInfo('invalid',1)==nil)
assert(GetQuestReward()==false)
for _,bad in ipairs({0,-1,7,1.5,math.huge,0/0,'2',false})do assert(GetQuestReward(bad)==false)end
assert(#calls==0)
for i=1,6 do assert(GetQuestReward(i));assert(calls[#calls][1]=='turnin' and calls[#calls][2]==1 and calls[#calls][3]==i)end
q.choices={};q.rewards=nil;q.reward={id=5,name='Legacy',count=2}
assert(GetNumQuestChoices()==0 and GetNumQuestRewards()==1 and GetQuestLogRewardInfo(1)=='Legacy')
assert(GetQuestReward(1)==false and GetQuestReward() and calls[#calls][3]==0)
__WoWPSLocal=nil
assert(GetQuestReward(4)=='online-4' and GetNumQuestChoices()==99)
print('PASS quest reward FrameXML Lua: four fixed/six choices, dialog/log item APIs, explicit selection, invalid values, legacy fallback and connected-mode delegation')
