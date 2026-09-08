#pragma once
namespace wowee::addons {
inline constexpr const char* kLocalAuctionLoadLua = R"lua(
local ok, why = LoadAddOn('Blizzard_AuctionUI')
if not ok then error('Auction UI: '..tostring(why)) end
assert(type(rawget(_G,'AuctionFrame_Show'))=='function', 'Auction UI show handler missing')
)lua";
inline constexpr const char* kLocalAuctionShowLua = R"lua(
if AuctionFrame and not AuctionFrame:IsShown() then AuctionFrame_Show() end
)lua";
} // namespace wowee::addons
