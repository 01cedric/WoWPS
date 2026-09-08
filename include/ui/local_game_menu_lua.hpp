#pragma once
namespace wowee::ui {
inline constexpr const char* kToggleLocalGameMenuLua = R"LUA(
local f = rawget(_G, 'GameMenuFrame')
if type(f) == 'table' then
    if f:IsShown() then
        if HideUIPanel then HideUIPanel(f) else f:Hide() end
    else
        if ShowUIPanel then ShowUIPanel(f) else f:Show() end
    end
end
)LUA";
}
