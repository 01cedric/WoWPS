#pragma once

/// Which LightParams row and which channel a light band belongs to.
///
/// LightIntBand and LightFloatBand hold one row per channel per LightParams
/// row: eighteen colour channels and six float channels. A row's own id is the
/// block index, and both the ids and the LightParams ids they address start at
/// one.
///
/// The mapping was `id / channels` and `id % channels`, which is off by one in
/// both terms: band 1 - the first colour channel of LightParams 1 - came out
/// as LightParams 0, channel 1. Every channel landed on the wrong row and in
/// the wrong slot, and LightParams 0 does not exist, so the first seventeen
/// channels of every params row were dropped entirely.
///
/// The counts check out against the files: LightIntBand has 15300 rows and
/// ids running to 16506, which is 850 rows of 18 and 917 x 18; LightFloatBand
/// has 5100 rows and ids to 5502, which is 850 x 6 and 917 x 6.

#include <cstdint>

namespace wowee::rendering {

/// Colour channels per LightParams row in LightIntBand.
inline constexpr uint32_t LIGHT_INT_CHANNELS = 18;

/// Float channels per LightParams row in LightFloatBand.
inline constexpr uint32_t LIGHT_FLOAT_CHANNELS = 6;

struct LightBandSlot {
    uint32_t lightParamsId = 0;
    uint32_t channel = 0;
    bool valid = false;
};

/// Split a band's block index into the row it belongs to and the channel it is.
///
/// `blockIndex` is the band row's own id, as stored. A zero is not a band:
/// the ids are one-based.
inline LightBandSlot lightBandSlot(uint32_t blockIndex, uint32_t channelsPerRow) {
    LightBandSlot out;
    if (blockIndex == 0 || channelsPerRow == 0) return out;
    const uint32_t zeroBased = blockIndex - 1;
    out.lightParamsId = zeroBased / channelsPerRow + 1;
    out.channel = zeroBased % channelsPerRow;
    out.valid = true;
    return out;
}

}  // namespace wowee::rendering
