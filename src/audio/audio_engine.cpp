#define MINIAUDIO_IMPLEMENTATION
#include "audio/audio_engine.hpp"
#include "audio/decoded_audio_cache.hpp"
#ifdef WOWEE_PS4
#include "platform/ps4/audio_backend_ps4.hpp"
#endif
#include "core/logger.hpp"
#ifdef WOWEE_PS4
#include "platform/ps4/ps4_platform.hpp"
#endif
#include "pipeline/asset_manager.hpp"


#include "../../extern/miniaudio.h"

#include <cstring>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace wowee {
namespace audio {

struct DecodedWavCacheEntry {
    ma_format format = ma_format_unknown;
    ma_uint32 channels = 0;
    ma_uint32 sampleRate = 0;
    ma_uint64 frames = 0;
    std::shared_ptr<std::vector<uint8_t>> pcmData;
};

namespace {

/// Owns malloc'd storage for a miniaudio object and unwinds it correctly on
/// any early return.
///
/// miniaudio initialises into caller-provided storage, so teardown is two
/// steps -- uninit the object, then free the storage -- and the uninit must
/// not run on storage that was never initialised. That is the distinction the
/// hand-written cleanup here used to encode by having a different free()
/// sequence at each of five early returns per function, duplicated across two
/// functions. Every one of them had to be right, and nothing checked that.
///
/// Call markInitialised() once the matching ma_*_init has succeeded, and
/// release() to hand the pointer to activeSounds_, which owns it from then on.
template <typename T, void (*Uninit)(T*)>
class MaStorage {
public:
    MaStorage() : ptr_(static_cast<T*>(std::malloc(sizeof(T)))) {}
    ~MaStorage() {
        if (ptr_ != nullptr) {
            if (initialised_) {
                Uninit(ptr_);
            }
            std::free(ptr_);
        }
    }

    MaStorage(const MaStorage&) = delete;
    MaStorage& operator=(const MaStorage&) = delete;

    explicit operator bool() const { return ptr_ != nullptr; }
    T* get() const { return ptr_; }
    void markInitialised() { initialised_ = true; }

    /// Give up ownership. The caller frees it from here on.
    T* release() {
        T* p = ptr_;
        ptr_ = nullptr;
        initialised_ = false;
        return p;
    }

private:
    T* ptr_ = nullptr;
    bool initialised_ = false;
};

using AudioBufferStorage = MaStorage<ma_audio_buffer, ma_audio_buffer_uninit>;
using SoundStorage = MaStorage<ma_sound, ma_sound_uninit>;



#ifdef WOWEE_PS4
static constexpr size_t kDecodedAudioCacheBytes = 16ull * 1024 * 1024;
#else
static constexpr size_t kDecodedAudioCacheBytes = 64ull * 1024 * 1024;
#endif
static DecodedAudioCache<DecodedWavCacheEntry> gDecodedWavCache(kDecodedAudioCacheBytes);
// Protects decoded PCM entries and their LRU order (including cache hits).
// Required because playSound2D() can be called from multiple threads
// (main thread, async loaders, animation callbacks).
static std::shared_mutex gDecodedWavCacheMutex;

static uint64_t makeWavCacheKey(const std::vector<uint8_t>& wavData) {
    // Full encoded content: distinct WAVs commonly have identical headers and
    // silent tails. Sampling only those bytes can play the wrong creature bark.
    constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
    constexpr uint64_t FNV_PRIME = 1099511628211ull;
    uint64_t h = FNV_OFFSET;
    for (uint8_t byte : wavData) { h ^= byte; h *= FNV_PRIME; }
    return h;
}

static bool decodeWavCached(const std::vector<uint8_t>& wavData, DecodedWavCacheEntry& out,
                            uint64_t* decodedKey = nullptr) {
    if (wavData.empty()) return false;

    const uint64_t key = makeWavCacheKey(wavData);
    if (decodedKey) *decodedKey = key;

    {
        std::lock_guard<std::shared_mutex> cacheLock(gDecodedWavCacheMutex);
        if (gDecodedWavCache.find(key, out)) return true;
    }

    ma_decoder decoder;
    ma_decoder_config decoderConfig = ma_decoder_config_init_default();
    ma_result result = ma_decoder_init_memory(
        wavData.data(),
        wavData.size(),
        &decoderConfig,
        &decoder
    );
    if (result != MA_SUCCESS) {
        LOG_ERROR("AudioEngine: Failed to decode WAV data (", wavData.size(), " bytes): error ", result);
        return false;
    }

    struct DecoderGuard {
        ma_decoder* decoder;
        ~DecoderGuard() { ma_decoder_uninit(decoder); }
    } decoderGuard{&decoder};

    ma_uint64 totalFrames = 0;
    result = ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
    if (result != MA_SUCCESS) totalFrames = 0;

    ma_format format = decoder.outputFormat;
    ma_uint32 channels = decoder.outputChannels;
    ma_uint32 sampleRate = decoder.outputSampleRate;
    ma_uint64 maxFrames = ma_uint64(sampleRate) * 60;
    if (totalFrames == 0 || totalFrames > maxFrames) totalFrames = maxFrames;

    const size_t frameBytes = size_t(channels) * ma_get_bytes_per_sample(format);
    constexpr size_t maxClipPcmBytes = 32ull * 1024 * 1024;
    if (frameBytes == 0 || totalFrames > maxClipPcmBytes / frameBytes) {
        LOG_WARNING("Audio clip exceeds 32 MiB decoded budget; use streaming for long tracks");
        return false;
    }
    size_t bufferSize = size_t(totalFrames) * frameBytes;
    auto pcmData = std::make_shared<std::vector<uint8_t>>(bufferSize);
    ma_uint64 framesRead = 0;
    result = ma_decoder_read_pcm_frames(&decoder, pcmData->data(), totalFrames, &framesRead);
    if (result != MA_SUCCESS || framesRead == 0) {
        LOG_ERROR("AudioEngine: Failed to read frames from WAV: error ", result, ", framesRead=", framesRead);
        return false;
    }

    pcmData->resize(framesRead * channels * ma_get_bytes_per_sample(format));

    DecodedWavCacheEntry entry;
    entry.format = format;
    entry.channels = channels;
    entry.sampleRate = sampleRate;
    entry.frames = framesRead;
    entry.pcmData = pcmData;
    // The old 256-entry limit could retain hundreds of MiB of long ambience
    // clips. Bound actual PCM allocation capacity and keep playing voices alive
    // through their shared owners when an old cache entry is evicted.
    {
        std::lock_guard<std::shared_mutex> writeLock(gDecodedWavCacheMutex);
        if (gDecodedWavCache.find(key, out)) return true;
        try {
            gDecodedWavCache.insert(key, entry);
        } catch (const std::bad_alloc&) {
            // Caching is optional: the decoded clip can still be played.
        }
    }
    out = entry;
    return true;
}

} // namespace

AudioEngine& AudioEngine::instance() {
    static AudioEngine instance;
    return instance;
}

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::initialize() {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (initialized_) {
        LOG_WARNING("AudioEngine already initialized");
        return true;
    }

    // Allocate miniaudio engine
    engine_ = new ma_engine();

#ifdef WOWEE_PS4
    // PS4: the only backend is the sceAudioOut custom backend
    // (src/platform/ps4/audio_backend_ps4.cpp); the engine gets a context that
    // knows nothing else so miniaudio never probes OSS/sndio device files.
    static ma_context ps4Context;
    static bool ps4ContextReady = false;
    if (!ps4ContextReady) {
        ma_context_config contextConfig = ma_context_config_init();
        // The device thread decodes MP3 (dr_mp3 scratch) and runs the engine
        // graph; do not depend on libkernel's default thread stack for it.
        contextConfig.threadStackSize = 2u * 1024u * 1024u;
        platform::ps4::miniaudioBackendCallbacks(contextConfig.custom);
        ma_backend backends[] = {ma_backend_custom};
        if (ma_context_init(backends, 1, &contextConfig, &ps4Context) != MA_SUCCESS) {
            LOG_ERROR("Failed to initialize the PS4 audio context");
            delete engine_;
            engine_ = nullptr;
            return false;
        }
        ps4ContextReady = true;
    }
    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.pContext = &ps4Context;
    ma_result result = ma_engine_init(&engineConfig, engine_);
#else
    // Initialize with default config
    ma_result result = ma_engine_init(nullptr, engine_);
#endif
    if (result != MA_SUCCESS) {
        LOG_ERROR("Failed to initialize miniaudio engine: ", result);
        delete engine_;
        engine_ = nullptr;
        return false;
    }

    // Set default master volume
    ma_engine_set_volume(engine_, masterVolume_);

    // Log audio backend info
    ma_backend backend = ma_engine_get_device(engine_)->pContext->backend;
    const char* backendName = "unknown";
    switch (backend) {
        case ma_backend_wasapi: backendName = "WASAPI"; break;
        case ma_backend_dsound: backendName = "DirectSound"; break;
        case ma_backend_winmm: backendName = "WinMM"; break;
        case ma_backend_coreaudio: backendName = "CoreAudio"; break;
        case ma_backend_sndio: backendName = "sndio"; break;
        case ma_backend_audio4: backendName = "audio(4)"; break;
        case ma_backend_oss: backendName = "OSS"; break;
        case ma_backend_pulseaudio: backendName = "PulseAudio"; break;
        case ma_backend_alsa: backendName = "ALSA"; break;
        case ma_backend_jack: backendName = "JACK"; break;
        case ma_backend_aaudio: backendName = "AAudio"; break;
        case ma_backend_opensl: backendName = "OpenSL|ES"; break;
        case ma_backend_webaudio: backendName = "WebAudio"; break;
        case ma_backend_custom: backendName = "Custom"; break;
        case ma_backend_null: backendName = "Null (no output)"; break;
        default: break;
    }

    initialized_ = true;
    LOG_INFO("Decoded audio cache budget: ", kDecodedAudioCacheBytes / (1024 * 1024), " MiB");
    LOG_INFO("AudioEngine initialized (miniaudio, backend: ", backendName, ")");
    return true;
}

std::string AudioEngine::getOutputDeviceName() const {
    if (!initialized_ || !engine_) return {};
    const ma_device* device = ma_engine_get_device(const_cast<ma_engine*>(engine_));
    if (!device || device->playback.name[0] == '\0') return {};
    return device->playback.name;
}

void AudioEngine::shutdown() {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (!initialized_) {
        return;
    }

    // Stop music
    stopMusic();

    stopNarration();
    stopAllSounds();

    if (engine_) {
        ma_engine_uninit(engine_);
        delete engine_;
        engine_ = nullptr;
    }

    initialized_ = false;
    LOG_INFO("AudioEngine shutdown");
}

void AudioEngine::setAssetManager(pipeline::AssetManager* am) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    // Also reset on reassigning the same provider after an expansion/source change.
    decodedPathKeys_.clear();
    assetManager_ = am;
}

void AudioEngine::setMasterVolume(float volume) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    masterVolume_ = glm::clamp(volume, 0.0f, 1.0f);
    if (engine_) {
        if (!suspended_) ma_engine_set_volume(engine_, masterVolume_);
    }
}
void AudioEngine::setSuspended(bool suspended) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (suspended_ == suspended) return;
    suspended_ = suspended;
    // The engine's own level is the one thing that changes; masterVolume_ is
    // left alone so the slider and the resume both still read it.
    if (engine_) ma_engine_set_volume(engine_, suspended_ ? 0.0f : masterVolume_);
}


void AudioEngine::setListenerPosition(const glm::vec3& position) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    listenerPosition_ = position;
    if (engine_) {
        ma_engine_listener_set_position(engine_, 0, position.x, position.y, position.z);
    }
}

void AudioEngine::setListenerOrientation(const glm::vec3& forward, const glm::vec3& up) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    listenerForward_ = forward;
    listenerUp_ = up;
    if (engine_) {
        ma_engine_listener_set_direction(engine_, 0, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(engine_, 0, up.x, up.y, up.z);
    }
}

bool AudioEngine::playSound2D(const std::vector<uint8_t>& wavData, float volume, float pitch) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (cinematicAudioExclusive_) return 0;
    // Size and volume, because this overload is handed decoded bytes and has
    // no name to report. The sample managers all cache their clips and call
    // this one, so the path-named log above never fires for them - and a sound
    // reported as playing loudly on every world entry produced no sfx: line at
    // all. A byte count identifies the file well enough to find it on disk.
    LOG_INFO("sfx2d: bytes=", wavData.size(), " vol=", volume);
    (void)pitch;
    if (!initialized_ || !engine_ || wavData.empty()) return false;
    update(0.0f); // menu sounds also retire when no world renderer is updating
    if (masterVolume_ <= 0.0f || !hasVoiceCapacity()) return false;

    DecodedWavCacheEntry decoded;
    if (!decodeWavCached(wavData, decoded) || !decoded.pcmData || decoded.frames == 0) {
        return false;
    }

    return playDecoded2D(decoded, volume);
}

bool AudioEngine::playDecoded2D(const DecodedWavCacheEntry& decoded, float volume) {
    // Both callers own playbackMutex_; retain the same original buffer/voice path.
    if (!decoded.pcmData || decoded.frames == 0) return false;
    if (!hasVoiceCapacity(decoded.pcmData->capacity())) return false;

    // Create audio buffer from decoded PCM data (heap allocated to keep alive)
    ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(
        decoded.format,
        decoded.channels,
        decoded.frames,
        decoded.pcmData->data(),
        nullptr  // No custom allocator
    );
    // Must set explicitly - miniaudio defaults to device sample rate, which causes
    // pitch distortion if it differs from the file's native rate (e.g. 22050 vs 44100 Hz).
    bufferConfig.sampleRate = decoded.sampleRate;

    AudioBufferStorage audioBuffer;
    if (!audioBuffer) return false;
    ma_result result = ma_audio_buffer_init(&bufferConfig, audioBuffer.get());
    if (result != MA_SUCCESS) {
        LOG_WARNING("Failed to create audio buffer: ", result);
        return false;
    }
    audioBuffer.markInitialised();

    // Create sound from audio buffer
    SoundStorage sound;
    if (!sound) return false;
    result = ma_sound_init_from_data_source(
        engine_,
        audioBuffer.get(),
        MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC | MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION,
        nullptr,
        sound.get()
    );

    if (result != MA_SUCCESS) {
        LOG_WARNING("Failed to create sound: ", result);
        return false;
    }
    sound.markInitialised();

    // Set volume (pitch not supported with NO_PITCH flag)
    ma_sound_set_volume(sound.get(), volume);

    // Start playback
    result = ma_sound_start(sound.get());
    if (result != MA_SUCCESS) {
        LOG_WARNING("Failed to start sound: ", result);
        return false;
    }

    // Track this sound for cleanup (decoded PCM shared across plays)
    activeSounds_.reserve(activeSounds_.size() + 1);
    activeSounds_.push_back({sound.release(), audioBuffer.release(), decoded.pcmData, 0u});

    return true;
}

uint32_t AudioEngine::playSound2DStoppable(const std::vector<uint8_t>& wavData, float volume) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (cinematicAudioExclusive_) return 0;
    if (!initialized_ || !engine_ || wavData.empty()) return 0;
    update(0.0f);
    if (masterVolume_ <= 0.0f || !hasVoiceCapacity()) return 0;

    DecodedWavCacheEntry decoded;
    if (!decodeWavCached(wavData, decoded) || !decoded.pcmData || decoded.frames == 0) return 0;

    if (!hasVoiceCapacity(decoded.pcmData->capacity())) return 0;

    ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(
        decoded.format, decoded.channels, decoded.frames, decoded.pcmData->data(), nullptr);
    bufferConfig.sampleRate = decoded.sampleRate;

    AudioBufferStorage audioBuffer;
    if (!audioBuffer) return 0;
    if (ma_audio_buffer_init(&bufferConfig, audioBuffer.get()) != MA_SUCCESS) {
        return 0;
    }
    audioBuffer.markInitialised();

    SoundStorage sound;
    if (!sound) return 0;
    ma_result result = ma_sound_init_from_data_source(
        engine_, audioBuffer.get(),
        MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC | MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION,
        nullptr, sound.get());
    if (result != MA_SUCCESS) {
        return 0;
    }
    sound.markInitialised();

    ma_sound_set_volume(sound.get(), volume);
    if (ma_sound_start(sound.get()) != MA_SUCCESS) {
        return 0;
    }

    uint32_t id = nextSoundId_++;
    if (nextSoundId_ == 0) nextSoundId_ = 1;  // Skip 0 (sentinel)
    activeSounds_.reserve(activeSounds_.size() + 1);
    activeSounds_.push_back({sound.release(), audioBuffer.release(), decoded.pcmData, id});
    return id;
}

void AudioEngine::stopSound(uint32_t id) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (id == 0) return;
    for (auto it = activeSounds_.begin(); it != activeSounds_.end(); ++it) {
        if (it->id == id) {
            ma_sound_stop(it->sound);
            ma_sound_uninit(it->sound);
            std::free(it->sound);
            ma_audio_buffer* buffer = static_cast<ma_audio_buffer*>(it->buffer);
            ma_audio_buffer_uninit(buffer);
            std::free(buffer);
            activeSounds_.erase(it);
            return;
        }
    }
}

bool AudioEngine::playSound2D(const std::string& mpqPath, float volume, float pitch) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (cinematicAudioExclusive_) return 0;
    // Which file, and how loud, for the one-shots that name a path.
    //
    // A sound reported as playing loudly on every world entry cannot be found
    // by reading: a dozen managers reach this and none of them is obviously the
    // one. This says so in a line, and the timestamp beside the world-entry
    // lines in the same log is what pins it.
    LOG_INFO("sfx: ", mpqPath, " vol=", volume);
    if (!assetManager_) {
        LOG_WARNING("AudioEngine::playSound2D(path): no AssetManager set");
        return false;
    }
    (void)pitch; // same NO_PITCH 2D channel as the byte overload
    if (!initialized_ || !engine_) return false;
    update(0.0f);
    if (masterVolume_ <= 0.0f || !hasVoiceCapacity()) return false;
    try {
        DecodedWavCacheEntry decoded;
        // A warm ambience clip needs neither a new 5 MiB MPQ read nor a hash.
        // The metadata does not pin payloads beyond the existing decoded LRU.
        if (auto found = decodedPathKeys_.find(mpqPath); found != decodedPathKeys_.end()) {
            bool cached = false;
            {
                std::lock_guard<std::shared_mutex> cacheLock(gDecodedWavCacheMutex);
                cached = gDecodedWavCache.find(found->second, decoded);
            }
            if (cached) return playDecoded2D(decoded, volume);
            decodedPathKeys_.erase(found);
        }
        auto data = assetManager_->readFileBounded(mpqPath, 32ull * 1024 * 1024);
        if (data.empty()) {
            LOG_WARNING("AudioEngine::playSound2D: failed to load '", mpqPath, "'");
            return false;
        }
        uint64_t contentKey = 0;
        if (!decodeWavCached(data, decoded, &contentKey)) return false;
        if (mpqPath.size() <= 512) {
            try {
                if (decodedPathKeys_.size() >= 256) decodedPathKeys_.clear();
                decodedPathKeys_.emplace(mpqPath, contentKey);
            } catch (const std::bad_alloc&) {
                // Optional metadata may fail; the already decoded clip can play.
            }
        }
        return playDecoded2D(decoded, volume);
    } catch (const std::bad_alloc&) {
        LOG_WARNING("Audio clip skipped: CPU allocation failed for ", mpqPath);
        return false;
    }
}

uint32_t AudioEngine::playSound2DStoppable(const std::string& mpqPath, float volume) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (cinematicAudioExclusive_) return 0;
    if (!assetManager_) return 0;
    try {
        auto data = assetManager_->readFileBounded(mpqPath, 32ull * 1024 * 1024);
        return playSound2DStoppable(data, volume);
    } catch (const std::bad_alloc&) {
        LOG_WARNING("Audio clip skipped: CPU allocation failed for ", mpqPath);
        return 0;
    }
}

bool AudioEngine::playSound3D(const std::vector<uint8_t>& wavData, const glm::vec3& position,
                              float volume, float pitch, float maxDistance, float referenceDistance) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (cinematicAudioExclusive_) return 0;
    if (!initialized_ || !engine_ || wavData.empty()) return false;
    update(0.0f); // menu sounds also retire when no world renderer is updating
    if (masterVolume_ <= 0.0f || !hasVoiceCapacity()) return false;

    DecodedWavCacheEntry decoded;
    if (!decodeWavCached(wavData, decoded) || !decoded.pcmData || decoded.frames == 0) {
        return false;
    }

    if (!hasVoiceCapacity(decoded.pcmData->capacity())) return false;

    LOG_DEBUG("playSound3D: cached WAV - format:", decoded.format,
              " channels:", decoded.channels, " sampleRate:", decoded.sampleRate,
              " pitch:", pitch);

    // Create audio buffer with correct sample rate
    ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(
        decoded.format,
        decoded.channels,
        decoded.frames,
        decoded.pcmData->data(),
        nullptr
    );
    // Must set explicitly - miniaudio defaults to device sample rate, which causes
    // pitch distortion if it differs from the file's native rate (e.g. 22050 vs 44100 Hz).
    bufferConfig.sampleRate = decoded.sampleRate;

    AudioBufferStorage audioBuffer;
    if (!audioBuffer) return false;
    ma_result result = ma_audio_buffer_init(&bufferConfig, audioBuffer.get());
    if (result != MA_SUCCESS) {
        return false;
    }
    audioBuffer.markInitialised();

    // Create 3D sound (spatialization enabled, pitch enabled)
    SoundStorage sound;
    if (!sound) return false;
    result = ma_sound_init_from_data_source(
        engine_,
        audioBuffer.get(),
        MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC,  // Removed NO_PITCH flag
        nullptr,
        sound.get()
    );

    if (result != MA_SUCCESS) {
        LOG_WARNING("playSound3D: Failed to create sound, error: ", result);
        return false;
    }
    sound.markInitialised();

    // Set 3D position and attenuation
    ma_sound_set_position(sound.get(), position.x, position.y, position.z);
    ma_sound_set_volume(sound.get(), volume);
    ma_sound_set_pitch(sound.get(), pitch);  // Enable pitch variation
    ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_inverse);
    ma_sound_set_min_gain(sound.get(), 0.0f);
    ma_sound_set_max_gain(sound.get(), 1.0f);
    ma_sound_set_min_distance(sound.get(), std::clamp(referenceDistance, 1.0f, std::max(1.0f, maxDistance)));
    ma_sound_set_max_distance(sound.get(), maxDistance);
    ma_sound_set_rolloff(sound.get(), 1.0f);

    result = ma_sound_start(sound.get());
    if (result != MA_SUCCESS) {
        return false;
    }

    // Track for cleanup
    activeSounds_.reserve(activeSounds_.size() + 1);
    activeSounds_.push_back({sound.release(), audioBuffer.release(), decoded.pcmData});

    return true;
}

bool AudioEngine::playSound3D(const std::string& mpqPath, const glm::vec3& position,
                              float volume, float pitch, float maxDistance, float referenceDistance) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (cinematicAudioExclusive_) return 0;
    if (!assetManager_) {
        LOG_WARNING("AudioEngine::playSound3D(path): no AssetManager set");
        return false;
    }
    try {
        auto data = assetManager_->readFileBounded(mpqPath, 32ull * 1024 * 1024);
        if (data.empty()) {
            LOG_WARNING("AudioEngine::playSound3D: failed to load '", mpqPath, "'");
            return false;
        }
        return playSound3D(data, position, volume, pitch, maxDistance, referenceDistance);
    } catch (const std::bad_alloc&) {
        LOG_WARNING("Audio clip skipped: CPU allocation failed for ", mpqPath);
        return false;
    }
}

bool AudioEngine::hasVoiceCapacity(size_t pcmBytes) const {
    constexpr size_t maxVoices = 64;
    constexpr size_t maxActivePcm = 32ull * 1024 * 1024;
    if (activeSounds_.size() >= maxVoices || pcmBytes > maxActivePcm) return false;
    size_t retained = 0;
    // Conservative count includes shared clips for each voice; it never
    // underestimates the memory pinned by sounds after decoded-cache eviction.
    for (const auto& active : activeSounds_) {
        if (active.pcmDataRef) retained += active.pcmDataRef->capacity();
    }
    return retained <= maxActivePcm - pcmBytes;
}

void AudioEngine::stopAllSounds() {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    for (auto& active : activeSounds_) {
        ma_sound_stop(active.sound);
        ma_sound_uninit(active.sound);
        std::free(active.sound);
        auto* buffer = static_cast<ma_audio_buffer*>(active.buffer);
        ma_audio_buffer_uninit(buffer);
        std::free(buffer);
    }
    activeSounds_.clear();
    decodedPathKeys_.clear();
    // Keep monotonically increasing handles, so old precast IDs cannot stop
    // unrelated sounds in the next world session.
    std::lock_guard<std::shared_mutex> cacheLock(gDecodedWavCacheMutex);
    gDecodedWavCache.clear();
}

bool AudioEngine::playNarration(const std::string& mpqPath, float volume, float startSeconds) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    stopNarration();
    if (!initialized_ || !engine_ || !assetManager_ || mpqPath.empty() ||
        !std::isfinite(startSeconds) || startSeconds < 0.0f) return false;
    try {
        auto data = std::make_shared<const std::vector<uint8_t>>(assetManager_->readFileBounded(mpqPath, 16ull * 1024 * 1024));
        if (data->empty() || data->size() > 16ull * 1024 * 1024) {
            LOG_WARNING("Narration unavailable or exceeds encoded budget: ", mpqPath);
            return false;
        }
        auto uninitDecoder = [](ma_decoder* value) { ma_decoder_uninit(value); };
        MaStorage<ma_decoder, +uninitDecoder> decoder;
        if (!decoder) return false;
        ma_decoder_config config = ma_decoder_config_init_default();
        if (ma_decoder_init_memory(data->data(), data->size(), &config, decoder.get()) != MA_SUCCESS)
            return false;
        decoder.markInitialised();
        SoundStorage sound;
        if (!sound || ma_sound_init_from_data_source(engine_, decoder.get(),
                MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH,
                nullptr, sound.get()) != MA_SUCCESS) return false;
        sound.markInitialised();
        if (startSeconds > 0.0f) {
            // A malformed clock cannot overflow conversion to the decoder's frame index.
            const double frame = double(startSeconds) * decoder.get()->outputSampleRate;
            if (frame >= double(UINT64_MAX) ||
                ma_sound_seek_to_pcm_frame(sound.get(), static_cast<ma_uint64>(frame)) != MA_SUCCESS)
                return false;
        }
        ma_sound_set_volume(sound.get(), glm::clamp(volume, 0.0f, 1.0f));
        ma_sound_set_looping(sound.get(), MA_FALSE);
        if (ma_sound_start(sound.get()) != MA_SUCCESS) return false;
        narrationData_ = std::move(data);
        narrationDecoder_ = decoder.release();
        narrationSound_ = sound.release();
        narrationPaused_ = false;
        LOG_INFO("Narration started: ", mpqPath, " at ", startSeconds, " seconds");
        return true;
    } catch (const std::bad_alloc&) {
        LOG_WARNING("Narration skipped: CPU allocation failed for ", mpqPath);
        return false;
    }
}

void AudioEngine::setCinematicAudioExclusive(bool exclusive) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (cinematicAudioExclusive_ == exclusive) return;
    cinematicAudioExclusive_ = exclusive;
    if (exclusive) stopAllSounds();
    if (musicSound_) ma_sound_set_volume(musicSound_, exclusive ? 0.0f : musicVolume_);
    LOG_INFO("[INTRO_AUDIO] exclusive=", exclusive);
}

void AudioEngine::stopNarration() {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (narrationSound_) {
        ma_sound_uninit(narrationSound_);
        std::free(narrationSound_);
        narrationSound_ = nullptr;
    }
    if (narrationDecoder_) {
        auto* decoder = static_cast<ma_decoder*>(narrationDecoder_);
        ma_decoder_uninit(decoder);
        std::free(decoder);
        narrationDecoder_ = nullptr;
    }
    narrationData_.reset();
    narrationPaused_ = false;
}

void AudioEngine::setNarrationPaused(bool paused) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (!narrationSound_ || paused == narrationPaused_) return;
    if (paused) ma_sound_stop(narrationSound_);
    else if (ma_sound_start(narrationSound_) != MA_SUCCESS) { stopNarration(); return; }
    narrationPaused_ = paused;
}

bool AudioEngine::isNarrationPlaying() const {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    return narrationSound_ && !narrationPaused_ && ma_sound_is_playing(narrationSound_);
}

bool AudioEngine::playMusic(std::shared_ptr<const std::vector<uint8_t>> musicData,
                            float volume, bool loop) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (!initialized_ || !engine_ || !musicData || musicData->empty()) {
        return false;
    }

    LOG_INFO("AudioEngine::playMusic - data size: ", musicData->size(), " bytes, volume: ", volume);

    // Stop any currently playing music
    stopMusic();

    // Keep the encoded bytes alive for as long as miniaudio's decoder streams them.
    musicData_ = std::move(musicData);
    musicVolume_ = volume;

    // Create decoder from memory (for streaming MP3/OGG)
    ma_decoder* decoder = new ma_decoder();
    ma_decoder_config decoderConfig = ma_decoder_config_init_default();
    ma_result result = ma_decoder_init_memory(
        musicData_->data(),
        musicData_->size(),
        &decoderConfig,
        decoder
    );

    if (result != MA_SUCCESS) {
        LOG_ERROR("Failed to create music decoder: ", result);
        delete decoder;
        musicData_.reset();
        return false;
    }

    LOG_INFO("Decoder created - format: ", decoder->outputFormat,
             ", channels: ", decoder->outputChannels,
             ", sampleRate: ", decoder->outputSampleRate);

    musicDecoder_ = decoder;

    // Create streaming sound from decoder
    musicSound_ = static_cast<ma_sound*>(std::malloc(sizeof(ma_sound)));
    if (!musicSound_) {
        ma_decoder_uninit(decoder);
        delete decoder;
        musicDecoder_ = nullptr;
        musicData_.reset();
        return false;
    }
    result = ma_sound_init_from_data_source(
        engine_,
        decoder,
        MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION,
        nullptr,
        musicSound_
    );

    if (result != MA_SUCCESS) {
        LOG_ERROR("Failed to create music sound: ", result);
        ma_decoder_uninit(decoder);
        delete decoder;
        musicDecoder_ = nullptr;
        std::free(musicSound_);
        musicSound_ = nullptr;
        musicData_.reset();
        return false;
    }

    // Set volume and looping
    ma_sound_set_volume(musicSound_, cinematicAudioExclusive_ ? 0.0f : volume);
    ma_sound_set_looping(musicSound_, loop ? MA_TRUE : MA_FALSE);

    // Start playback
#ifdef WOWEE_PS4
    platform::ps4::reportBootStage("audio: music start");
#endif
    result = ma_sound_start(musicSound_);
#ifdef WOWEE_PS4
    platform::ps4::reportBootStage(result == MA_SUCCESS ? "audio: music started" : "audio: music start failed");
#endif
    if (result != MA_SUCCESS) {
        LOG_ERROR("Failed to start music playback: ", result);
        ma_sound_uninit(musicSound_);
        std::free(musicSound_);
        musicSound_ = nullptr;
        ma_decoder_uninit(decoder);
        delete decoder;
        musicDecoder_ = nullptr;
        // The two failure paths above release this and this one did not, so a
        // failed start pinned the whole encoded file until the next playMusic
        // reassigned the member.
        musicData_.reset();
        return false;
    }

    LOG_INFO("Music playback started successfully - volume: ", volume,
             ", loop: ", loop,
             ", is_playing: ", ma_sound_is_playing(musicSound_));

    return true;
}

void AudioEngine::stopMusic() {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (musicSound_) {
        ma_sound_uninit(musicSound_);
        std::free(musicSound_);
        musicSound_ = nullptr;
    }
    if (musicDecoder_) {
        ma_decoder* decoder = static_cast<ma_decoder*>(musicDecoder_);
        ma_decoder_uninit(decoder);
        delete decoder;
        musicDecoder_ = nullptr;
    }
    musicData_.reset();
}

bool AudioEngine::isMusicPlaying() const {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    if (!musicSound_) {
        return false;
    }
    return ma_sound_is_playing(musicSound_) == MA_TRUE;
}

void AudioEngine::setMusicVolume(float volume) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    musicVolume_ = glm::clamp(volume, 0.0f, 1.0f);
    if (musicSound_) {
        ma_sound_set_volume(musicSound_, cinematicAudioExclusive_ ? 0.0f : musicVolume_);
    }
}

void AudioEngine::update(float deltaTime) {
    std::lock_guard<std::recursive_mutex> playbackLock(playbackMutex_);
    (void)deltaTime;

    if (!initialized_ || !engine_) {
        return;
    }

    if (narrationSound_ && !narrationPaused_ && ma_sound_at_end(narrationSound_))
        stopNarration();

    // Clean up finished sounds - swap-and-pop avoids the O(N) shift that
    // vector::erase does for each removal (and the ref-count atomics in
    // ActiveSound's shared_ptr made that shift noticeably more expensive).
    for (size_t i = 0; i < activeSounds_.size(); ) {
        if (!ma_sound_is_playing(activeSounds_[i].sound)) {
            ma_sound_uninit(activeSounds_[i].sound);
            std::free(activeSounds_[i].sound);
            ma_audio_buffer* buffer = static_cast<ma_audio_buffer*>(activeSounds_[i].buffer);
            ma_audio_buffer_uninit(buffer);
            std::free(buffer);
            activeSounds_[i] = std::move(activeSounds_.back());
            activeSounds_.pop_back();
        } else {
            ++i;
        }
    }
}

} // namespace audio
} // namespace wowee
