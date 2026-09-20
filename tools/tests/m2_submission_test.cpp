#include "rendering/m2_submission.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>

using namespace wowee::rendering;
struct Entry {
    uint32_t instanceIdx;
    float fadeAlpha;
    bool useBones;
    uint16_t targetLOD;
    bool operator==(const Entry& b) const {
        return instanceIdx == b.instanceIdx && fadeAlpha == b.fadeAlpha &&
               useBones == b.useBones && targetLOD == b.targetLOD;
    }
};

int main() {
    std::vector<Entry> entries, scratch;
    std::mt19937 rng(258);
    // Compare production bucket grouping with stable_sort, preserving every
    // instance/fade/skinning payload for empty, single, one-LOD and mixed sets.
    for (unsigned size : {0u, 1u, 2u, 3u, 4u, 127u, 128u, 129u, 4096u}) {
        for (unsigned mode = 0; mode < 6; ++mode) {
            entries.clear();
            for (unsigned i = 0; i < size; ++i) {
                const auto lod = static_cast<uint16_t>(mode < 4 ? mode :
                    (mode == 4 ? i % 4 : rng() % 4));
                entries.push_back({i, float(i % 11) / 10.0f, bool(i % 2), lod});
            }
            auto reference = entries;
            std::stable_sort(reference.begin(), reference.end(),
                [](const Entry& a, const Entry& b) { return a.targetLOD < b.targetLOD; });
            m2GroupOpaqueLods(entries, scratch);
            assert(entries == reference);
        }
    }
    // Warmed scratch storage survives small and large subsequent model groups.
    const auto retainedCapacity = entries.capacity() + scratch.capacity();
    for (unsigned pass = 0; pass < 10; ++pass) {
        entries.clear();
        for (unsigned i = 0; i < 4096; ++i)
            entries.push_back({i, 0.5f, true, static_cast<uint16_t>(i % 4)});
        m2GroupOpaqueLods(entries, scratch);
        assert(entries.capacity() + scratch.capacity() == retainedCapacity);
    }

    const glm::vec2 origin(0.0f);
    const glm::vec4 identity(1, 0, 0, 1);
    M2TransparentRecordReuse reuse;
    uint32_t slot = 999;
    assert(!reuse.find(origin, identity, slot));
    assert(slot == 999); // a miss cannot invent a valid upload
    reuse.remember(origin, identity, 37);
    assert(reuse.find(origin, identity, slot) && slot == 37);
    // Every UV component participates, including rotation/shear and scale.
    for (unsigned component = 0; component < 6; ++component) {
        auto offset = origin;
        auto linear = identity;
        if (component < 2) offset[component] = 0.125f;
        else linear[component - 2] += 0.125f;
        assert(!reuse.find(offset, linear, slot));
    }
    auto tiny = origin;
    tiny.x = std::nextafter(0.0f, 1.0f);
    assert(!reuse.find(tiny, identity, slot)); // exact, no animation tolerance
    auto bad = origin;
    bad.x = std::numeric_limits<float>::quiet_NaN();
    reuse.remember(bad, identity, 38);
    assert(!reuse.find(bad, identity, slot));

    // Match six draws across two instances. Each gets fresh local ownership;
    // repeated layers share only that instance's slot. Animated UV changes
    // require a new entry, while draw order/count and UV values remain exact.
    uint32_t writes = 0;
    std::vector<uint32_t> drawSlots;
    for (unsigned instance = 0; instance < 2; ++instance) {
        M2TransparentRecordReuse local;
        for (const auto offset : {origin, origin, glm::vec2(0.2f, 0.3f)}) {
            uint32_t selected;
            if (!local.find(offset, identity, selected)) {
                selected = writes++;
                local.remember(offset, identity, selected);
            }
            drawSlots.push_back(selected);
        }
    }
    assert(writes == 4);
    assert((drawSlots == std::vector<uint32_t>{0, 0, 1, 2, 2, 3}));
    // A fresh render/frame cannot reuse a prior frame's same-valued UV record.
    M2TransparentRecordReuse nextFrame;
    assert(!nextFrame.find(origin, identity, slot));
    std::cout << "PASS M2 stable four-LOD grouping, payload retention, warmed scratch, "
                 "exact UV matching and instance/frame-local slot ownership\n";
}
