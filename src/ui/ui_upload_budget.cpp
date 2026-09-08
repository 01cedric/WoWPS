#include "ui/ui_upload_budget.hpp"

#include "imgui.h"

namespace wowee::ui {

bool claimUiTextureUpload() {
    // Keyed off ImGui's own frame counter rather than a render callback, so
    // the budget resets exactly once per frame no matter which screens drew.
    static int spent = 0;
    static int frame = -1;
    const int now = ImGui::GetFrameCount();
    if (now != frame) { frame = now; spent = 0; }

    // Six across the whole interface. Enough that a window fills in within a
    // few frames of opening, few enough that the waits behind them cannot add
    // up to a stall a driver would treat as a hang.
    constexpr int kPerFrame = 6;
    if (spent >= kPerFrame) return false;
    ++spent;
    return true;
}

} // namespace wowee::ui
