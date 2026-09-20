-- Host widget/template fixtures, not retail pixels. Executes the complete
-- production options-registration script, then its dropdown callbacks.
local methods = {}
function methods:GetName() return self.frameName end
function methods:GetWidth() return self.width or 413 end
function methods:GetHeight() return self.height or 428 end
function methods:SetWidth(v) self.width=v end
function methods:SetHeight(v) self.height=v end
function methods:SetParent(v) self.owner=v end
function methods:GetParent() return self.owner end
function methods:SetAllPoints(v) self.width=v:GetWidth();self.height=v:GetHeight() end
function methods:Hide() self.shown=false end
function methods:Show() self.shown=true end
function methods:SetText(v) self.text=v end
function methods:GetText() return self.text or '' end
function methods:SetScript(k,v) self.scripts[k]=v end
function methods:SetChecked(v) self.checked=v end
function methods:GetChecked() return self.checked end
function methods:Enable() self.enabled=true end
function methods:Disable() self.enabled=false end
function methods:SetValue(v) self.value=v end
function methods:SetPoint(point,a,b,c,d)
 if point=='TOPLEFT' and type(a)=='number' then self.anchorX=a;self.anchorY=b end
end
local noop=function()end
for _,k in ipairs({'ClearAllPoints','SetJustifyH','SetJustifyV','SetTexture',
 'SetVertexColor','SetAutoFocus','ClearFocus','SetTextColor','SetAlpha','SetMinMaxValues','SetValueStep'}) do methods[k]=noop end
function CreateFrame(kind,name,parent)
 local f=setmetatable({frameName=name,kind=kind,owner=parent,scripts={},enabled=true}, {__index=methods})
 if name then _G[name]=f end
 return f
end
function methods:CreateFontString(name) return CreateFrame('FontString',name,self) end
function methods:CreateTexture(name) return CreateFrame('Texture',name,self) end
__WoweeInterfaceVersion=30300
local values={}
function WoweeGetSetting(k) return values[k] or '0' end
function WoweeSetSetting(k,v) values[k]=v end
function WoweeVersion() return 'the implementation test' end
for _,name in ipairs({'VideoOptionsFrame','AudioOptionsFrame','InterfaceOptionsFrame'}) do
 local f=CreateFrame('Frame',name)
 f.panelContainer=CreateFrame('Frame',name..'PanelContainer',f)
 f.categories={}; f.categoryFrame={update=noop}
end
function OptionsFrame_AddCategory(host,panel)
 -- Model the real nesting contract, including its collapsed initial state.
 for _,p in ipairs(host.categories) do
  if panel.parent==p.name then p.collapsed=true;panel.hidden=true end
 end
 table.insert(host.categories,panel)
end
function InterfaceOptions_AddCategory(p) OptionsFrame_AddCategory(InterfaceOptionsFrame,p) end
InterfaceCategoryList_Update=noop
UIDropDownMenu_SetWidth=noop
UIDropDownMenu_SetText=function(f,v) f.selectedText=v end
UIDropDownMenu_EnableDropDown=function(f) f.enabled=true end
UIDropDownMenu_DisableDropDown=function(f) f.enabled=false end
UIDropDownMenu_Initialize=function(f,cb) f.initialize=cb end
UIDropDownMenu_CreateInfo=function() return {} end
local buttons={}
UIDropDownMenu_AddButton=function(info) buttons[#buttons+1]=info end
CloseDropDownMenus=noop
function verifySettingsAccess()
 local effects=assert(WoweeOptionsEffects)
 assert(effects.owner==VideoOptionsFrame.panelContainer,'Effects must be parented to Video')
 local found=false
 for _,p in ipairs(VideoOptionsFrame.categories) do if p==effects then found=true end end
 assert(found,'Effects registered in Video list')
 for _,p in ipairs(InterfaceOptionsFrame.categories) do assert(p~=effects,'Effects not misplaced in Interface') end
 for _,host in ipairs({VideoOptionsFrame,AudioOptionsFrame,InterfaceOptionsFrame}) do
  for _,p in ipairs(host.categories) do
   if p.parent then assert(p.hidden==false,'child category is visible: '..p.name)
   else assert(not p.collapsed,'heading starts expanded') end
  end
 end
 effects.refresh()
 local dropdown=assert(WoweeOptionsEffectsvolumetricdebug)
 assert(dropdown.enabled,'inspection dropdown enabled')
 dropdown.initialize(dropdown,1)
 assert(#buttons==5,'all inspection modes reachable')
 assert(buttons[5].text=='Surface shadow factor')
 buttons[5].func(buttons[5]);assert(values.volumetricdebug=='4')
 effects.okay();buttons[1].func(buttons[1]);assert(values.volumetricdebug=='0')
 effects.cancel();assert(values.volumetricdebug=='4','Cancel restores opening value')
 effects.default();assert(values.volumetricdebug=='0','Defaults restore Normal')
 local lighting=assert(WoweeOptionsLighting)
 assert(lighting.owner==VideoOptionsFrame.panelContainer,'Lighting belongs to Video')
 local ls={}
 for _,s in ipairs(WoweeSettingList()) do if s.category=='Lighting' then
  local f=assert(_G['WoweeOptionsLighting'..s.key],s.key)
  assert(f.anchorY and f.anchorY >= -390 and f.anchorY <= -52,'reachable vertical layout '..s.key)
  assert(f.anchorX >= -4 and f.anchorX < 413,'reachable horizontal layout '..s.key)
  ls[s.key]=f
 end end
 lighting.default()
 assert(values.volumetricraysenabled=='1' and values.volumetricfogenabled=='1' and values.bloomenabled=='1')
 ls.volumetricfogintensity.scripts.OnValueChanged(ls.volumetricfogintensity,.65)
 ls.bloomintensity.scripts.OnValueChanged(ls.bloomintensity,.45)
 lighting.okay()
 for _,k in ipairs({'volumetricraysenabled','volumetricfogenabled','bloomenabled'}) do
  local b=ls[k];b:SetChecked(false);b.scripts.OnClick(b);assert(values[k]=='0')
 end
 assert(tonumber(values.volumetricfogintensity)==.65 and tonumber(values.bloomintensity)==.45,'off preserves saved levels')
 assert(not ls.volumetricfogintensity.enabled and not ls.bloomintensity.enabled,'sliders disabled with toggle')
 lighting.cancel()
 assert(values.volumetricraysenabled=='1' and values.volumetricfogenabled=='1' and values.bloomenabled=='1','cancel restores independent toggles')
 assert(tonumber(values.volumetricfogintensity)==.65 and tonumber(values.bloomintensity)==.45,'cancel retains opening levels')
 ls.volumetricfogintensity.scripts.OnValueChanged(ls.volumetricfogintensity,0)
 assert(tonumber(values.volumetricfogintensity)==0 and values.volumetricraysenabled=='1','fog density zero does not disable rays')
 lighting.default()
 assert(math.abs(tonumber(values.volumetricfogintensity)-.35)<.00001 and tonumber(values.bloomintensity)==.25,'defaults density and glow')
 print('PASS the implementation production Lighting Lua: authored layout fits 413x428, independent toggles, saved levels, apply/cancel/default')
 print('PASS complete production options Lua: Effects Video routing, expanded children, 5 inspection choices, apply/cancel/default')
end
