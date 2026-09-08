#include "audio/music_manager.hpp"

#include <chrono>
#include "audio/audio_engine.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/thread_pool.hpp"
#include "core/logger.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace wowee {
namespace audio {

MusicManager::MusicManager() = default;

MusicManager::~MusicManager() {
    shutdown();
}

bool MusicManager::initialize(pipeline::AssetManager* assets) {
    assetManager = assets;
    LOG_INFO("Music manager initialized");
    return true;
}

float MusicManager::effectiveMusicVolume() const {
    float vol = volumePercent / 100.0f;
    if (underwaterMode) {
        vol *= 0.3f;
    }
    return vol;
}

void MusicManager::shutdown() {
    cancelPendingFileLoad();
    AudioEngine::instance().stopMusic();
    playing = false;
    fadingIn = false;
    fadingOut = false;
    crossfading = false;
    fadeInTimer = 0.0f;
    fadeInDuration = 0.0f;
    fadeInTargetVolume = 0.0f;
    currentTrack.clear();
    currentTrackIsFile = false;
    pendingTrack.clear();
    pendingIsFile = false;
    musicDataCache_.clear();
    assetManager = nullptr;
}

void MusicManager::resetWorldState() {
    stopMusic(0.0f);
    setUnderwaterMode(false);
    // Zone tracks are reloadable; preserve settings and the asset provider only.
    musicDataCache_.clear();
}

void MusicManager::preloadMusic(const std::string& mpqPath) {
    if (!assetManager || mpqPath.empty()) return;
    if (musicDataCache_.find(mpqPath) != musicDataCache_.end()) return;

    constexpr size_t maxEncodedMusicBytes = 16ull * 1024 * 1024;
    auto data = assetManager->readFileBounded(mpqPath, maxEncodedMusicBytes);
    if (!data.empty() && data.capacity() <= maxEncodedMusicBytes) {
        size_t cachedBytes = 0;
        for (const auto& entry : musicDataCache_)
            if (entry.second) cachedBytes += entry.second->capacity();
        // Playing tracks own their bytes independently; eviction never cuts audio.
        if (cachedBytes > maxEncodedMusicBytes - data.capacity()) musicDataCache_.clear();
        musicDataCache_[mpqPath] =
            std::make_shared<const std::vector<uint8_t>>(std::move(data));
    }
}

void MusicManager::playMusic(const std::string& mpqPath, bool loop, float fadeInMs) {
    if (!assetManager) return;
    if (mpqPath == currentTrack && playing) return;

    // Check if AudioEngine is ready
    if (!AudioEngine::instance().isInitialized()) {
        LOG_WARNING("Music: AudioEngine not initialized");
        return;
    }

    // A newer request wins; retire the previous read before loading its replacement.
    cancelPendingFileLoad();

    // Read music file from cache or MPQ
    auto cacheIt = musicDataCache_.find(mpqPath);
    if (cacheIt == musicDataCache_.end()) {
        preloadMusic(mpqPath);
        cacheIt = musicDataCache_.find(mpqPath);
    }
    if (cacheIt == musicDataCache_.end() || !cacheIt->second || cacheIt->second->empty()) {
        LOG_WARNING("Music: Could not read: ", mpqPath);
        return;
    }

    // Play with AudioEngine (non-blocking, streams from memory)
    float targetVolume = effectiveMusicVolume();
    float startVolume = (fadeInMs > 0.0f) ? 0.0f : targetVolume;
    if (AudioEngine::instance().playMusic(cacheIt->second, startVolume, loop)) {
        playing = true;
        fadingIn = false;
        if (fadeInMs > 0.0f) {
            fadingIn = true;
            fadeInTimer = 0.0f;
            fadeInDuration = std::max(0.05f, fadeInMs / 1000.0f);
            fadeInTargetVolume = targetVolume;
            AudioEngine::instance().setMusicVolume(0.0f);
        }
        currentTrack = mpqPath;
        currentTrackIsFile = false;
        LOG_INFO("Music: Playing ", mpqPath);
    } else {
        LOG_ERROR("Music: Failed to play music via AudioEngine");
    }
}

void MusicManager::playFilePath(const std::string& filePath, bool loop, float fadeInMs) {
    if (filePath.empty()) return;
    if (filePath == currentTrack && playing) return;
    if (!std::filesystem::exists(filePath)) {
        LOG_WARNING("Music: file not found: ", filePath);
        return;
    }

    // Check if AudioEngine is ready
    if (!AudioEngine::instance().isInitialized()) {
        LOG_WARNING("Music: AudioEngine not initialized");
        return;
    }

    // Reuse an in-flight read of the same file with the newest playback options.
    if (pendingFileLoad_ && pendingFileLoad_->path == filePath) {
        pendingFileLoad_->loop = loop;
        pendingFileLoad_->fadeInMs = fadeInMs;
        return;
    }

    cancelPendingFileLoad(); // at most one outstanding encoded track allocation
    // Read the track on a worker. A login track is ~6MB, and reading it here stalled
    // the render frame that asked for it. pollPendingFileLoad() picks it up from
    // update(), so every AudioEngine call still happens on the main thread.
    pendingFileLoad_ = PendingFileLoad{
        core::ThreadPool::ioWorkers().submit([filePath]()
                -> std::shared_ptr<const std::vector<uint8_t>> {
            try {
                std::ifstream file(filePath, std::ios::binary | std::ios::ate);
                if (!file) return nullptr;
                std::streamsize size = file.tellg();
                if (size <= 0 || size > 16 * 1024 * 1024) return nullptr;
                file.seekg(0, std::ios::beg);
                std::vector<uint8_t> bytes(static_cast<size_t>(size));
                if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) return nullptr;
                return std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
            } catch (const std::bad_alloc&) { return nullptr; }
        }),
        filePath, loop, fadeInMs
    };
}

void MusicManager::cancelPendingFileLoad() {
    // Pool futures do not join on destruction. Drain the bounded read before
    // dropping its owner so logout/quit cannot retain an orphaned track worker.
    if (pendingFileLoad_ && pendingFileLoad_->future.valid()) pendingFileLoad_->future.wait();
    pendingFileLoad_.reset();
}

void MusicManager::pollPendingFileLoad() {
    if (!pendingFileLoad_) return;
    auto& pending = *pendingFileLoad_;
    if (!pending.future.valid()) { pendingFileLoad_.reset(); return; }
    if (pending.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    auto data = pending.future.get();
    const std::string filePath = pending.path;
    const bool loop = pending.loop;
    const float fadeInMs = pending.fadeInMs;
    pendingFileLoad_.reset();

    if (!data || data->empty()) {
        LOG_ERROR("Music: Could not read file: ", filePath);
        return;
    }

    // Play with AudioEngine
    float targetVolume = effectiveMusicVolume();
    float startVolume = (fadeInMs > 0.0f) ? 0.0f : targetVolume;
    if (AudioEngine::instance().playMusic(std::move(data), startVolume, loop)) {
        playing = true;
        fadingIn = false;
        if (fadeInMs > 0.0f) {
            fadingIn = true;
            fadeInTimer = 0.0f;
            fadeInDuration = std::max(0.05f, fadeInMs / 1000.0f);
            fadeInTargetVolume = targetVolume;
            AudioEngine::instance().setMusicVolume(0.0f);
        }
        currentTrack = filePath;
        currentTrackIsFile = true;
        LOG_INFO("Music: Playing file ", filePath);
    } else {
        LOG_ERROR("Music: Failed to play music via AudioEngine");
    }
}

void MusicManager::stopMusic(float fadeMs) {
    cancelPendingFileLoad();
    fadingIn = false;
    crossfading = false;
    pendingTrack.clear();
    pendingIsFile = false;
    if (!playing && fadeMs > 0.0f) return;

    if (fadeMs > 0.0f) {
        // Begin fade-out; actual stop happens once volume reaches zero in update()
        fadingOut = true;
        fadeOutTimer = 0.0f;
        fadeOutDuration = fadeMs / 1000.0f;
        fadeOutStartVolume = effectiveMusicVolume();
    } else {
        AudioEngine::instance().stopMusic();
        playing = false;
        fadingOut = false;
        currentTrack.clear();
        currentTrackIsFile = false;
    }
}

void MusicManager::setVolume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    if (volumePercent == volume) return;
    volumePercent = volume;

    // Update AudioEngine music volume directly (no restart needed!)
    float vol = effectiveMusicVolume();
    if (fadingIn) {
        fadeInTargetVolume = vol;
        float t = std::clamp(fadeInTimer / std::max(fadeInDuration, 0.001f), 0.0f, 1.0f);
        AudioEngine::instance().setMusicVolume(fadeInTargetVolume * t);
    } else {
        AudioEngine::instance().setMusicVolume(vol);
    }
}

void MusicManager::setUnderwaterMode(bool underwater) {
    if (underwaterMode == underwater) return;
    underwaterMode = underwater;

    // Apply volume change immediately
    float vol = effectiveMusicVolume();
    if (fadingIn) {
        fadeInTargetVolume = vol;
        float t = std::clamp(fadeInTimer / std::max(fadeInDuration, 0.001f), 0.0f, 1.0f);
        AudioEngine::instance().setMusicVolume(fadeInTargetVolume * t);
    } else {
        AudioEngine::instance().setMusicVolume(vol);
    }
}

void MusicManager::crossfadeTo(const std::string& mpqPath, float fadeMs) {
    if (mpqPath == currentTrack && playing) return;

    // Simple implementation: stop and start (no actual crossfade yet)
    if (fadeMs > 0 && playing) {
        crossfading = true;
        pendingTrack = mpqPath;
        pendingIsFile = false;
        fadeTimer = 0.0f;
        fadeDuration = fadeMs / 1000.0f;
        AudioEngine::instance().stopMusic();
    } else {
        playMusic(mpqPath, loopTracks_);
    }
}

void MusicManager::crossfadeToFile(const std::string& filePath, float fadeMs) {
    if (filePath == currentTrack && playing) return;

    if (fadeMs > 0 && playing) {
        crossfading = true;
        pendingTrack = filePath;
        pendingIsFile = true;
        fadeTimer = 0.0f;
        fadeDuration = fadeMs / 1000.0f;
        AudioEngine::instance().stopMusic();
    } else {
        playFilePath(filePath, loopTracks_);
    }
}

void MusicManager::update(float deltaTime) {
    // Hand off any background-loaded track before touching playback state.
    pollPendingFileLoad();

    // Check if music is still playing
    if (playing && !AudioEngine::instance().isMusicPlaying()) {
        playing = false;
    }

    if (fadingOut) {
        fadeOutTimer += deltaTime;
        float t = std::clamp(1.0f - fadeOutTimer / std::max(fadeOutDuration, 0.001f), 0.0f, 1.0f);
        AudioEngine::instance().setMusicVolume(fadeOutStartVolume * t);
        if (t <= 0.0f) {
            // Fade complete - stop playback and restore volume for next track
            fadingOut = false;
            AudioEngine::instance().stopMusic();
            AudioEngine::instance().setMusicVolume(effectiveMusicVolume());
            playing = false;
            currentTrack.clear();
            currentTrackIsFile = false;
        }
        return;  // Don't process other fade logic while fading out
    }

    if (fadingIn) {
        fadeInTimer += deltaTime;
        float t = std::clamp(fadeInTimer / std::max(fadeInDuration, 0.001f), 0.0f, 1.0f);
        AudioEngine::instance().setMusicVolume(fadeInTargetVolume * t);
        if (t >= 1.0f) {
            fadingIn = false;
        }
    }

    // Handle crossfade
    if (crossfading) {
        fadeTimer += deltaTime;
        if (fadeTimer >= fadeDuration * 0.3f) {
            // Start new track after brief pause
            crossfading = false;
            if (pendingIsFile) {
                playFilePath(pendingTrack, loopTracks_);
            } else {
                playMusic(pendingTrack, loopTracks_);
            }
            pendingTrack.clear();
            pendingIsFile = false;
        }
    }
}

} // namespace audio
} // namespace wowee
