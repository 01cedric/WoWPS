#include "ui/framexml_pad_navigation.hpp"
#include <cstdio>
#include "ui/local_pad_focus.hpp"
#include "game/auction_filters.hpp"
#include <cstdlib>
#include <limits>
using namespace wowee::ui;
static void require(bool ok,const char* text) { if(!ok){std::fprintf(stderr,"FAIL %s\n",text);std::exit(1);} }
static uint32_t make(WidgetTree& tree,const char* name,uint32_t parent,float x,float y,float width=40,float height=40) {
    auto id=tree.create(WidgetKind::Frame,parent,name); auto& w=*tree.get(id);
    w.objectType="Button";w.visible=w.shown=w.enabled=w.mouseEnabled=true;
    w.left=x;w.bottom=y;w.rectW=width;w.rectH=height;w.effScale=1;w.alpha=1;w.effLevel=10;
    return id;
}
int main() {
    require(!localTargetOwnsShoulders(1,false,false),"combat target keeps spell navigation");
    require(localTargetOwnsShoulders(1,false,false,false,true),"noncombat target keeps interaction");
    require(localTargetOwnsShoulders(0,false,true),"Triangle clears bar immediately");
    require(!localTargetOwnsShoulders(1,false,false,true),"explicit release preserves target for bar spells");
    require(!localTargetOwnsShoulders(0,false,false),"cleared target restores bars");
    require(!localTargetOwnsShoulders(1,true,false),"opened panel still navigable");
    WidgetTree dropdownTree;auto dropdown=make(dropdownTree,"DropDownList1",0,0,0);auto option=make(dropdownTree,"DropDownList1Button1",dropdown,0,0);auto auction=make(dropdownTree,"AuctionFrame",0,200,0);
    require(padModalPanel("DropDownList1") && padPanelAllows(dropdownTree,*dropdownTree.get(dropdown),*dropdownTree.get(option)),"dropdown owns its option");
    require(!padPanelAllows(dropdownTree,*dropdownTree.get(dropdown),*dropdownTree.get(auction)),"dropdown excludes underlying auction");
    std::puts("PASS target shoulder ownership and dropdown focus scope");
    int subs=0;const auto* misc=wowee::game::auctionSubsFor(15,subs);
    require(subs==7 && misc && misc[6].subId==5,"Miscellaneous Mount maps to subclass5");
    const auto* goods=wowee::game::auctionSubsFor(7,subs);
    require(subs==17 && goods && goods[6].subId==5,"Trade Goods Cloth maps to subclass5");
    require(padInitialPriority("AuctionFrame","BrowseSearchButton")<padInitialPriority("AuctionFrame","AuctionFrameCloseButton"),"auction initial focus favors Search");
    std::puts("PASS Miscellaneous/Trade Goods filter IDs and auction Search priority");
    WidgetTree t;
    auto bank=make(t,"BankFrame",0,0,0,200,300);t.get(bank)->mouseEnabled=false;
    auto bag=make(t,"ContainerFrame1",0,300,0,200,300);t.get(bag)->mouseEnabled=false;
    auto a=make(t,"BankFrameItem1",bank,10,20),b=make(t,"ContainerFrame1Item1",bag,310,20);
    require(padPanelAllows(t,*t.get(bag),*t.get(a)),"bank reachable with bag on top");
    require(padPanelAllows(t,*t.get(bank),*t.get(b)),"bag reachable with bank on top");
    require(padControlPoint(t,t.get(a),800,600).has_value(),"bank target hits");
    require(padControlPoint(t,t.get(b),800,600).has_value(),"bag target hits");
    auto action=make(t,"ActionButton1",0,600,10);
    require(!padPanelAllows(t,*t.get(bank),*t.get(action)),"action bar excluded with empty cursor");
    require(padPanelAllows(t,*t.get(bank),*t.get(action),true),"carried action can reach bar");
    std::puts("PASS side-by-side bank/bag scope and carried action scope");

    auto popup=make(t,"StaticPopup1",0,500,200);t.get(popup)->mouseEnabled=false;
    auto accept=make(t,"StaticPopup1Button1",popup,500,200);
    require(padPanelAllows(t,*t.get(popup),*t.get(accept)),"popup accepts own button");
    require(!padPanelAllows(t,*t.get(popup),*t.get(a)),"popup blocks bank");
    require(!padPanelAllows(t,*t.get(popup),*t.get(action),true),"popup blocks carried action");
    require(padModalPanel("GameMenuFrame") && padModalPanel("InterfaceOptionsFrame"),"menus capture pad");
    t.get(bank)->shown=false;
    require(!padPanelAllows(t,*t.get(bag),*t.get(a)) && !padControlPoint(t,t.get(a),800,600),"hidden ancestor rejected before layout");
    t.get(bank)->shown=true;
    std::puts("PASS modal isolation and immediately hidden ancestor");

    auto clip=make(t,"ScrollFrame",bank,0,100,200,100);t.get(clip)->mouseEnabled=false;
    auto row=make(t,"QuestRow",clip,20,185,100,50);t.get(row)->clipTo=clip;
    auto p=padControlPoint(t,t.get(row),800,600);
    require(p && p->y>=185 && p->y<200,"partially clipped row has visible target");
    require(t.hitTest(p->x,p->y)==row,"clipped target dispatches to row");
    t.get(row)->bottom=201;
    require(!padControlPoint(t,t.get(row),800,600),"fully clipped row unreachable");
    t.get(row)->bottom=185;
    auto outer=make(t,"OuterScrollFrame",bank,0,100,200,90);t.get(outer)->mouseEnabled=false;t.get(clip)->clipTo=outer;
    p=padControlPoint(t,t.get(row),800,600);
    require(p && p->y<190,"nested clipping respected");
    std::puts("PASS partial, full and nested scroll clipping");

    auto inset=make(t,"InsetButton",bag,320,100,50,100);t.get(inset)->hitInsetTop=80;
    p=padControlPoint(t,t.get(inset),800,600);
    require(p && p->y<120 && t.hitTest(p->x,p->y)==inset,"asymmetric insets use actual hit area");
    t.get(inset)->hitInsetBottom=30;
    require(!padControlPoint(t,t.get(inset),800,600),"empty inset area rejected");
    t.get(inset)->hitInsetBottom=0;
    auto edge=make(t,"EdgeButton",bag,780,500,100,50);
    p=padControlPoint(t,t.get(edge),800,600);
    require(p && p->x<800,"partly offscreen control reachable");
    t.get(edge)->left=801;require(!padControlPoint(t,t.get(edge),800,600),"offscreen control rejected");
    std::puts("PASS inset and viewport intersection");

    auto cover=make(t,"Cover",bag,330,100,30,40);t.get(cover)->effLevel=20;
    p=padControlPoint(t,t.get(inset),800,600);
    require(p && t.hitTest(p->x,p->y)==inset,"uncovered part of control remains reachable");
    t.get(cover)->left=310;t.get(cover)->rectW=100;
    require(!padControlPoint(t,t.get(inset),800,600),"covered control cannot receive synthetic click");
    std::puts("PASS occlusion follows production hit ordering");

    t.get(b)->enabled=false;require(!padControlPoint(t,t.get(b),800,600),"disabled rejected");t.get(b)->enabled=true;
    t.get(b)->left=std::numeric_limits<float>::quiet_NaN();require(!padControlPoint(t,t.get(b),800,600),"NaN rejected");t.get(b)->left=310;
    for(float scale:{.64f,1.f,1.5f}) for(auto width:{1280.f,1920.f,3840.f}) {
        p=padControlPoint(t,t.get(b),width/scale,1080/scale);
        require(p && t.hitTest((p->x*scale)/scale,(p->y*scale)/scale)==b,"scaled screen/widget point roundtrip");
    }
    t.get(bank)->parent=bank;require(!padWidgetShown(t,t.get(a)),"malformed ancestry terminates");
    std::puts("PASS disabled/invalid controls and resolution/scale roundtrips");

    WidgetTree q;
    auto quest=make(q,"QuestFrame",0,0,0,300,500);q.get(quest)->mouseEnabled=false;
    auto close=make(q,"QuestFrameCloseButton",quest,250,450);
    auto cancel=make(q,"QuestFrameCancelButton",quest,150,10);
    auto complete=make(q,"QuestFrameCompleteQuestButton",quest,10,10);
    auto next=make(q,"QuestFrameCompleteButton",quest,10,10);
    auto take=make(q,"QuestFrameAcceptButton",quest,10,10);
    auto shown=[](WidgetTree& tree,uint32_t id,bool value){tree.get(id)->shown=tree.get(id)->visible=value;};
    shown(q,complete,false);shown(q,next,false);
    auto eligible=[&](const Widget* w){return w && padPanelAllows(q,*q.get(quest),*w) && padControlPoint(q,w,800,600).has_value();};
    require(padInitialControl(q,quest,eligible)->id==take,"accept preferred despite close/cancel creation order");
    require(eligible(q.get(close)),"close remains reachable");
    shown(q,take,false);shown(q,next,true);
    require(padInitialControl(q,quest,eligible)->id==next,"progress selects Continue in reused QuestFrame");
    shown(q,next,false);shown(q,complete,true);
    require(padInitialControl(q,quest,eligible)->id==complete,"reward selects Complete Quest in reused QuestFrame");
    std::puts("PASS NPC quest accept/progress/reward initial actions and reachable close");

    q.get(complete)->enabled=false;
    require(padInitialControl(q,quest,eligible)->id==cancel,"disabled completion falls back to bottom cancel");
    q.get(complete)->enabled=true;
    auto overlay=make(q,"Overlay",quest,0,0,100,60);q.get(overlay)->effLevel=30;
    require(padInitialControl(q,quest,eligible)->id==cancel,"covered completion is skipped");
    shown(q,cancel,false);shown(q,overlay,false);shown(q,complete,false);
    require(padInitialControl(q,quest,eligible)->id==close,"close still usable when no action exists");
    shown(q,close,false);
    require(!padInitialControl(q,quest,eligible),"empty dialog has no invisible focus");
    std::puts("PASS disabled/covered NPC actions and empty-dialog fallback");

    WidgetTree g;
    auto gossip=make(g,"GossipFrame",0,0,0,300,500);g.get(gossip)->mouseEnabled=false;
    make(g,"GossipFrameCloseButton",gossip,250,450);
    auto bye=make(g,"GossipFrameGreetingGoodbyeButton",gossip,150,10);
    auto option1=make(g,"GossipTitleButton1",gossip,10,350);
    auto option2=make(g,"GossipTitleButton2",gossip,10,300);
    auto gossipEligible=[&](const Widget* w){return w && padPanelAllows(g,*g.get(gossip),*w) && padControlPoint(g,w,800,600).has_value();};
    require(padInitialControl(g,gossip,gossipEligible)->id==option1,"gossip begins on first dialog option");
    shown(g,option1,false);
    require(padInitialControl(g,gossip,gossipEligible)->id==option2,"hidden gossip option skipped");
    shown(g,option2,false);
    require(padInitialControl(g,gossip,gossipEligible)->id==bye,"greeting without options begins on Goodbye");
    std::puts("PASS gossip options before Goodbye and close");

    auto modal=make(g,"StaticPopup1",0,400,100);g.get(modal)->mouseEnabled=false;
    auto yes=make(g,"StaticPopup1Button1",modal,400,100);
    auto modalEligible=[&](const Widget* w){return w && padPanelAllows(g,*g.get(modal),*w) && padControlPoint(g,w,800,600).has_value();};
    require(padInitialControl(g,modal,modalEligible)->id==yes,"NPC preferences cannot escape modal");
    require(!padInitialControl(g,0,modalEligible),"missing root has no initial focus");
    std::puts("PASS NPC initial focus preserves modal ownership");
}
