#pragma once

#include "core/collision_height.hpp"
#include "game/world_packets.hpp"
#include "network/packet.hpp"
#include <cmath>

namespace wowee::game {

// WotLK 3.3.5: SMSG has a change counter; MSG broadcasts MovementInfo and
// has no counter. Both use packed GUIDs. Keep parsing transactional because
// Packet's scalar reads otherwise turn truncated input into zero values.
struct CollisionHeightChange {
    uint64_t guid = 0;
    uint32_t counter = 0;
    float height = core::DEFAULT_COLLISION_HEIGHT;
    MovementInfo movement;
};

inline bool readCollisionHeightChange(network::Packet& packet,
                                      CollisionHeightChange& out,
                                      bool broadcast) {
    const size_t start = packet.getReadPos();
    auto fail = [&]() { packet.setReadPos(start); return false; };
    CollisionHeightChange change;
    if (!packet.hasFullPackedGuid()) return fail();
    change.guid = packet.readPackedGuid();
    if (change.guid == 0) return fail();
    if (!broadcast) {
        if (packet.getRemainingSize() != 8) return fail();
        change.counter = packet.readUInt32();
    } else {
        auto& info = change.movement;
        if (!packet.hasRemaining(26)) return fail();
        info.flags = packet.readUInt32();
        info.flags2 = packet.readUInt16();
        info.time = packet.readUInt32();
        info.x = packet.readFloat();
        info.y = packet.readFloat();
        info.z = packet.readFloat();
        info.orientation = packet.readFloat();
        if (!std::isfinite(info.x) || !std::isfinite(info.y) ||
            !std::isfinite(info.z) || !std::isfinite(info.orientation)) return fail();
        if (info.hasFlag(MovementFlags::ONTRANSPORT)) {
            if (!packet.hasFullPackedGuid()) return fail();
            info.transportGuid = packet.readPackedGuid();
            if (!packet.hasRemaining(21)) return fail();
            info.transportX = packet.readFloat();
            info.transportY = packet.readFloat();
            info.transportZ = packet.readFloat();
            info.transportO = packet.readFloat();
            info.transportTime = packet.readUInt32();
            info.transportSeat = static_cast<int8_t>(packet.readUInt8());
            if (!std::isfinite(info.transportX) || !std::isfinite(info.transportY) ||
                !std::isfinite(info.transportZ) || !std::isfinite(info.transportO)) return fail();
            if (info.flags2 & 0x0400) {
                if (!packet.hasRemaining(4)) return fail();
                info.transportTime2 = packet.readUInt32();
            }
        }
        if (info.hasFlag(MovementFlags::SWIMMING) || info.hasFlag(MovementFlags::FLYING) ||
            (info.flags2 & 0x0020)) {
            if (!packet.hasRemaining(4)) return fail();
            info.pitch = packet.readFloat();
            if (!std::isfinite(info.pitch)) return fail();
        }
        if (!packet.hasRemaining(4)) return fail();
        info.fallTime = packet.readUInt32();
        if (info.hasFlag(MovementFlags::FALLING)) {
            if (!packet.hasRemaining(16)) return fail();
            info.jumpVelocity = packet.readFloat();
            info.jumpSinAngle = packet.readFloat();
            info.jumpCosAngle = packet.readFloat();
            info.jumpXYSpeed = packet.readFloat();
            if (!std::isfinite(info.jumpVelocity) || !std::isfinite(info.jumpSinAngle) ||
                !std::isfinite(info.jumpCosAngle) || !std::isfinite(info.jumpXYSpeed)) return fail();
        }
        if (info.flags & 0x04000000) {
            if (!packet.hasRemaining(4)) return fail();
            info.splineElevation = packet.readFloat();
            if (!std::isfinite(info.splineElevation)) return fail();
        }
        if (packet.getRemainingSize() != 4) return fail();
    }
    change.height = packet.readFloat();
    if (!core::validCollisionHeight(change.height)) return fail();
    out = change;
    return true;
}

} // namespace wowee::game
