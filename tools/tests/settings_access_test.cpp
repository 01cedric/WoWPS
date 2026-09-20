#include "ui/framexml_frame_type.hpp"
#include "ui/framexml_pad_navigation.hpp"
#include "ui/settings_schema.hpp"
#include "addons/addon_lua_snippets.hpp"
#include <cstdio>
#include <cstdlib>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
using namespace wowee;
static void require(bool ok,const char* message) { if(!ok){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);} }
// The test runner extracts the exact production CreateFrame widget-init block.
// Lua markers are harmless with a real empty table atop this host state.
static void initialize(lua_State* L,ui::WidgetTree* tree,uint32_t id,const char* frameType) {
 bool createdStatusBar=false,createdSlider=false;
#include "frame_create_init.inc"
}
static int schema(lua_State* L) {
 std::size_t count=0;const auto* rows=ui::clientSettingsSchema(count);lua_newtable(L);
 for(std::size_t i=0;i<count;++i){const auto& s=rows[i];lua_newtable(L);
  const auto str=[&](const char* k,const char* v){lua_pushstring(L,v);lua_setfield(L,-2,k);};
  const auto num=[&](const char* k,float v){lua_pushnumber(L,v);lua_setfield(L,-2,k);};
  str("key",s.key);str("label",s.label);str("category",s.category);str("section",s.section);
  str("tooltip",s.tooltip);str("choices",s.choices);str("enabledwhen",s.enabledWhen);
  str("kind",s.kind==ui::SettingKind::Bool?"bool":s.kind==ui::SettingKind::Enum?"enum":"float");
  num("min",s.minValue);num("max",s.maxValue);num("step",s.step);num("default",s.defaultValue);
  lua_rawseti(L,-2,i+1);
 }return 1;
}
static void run(lua_State* L,const char* script){if(luaL_dostring(L,script)){std::fprintf(stderr,"Lua: %s\n",lua_tostring(L,-1));std::exit(1);}}
int main(int argc,char** argv){
 require(argc==2,"fixture path");lua_State* L=luaL_newstate();luaL_openlibs(L);lua_newtable(L);
 for(const char* kind:{"BUTTON","button","bUtToN","Button"}){
  ui::WidgetTree tree;const auto root=tree.create(ui::WidgetKind::Frame,0,"VideoOptionsFrame");
  auto* r=tree.get(root);r->shown=r->visible=true;r->mouseEnabled=false;
  const auto id=tree.create(ui::WidgetKind::Frame,root,"OptionsListButton1");initialize(L,&tree,id,kind);
  auto* w=tree.get(id);w->shown=w->visible=w->enabled=true;w->left=10;w->bottom=10;w->rectW=180;w->rectH=20;w->effScale=w->alpha=1;w->effLevel=10;
  require(w->objectType=="Button" && w->mouseEnabled,"case-insensitive button defaults");
  require(ui::padPanelAllows(tree,*tree.get(root),*w),"category belongs to active Video modal");
  auto point=ui::padControlPoint(tree,w,800,600);require(point.has_value(),"category is pad reachable, not only toggle");
  require(tree.hitTest(point->x,point->y)==id,"pad activation hits actual category row");
  // Reproduce the implementation even when the template repairs mouseEnabled: the pad
  // rejects uppercase type identity, independently of pointer hit testing.
  w->objectType="BUTTON";w->mouseEnabled=true;
  require(tree.hitTest(point->x,point->y)==id,"old uppercase row can still receive pointer hits");
  require(!ui::padControlPoint(tree,w,800,600),"the reference uppercase row rejected despite template enabling mouse");
  w->objectType=ui::canonicalFrameType("BUTTON");
  require(ui::padControlPoint(tree,w,800,600).has_value(),"normalization restores row navigation");
 }
 for(const char* kind:{"SLIDER","CHECKBUTTON","EDITBOX","STATUSBAR","SCROLLFRAME","COOLDOWN","SIMPLEHTML"}){
  ui::WidgetTree tree;const auto id=tree.create(ui::WidgetKind::Frame,0,"typed");initialize(L,&tree,id,kind);
  const auto* w=tree.get(id);const auto type=ui::canonicalFrameType(kind);require(w->objectType==type,"other frame types normalized");
  if(type=="Slider")require(w->isSlider && w->mouseEnabled && w->barVertical,"slider state");
  if(type=="EditBox")require(w->isEditBox && w->mouseEnabled,"editbox state");
  if(type=="StatusBar")require(w->isStatusBar,"statusbar state");
  if(type=="SimpleHTML")require(w->isSimpleHtml,"HTML state");
  if(type=="Cooldown")require(w->isCooldown,"cooldown state");
 }
 std::size_t n=0; const auto* rows=ui::clientSettingsSchema(n);
 for(std::size_t i=0;i<n;++i) if(std::string(rows[i].category)=="Lighting" || std::string(rows[i].key)=="shadowquality") {
  ui::WidgetTree tree;const auto modal=tree.create(ui::WidgetKind::Frame,0,"VideoOptionsFrame");
  auto* m=tree.get(modal);m->shown=m->visible=true;m->mouseEnabled=false;
  const auto panel=tree.create(ui::WidgetKind::Frame,modal,"WoweeOptionsLighting");
  auto* p=tree.get(panel);p->shown=p->visible=true;p->mouseEnabled=false;
  const std::string name=std::string("WoweeOptionsLighting")+rows[i].key+(rows[i].kind==ui::SettingKind::Enum?"Button":"");
  const auto id=tree.create(ui::WidgetKind::Frame,panel,name);
  const char* kind=rows[i].kind==ui::SettingKind::Bool?"CheckButton":rows[i].kind==ui::SettingKind::Enum?"Button":"Slider";
  initialize(L,&tree,id,kind);auto* w=tree.get(id);
  w->shown=w->visible=w->enabled=true;w->left=24;w->bottom=100;w->rectW=170;w->rectH=20;w->effScale=w->alpha=1;w->effLevel=10;
  require(ui::padPanelAllows(tree,*tree.get(modal),*w),"Lighting row belongs to Video pad modal");
  auto point=ui::padControlPoint(tree,w,800,600);
  require(point.has_value() && tree.hitTest(point->x,point->y)==id,"Lighting and shadow toggle controller activation hits row");
 }
 std::puts("PASS the reference schema Lighting controls and existing shadow toggle: production pad selection + hit testing");
 require(ui::canonicalFrameType("CustomType")=="CustomType","unknown names preserved");
 std::puts("PASS actual CreateFrame initializer: uppercase/mixed/lowercase Button hit testing + modal pad reachability; other typed defaults");
 lua_pop(L,1);lua_pushcfunction(L,schema);lua_setglobal(L,"WoweeSettingList");
 if(luaL_dofile(L,argv[1])){std::fprintf(stderr,"fixture: %s\n",lua_tostring(L,-1));return 1;}
 run(L,addons::kWoweeOptionsPanelLua);run(L,"verifySettingsAccess()");lua_close(L);
}
