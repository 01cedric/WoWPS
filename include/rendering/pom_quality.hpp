#pragma once

/// How many steps a parallax trace takes at each quality level.
///
/// This was spelled in five places before it was spelled here: a table in the
/// WMO renderer, two sets of inline branches in the character renderer, the
/// settings schema's prose, and the login screen's dropdown - and the login
/// screen's copy was wrong. It named its two entries "Medium" and "High" while
/// writing the selected index of a three-entry scale into the same config key
/// the in-game panel reads, so its "Medium" asked for the 16 steps the panel
/// calls Low, its "High" asked for the 32 the panel calls Medium, and the real
/// 64 could not be chosen there at all. Choosing High on the login screen
/// downgraded anyone who had High set in game.
///
/// It carries no includes on purpose. The numbers are wanted by the renderers,
/// by the settings schema and by tests that have no Vulkan SDK to find them
/// with, which is the same reason ui/graphics_defaults.hpp exists.

namespace wowee {
namespace rendering {

/// The step count each quality level traces with, lowest first.
inline constexpr int kPomSampleCounts[] = {16, 32, 64};

/// What each level is called wherever one is offered to a player. The order is
/// the order of kPomSampleCounts, and the index of a label is the value that
/// gets written to the config file.
inline constexpr const char* kPomQualityLabels[] = {"Low", "Medium", "High"};

inline constexpr int kPomQualityCount = static_cast<int>(sizeof(kPomSampleCounts) / sizeof(kPomSampleCounts[0]));

static_assert(sizeof(kPomQualityLabels) / sizeof(kPomQualityLabels[0]) == static_cast<size_t>(kPomQualityCount),
              "every parallax quality level needs a name, and every name a level");

/// The step count for a quality index, for indices that came off a config file
/// and so might not be in range.
inline constexpr int pomSamplesFor(int quality) {
    if (quality < 0) quality = 0;
    if (quality >= kPomQualityCount) quality = kPomQualityCount - 1;
    return kPomSampleCounts[quality];
}

}  // namespace rendering
}  // namespace wowee
