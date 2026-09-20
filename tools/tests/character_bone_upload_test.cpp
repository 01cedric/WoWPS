#include "rendering/bone_upload_state.hpp"
#include <array>
#include <cassert>
#include <cstdio>

int main() {
    using wowee::rendering::BoneUploadState;
    BoneUploadState state;
    std::array<float, 16 * 64> pose{}, slot0{}, slot1{};
    pose.fill(1.0f);
    assert(state.upload(0, slot0.data(), pose.data(), sizeof(pose)));
    assert(slot0 == pose);
    // Shadow, reflection and color may run in any order for the same pose.
    assert(!state.upload(0, slot0.data(), pose.data(), sizeof(pose)));
    assert(!state.upload(0, slot0.data(), pose.data(), sizeof(pose)));
    assert(state.upload(1, slot1.data(), pose.data(), sizeof(pose)));
    assert(slot1 == pose);
    // Animation throttling and temporarily culled casters retain their slots.
    assert(!state.upload(0, slot0.data(), pose.data(), sizeof(pose)));
    assert(!state.upload(1, slot1.data(), pose.data(), sizeof(pose)));
    pose.fill(2.0f);
    state.invalidate();
    assert(!state.upload(0, nullptr, pose.data(), sizeof(pose)));
    assert(!state.upload(0, slot0.data(), nullptr, sizeof(pose)));
    assert(!state.upload(0, slot0.data(), pose.data(), 0));
    assert(state.upload(0, slot0.data(), pose.data(), sizeof(pose)));
    assert(slot0 == pose && slot1 != pose);
    // A second pose before slot 1 is reused must upload the newest pose.
    pose.fill(3.0f);
    state.invalidate();
    assert(state.upload(1, slot1.data(), pose.data(), sizeof(pose)));
    assert(state.upload(0, slot0.data(), pose.data(), sizeof(pose)));
    assert(slot0 == pose && slot1 == pose);
    // A recreated buffer needs data even if animation has not changed.
    slot0.fill(0.0f);
    state.invalidateSlot(0);
    assert(state.upload(0, slot0.data(), pose.data(), sizeof(pose)));
    assert(slot0 == pose);
    assert(!state.upload(1, slot1.data(), pose.data(), sizeof(pose)));
    assert(!state.upload(2, slot0.data(), pose.data(), sizeof(pose)));

    // Count actual production-helper copies, not an assumed frame-time gain.
    size_t copies = 0;
    constexpr size_t frames = 120, passes = 3;
    for (size_t frame = 0; frame < frames; ++frame) {
        state.invalidate();
        for (size_t pass = 0; pass < passes; ++pass)
            copies += state.upload(frame % 2, frame % 2 ? slot1.data() : slot0.data(),
                                   pose.data(), sizeof(pose));
    }
    assert(copies == frames);
    std::printf("PASS bone upload slot/pose/reallocation/retry regressions; "
                "120-frame three-pass CPU fixture: %zu copies vs %zu unconditional "
                "(%zu vs %zu bytes). No GPU/FPS measurement.\n",
                copies, frames * passes, copies * sizeof(pose),
                frames * passes * sizeof(pose));
}
