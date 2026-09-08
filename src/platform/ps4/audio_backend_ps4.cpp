// audio_backend_ps4.cpp - miniaudio custom backend on sceAudioOut.
//
// One playback device: the system main audio port, 48 kHz
// stereo float, 256-frame grains. miniaudio's default audio thread calls
// onDeviceWrite() with one period (256 frames) at a time; sceAudioOutOutput
// blocks until the previous grain has been consumed, which paces the thread.
#include "platform/ps4/audio_backend_ps4.hpp"
#include "platform/ps4/ps4_platform.hpp"

#include "core/logger.hpp"

#include <orbis/AudioOut.h>
#include <orbis/UserService.h>

#include <cstring>
#include <cstdio>

#include "miniaudio.h"   // declarations only (MINIAUDIO_IMPLEMENTATION lives in audio_engine.cpp)

namespace {

constexpr ma_uint32 kSampleRate = 48000;
constexpr ma_uint32 kChannels = 2;
constexpr ma_uint32 kGrainFrames = 256;

struct PortState {
    int32_t handle = -1;
    bool started = false;
};
PortState g_port;   // one device per process

std::string resultCode(int32_t result) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", static_cast<uint32_t>(result));
    return text;
}

ma_result ctxInit(ma_context* pContext, const ma_context_config*, ma_backend_callbacks* pCallbacks) {
    (void)pContext;
    int rc = sceAudioOutInit();
    // Already-initialised is not an error (the platform layer may have called it).
    if (rc < 0 && rc != (int)0x8026000C /* SCE_AUDIO_OUT_ERROR_ALREADY_INIT */) {
        LOG_ERROR("sceAudioOutInit failed: ", resultCode(rc), " (", rc, ")");
        return MA_FAILED_TO_INIT_BACKEND;
    }
    wowee::platform::ps4::miniaudioBackendCallbacks(*pCallbacks);
    return MA_SUCCESS;
}

ma_result ctxUninit(ma_context*) { return MA_SUCCESS; }

ma_result ctxEnumerateDevices(ma_context* pContext, ma_enum_devices_callback_proc callback, void* pUserData) {
    ma_device_info info;
    std::memset(&info, 0, sizeof info);
    std::strncpy(info.name, "PS4 main audio output", sizeof(info.name) - 1);
    info.isDefault = MA_TRUE;
    callback(pContext, ma_device_type_playback, &info, pUserData);
    return MA_SUCCESS;
}

ma_result ctxGetDeviceInfo(ma_context*, ma_device_type deviceType, const ma_device_id*, ma_device_info* pDeviceInfo) {
    if (deviceType != ma_device_type_playback) return MA_NO_DEVICE;
    std::memset(pDeviceInfo, 0, sizeof *pDeviceInfo);
    std::strncpy(pDeviceInfo->name, "PS4 main audio output", sizeof(pDeviceInfo->name) - 1);
    pDeviceInfo->isDefault = MA_TRUE;
    pDeviceInfo->nativeDataFormatCount = 1;
    pDeviceInfo->nativeDataFormats[0].format = ma_format_f32;
    pDeviceInfo->nativeDataFormats[0].channels = kChannels;
    pDeviceInfo->nativeDataFormats[0].sampleRate = kSampleRate;
    pDeviceInfo->nativeDataFormats[0].flags = 0;
    return MA_SUCCESS;
}

ma_result devInit(ma_device* pDevice, const ma_device_config* pConfig, ma_device_descriptor* pDescriptorPlayback, ma_device_descriptor* pDescriptorCapture) {
    (void)pDevice;
    if (pConfig->deviceType != ma_device_type_playback || !pDescriptorPlayback) {
        LOG_ERROR("PS4 audio backend: only playback devices are supported");
        return MA_DEVICE_TYPE_NOT_SUPPORTED;
    }
    (void)pDescriptorCapture;
    if (g_port.handle >= 0) return MA_ALREADY_EXISTS;

    // The application's MAIN output is the SYSTEM port, as in OpenOrbis'
    // samples/audio-wav/audio-wav/main.cpp. The B2 boot log returned
    // 0x809b0001 when a logged-in profile ID was used here. That profile is
    // still used for pad/IME input; it must not select the system mixer.
    constexpr auto userId = ORBIS_USER_SERVICE_USER_ID_SYSTEM;
    LOG_INFO("PS4 audio: opening SYSTEM main output, user=", userId,
             " rate=", kSampleRate, " frames=", kGrainFrames,
             " format=", static_cast<unsigned>(ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO));
    int32_t h = sceAudioOutOpen(userId, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, kGrainFrames, kSampleRate,
                                ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO);
    if (h < 0) {
        LOG_ERROR("sceAudioOutOpen SYSTEM failed: ", resultCode(h), " (", h, ")");
        return MA_FAILED_TO_OPEN_BACKEND_DEVICE;
    }
    g_port.handle = h;

    pDescriptorPlayback->format = ma_format_f32;
    pDescriptorPlayback->channels = kChannels;
    pDescriptorPlayback->sampleRate = kSampleRate;
    ma_channel_map_init_standard(ma_standard_channel_map_default, pDescriptorPlayback->channelMap,
                                 (ma_uint32)(sizeof(pDescriptorPlayback->channelMap) / sizeof(pDescriptorPlayback->channelMap[0])), kChannels);
    pDescriptorPlayback->periodSizeInFrames = kGrainFrames;
    pDescriptorPlayback->periodSizeInMilliseconds = 0;
    pDescriptorPlayback->periodCount = 2;
    LOG_INFO("PS4 audio: SYSTEM sceAudioOut port ", h, " opened (48 kHz stereo float, 256-frame grains)");
    return MA_SUCCESS;
}

ma_result devUninit(ma_device*) {
    if (g_port.handle >= 0) {
        sceAudioOutClose(g_port.handle);
        g_port.handle = -1;
    }
    g_port.started = false;
    return MA_SUCCESS;
}

ma_result devStart(ma_device*) { g_port.started = true; return MA_SUCCESS; }

ma_result devStop(ma_device*) {
    g_port.started = false;
    if (g_port.handle >= 0) sceAudioOutOutput(g_port.handle, nullptr);   // drain: NULL waits for the last grain
    return MA_SUCCESS;
}

ma_result devWrite(ma_device* pDevice, const void* pFrames, ma_uint32 frameCount, ma_uint32* pFramesWritten) {
    (void)pDevice;
    if (pFramesWritten) *pFramesWritten = 0;
    if (g_port.handle < 0) return MA_DEVICE_NOT_INITIALIZED;
    const float* src = static_cast<const float*>(pFrames);
    ma_uint32 done = 0;
    alignas(16) float grain[kGrainFrames * kChannels];
    // First write from the device thread, once: the boot log then shows the
    // audio thread reached the hardware before a crash elsewhere.
    static bool firstWriteReported = false;
    if (!firstWriteReported) {
        firstWriteReported = true;
        wowee::platform::ps4::registerCrashReportingThread("audio device");
        wowee::platform::ps4::reportBootStage("audio: first device write");
    }
    while (done < frameCount) {
        ma_uint32 n = frameCount - done;
        if (n > kGrainFrames) n = kGrainFrames;
        // sceAudioOutOutput consumes whole grains: pad a short tail with silence.
        std::memcpy(grain, src + (size_t)done * kChannels, (size_t)n * kChannels * sizeof(float));
        if (n < kGrainFrames) std::memset(grain + (size_t)n * kChannels, 0, (size_t)(kGrainFrames - n) * kChannels * sizeof(float));
        int rc = sceAudioOutOutput(g_port.handle, grain);
        if (rc < 0) {
            LOG_ERROR("sceAudioOutOutput failed: ", resultCode(rc), " (", rc, "), port=", g_port.handle);
            return MA_ERROR;
        }
        done += n;
    }
    if (pFramesWritten) *pFramesWritten = done;
    return MA_SUCCESS;
}

}  // namespace

namespace wowee {
namespace platform {
namespace ps4 {

void miniaudioBackendCallbacks(ma_backend_callbacks& out) {
    std::memset(&out, 0, sizeof out);
    out.onContextInit = ctxInit;
    out.onContextUninit = ctxUninit;
    out.onContextEnumerateDevices = ctxEnumerateDevices;
    out.onContextGetDeviceInfo = ctxGetDeviceInfo;
    out.onDeviceInit = devInit;
    out.onDeviceUninit = devUninit;
    out.onDeviceStart = devStart;
    out.onDeviceStop = devStop;
    out.onDeviceWrite = devWrite;
}

} // namespace ps4
} // namespace platform
} // namespace wowee
