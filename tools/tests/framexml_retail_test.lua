-- Runs unmodified retail Lua supplied outside the project, after the local API.
-- The widgets are host fixtures; this verifies real script control flow, not GPU pixels.
assert(loadfile(retailRoot..'/WatchFrame.lua'))()
assert(loadfile(retailRoot..'/ContainerFrame.lua'))()
ceil=math.ceil;mod=math.fmod;max=math.max
function table.wipe(t)for k in pairs(t)do t[k]=nil end return t end
bit={band=function(a,b)local n,p=0,1;while a>0 and b>0 do
 if a%2==1 and b%2==1 then n=n+p end;a=math.floor(a/2);b=math.floor(b/2);p=p*2 end;return n end}
local methods={}
function methods:Reset()end
function methods:GetName()return self.name end
function methods:SetHeight(h)self.height=h end
function methods:GetHeight()return self.height or 37 end
function methods:SetWidth(w)self.width=w end
function methods:GetWidth()return self.width or 100 end
function methods:SetTexCoord(...)self.uv={...}end
function methods:SetTexture(v)self.texture=v end
function methods:SetText(v)self.textValue=v end
function methods:ClearAllPoints()self.point=nil end
function methods:SetPoint(...)self.point={...}end
function methods:Show()self.shown=true end
function methods:Hide()self.shown=false end
function methods:IsShown()return self.shown end
function methods:SetID(id)self.id=id end
function methods:GetID()return self.id end
function methods:GetTop()return 400 end
function methods:GetBottom()return self.bottom or 100 end
local function widget(name)local w=setmetatable({name=name},{__index=methods});_G[name]=w;return w end
function methods:CreateTexture(name)return widget(name) end
local bag=widget('ContainerFrame1');bag.bags={};bag.bagsShown=0
for _,n in ipairs({'BackgroundTop','BackgroundMiddle1','BackgroundMiddle2','BackgroundBottom','Background1Slot','MoneyFrame','Name','Portrait','PortraitButton'})do widget('ContainerFrame1'..n)end
for i=1,36 do widget('ContainerFrame1Item'..i)end
function SetBagPortraitTexture()end
function GetBagName()return 'Backpack'end
function updateContainerFrameAnchors()end -- screen placement is outside GenerateFrame
__WoWPSLocal={name='Human',money=0,log={11},quests={[11]={id=11,title='Trial',level=1,active=true,complete=false,
 objectives={{text='Boar',done=1,count=4,type='monster'},{text='Wolf',done=2,count=6,type='monster'}}}},bags={},spells={}}
assert(WoWPS_InstallLocalFrameXmlHooks())
ContainerFrame_GenerateFrame(bag,24,0)
assert(bag.height==322 and ContainerFrame1Item1.point[5]==-290)
assert(ContainerFrame1Item1.id==24 and ContainerFrame1Item24.id==1 and not ContainerFrame1Item25.shown)
assert(not ContainerFrame1BackgroundMiddle1.shown and not ContainerFrame1BackgroundMiddle2.shown)
assert(#bag.wowpsBackpackRows==6 and ContainerFrame1MoneyFrame.point[5]==-298)
-- Solve actual anchored rectangles, including retail's 256px artwork on a
-- 192px frame. Merely asserting SetPoint arguments missed the old +64px shift.
local fractions={TOPRIGHT={1,1},TOPLEFT={0,1},BOTTOMRIGHT={1,0},BOTTOMLEFT={0,0}}
local function rect(w)
 if type(w)=='string' then w=_G[w] end
 if w==bag then return 100,200,bag.width,bag.height end
 local p=w.point;assert(p,'missing anchor '..w.name)
 local rx,ry,rw,rh=rect(p[2]);local a,b=fractions[p[1]],fractions[p[3]]
 local width,height=w.width or 37,w.height or 37
 return rx+b[1]*rw+p[4]-a[1]*width,ry+b[2]*rh+p[5]-a[2]*height,width,height
end
for row,strip in ipairs(bag.wowpsBackpackRows) do
 local x,y,w,h=rect(strip)
 assert(x==36 and w==256 and h==41) -- transparent left margin, original scale
 assert(strip.uv[3]==48/256 and strip.uv[4]==89/256)
 for column=1,4 do
  local button=_G['ContainerFrame1Item'..((6-row)*4+column)]
  local bx,by,bw,bh=rect(button)
  assert(bx>=x+64 and bx+bw<=x+w and by>=y and by+bh==y+h)
 end
end
assert(ContainerFrame1BackgroundBottom.uv[3]==212/256)

-- Original row anchoring: the sixth row stays below the header after extension.
assert(ContainerFrame1Item21.point[2]=='ContainerFrame1Item17' and ContainerFrame1Item21.point[5]==4)
local watch=widget('WatchFrame');watch.bottom=0
watch.buttonCache={GetFrame=function()return widget('testLink')end}
watch.lineCache={ReleaseFrame=function()end,GetFrame=function()
 local line=widget('testLine');line.text=widget('testText');line.dash=widget('testDash');line.frameCache=watch.lineCache;return line end}
function GetCurrentMapZone()return 1 end
function WatchFrame_ResetQuestLines()end
function WatchFrame_GetQuestLine()return widget('testLine')end
function WatchFrame_ReleaseUnusedQuestLines()end
local lines={}
function WatchFrame_SetLine(line,prev,spacing,header,text)line.text=widget('testText');lines[#lines+1]=text end
function QuestPOI_HideButtons()end
WorldMapFrame=nil
WATCHFRAME_FILTER_TYPE=3 -- default CVar: completed quests included, remote filter on
function WatchFrame_Update()end -- use the unmodified tracked-quest traversal directly
WoWPS_RefreshLocalQuestTracking();WatchFrame_GetCurrentMapQuests()
WatchFrame_DisplayTrackedQuests(watch,0,400,204)
assert(#VISIBLE_WATCHES==1 and #lines==3)
assert(table.concat(lines,'|'):find('Boar') and table.concat(lines,'|'):find('1 / 4'))
assert(table.concat(lines,'|'):find('Wolf') and table.concat(lines,'|'):find('2 / 6'))
__WoWPSLocal.quests[11].objectives[1].done=3;lines={}
WatchFrame_GetCurrentMapQuests();WatchFrame_DisplayTrackedQuests(watch,0,400,204)
assert(table.concat(lines,'|'):find('3 / 4'))
__WoWPSLocal.quests[11].complete=true;lines={}
WatchFrame_DisplayTrackedQuests(watch,0,400,204)
assert(#lines==2 and lines[2]:find('quest giver'))
print('PASS actual retail Lua: late hooks, all 24 slots aligned to unscaled artwork, money footer, two objectives, progress and completion')

-- The character micro button in retail XML deliberately has no OnClick.
-- Verify its actual handlers complete the toggle on a full mouse press/release.
local xmlFile=assert(io.open(retailRoot..'/MainMenuBarMicroButtons.xml','rb'))
local xml=xmlFile:read('*a');xmlFile:close()
local block=assert(xml:match('<Button name="CharacterMicroButton".-%>(.-)</Button>'))
assert(not block:find('<OnClick'))
local down=assert(loadstring('return function(self,button) '..assert(block:match('<OnMouseDown>(.-)</OnMouseDown>'))..' end'))()
local up=assert(loadstring('return function(self,button) '..assert(block:match('<OnMouseUp>(.-)</OnMouseUp>'))..' end'))()
local character={over=true,state='NORMAL',IsMouseOver=function(self)return self.over end,
 GetButtonState=function(self)return self.state end}
local toggles=0
function CharacterMicroButton_SetPushed()end
function CharacterMicroButton_SetNormal()end
function UpdateMicroButtons()end
function ToggleCharacter(panel)assert(panel=='PaperDollFrame');toggles=toggles+1 end
for i=1,2 do down(character,'LeftButton');up(character,'LeftButton') end
assert(toggles==2 and not character.down)
character.over=false;down(character,'LeftButton');up(character,'LeftButton');assert(toggles==2)
print('PASS actual retail XML: character panel mouse-only toggle opens/closes once, release outside cancels')
