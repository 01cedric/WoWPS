#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace wowee::game {
inline constexpr uint16_t kLocalRuneRechargeMs = 10000;
using LocalRuneCooldowns = std::array<uint16_t, 6>;
using LocalRuneCost = std::array<uint8_t, 3>; // Blood, Unholy, Frost (DBC order).

inline bool validLocalRunes(const LocalRuneCooldowns& cooldowns) {
    for (const auto remaining : cooldowns) if (remaining > kLocalRuneRechargeMs) return false;
    return true;
}
inline bool hasLocalRuneCost(const LocalRuneCost& cost) {
    return cost[0] || cost[1] || cost[2];
}
// Selection is read-only. Failed casts never spend any subset of their cost.
// No Death conversion is manufactured: only the two authored base runes of
// each type are available until talent/aura conversion rules are implemented.
inline std::optional<uint8_t> selectLocalRunes(uint8_t classId,
        const LocalRuneCooldowns& cooldowns, const LocalRuneCost& cost) {
    if (!validLocalRunes(cooldowns)) return {};
    if (hasLocalRuneCost(cost) && classId != 6) return {};
    uint8_t mask = 0;
    for (uint8_t type = 0; type < 3; ++type) {
        uint8_t required = cost[type];
        if (required > 2) return {};
        for (uint8_t i = type * 2; i < type * 2 + 2 && required; ++i)
            if (!cooldowns[i]) { mask |= uint8_t(1u << i); --required; }
        if (required) return {};
    }
    return mask;
}
inline void consumeLocalRunes(LocalRuneCooldowns& cooldowns, uint8_t selected) {
    for (uint8_t i = 0; i < 6; ++i)
        if (selected & (1u << i)) cooldowns[i] = kLocalRuneRechargeMs;
}
inline bool advanceLocalRunes(LocalRuneCooldowns& cooldowns, uint32_t elapsedMs) {
    bool changed = false;
    if (!elapsedMs) return false;
    for (auto& remaining : cooldowns) if (remaining) {
        remaining = elapsedMs >= remaining ? 0 : uint16_t(remaining - elapsedMs);
        changed = true;
    }
    return changed;
}
} // namespace wowee::game
