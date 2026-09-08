#pragma once

/// Where the first key of an M2 colour track is.
///
/// An M2Color is a vec3 colour track followed by an alpha track, and the
/// colour is what tints a batch. A glow card is painted white and coloured
/// here: Orgrimmar's bonfire carries (1.0, 0.329, 0.0), so a model whose
/// colour is not read burns white.
///
/// The two versions store the keys differently, and this is the part that
/// cannot be guessed at, because reading one as the other yields a plausible
/// colour rather than a failure - three floats out of whatever the offset
/// happened to land on, clamped into range and used.
///
///   WotLK (>= 264), track is 20 bytes:
///       0   interpolationType, globalSequence
///       4   nTimestamps, ofsTimestamps
///      12   nKeys, ofsKeys        -> an array of M2Array, one per sequence,
///                                    each pointing at that sequence's values
///
///   Vanilla and TBC (< 264), track is 28 bytes:
///       0   interpolationType, globalSequence
///       4   nRanges, ofsRanges          (absent in WotLK)
///      12   nTimestamps, ofsTimestamps
///      20   nKeys, ofsKeys        -> the values themselves, flat
///
/// So the WotLK read is two hops and the vanilla read is one. Doing two on a
/// vanilla file reads a float's bit pattern as a file offset.

#include <cstddef>
#include <cstdint>

namespace wowee::pipeline {

/// Byte offset of a colour track's key array within the track record.
inline constexpr uint32_t m2ColorTrackKeysOffset(bool wotlk) {
    return wotlk ? 12u : 20u;
}

/// Size of one M2Color record: two tracks back to back.
inline constexpr uint32_t m2ColorRecordSize(bool wotlk) {
    return wotlk ? 40u : 56u;
}

/// Byte offset of the alpha track within an M2Color record.
inline constexpr uint32_t m2ColorAlphaTrackOffset(bool wotlk) {
    return wotlk ? 20u : 28u;
}

/// Find the first colour key of the track at `trackOffset`.
///
/// `readU32` reads a little-endian uint32 at a byte offset. Returns false and
/// leaves `outOffset` alone when the track carries no keys or any offset would
/// run past the end of the file.
template <typename ReadU32>
bool m2ColorTrackFirstKeyOffset(size_t fileSize, uint32_t trackOffset,
                                bool wotlk, ReadU32&& readU32,
                                uint32_t& outOffset) {
    const uint32_t keysAt = trackOffset + m2ColorTrackKeysOffset(wotlk);
    if (static_cast<size_t>(keysAt) + 8 > fileSize) return false;

    const uint32_t count = readU32(keysAt);
    const uint32_t offset = readU32(keysAt + 4);
    if (count == 0 || offset == 0) return false;

    if (!wotlk) {
        // The keys are the values.
        if (static_cast<size_t>(offset) + 12 > fileSize) return false;
        outOffset = offset;
        return true;
    }

    // One M2Array per sequence; take the first that holds anything.
    if (static_cast<size_t>(offset) + 8 > fileSize) return false;
    const uint32_t seqCount = readU32(offset);
    const uint32_t seqOffset = readU32(offset + 4);
    if (seqCount == 0 || seqOffset == 0) return false;
    if (static_cast<size_t>(seqOffset) + 12 > fileSize) return false;
    outOffset = seqOffset;
    return true;
}

}  // namespace wowee::pipeline
