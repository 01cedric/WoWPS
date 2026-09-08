#pragma once
// miniaudio custom backend on sceAudioOut (PS4). See src/platform/ps4/audio_backend_ps4.cpp.
//
// AudioEngine (src/audio/audio_engine.cpp) creates its ma_engine on a
// ma_context whose only backend is ma_backend_custom with these callbacks,
// so the rest of the audio code is unchanged.

struct ma_backend_callbacks;

namespace wowee {
namespace platform {
namespace ps4 {

/// Fills `out` with the sceAudioOut backend callbacks (48 kHz, stereo, float).
void miniaudioBackendCallbacks(ma_backend_callbacks& out);

} // namespace ps4
} // namespace platform
} // namespace wowee
