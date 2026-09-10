#pragma once

namespace wowee::ui {

constexpr int kInterfaceLayoutVersion = 162;
constexpr int kDefaultSafeAreaPercent = 0;

// 01.61 saved its automatic four-percent console margin as if the user had
// selected it. Remove that old default once; other values and later choices
// of four percent remain explicit preferences.
constexpr int migratedSafeAreaPercent(int percent, int savedVersion) {
    return savedVersion < kInterfaceLayoutVersion && percent == 4
        ? kDefaultSafeAreaPercent : percent;
}

} // namespace wowee::ui
