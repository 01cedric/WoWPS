#pragma once

// Parsing WoW's inline text markup out of a label.
//
// The interface writes colour into the string itself - "|cffffd200(M)|r" is
// the world map's keybinding hint in gold - and draws it with the same call as
// any other label. Drawn without parsing, the escape is what appears on
// screen, which is what the map button's tooltip was showing.
//
// Also handled: |H...|h link markers, which wrap the display text of an item
// or spell link, and |T...|t inline textures, which name a file to draw in
// the middle of a line. Those used to be dropped, so a price came out as
// bare numbers with no coin beside them. "||" is a literal bar.
// The wrap works on these too, so there is one definition rather than two
// that have to agree - see ui/text_wrap.hpp.
//
// Header-only and free of ImGui so it can be tested: the drawing side only
// consumes the runs this hands back. It lived inside widget_renderer.cpp and
// could not be, which is how a link's display text came to be dropped for
// months without anything noticing.

#include "ui/text_wrap.hpp"

#include <cstdlib>
#include <string>
#include <vector>

namespace wowee {
namespace ui {

inline std::vector<WrapRun> parseMarkup(const std::string& in) {
    std::vector<WrapRun> runs;
    WrapRun cur;
    auto flush = [&] {
        if (!cur.text.empty()) runs.push_back(cur);
        cur.text.clear();
    };
    auto hexPair = [](const std::string& s, size_t at) {
        auto v = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = v(s[at]), lo = v(s[at + 1]);
        return (hi < 0 || lo < 0) ? -1 : hi * 16 + lo;
    };

    for (size_t i = 0; i < in.size();) {
        if (in[i] != '|') { cur.text += in[i++]; continue; }
        if (i + 1 >= in.size()) { cur.text += in[i++]; continue; }
        const char tag = in[i + 1];
        if (tag == '|') { cur.text += '|'; i += 2; continue; }
        if ((tag == 'c' || tag == 'C') && i + 9 < in.size()) {
            // |cAARRGGBB - alpha first, and the interface almost always sends
            // ff for it. Ignored rather than applied: a label's own alpha
            // already governs it, and multiplying the two fades text that the
            // real client draws solid.
            const int r = hexPair(in, i + 4), g = hexPair(in, i + 6),
                      b = hexPair(in, i + 8);
            if (r >= 0 && g >= 0 && b >= 0) {
                flush();
                cur.hasColor = true;
                cur.rgba[0] = r / 255.0f; cur.rgba[1] = g / 255.0f;
                cur.rgba[2] = b / 255.0f; cur.rgba[3] = 1.0f;
                i += 10;
                continue;
            }
        }
        if (tag == 'r' || tag == 'R') { flush(); cur.hasColor = false; i += 2; continue; }
        // |n is WoW's line break, and it was not handled at all - so every one
        // of them drew as a literal bar and an n. globalstrings.lua alone has a
        // hundred and thirty-eight.
        if (tag == 'n' || tag == 'N') { cur.text += '\n'; i += 2; continue; }
        if (tag == 'H') {
            // |Hitem:3299|h[Fractured Canine]|h - the payload runs to the
            // first |h, the display text follows it, and a second |h closes.
            //
            // This skipped to the next bar, which is the |h *before* the
            // display text, and then the 'h' branch skipped again to the |h
            // after it - so the name between them was thrown away with the
            // payload. "You receive loot: [Fractured Canine]." drew as "You
            // receive loot: ." on every FrameXML surface that renders a link.
            const size_t close = in.find("|h", i + 2);
            if (close == std::string::npos) { i = in.size(); continue; }
            flush();
            cur.link = in.substr(i + 2, close - (i + 2));
            i = close + 2;
            continue;
        }
        if (tag == 'h') {          // the closing marker; the link ends here
            flush();
            cur.link.clear();
            i += 2;
            continue;
        }
        if (tag == 'T') {
            // |Tpath:height:width:offsetX:offsetY|t. Everything after the path
            // is optional and a zero means "the height of the line", which is
            // what all three coin escapes ask for.
            const size_t end = in.find("|t", i + 2);
            const size_t stop = (end == std::string::npos) ? in.size() : end;
            const std::string payload = in.substr(i + 2, stop - (i + 2));
            i = (end == std::string::npos) ? in.size() : end + 2;

            const size_t firstColon = payload.find(':');
            std::string path = payload.substr(0, firstColon);
            if (!path.empty()) {
                flush();
                WrapRun tex = cur;          // keeps the colour and link context
                tex.text.clear();
                tex.texture = std::move(path);
                if (firstColon != std::string::npos) {
                    // Height then width. Anything unparsable stays zero, which
                    // is the same as the escape asking for the line height.
                    size_t at = firstColon + 1;
                    float* fields[2] = {&tex.texHeight, &tex.texWidth};
                    for (float* field : fields) {
                        if (at > payload.size()) break;
                        const size_t next = payload.find(':', at);
                        const std::string part = payload.substr(
                            at, next == std::string::npos ? std::string::npos : next - at);
                        *field = static_cast<float>(std::atof(part.c_str()));
                        if (next == std::string::npos) break;
                        at = next + 1;
                    }
                }
                runs.push_back(std::move(tex));
            }
            continue;
        }
        if (tag == 't') { i += 2; continue; }   // a closing marker with no opener
        cur.text += in[i++];   // a bar that means nothing in particular
    }
    flush();
    return runs;
}

/// The next caret position after `at`, treating an escape as one step.
///
/// A caret walks what is drawn, not what is held. "|Hitem:3299|h[Fractured
/// Canine]|h" is fifty-odd bytes and eighteen characters on screen, so
/// stepping one byte at a time leaves the caret apparently frozen for forty
/// keypresses while it crawls through the payload - and then jumping a whole
/// word when it reaches the display text. Only reachable since links became
/// clickable and shift-click began putting them in the box.
///
/// A colour escape and an inline texture draw nothing at all, so the caret
/// passes over them without stopping. A link is one step whole - the caret
/// cannot rest inside it, the same way it cannot in the real client, and a
/// backspace therefore removes the link rather than a byte of its payload.
inline size_t caretStepRight(const std::string& s, size_t at) {
    if (at >= s.size()) return s.size();
    while (at + 1 < s.size() && s[at] == '|') {
        const char tag = s[at + 1];
        if (tag == '|') return at + 2;                     // a literal bar
        if (tag == 'c' || tag == 'C') { at += 10; continue; }
        if (tag == 'r' || tag == 'R') { at += 2;  continue; }
        if (tag == 'H') {
            // A whole link is one step: payload, display text and closing
            // marker together. The caret cannot rest inside one and a
            // backspace takes all of it, which is the only arrangement where
            // half an escape can never exist - erasing "]" out of
            // "|Hitem:1|h[AB]|h" leaves "[AB|h", and the parser then reads the
            // wreckage as whatever it resembles.
            const size_t payload = s.find("|h", at + 2);
            const size_t close = (payload == std::string::npos)
                ? std::string::npos : s.find("|h", payload + 2);
            return (close == std::string::npos) ? s.size() : close + 2;
        }
        if (tag == 'h') { at += 2; continue; }   // a stray closing marker
        if (tag == 'T' || tag == 't') {
            const size_t close = s.find("|t", at + 2);
            at = (close == std::string::npos) ? s.size() : close + 2;
            continue;
        }
        break;                                             // a bar meaning nothing
    }
    return (at >= s.size()) ? s.size() : at + 1;
}

/// The previous caret position, by the same rule. Walked forward from the
/// start rather than backward: the escapes are only unambiguous read in the
/// direction they were written, and a box holds a line of chat, not a book.
inline size_t caretStepLeft(const std::string& s, size_t at) {
    size_t prev = 0;
    for (size_t p = 0; p < at;) {
        const size_t next = caretStepRight(s, p);
        if (next <= p) break;
        if (next >= at) return p;
        prev = next;
        p = next;
    }
    return prev;
}

/// The nearest position at or before `at` that the caret can actually occupy.
///
/// Everything that moves the caret by stepping keeps it on a boundary already.
/// This is for the one that does not: SetCursorPosition takes a number from
/// Lua and, unguarded, will put the caret inside a link - after which a
/// backspace splits the escape, which is the whole thing the stepping was
/// arranged to prevent. FrameXML only ever asks for 0 or the end, so today
/// this changes nothing; an invariant that holds because no caller happens to
/// break it is not one.
inline size_t caretSnap(const std::string& s, size_t at) {
    if (at >= s.size()) return s.size();
    size_t p = 0;
    while (p < at) {
        const size_t next = caretStepRight(s, p);
        if (next <= p) break;          // no progress: stop rather than spin
        if (next > at) return p;       // `at` fell inside this step
        p = next;
    }
    return p;
}

/// How many characters a string shows, ignoring the markup that draws none.
///
/// An edit box's letter limit counts these rather than bytes, unless the box
/// asked for countInvisibleLetters. The chat box does not ask: it declares
/// letters="255" and nothing else, so the colour codes and hyperlink escapes
/// that a shift-clicked item link brings with it must not be charged against
/// the limit. One such link is around sixty bytes and shows eighteen, so
/// counting bytes let three of them fill a message that WoW would have let
/// carry a dozen.
inline size_t visibleLength(const std::string& s) {
    size_t n = 0;
    for (const WrapRun& r : parseMarkup(s)) n += r.text.size();
    return n;
}

/// What an edit box holds after a selected run is replaced.
///
/// The whole of it, because the pieces are where the mistakes are: a selection
/// is a byte range and a caret is a byte offset, and getting either end wrong
/// leaves a character behind or eats one that was not selected.
///
/// Returns the new caret. The text is edited in place. A range that is empty,
/// inverted, or reaching past the end is treated as no selection at all -
/// FrameXML clears a highlight by passing equal offsets, and a zero-width run
/// that still counted would swallow the next character typed.
struct EditSelection {
    bool   active = false;
    size_t start = 0;
    size_t stop = 0;
};

inline size_t replaceSelection(std::string& text, size_t cursor,
                               const EditSelection& sel) {
    if (!sel.active) return cursor;
    if (sel.stop <= sel.start) return cursor;
    if (sel.stop > text.size()) return cursor;
    text.erase(sel.start, sel.stop - sel.start);
    return sel.start;
}

}  // namespace ui
}  // namespace wowee
