#pragma once

namespace wowee::ui {
// Replayed on login as well as when changing the setting. Keep statement
// boundaries in the source: adjacent C++ literals previously formed endendif.
inline constexpr const char* kApplyChatBoxVisibilityLua = R"lua(
for i = 1, NUM_CHAT_WINDOWS do
    local e = _G['ChatFrame' .. i .. 'EditBox']
    if e and not e:HasFocus() then
        ChatEdit_DeactivateChat(e)
        if GetCVar('chatStyle') == 'classic' then e:Hide() end
    end
end
if GetCVar('chatStyle') == 'im' then
    local send = ChatEdit_ChooseBoxForSend()
    if send and not send:HasFocus() then send:Show() end
end
)lua";
} // namespace wowee::ui
