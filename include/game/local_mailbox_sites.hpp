#pragma once
#include "game/local_gameplay.hpp"
#include <array>

namespace wowee::game {
// Authored type-19 gameobject coordinates, pinned AC4e80596; selection/provenance
// in assets/local_realm/mailbox_sites.json. Never derive from NPC positions.
inline constexpr std::array<LocalMailboxSite, 13> kLocalMailboxSites{{
    {0x0A1D000000000000ULL | 100156ULL, 0, -8815.17000000f, 652.92700000f, 94.89660000f, 4.87820000f}, // Stormwind auction house
    {0x0A1D000000000000ULL | 10107ULL, 1, 1615.58000000f, -4391.60000000f, 10.33500000f, 3.94445000f}, // Orgrimmar bank
    {0x0A1D000000000000ULL | 150747ULL, 1, 1657.87000000f, -4433.03000000f, 17.48180000f, 5.58505000f}, // Orgrimmar auction house
    {0x0A1D000000000000ULL | 866ULL, 0, -4910.38000000f, -976.21200000f, 501.40800000f, 2.26893000f}, // Ironforge bank
    {0x0A1D000000000000ULL | 20426ULL, 1, -1263.31000000f, 44.54510000f, 127.54500000f, 4.72984000f}, // Thunder Bluff bank
    {0x0A1D000000000000ULL | 49532ULL, 1, 9943.00000000f, 2497.74000000f, 1317.69000000f, 3.63029000f}, // Darnassus bank
    {0x0A1D000000000000ULL | 22851ULL, 530, 9548.13000000f, -7263.01000000f, 14.11760000f, 4.69494000f}, // Silvermoon bank east side
    {0x0A1D000000000000ULL | 22852ULL, 530, 9515.66000000f, -7262.47000000f, 14.19130000f, 4.72984000f}, // Silvermoon bank west side
    {0x0A1D000000000000ULL | 26176ULL, 530, 9657.66000000f, -7116.19000000f, 14.28750000f, 4.27606000f}, // Silvermoon Bazaar auction house
    {0x0A1D000000000000ULL | 25036ULL, 530, -2034.09000000f, 5336.11000000f, -9.37981000f, 3.70010000f}, // Shattrath Aldor bank first side
    {0x0A1D000000000000ULL | 25037ULL, 530, -2041.20000000f, 5350.26000000f, -9.38034000f, 3.49067000f}, // Shattrath Aldor bank second side
    {0x0A1D000000000000ULL | 25038ULL, 530, -1694.76000000f, 5522.43000000f, -9.81281000f, 3.75246000f}, // Shattrath Scryer bank first side
    {0x0A1D000000000000ULL | 25039ULL, 530, -1687.79000000f, 5508.52000000f, -9.80751000f, 3.44703000f}, // Shattrath Scryer bank second side
}};
} // namespace wowee::game
