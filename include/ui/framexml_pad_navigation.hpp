#pragma once
#include "ui/widget_tree.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace wowee::ui {
inline constexpr std::string_view kPadPanels[] = {
    "CharacterFrame", "SpellBookFrame", "QuestLogFrame", "WorldMapFrame",
    "ContainerFrame1", "ContainerFrame2", "ContainerFrame3", "ContainerFrame4", "ContainerFrame5",
    "GossipFrame", "QuestFrame", "MerchantFrame", "BankFrame", "ClassTrainerFrame", "AuctionFrame",
    "TalentFrame", "PlayerTalentFrame", "TradeSkillFrame", "MailFrame", "TradeFrame",
    "GameMenuFrame", "InterfaceOptionsFrame", "VideoOptionsFrame", "AudioOptionsFrame",
    "StaticPopup1", "StaticPopup2", "StaticPopup3", "StaticPopup4"
    , "DropDownList1", "DropDownList2", "DropDownList3"
};
inline bool padModalPanel(std::string_view name) {
    return name.starts_with("DropDownList") || name.starts_with("StaticPopup") || name.ends_with("OptionsFrame") || name=="GameMenuFrame";
}
inline bool padWidgetShown(const WidgetTree& tree, const Widget* w) {
    for (int depth=0; w && depth<128; ++depth) {
        if (!w->shown) return false;
        if (!w->parent) return true;
        w=tree.get(w->parent);
    }
    return false;
}
inline const Widget* padOwningPanel(const WidgetTree& tree, const Widget* w) {
    for (int depth=0; w && depth<128; ++depth) {
        for (auto name:kPadPanels) if (w->name==name) return w;
        w=tree.get(w->parent);
    }
    return nullptr;
}
inline bool padPanelAllows(const WidgetTree& tree, const Widget& top, const Widget& w,
                           bool carriedAction=false) {
    const auto* owner=padOwningPanel(tree,&w);
    if (padModalPanel(top.name)) return owner && owner->id==top.id;
    return carriedAction || (owner && owner->visible && padWidgetShown(tree,owner) &&
                             !padModalPanel(owner->name) && owner->name!="WorldMapFrame");
}
// Authored NPC panels create their close control before the useful actions.
// Rank only the initial choice; ordinary directional navigation still includes
// every eligible control, including Close. The caller supplies real hit testing.
inline int padInitialPriority(std::string_view panel, std::string_view control) {
    if(panel=="AuctionFrame") {
        if(control=="BrowseSearchButton")return 0;
        if(control.starts_with("AuctionFilterButton"))return 1;
        if(control.find("CloseButton")!=std::string_view::npos)return 4;
        return 2;
    }
    if (panel!="QuestFrame" && panel!="GossipFrame") return 0;
    if (control=="QuestFrameAcceptButton" || control=="QuestFrameCompleteButton" ||
        control=="QuestFrameCompleteQuestButton") return 0;
    if (control.starts_with("GossipTitleButton") || control.starts_with("QuestTitleButton")) return 1;
    if (control=="QuestFrameDeclineButton" || control=="QuestFrameCancelButton" ||
        control=="QuestFrameGoodbyeButton" || control=="QuestFrameGreetingGoodbyeButton" ||
        control=="GossipFrameGreetingGoodbyeButton") return 2;
    if (control=="QuestFrameCloseButton" || control=="GossipFrameCloseButton") return 4;
    return 3;
}
template<class Eligible>
inline const Widget* padInitialControl(const WidgetTree& tree, uint32_t rootId, Eligible eligible) {
    const auto* root=tree.get(rootId);
    if (!root) return nullptr;
    const Widget* best=nullptr;
    int priority=100;
    for (int pass=0;pass<2 && !best;++pass)
        for (uint32_t id=1;id<tree.size();++id) if (const auto* w=tree.get(id);eligible(w)) {
            const auto* owner=padOwningPanel(tree,w);
            if (pass==0 && (!owner || owner->id!=rootId)) continue;
            const int rank=pass==0?padInitialPriority(root->name,w->name):0;
            if (rank<priority) {best=w;priority=rank;}
            if (rank==0) break;
        }
    return best;
}
struct PadControlPoint { float x,y; }; // Unscaled, bottom-left widget coordinates.
inline std::optional<PadControlPoint> padControlPoint(const WidgetTree& tree, const Widget* w,
                                                     float viewportWidth, float viewportHeight) {
    if (!w || !w->visible || !w->enabled || !w->mouseEnabled || w->alpha<=.001f ||
        !padWidgetShown(tree,w) || w->rectW<=0 || w->rectH<=0) return {};
    if (w->objectType!="Button" && w->objectType!="CheckButton" &&
        w->objectType!="Slider" && w->objectType!="EditBox") return {};
    float l=w->left+w->hitInsetLeft*w->effScale;
    float r=w->left+w->rectW-w->hitInsetRight*w->effScale;
    float b=w->bottom+w->hitInsetBottom*w->effScale;
    float t=w->bottom+w->rectH-w->hitInsetTop*w->effScale;
    if (!std::isfinite(l) || !std::isfinite(r) || !std::isfinite(b) || !std::isfinite(t) ||
        !std::isfinite(viewportWidth) || !std::isfinite(viewportHeight)) return {};
    l=std::max(l,0.f); r=std::min(r,viewportWidth);
    b=std::max(b,0.f); t=std::min(t,viewportHeight);
    uint32_t clipId=w->clipTo;
    for (int depth=0; clipId && depth<128; ++depth) {
        const auto* clip=tree.get(clipId); if (!clip) return {};
        l=std::max(l,clip->left); r=std::min(r,clip->left+clip->rectW);
        b=std::max(b,clip->bottom); t=std::min(t,clip->bottom+clip->rectH);
        clipId=clip->clipTo;
    }
    if (clipId || r<=l || t<=b) return {};
    // Prefer the visible centre; sample inset points if a sibling covers it.
    // Every point must still hit this exact control in production hit ordering.
    for (float fy:{.5f,.1f,.9f}) for (float fx:{.5f,.1f,.9f}) {
        PadControlPoint p{l+(r-l)*fx,b+(t-b)*fy};
        if (tree.hitTest(p.x,p.y)==w->id) return p;
    }
    return {};
}
} // namespace wowee::ui
