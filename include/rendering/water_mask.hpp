#pragma once

/// Which of a water chunk's sixty-four sub-tiles a surface draws.
///
/// Here rather than inline in the renderer because this rule has been got wrong
/// repeatedly and every failure looks like scenery: water outside a pond's
/// banks, a gap in open sea, or - the one that named this file - an ocean drawn
/// across a dry chunk, roofing the Caverns of Time. None of them raises, fails a
/// test or writes a line of log, so the rule needs somewhere it can be stated
/// once and checked without a device.
///
/// The mask is the canonical chunk-wide 8x8 form both MH2O and MCLQ are
/// normalised into by the ADT loader: bit index = row * 8 + col, LSB first,
/// where row is the MH2O y axis and col the x. See the water/terrain axis notes
/// - every read of it is LSB-only, and the old mirror-OR and neighbour dilation
/// were compensation for bugs that are fixed.

#include <cstdint>
#include <vector>

namespace wowee::rendering {

/// The sub-tiles of one chunk that should be drawn, as an 8x8 bitmask packed
/// into a uint64_t with the same row*8+col, LSB-first ordering.
///
/// `isOcean` is a whole-chunk statement, not a per-tile one: an ocean layer's
/// own sub-rect is frequently sparse, and honouring it leaves holes in open sea.
/// A chunk that declares ocean therefore fills completely.
///
/// **A chunk that declares no liquid never reaches here.** It contributes no
/// layer, so it gets no bits. The bug this replaced seeded every sub-tile of the
/// whole ADT before the chunks were consulted, which gave water to chunks that
/// had none - invisible against land above sea level, and a ceiling of ocean
/// over anything below it.
inline uint64_t chunkWaterMask(const std::vector<uint8_t>& layerMask,
                               int x, int y, int width, int height,
                               bool isOcean) {
    if (isOcean) return ~uint64_t{0};

    uint64_t out = 0;
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const int tile = (y + row) * 8 + (x + col);
            if (tile < 0 || tile >= 64) continue;
            // An absent or short mask means the sub-rect is solid, which is how
            // a layer with no exists bitmap is defined.
            bool render = true;
            const int byteIdx = tile / 8;
            if (!layerMask.empty() && byteIdx < static_cast<int>(layerMask.size()))
                render = (layerMask[byteIdx] & (1u << (tile % 8))) != 0;
            if (render) out |= uint64_t{1} << tile;
        }
    }
    return out;
}


/// Whether one cell of a surface's own grid is water, read straight from the
/// layer bitmap the surface carries.
///
/// The renderer needs this per cell while it builds a surface's geometry, and
/// it asked four times: once each for the vertex grid, the index buffer and
/// the two collision helpers. Each of those four restated all three rules
/// below, which is three chances each to get an indexing rule wrong that no
/// test and no log line would notice.
///
/// This is the same rule chunkWaterMask() applies at load time, asked one cell
/// at a time rather than a chunk at a time, and it has to cope with the two
/// shapes a surface's mask can still be in by the time it reaches the
/// renderer:
///
///   * A per-chunk terrain surface carries the chunk-wide 8x8 mask however
///     small its own rect is, so its cell has to be offset into the chunk
///     before indexing. `wmoId == 0 && width <= 8 && mask.size() >= 8` is what
///     identifies one; a mask under eight bytes cannot be chunk-wide, so it is
///     read the other way.
///   * Merged terrain and WMO liquid are packed by the surface's own width,
///     row-major, and the rect's offsets do not enter into it.
///
/// An empty mask means the whole surface is water, as everywhere else here:
/// absent is not empty. A cell whose bit falls past the end of a short mask is
/// water too, which is the safer way round, since a surface drawn slightly too
/// far is visible and reportable where one that vanishes reads as missing
/// terrain.
inline bool waterCellRendered(const std::vector<uint8_t>& mask, uint32_t wmoId,
                              uint8_t width, uint8_t xOffset, uint8_t yOffset,
                              int cellX, int cellY) {
    if (mask.empty()) return true;

    int tile;
    if (wmoId == 0 && width <= 8 && mask.size() >= 8) {
        tile = (static_cast<int>(yOffset) + cellY) * 8 +
               (static_cast<int>(xOffset) + cellX);
    } else {
        tile = cellY * static_cast<int>(width) + cellX;
    }

    const int byteIdx = tile / 8;
    if (tile < 0 || byteIdx >= static_cast<int>(mask.size())) return true;
    return (mask[static_cast<size_t>(byteIdx)] & (1u << (tile % 8))) != 0;
}

}  // namespace wowee::rendering
