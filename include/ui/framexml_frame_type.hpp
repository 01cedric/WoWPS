#pragma once
#include <string>
#include <string_view>

namespace wowee::ui {
// CreateFrame accepts case-insensitive type names. Retail OptionsList_OnLoad
// requests "BUTTON", whereas XML-created controls normally use "Button".
// Store one spelling so hit testing, pad navigation and type-specific state
// all see the same kind; preserve unknown names rather than inventing a type.
inline std::string canonicalFrameType(std::string_view requested) {
    constexpr std::string_view types[] = {
        "Frame", "Button", "CheckButton", "Slider", "EditBox", "StatusBar",
        "ScrollFrame", "Cooldown", "SimpleHTML", "GameTooltip", "ColorSelect",
        "Model", "PlayerModel", "DressUpModel", "TabardModel", "MovieFrame",
        "Minimap", "MessageFrame", "ScrollingMessageFrame", "WorldFrame"
    };
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; };
    for (auto type : types) {
        if (type.size() != requested.size()) continue;
        bool equal = true;
        for (std::size_t i = 0; i < type.size(); ++i)
            if (lower(type[i]) != lower(requested[i])) { equal = false; break; }
        if (equal) return std::string(type);
    }
    return std::string(requested);
}
} // namespace wowee::ui
