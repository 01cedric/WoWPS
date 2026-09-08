#include "pipeline/m2_loader.hpp"
#include "rendering/glue_camera.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using wowee::pipeline::M2AnimationTrack;
    struct PriorSequenceKeys {
        std::vector<uint32_t> timestamps;
        std::vector<glm::vec3> vec3Values;
        std::vector<glm::quat> quatValues;
        std::vector<float> floatValues;
        std::vector<glm::vec3> vec3InTangents, vec3OutTangents;
        std::vector<float> floatInTangents, floatOutTangents;
    };
    static_assert(sizeof(M2AnimationTrack::SequenceKeys) < sizeof(PriorSequenceKeys));
    constexpr size_t slots = 100 * 3 * 300;
    std::vector<M2AnimationTrack::SequenceKeys> boneSlots(slots);
    for (const auto& slot : boneSlots) {
        assert(!slot.hasSplineStorage());
        assert(slot.spline().vec3InTangents.empty());
        assert(!slot.hasSplineStorage()); // read-only access does not allocate
    }
    M2AnimationTrack track;
    track.sequences.resize(1);
    auto& keys = track.sequences[0];
    keys.timestamps = {0, 1000, 2000};
    keys.vec3Values = {{0, 0, 0}, {2, 4, 8}, {4, 1, 12}};
    keys.spline().vec3InTangents = {{1, 2, 3}, {3, 0, 6}, {1, 0, 7}};
    keys.spline().vec3OutTangents = {{2, 3, 1}, {4, 5, 1}, {9, 2, 1}};
    keys.floatValues = {0, 1, 0.3f};
    keys.spline().floatInTangents = {0.1f, 0.4f, 0.7f};
    keys.spline().floatOutTangents = {0.6f, 0.2f, 0.8f};
    for (uint16_t kind : {uint16_t(0), uint16_t(1), uint16_t(2), uint16_t(3)}) {
        track.interpolationType = kind;
        for (uint32_t ms = 0; ms <= 2000; ++ms) {
            const size_t segment = ms < 1000 ? 0 : 1;
            const float t = float(ms - segment * 1000) / 1000.f;
            auto expected = wowee::rendering::glue::interpolate(kind,
                keys.vec3Values[segment], keys.vec3Values[segment + 1],
                keys.spline().vec3OutTangents[segment], keys.spline().vec3InTangents[segment + 1], t, true);
            auto expectedRoll = wowee::rendering::glue::interpolate(kind,
                keys.floatValues[segment], keys.floatValues[segment + 1],
                keys.spline().floatOutTangents[segment], keys.spline().floatInTangents[segment + 1], t, true);
            if (ms == 2000) { expected = keys.vec3Values.back(); expectedRoll = keys.floatValues.back(); }
            const auto actual = wowee::rendering::glue::samplePosition(track, 0, float(ms), ms, {});
            const auto roll = wowee::rendering::glue::sampleRoll(track, 0, float(ms), ms, {});
            assert(glm::length(actual - expected) < 0.00001f);
            assert(std::abs(roll - expectedRoll) < 0.00001f);
        }
    }
    auto copy = track;
    copy.sequences[0].spline().vec3InTangents[0].x = 99;
    assert(keys.spline().vec3InTangents[0].x == 1); // independent ownership after copying
    auto moved = std::move(copy);
    assert(moved.sequences[0].spline().vec3InTangents[0].x == 99);
    M2AnimationTrack::SequenceKeys assigned;
    assigned = keys;
    assert(assigned.spline().floatOutTangents == keys.spline().floatOutTangents);
    assigned = M2AnimationTrack::SequenceKeys{};
    assert(!assigned.hasSplineStorage());
    std::printf("PASS M2 sequence storage: slot %zu -> %zu bytes; %zu bytes saved in 90000-slot fixture; all 8004 camera position/roll samples preserved; copy/move ownership intact\n",
        sizeof(PriorSequenceKeys), sizeof(M2AnimationTrack::SequenceKeys),
        slots * (sizeof(PriorSequenceKeys) - sizeof(M2AnimationTrack::SequenceKeys)));
}
