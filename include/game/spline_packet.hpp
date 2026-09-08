// include/game/spline_packet.hpp
// Consolidated spline packet parsing - replaces 7 duplicated parsing locations.
#pragma once
#include "network/packet.hpp"

#include "game/world_packets.hpp"  // MonsterMoveData
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace wowee::game {

/// Decoded spline data from a movement or MonsterMove packet.
struct SplineBlockData {
    uint32_t splineFlags = 0;
    uint32_t duration = 0;

    // Animation (splineFlag 0x00400000)
    bool     hasAnimation = false;
    uint8_t  animationType = 0;
    uint32_t animationStartTime = 0;

    // Parabolic (splineFlag 0x00000800 for MonsterMove, 0x00000008 for MoveUpdate)
    bool     hasParabolic = false;
    float    verticalAcceleration = 0.0f;
    uint32_t parabolicStartTime = 0;

    // FINAL_POINT / FINAL_TARGET / FINAL_ANGLE (movement update only)
    bool     hasFinalPoint = false;
    glm::vec3 finalPoint{0};
    bool     hasFinalTarget = false;
    uint64_t finalTarget = 0;
    bool     hasFinalAngle = false;
    float    finalAngle = 0.0f;

    // Timing (movement update only)
    uint32_t timePassed = 0;
    uint32_t splineId = 0;

    // Waypoints (server coordinates, decoded from packed-delta if compressed)
    std::vector<glm::vec3> waypoints;
    glm::vec3 destination{0};
    bool hasDest = false;

    // SplineMode (movement update WotLK only)
    uint8_t splineMode = 0;
    glm::vec3 endPoint{0};
    bool hasEndPoint = false;
};

// ── Spline flag constants ───────────────────────────────────────
namespace SplineFlag {
    // Classic/TBC ground locomotion for SMSG_MONSTER_MOVE: set means Run,
    // clear means Walk. WotLK repurposed this bit as DONE and sends separate
    // SET_WALK_MODE / SET_RUN_MODE opcodes instead.
    constexpr uint32_t PRE_WOTLK_RUNMODE = 0x00000100;
    constexpr uint32_t FINAL_POINT    = 0x00010000;
    constexpr uint32_t FINAL_TARGET   = 0x00020000;
    constexpr uint32_t FINAL_ANGLE    = 0x00040000;
    constexpr uint32_t CATMULLROM     = 0x00080000; // Uncompressed Catmull-Rom
    constexpr uint32_t CYCLIC         = 0x00100000; // Cyclic path
    constexpr uint32_t ENTER_CYCLE    = 0x00200000; // Entering cyclic path
    constexpr uint32_t ANIMATION      = 0x00400000; // Animation spline
    constexpr uint32_t PARABOLIC_MM   = 0x00000800; // Parabolic in MonsterMove
    constexpr uint32_t PARABOLIC_MU   = 0x00000008; // Parabolic in MoveUpdate

    // Mask: if any of these are set, waypoints are uncompressed
    constexpr uint32_t UNCOMPRESSED_MASK = CATMULLROM | CYCLIC | ENTER_CYCLE;
    // TBC-era alternative for uncompressed check
    constexpr uint32_t UNCOMPRESSED_MASK_TBC = CATMULLROM | 0x00002000;
} // namespace SplineFlag

// WotLK moved every one of these down a bit.
//
// The values above are the pre-WotLK SplineFlags the vanilla and TBC clients
// use, and they were being applied to 3.3.5 as well - where the enum is
// MoveSplineFlag and Final_Point is 0x8000, not 0x10000. Every value from
// Final_Point up is therefore off by one position, which is not a cosmetic
// naming problem: the facing that follows the flags is a float for an angle,
// eight bytes for a target guid and twelve for a point, so reading the wrong
// one leaves the rest of the movement block shifted and the whole object
// update is dropped. Reading 0x10000 as Final_Point took twelve bytes where
// the server had written eight.
namespace SplineFlagWotlk {
    constexpr uint32_t DONE            = 0x00000100;
    constexpr uint32_t FALLING         = 0x00000200;
    constexpr uint32_t NO_SPLINE       = 0x00000400;
    constexpr uint32_t PARABOLIC       = 0x00000800;
    constexpr uint32_t WALKMODE        = 0x00001000;
    constexpr uint32_t FLYING          = 0x00002000;
    constexpr uint32_t ORIENT_FIXED    = 0x00004000;
    constexpr uint32_t FINAL_POINT     = 0x00008000;
    constexpr uint32_t FINAL_TARGET    = 0x00010000;
    constexpr uint32_t FINAL_ANGLE     = 0x00020000;
    constexpr uint32_t CATMULLROM      = 0x00040000;
    constexpr uint32_t CYCLIC          = 0x00080000;
    constexpr uint32_t ENTER_CYCLE     = 0x00100000;
    constexpr uint32_t ANIMATION       = 0x00200000;
    constexpr uint32_t FROZEN          = 0x00400000;

    constexpr uint32_t UNCOMPRESSED_MASK = CATMULLROM | CYCLIC | ENTER_CYCLE;
} // namespace SplineFlagWotlk

[[nodiscard]] constexpr bool isPreWotlkSplineWalking(uint32_t splineFlags) {
    return (splineFlags & SplineFlag::PRE_WOTLK_RUNMODE) == 0;
}

/// Decode a single packed-delta waypoint.
/// Format: bits [0:10] = X (11-bit signed), [11:21] = Y (11-bit signed), [22:31] = Z (10-bit signed).
/// Each component is multiplied by 0.25 and subtracted from `midpoint`.
[[nodiscard]] glm::vec3 decodePackedDelta(uint32_t packed, const glm::vec3& midpoint);

/// Parse a MonsterMove spline body (after splineFlags has already been read).
/// Handles: Animation, duration, Parabolic, pointCount, compressed/uncompressed waypoints.
/// `startPos` is the creature's current position (needed for packed-delta midpoint calculation).
/// `splineFlags` is the already-read spline flags value.
/// `useTbcUncompressedMask`: if true, use 0x00080000|0x00002000 for uncompressed check (TBC format).
/// Which generation of spline flags the bytes were written with. WotLK moved
/// every value from Final_Point up by one bit position, and two of the
/// decisions this parser makes read those bits: whether an animation block
/// follows, and whether waypoints are twelve bytes each or four. Getting it
/// wrong is not a misreported flag, it is a misread packet.
enum class SplineFlagSet { PreWotlk, Wotlk };

[[nodiscard]] bool parseMonsterMoveSplineBody(
    network::Packet& packet,
    SplineBlockData& out,
    uint32_t splineFlags,
    const glm::vec3& startPos,
    bool useTbcUncompressedMask = false,
    SplineFlagSet flagSet = SplineFlagSet::PreWotlk);

/// Parse a MonsterMove spline body where waypoints are always compressed (Vanilla format).
/// `startPos` is the creature's current position.
[[nodiscard]] bool parseMonsterMoveSplineBodyVanilla(
    network::Packet& packet,
    SplineBlockData& out,
    uint32_t splineFlags,
    const glm::vec3& startPos);

/// Read SMSG_MONSTER_MOVE's move type and whatever facing follows it.
///
/// The byte says how the creature is turning, and each answer is followed by a
/// different amount of data: a point (three floats), a target (a guid), an
/// angle (one float), or - for Stop - nothing at all, the packet ending there.
///
/// `stopped` comes back true for Stop, and the caller must return without
/// reading a spline: there is none, and the destination is where the creature
/// already is.
///
/// This is the same three expansions' worth of bytes - the block was written
/// out identically in the WotLK parser, the vanilla one and the TBC one, which
/// is three places to get a length wrong in a packet where a wrong length is
/// every field after it.
[[nodiscard]] bool parseMonsterMoveFacing(network::Packet& packet,
                                          MonsterMoveData& data,
                                          bool& stopped);

/// Parse a Classic/Turtle movement update spline block.
/// Format: splineFlags, FINAL_POINT/TARGET/ANGLE, timePassed, duration, splineId,
/// pointCount, uncompressed waypoints (12 bytes each), endPoint (no splineMode).
[[nodiscard]] bool parseClassicMoveUpdateSpline(
    network::Packet& packet,
    SplineBlockData& out);

/// Parse a WotLK movement update spline block.
/// Format: splineFlags, FINAL_POINT/TARGET/ANGLE, timePassed, duration, splineId,
/// then WotLK header (durationMod, durationModNext, [Animation], [Parabolic],
/// pointCount, splineMode, endPoint) with multi-strategy fallback.
[[nodiscard]] bool parseWotlkMoveUpdateSpline(
    network::Packet& packet,
    SplineBlockData& out,
    const glm::vec3& entityPos = glm::vec3(0));

} // namespace wowee::game
