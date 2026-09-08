#pragma once

// Shared sound-bank policy. Desktop builds preload samples; PS4 registers
// available paths and reads a selected clip through AudioEngine at playback.
// This keeps hundreds of unused race/zone/spell WAVs out of the CPU heap.

#include <exception>
#include <random>
#include <string>
#include <vector>

#include "core/logger.hpp"
#include "pipeline/asset_manager.hpp"

namespace wowee::audio {

/// Prepare `sample` from `path`. False leaves it marked unavailable.
///
/// `who` names the manager in the log line, or is null to fail quietly. That
/// is not a detail: the combat, movement and spell banks ask for sounds that
/// legitimately may not be present in a given install and say so in a comment,
/// while the UI and ambient banks report a miss. Flattening the five into one
/// that always logs would have turned three of them into log spam on a normal
/// client, which is why the distinction is a parameter rather than a default.
template <typename Sample, typename Assets = pipeline::AssetManager>
bool loadSampleFile(const std::string& path, Sample& sample,
                    Assets* assets, const char* who) {
    sample.path = path;
    sample.loaded = false;
    if (!assets) return false;

    try {
#ifdef WOWEE_PS4
        // A catalog covers every race, zone and spell; retaining all WAVs
        // here exhausted the flexible heap before the first world frame.
        // The AudioEngine path overload loads only the selected original clip.
        std::vector<uint8_t>().swap(sample.data);
        sample.loaded = assets->fileExists(path);
        return sample.loaded;
#else
        sample.data = assets->readFile(path);
        if (!sample.data.empty()) {
            sample.loaded = true;
            return true;
        }
#endif
    } catch (const std::exception& e) {
        if (who) LOG_ERROR(who, ": Failed to load ", path, ": ", e.what());
    }
    return false;
}

/// Select the on-demand path on PS4, or the preloaded bytes elsewhere.
/// Keeping this choice at the call boundary also covers stoppable spell audio.
template <typename Sample>
decltype(auto) samplePlaybackSource(const Sample& sample) {
#ifdef WOWEE_PS4
    return (sample.path);
#else
    return (sample.data);
#endif
}

/// Which of `library`'s loaded samples to play, or null when none are.
///
/// The combat, movement and spell banks each wrote this out: gather the ones
/// that loaded, and if any did, pick one uniformly. They differed only in the
/// sample type and in the base volume, which stays at the call sites because
/// it is a decision about that bank rather than about picking.
///
/// Choosing among the *loaded* ones matters and is easy to get wrong by
/// indexing the whole library instead: a bank whose files are half missing
/// would then fall silent half the time it was asked to play, with nothing
/// reported either way.
template <typename Sample, typename Rng>
const Sample* pickLoadedSample(const std::vector<Sample>& library, Rng& gen) {
    std::vector<const Sample*> loaded;
    loaded.reserve(library.size());
    for (const Sample& sample : library) {
        if (sample.loaded) loaded.push_back(&sample);
    }
    if (loaded.empty()) return nullptr;
    std::uniform_int_distribution<size_t> pick(0, loaded.size() - 1);
    return loaded[pick(gen)];
}

}  // namespace wowee::audio
