#pragma once

/// The names the footstep sounds are stored under.
///
/// Six families, each a stem and a run of suffixes, and none of them derivable
/// from another: the solid surfaces run A to L under one stem, the huge ones A
/// to E under a different stem, the horse ones are numbered 01 to 05 in a
/// different folder entirely, and water does not follow the solid naming at
/// all - there is no mFootMediumLargeWater in the data, which is why the water
/// surface once loaded an empty clip set and walking through shallows was
/// silent.
///
/// Kept here rather than in the manager that plays them because the activity
/// sound manager builds the same solid-surface set for its own use, and had
/// its own copy of it. A path that stops matching the archive produces no
/// sound and no error, so two copies is two chances to be silent in one place
/// and not the other.

#include <cstdio>
#include <string>
#include <vector>

namespace wowee::audio {

/// The solid surfaces - stone, dirt, grass, wood, snow.
///
/// Twelve candidate names, A to L, and that is a probe rather than a count:
/// the loader tests each against the archive and skips the ones that are not
/// there. Measured against an extracted 3.3.5a install, stone has ten, dirt
/// nine, wood eight, and grass and snow five each - so a range fitted to any
/// one material would lose clips from the others. Metal has none at all under
/// this stem and lives in its own folder; see altFootstepPaths.
inline std::vector<std::string> classicFootstepPaths(const std::string& material) {
    std::vector<std::string> out;
    for (char c = 'A'; c <= 'L'; ++c) {
        out.push_back("Sound\\Character\\Footsteps\\mFootMediumLarge" + material +
                      std::string(1, c) + ".wav");
    }
    return out;
}

/// Surfaces stored as a numbered run in their own folder, 01 to 08.
///
/// Metal is the one that is stored this way, under
/// MediumLargeMetalFootsteps\\MediumLargeFootstepMetal_NN, and all eight are
/// present. The mFootMediumLargeMetal names the solid surfaces would imply do
/// not exist, which is why that spelling is only a fallback.
inline std::vector<std::string> altFootstepPaths(const std::string& folder,
                                                 const std::string& stem) {
    std::vector<std::string> out;
    for (int i = 1; i <= 8; ++i) {
        char index[4];
        std::snprintf(index, sizeof(index), "%02d", i);
        out.push_back("Sound\\Character\\Footsteps\\" + folder + "\\" + stem + "_" +
                      index + ".wav");
    }
    return out;
}

/// A mount's hooves, under Creature rather than Character. Five clips.
inline std::vector<std::string> horseFootstepPaths(const std::string& material) {
    std::vector<std::string> out;
    for (int i = 1; i <= 5; ++i) {
        char index[3];
        std::snprintf(index, sizeof(index), "%02d", i);
        out.push_back("Sound\\Creature\\Horse\\mFootstepsHorse" + material + index +
                      ".wav");
    }
    return out;
}

/// The large races. A different stem from the solid surfaces, and five clips
/// rather than twelve.
inline std::vector<std::string> hugeFootstepPaths(const std::string& material) {
    std::vector<std::string> out;
    for (char c = 'A'; c <= 'E'; ++c) {
        out.push_back("Sound\\Character\\Footsteps\\mFootHuge" + material +
                      std::string(1, c) + ".wav");
    }
    return out;
}

/// Water, which follows none of the above: its own folder and its own stem.
inline std::vector<std::string> waterFootstepPaths() {
    std::vector<std::string> out;
    for (char c = 'A'; c <= 'E'; ++c) {
        out.push_back("Sound\\Character\\Footsteps\\WaterSplash\\FootStepsMediumWater" +
                      std::string(1, c) + ".wav");
    }
    return out;
}

/// Water under a large race - and not in the WaterSplash folder, unlike the
/// medium one above.
inline std::vector<std::string> hugeWaterFootstepPaths() {
    std::vector<std::string> out;
    for (char c = 'A'; c <= 'E'; ++c) {
        out.push_back("Sound\\Character\\Footsteps\\FootstepsHugeWater" +
                      std::string(1, c) + ".wav");
    }
    return out;
}

}  // namespace wowee::audio
