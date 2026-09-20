#include "rendering/terrain_placement_index.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <random>
#include <unordered_set>
struct Placement { uint32_t uniqueId; };
int main() {
    using wowee::rendering::TerrainPlacementIndex;
    TerrainPlacementIndex index;
    assert(!index.contains(0));
    assert(!index.contains(1));
    std::vector<Placement> placements{{0}, {7}, {7}, {1}, {UINT32_MAX}};
    index.assign(placements);
    assert(index.contains(1) && index.contains(7) && index.contains(UINT32_MAX));
    assert(!index.contains(0) && !index.contains(2));
    std::mt19937 rng(275);
    std::unordered_set<uint32_t> expected;
    placements.clear();
    for (int n = 0; n < 20000; ++n) {
        uint32_t id = rng() % 30000;
        placements.push_back({id});
        if (id) expected.insert(id);
    }
    index.assign(placements);
    // Neighbor placement ownership must exactly match the prior linear scan.
    for (uint32_t id = 0; id < 40000; ++id)
        assert(index.contains(id) == (expected.count(id) != 0));
    index.assign(std::vector<Placement>{{42}});
    assert(index.contains(42) && !index.contains(7) && !index.contains(UINT32_MAX));
    index.assign(std::vector<Placement>{});
    assert(!index.contains(42));
    std::puts("PASS terrain placement membership: 40,000 queries, zero IDs, duplicates, reassignment");
}
