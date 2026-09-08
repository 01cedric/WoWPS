#pragma once

// The one place the interface's coordinate space is converted to and from.
//
// Two passes meet over a hyperlink and they do not agree about y. The draw
// pass works in screen pixels with y growing down, because that is what ImGui
// hands it. The input pass works in interface units with y growing up, because
// that is what the widget tree holds and what hitTest compares against - its
// own comment says so: "top is the upper edge, and y grows upward here".
//
// A link rect filed in the first space and tested in the second misses by the
// interface scale and by the whole height of the screen, and nothing says so:
// both sides compile, both run, and every click quietly lands on nothing. That
// is what happened, and it is why both halves of the conversion live here
// rather than one at each end where they can drift apart.
//
// Testable because it is arithmetic. The invariant worth pinning is not either
// conversion on its own but the round trip: a link drawn at a screen position
// must be found by a click at that same position on screen.

#include "ui/widget_tree.hpp"

namespace wowee {
namespace ui {

/// A mouse position in window pixels, as the interface tree sees it.
///
/// ImGui measures from the top-left and the tree from the bottom-left, so the
/// height comes in to flip it; the scale is the interface scale the tree is
/// laid out at.
inline void mouseToTreeSpace(float& x, float& y, float screenH, float scale) {
    y = screenH - y;
    if (scale > 0.0f) { x /= scale; y /= scale; }
}

/// A run of link text drawn at (x, y) in screen pixels, as a rect in tree
/// space. `y` is the top of the line, as ImGui draws it, and `lineH` its
/// height - so the *bottom* in tree space comes off y + lineH.
inline LinkRect linkRectFromDraw(uint32_t owner, const std::string& link,
                                 const std::string& text,
                                 float x, float y, float runW, float lineH,
                                 float screenH, float scale) {
    const float s = (scale > 0.0f) ? scale : 1.0f;
    LinkRect r;
    r.widget = owner;
    r.link = link;
    r.text = text;
    r.x0 = x / s;
    r.x1 = (x + runW) / s;
    r.y0 = (screenH - (y + lineH)) / s;
    r.y1 = (screenH - y) / s;
    return r;
}

/// The nearest frame at or above `start` that declares a script.
///
/// FrameXML puts OnHyperlinkClick on the chat frame; the text is drawn in a
/// font string several levels below it, and the link rect names the font
/// string because that is what drew it. So the click has to walk up.
///
/// Takes a predicate rather than asking Lua, so the walk can be tested on its
/// own - the walk is the part with a loop in it, and the part that stops at
/// the root, refuses a cycle, and returns zero when nothing along the chain
/// wants the click.
///
/// Zero means nobody claimed it, and the caller lets the click carry on as an
/// ordinary one.
template <typename Tree, typename HasScript>
uint32_t findScriptOwner(const Tree& tree, uint32_t start, HasScript hasScript) {
    // Bounded by the number of widgets: a parent chain that loops would
    // otherwise spin here, and a tree is built by code that can have bugs.
    uint32_t guard = 0;
    for (uint32_t w = start; w != 0 && guard <= tree.size(); ++guard) {
        if (hasScript(w)) return w;
        const auto* node = tree.get(w);
        w = node ? node->parent : 0;
    }
    return 0;
}

/// Whether `node` is `ancestor`, or sits beneath it in the tree.
///
/// What decides whether a link under the cursor is actually the thing being
/// clicked. A link rect is filed wherever its text was drawn, and nothing about
/// the rect says whether a window has since been opened over the top of it - so
/// a link answers a click only when the frame the mouse is really on belongs to
/// the same chain.
template <typename Tree>
bool isSelfOrDescendantOf(const Tree& tree, uint32_t node, uint32_t ancestor) {
    if (ancestor == 0) return false;
    uint32_t guard = 0;
    for (uint32_t w = node; w != 0 && guard <= tree.size(); ++guard) {
        if (w == ancestor) return true;
        const auto* n = tree.get(w);
        w = n ? n->parent : 0;
    }
    return false;
}

}  // namespace ui
}  // namespace wowee
