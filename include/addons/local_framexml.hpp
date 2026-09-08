#pragma once
#include <functional>
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include "game/local_ui_changes.hpp"
#include "ui/local_pad_focus.hpp"
struct lua_State;
namespace wowee::game { class LocalRealm; class GameHandler; }
namespace wowee::addons {
class LuaEngine;

/// What a pad Cross press means for the control with this name.
///
/// A pad has no drag, so picking something up has to be a button contract
/// rather than a synthesised mouse gesture - and the only thing a focused
/// control carries that says which of the interface's three pickup calls owns
/// the press is the name the interface gave it. Parsed rather than looked up in
/// a table because the bars and the bags run to a hundred-odd buttons between
/// them and a written-out table of every name would drift from the XML that
/// creates them.
///
/// The numbers are the ones in the name and nothing more. Which bag a
/// ContainerFrame is showing, and which page an action button is on, are
/// runtime facts the widget itself answers; putting the name's number to that
/// use is how an item ends up in the wrong bag.
struct PadPickupTarget {
    enum class Kind : std::uint8_t { None, Container, Spellbook, Action };
    Kind kind = Kind::None;
    /// ContainerFrame index, spell button index, or action button index.
    int primary = 0;
    /// The item index within a ContainerFrame; zero for every other kind.
    int secondary = 0;
    friend bool operator==(const PadPickupTarget&, const PadPickupTarget&) = default;
};

namespace detail {
/// Read a positive decimal run at `at`, advancing past it.
///
/// Rejects zero and a leading run of zeros as well as an empty one: the
/// interface numbers its buttons from one, so "ActionButton0" is not a button
/// that exists and treating it as slot zero would place into slot minus one.
inline bool padPickupIndex(std::string_view name, std::size_t& at, int& out) {
    const std::size_t begin = at;
    int value = 0;
    while (at < name.size() && name[at] >= '0' && name[at] <= '9') {
        value = value * 10 + (name[at] - '0');
        if (value > 9999) return false;  // nothing the interface creates counts this high
        ++at;
    }
    if (at == begin || value <= 0) return false;
    out = value;
    return true;
}
} // namespace detail

/// Classify a widget name for the pad's pick-up/put-down contract.
inline PadPickupTarget padPickupTargetForName(std::string_view name) {
    using Kind = PadPickupTarget::Kind;
    // Bags. 3.3.5 names every bag slot ContainerFrame<N>Item<M>, the backpack
    // included - it is a ContainerFrame like any other, which is why there is
    // no separate backpack family here.
    if (name.starts_with("ContainerFrame")) {
        std::size_t at = sizeof("ContainerFrame") - 1;
        PadPickupTarget target{Kind::Container, 0, 0};
        if (!detail::padPickupIndex(name, at, target.primary)) return {};
        if (!name.substr(at).starts_with("Item")) return {};
        at += sizeof("Item") - 1;
        if (!detail::padPickupIndex(name, at, target.secondary)) return {};
        // Anything trailing is a child of the button - its icon, its count, its
        // cooldown - and none of those is the button.
        return at == name.size() ? target : PadPickupTarget{};
    }
    if (name.starts_with("SpellButton")) {
        std::size_t at = sizeof("SpellButton") - 1;
        PadPickupTarget target{Kind::Spellbook, 0, 0};
        if (!detail::padPickupIndex(name, at, target.primary)) return {};
        return at == name.size() ? target : PadPickupTarget{};
    }
    // The action bars, and only the five the pad's own bar lane already walks:
    // a pet or stance button is a different slot space with a different pickup,
    // and PickupAction on one of those numbers would move a real action.
    for (std::string_view prefix : {std::string_view("ActionButton"),
                                    std::string_view("MultiBarBottomLeftButton"),
                                    std::string_view("MultiBarBottomRightButton"),
                                    std::string_view("MultiBarRightButton"),
                                    std::string_view("MultiBarLeftButton")}) {
        if (!name.starts_with(prefix)) continue;
        std::size_t at = prefix.size();
        PadPickupTarget target{Kind::Action, 0, 0};
        if (!detail::padPickupIndex(name, at, target.primary)) return {};
        return at == name.size() ? target : PadPickupTarget{};
    }
    return {};
}

/// The pad's focus outline in screen pixels, for a control's own rect.
///
/// ImGui strokes a path; it does not fill a border. AddRect lays the path half
/// a pixel inside the rectangle it is given and PathStroke centres the line on
/// it, so an outline asked for at the button's own corners is drawn
/// `thickness/2 - 0.5` pixels *outside* them on every edge. The bar lane asked
/// for three pixels where the panel lane asks for two, which put a pixel of
/// overshoot per edge on top of a stroke already a pixel fatter - and around a
/// thirty-six unit action button drawn at console scale that reads as a box
/// standing off the button rather than one on it.
///
/// So the rect handed to AddRect is the control's own rect pulled in by
/// `thickness/2 - 0.5`, which puts the stroke's outer edge exactly on the
/// control's edge at any thickness and any scale. Pure, and shared by both
/// lanes, because two copies of this arithmetic are how they came to differ.
struct PadFocusRect {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    [[nodiscard]] float width() const { return x1 - x0; }
    [[nodiscard]] float height() const { return y1 - y0; }
};

/// How thick the focus outline is, in pixels. One number for both lanes.
inline constexpr float kPadFocusThickness = 2.0f;

/// `left`, `bottom`, `rectW` and `rectH` are the widget's laid-out rect in
/// interface units with the origin bottom left - what WidgetTree::layout
/// leaves behind, already multiplied by the frame's own scale chain. `uiScale`
/// and `displayHeight` turn that into the pixels ImGui draws in, exactly as
/// the widget renderer does.
inline PadFocusRect padFocusRect(float left, float bottom, float rectW, float rectH,
                                 float uiScale, float displayHeight,
                                 float thickness = kPadFocusThickness) {
    PadFocusRect r;
    r.x0 = left * uiScale;
    r.x1 = (left + rectW) * uiScale;
    r.y0 = displayHeight - (bottom + rectH) * uiScale;
    r.y1 = displayHeight - bottom * uiScale;
    // Never past the middle: a control smaller than the stroke would otherwise
    // come out inside out, and an inverted rect is not drawn at all - which
    // would lose the outline on exactly the small controls that need it most.
    const float inset = thickness * 0.5f - 0.5f;
    const float ix = inset < (r.x1 - r.x0) * 0.5f ? inset : (r.x1 - r.x0) * 0.5f;
    const float iy = inset < (r.y1 - r.y0) * 0.5f ? inset : (r.y1 - r.y0) * 0.5f;
    r.x0 += ix; r.x1 -= ix;
    r.y0 += iy; r.y1 -= iy;
    return r;
}

/// The pad button that opens and closes the world map.
///
/// Not the PS button, which is what "the big middle button" is usually taken
/// to mean: the system owns it outright. OrbisPadButton carries no bit for it
/// - L3, R3, Options, the D-pad, the four triggers, the four faces and
/// TOUCH_PAD are the whole enum - and scePadReadState never reports one,
/// because pressing it suspends the title and raises the quick menu before the
/// game is asked. Binding it would produce a button that never fires.
///
/// The touchpad click is the other button in the middle of a DualShock 4, it
/// is by a distance the largest thing on the pad, and it is delivered. It held
/// chat, which moves to the left stick's click - see kBindings.
inline constexpr std::uint32_t kPadWorldMapButton = 0x00100000u;  // ORBIS_PAD_BUTTON_TOUCH_PAD

/// Whether this window holds slots something can be put into.
///
/// Letting go of what the cursor is carrying is a gesture for leaving the
/// windows that deal in slots, not for aiming badly inside one. A spellbook
/// button cannot take a bag item, and the real client answers that by keeping
/// the item on the cursor - it does not offer to destroy something the player
/// only mis-aimed. So the destroy prompt is raised for a press in a window that
/// bears no slots at all: the quest log, the map, the game menu.
///
/// The names are the subset of the pad's own panel list that can hold a slot.
/// Deliberately a list rather than a guess from the widget tree: whether a
/// window takes a drop is a fact about the interface, and a window that grows
/// slots later should be added here on purpose.
inline bool padWindowBearsSlots(std::string_view name) {
    if (name.starts_with("ContainerFrame")) return true;
    for (std::string_view bearer : {std::string_view("SpellBookFrame"),
                                    std::string_view("CharacterFrame"),
                                    std::string_view("PaperDollFrame"),
                                    std::string_view("BankFrame"),
                                    std::string_view("MerchantFrame"),
                                    std::string_view("TradeFrame"),
                                    std::string_view("MailFrame"),
                                    std::string_view("ClassTrainerFrame"),
                                    std::string_view("TradeSkillFrame"),
                                    std::string_view("GuildBankFrame"),
                                    std::string_view("AuctionFrame")}) {
        if (name == bearer) return true;
    }
    return false;
}

class LocalFrameXml {
public:
    /// `itemIcon` turns an item's display id into "Interface\Icons\<name>".
    /// Optional: without one the bags fall back to whatever the Lua bindings
    /// registered through frameXmlSetItemIconResolver, and to no icon at all if
    /// nothing did.
    void install(LuaEngine& engine, std::function<game::LocalRealm*()> realm,
                 std::function<uint64_t()> target, std::function<void()> logout,
                 std::function<void(uint64_t)> greeting = {},
                 std::function<std::string(uint32_t)> itemIcon = {},
                 game::GameHandler* handler = nullptr);
    void update(float dt);
    bool open(uint64_t npc);
    bool ready() const;
    void reset();
    void activate(bool enabled);
    bool panelOpen() const;
    bool navigate();
    bool navigateBars();
    /// Whether the original interface's world map is the panel on top.
    ///
    /// While it is, it owns the pad the way a modal does: the bar lane stands
    /// down, the D-pad stops walking controls, and the right stick's cursor is
    /// what the map is read with. Closing it hands everything back.
    bool worldMapOwnsPad() const;
    bool padBarFocused() const {return padFocus_.lane!=ui::LocalPadFocus::Lane::None;}
    void clearPadBarFocus(){padFocus_.clear();}
    bool toggleGameMenu();
private:
    static int command(lua_State* L);
    bool act(const std::string& name, uint32_t id, uint32_t quantity = 0);
    void publish();
    std::string itemIcon(uint32_t displayId) const;
    /// Run the interface's own pickup for the focused control, if it owns one.
    /// Each of the three calls implements both halves of a drag already, so one
    /// press lifts the slot's contents and the next puts them down.
    bool padPickup(const std::string& name);
    bool activatePadControl(uint32_t id);
    /// Let go of what the cursor is carrying, asking first if it is destroyed.
    bool padDropCarried();
    /// Put the pad's focus where the renderer draws the carried icon.
    void publishPadCursor();
    /// The world map's own press: open it on the button below, close it again.
    /// Answers true when it spent the press, so nothing else reads it.
    bool padWorldMapToggle();
    /// One frame of the map's own controls: the right stick's cursor over the
    /// map, and Cross where that cursor is. True whenever the map has the pad.
    bool padWorldMapFrame();
    LuaEngine* engine_=nullptr;
    game::GameHandler* handler_=nullptr;
    std::function<game::LocalRealm*()> realm_;
    std::function<std::string(uint32_t)> itemIcon_;
    std::function<uint64_t()> target_;
    std::function<void()> logout_;
    std::function<void(uint64_t)> greeting_;
    game::LocalUiChanges changes_;
    uint64_t npc_=0, lastTarget_=0, revision_=0;
    enum class DialoguePhase { None, Gossip, Detail, Progress, Reward, Merchant };
    DialoguePhase phase_=DialoguePhase::None;
    uint32_t selected_=0, pendingQuest_=0;
    bool pendingTurnIn_=false;
    float missingNpcSeconds_=0, pendingQuestSeconds_=0, merchantRefreshSeconds_=0;
    uint32_t navigationRoot_=0;

    float timer_=0;
    /// Cross was claimed by the pick-up contract for the press now in progress,
    /// so the synthetic mouse must not also hold a button down at the same
    /// control - that pairing is the accidental drag this contract replaces.
    bool padCrossHandled_=false;
    bool enabled_=false, installed_=false, closing_=false;
    double snapshotTime_=0;
    uint32_t focus_=0;
    ui::LocalPadFocus padFocus_;
};
}
