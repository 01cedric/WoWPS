// display_modes.hpp - the resolutions this client offers, in one place.
//
// Both interfaces put a resolution picker on the same window. This client's
// settings panel had the list inside the function that drew it, and FrameXML's
// video panel asked GetScreenResolutions, which answered the current size and
// nothing else - a dropdown with one entry, whose SetScreenResolution did
// nothing anyway. Sharing the list rather than writing a second one, because
// the *position* in it is the protocol: the video panel's dropdown carries an
// index, not a size, and hands that index back to SetScreenResolution.
#pragma once

#include <string>

namespace wowee {
namespace ui {

#if defined(WOWEE_PS4)
// The console output is fixed at 1080p and the window cannot be resized, so a
// list of desktop monitor sizes offers the player nothing: picking 1440p on a
// PS4 changed a stored number and not one pixel.
//
// What the player can usefully choose is how large the *world* is rendered
// before it is sampled up to the 1080p output - which is a real quality and
// frame-rate decision, and the one the B21 scene profile already implements.
// So on the console this list is those two choices, in the same index-carrying
// shape both option screens already speak.
//
// The sizes stay real numbers rather than the words "high" and "normal": the
// label below is what the interface shows, and the FrameXML video panel reads
// the digits out of it.
inline constexpr int kDisplayResolutions[][2] = {
    {1920, 1080},   // High Definition - the world rendered at full output size
    {1280, 720},    // Normal HD - the performance profile
};
#else
inline constexpr int kDisplayResolutions[][2] = {
    {1280, 720},
    {1600, 900},
    {1920, 1080},
    {2560, 1440},
    {3840, 2160},
};
#endif
inline constexpr int kNumDisplayResolutions =
    static_cast<int>(sizeof(kDisplayResolutions) / sizeof(kDisplayResolutions[0]));

/// "1920x1080", with the console profile's name appended after it.
///
/// The video panel finds the two numbers in this string to work out whether to
/// tag a mode as widescreen, so the "WxH" prefix is part of the contract rather
/// than presentation - which is why the descriptive half goes at the end and
/// never replaces it.
/// Strictly "WxH", with nothing appended, ever.
///
/// This is the string GetScreenResolutions hands to the original interface, and
/// FrameXML does not treat it as a label - VideoOptionsPanels.lua splits it with
/// strsplit("x", value) and divides the two halves to get an aspect ratio. A
/// friendly name appended here made the second half "1080 (High Definition)",
/// which is a string Lua cannot divide by: the panel raised
///
///   attempt to perform arithmetic on local 'height' (a string value)
///
/// VideoOptionsPanels.xml then failed to build, and because that is one of the
/// 139 files of the original interface, the whole load was abandoned and the
/// game came up with no interface at all. The name belongs in the client's own
/// settings window, which is what displayResolutionName is for.
inline std::string displayResolutionLabel(int index) {
    if (index < 0 || index >= kNumDisplayResolutions) return {};
    return std::to_string(kDisplayResolutions[index][0]) + "x" +
           std::to_string(kDisplayResolutions[index][1]);
}

/// The same entry as a person reads it, for this client's own settings window.
///
/// Only this client's panel calls it, and only this client's panel may: nothing
/// in the original interface parses what it returns.
inline std::string displayResolutionName(int index) {
    std::string label = displayResolutionLabel(index);
    if (label.empty()) return label;
#if defined(WOWEE_PS4)
    // On the console these two entries are render-quality profiles rather than
    // display modes - the output stays 1080p either way - so the plain numbers
    // do not say what a player is choosing between.
    label += kDisplayResolutions[index][1] >= 1080 ? " (High Definition)" : " (Normal HD)";
#endif
    return label;
}

/// The world height this entry selects, or 0 for "render at the output size".
/// Only meaningful on the console, where the entry is a render-quality profile
/// rather than a display mode.
inline int displayResolutionSceneHeight(int index) {
    if (index < 0 || index >= kNumDisplayResolutions) return 0;
    const int height = kDisplayResolutions[index][1];
    return height >= 1080 ? 0 : height;
}

/// The inverse of displayResolutionSceneHeight: which entry a world height is.
///
/// This is how the picker reads back what is actually in force. On the console
/// the setting does not live in the window - the window is always the 1080p
/// VideoOut surface - it lives in the scene height the renderer is running at,
/// so that is the only honest thing to ask.
///
/// Never -1, for the same reason displayResolutionIndexFor is not: a dropdown
/// selects by position and no position is not a selection. A height that is on
/// no entry - WOWEE_PS4_SCENE_HEIGHT=900 sets one, and it is a documented
/// value - answers with the nearest profile rather than silently answering
/// zero, which is what a straight equality search did: 900 matched neither
/// entry, the loop fell through, and both option screens reported "High
/// Definition" while the world was being drawn at 900.
///
/// Ties go to the smaller profile. A player at 900 is on a performance
/// setting, and naming the one above it overstates what they are looking at.
inline int displayResolutionIndexForSceneHeight(int sceneHeight) {
    // 0 is "render at the output size", which is the entry with no reduction -
    // the tallest one. Comparing against each entry's own listed height rather
    // than against its scene height keeps that case in the same arithmetic as
    // every other instead of needing a branch of its own.
    int wanted = sceneHeight;
    if (wanted <= 0) {
        for (int i = 0; i < kNumDisplayResolutions; ++i)
            wanted = kDisplayResolutions[i][1] > wanted ? kDisplayResolutions[i][1] : wanted;
    }
    int best = 0;
    int bestDistance = -1;
    for (int i = 0; i < kNumDisplayResolutions; ++i) {
        const int height = kDisplayResolutions[i][1];
        const int distance = height > wanted ? height - wanted : wanted - height;
        if (bestDistance < 0 || distance < bestDistance ||
            (distance == bestDistance && height < kDisplayResolutions[best][1])) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

/// Which entry a window of this size is, or the closest one below it - never
/// -1, because the dropdown selects by index and nothing is not a selection.
/// A window dragged to some size of its own lands on the largest mode it still
/// covers, which is what the picker should show as current.
inline int displayResolutionIndexFor(int w, int h) {
    int best = 0;
    for (int i = 0; i < kNumDisplayResolutions; ++i) {
        if (kDisplayResolutions[i][0] == w && kDisplayResolutions[i][1] == h) return i;
        if (kDisplayResolutions[i][0] <= w && kDisplayResolutions[i][1] <= h) best = i;
    }
    return best;
}

} // namespace ui
} // namespace wowee
