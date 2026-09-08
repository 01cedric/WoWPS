#include "audio/movement_sound_manager.hpp"
#include "audio/sample_load.hpp"
#include "audio/audio_engine.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include <random>

namespace wowee {
namespace audio {

namespace {
    std::random_device rd;
    std::mt19937 gen(rd());
}

bool MovementSoundManager::initialize(pipeline::AssetManager* assets) {
    if (!assets) {
        LOG_ERROR("MovementSoundManager: AssetManager is null");
        return false;
    }

    LOG_INFO("MovementSoundManager: Initializing...");

    // Load water splash sounds - entering water
    enterWaterSmallSounds_.resize(1);
    loadSound("Sound\\Spells\\EnterWaterSmall.wav", enterWaterSmallSounds_[0], assets);

    enterWaterMediumSounds_.resize(1);
    loadSound("Sound\\Spells\\EnterWaterMedium.wav", enterWaterMediumSounds_[0], assets);

    enterWaterGiantSounds_.resize(1);
    loadSound("Sound\\Spells\\EnterWaterGiant.wav", enterWaterGiantSounds_[0], assets);

    // Water footsteps - walking through the shallows. These used to point at
    // Sound\\Spells\\WaterFootstep*, which does not exist in the data, so every
    // one of these loaded nothing and wading was silent. The real sets are the
    // WaterSplash footsteps (character-sized) and the Huge variants.
    const char kVariants[] = {'A', 'B', 'C', 'D', 'E'};
    waterFootstepSmallSounds_.resize(5);
    waterFootstepMediumSounds_.resize(5);
    waterFootstepHugeSounds_.resize(5);
    for (int i = 0; i < 5; ++i) {
        const std::string v(1, kVariants[i]);
        loadSound("Sound\\Character\\Footsteps\\WaterSplash\\FootStepsMediumWater" + v + ".wav",
                  waterFootstepSmallSounds_[i], assets);
        loadSound("Sound\\Character\\Footsteps\\WaterSplash\\FootStepsMediumWater" + v + ".wav",
                  waterFootstepMediumSounds_[i], assets);
        loadSound("Sound\\Character\\Footsteps\\FootStepsHugeWater" + v + ".wav",
                  waterFootstepHugeSounds_[i], assets);
    }

    LOG_INFO("MovementSoundManager: Water sounds - Enter small: ", enterWaterSmallSounds_[0].loaded ? "YES" : "NO",
             ", Enter medium: ", enterWaterMediumSounds_[0].loaded ? "YES" : "NO",
             ", Enter giant: ", enterWaterGiantSounds_[0].loaded ? "YES" : "NO");

    initialized_ = true;
    LOG_INFO("MovementSoundManager: Initialization complete");
    return true;
}

void MovementSoundManager::shutdown() {
    initialized_ = false;
}

bool MovementSoundManager::loadSound(const std::string& path, MovementSample& sample, pipeline::AssetManager* assets) {
    // Quietly: not every sound in this bank ships with every install.
    return loadSampleFile(path, sample, assets, nullptr);
}

void MovementSoundManager::playSound(const std::vector<MovementSample>& library, float volumeMultiplier) {
    if (!initialized_ || library.empty() || !library[0].loaded) return;

    float volume = 0.7f * volumeScale_ * volumeMultiplier;
    AudioEngine::instance().playSound2D(samplePlaybackSource(library[0]), volume, 1.0f);
}

void MovementSoundManager::playRandomSound(const std::vector<MovementSample>& library, float volumeMultiplier) {
    if (!initialized_) return;
    // Among the ones that loaded - see pickLoadedSample. The base volume is
    // this bank's own and stays here.
    const MovementSample* chosen = pickLoadedSample(library, gen);
    if (!chosen) return;
    const float volume = 0.7f * volumeScale_ * volumeMultiplier;
    AudioEngine::instance().playSound2D(samplePlaybackSource(*chosen), volume, 1.0f);
}

void MovementSoundManager::setVolumeScale(float scale) {
    volumeScale_ = std::max(0.0f, std::min(1.0f, scale));
}

void MovementSoundManager::playEnterWater(CharacterSize size) {
    switch (size) {
        case CharacterSize::SMALL:
            playSound(enterWaterSmallSounds_, 0.8f);
            break;
        case CharacterSize::MEDIUM:
            playSound(enterWaterMediumSounds_, 1.0f);
            break;
        case CharacterSize::LARGE:
        case CharacterSize::GIANT:
            playSound(enterWaterGiantSounds_, 1.2f);
            break;
    }
}

void MovementSoundManager::playWaterFootstep(CharacterSize size) {
    switch (size) {
        case CharacterSize::SMALL:
            playRandomSound(waterFootstepSmallSounds_, 0.6f);
            break;
        case CharacterSize::MEDIUM:
            playRandomSound(waterFootstepMediumSounds_, 0.8f);
            break;
        case CharacterSize::LARGE:
        case CharacterSize::GIANT:
            playRandomSound(waterFootstepHugeSounds_, 1.0f);
            break;
    }
}

} // namespace audio
} // namespace wowee
