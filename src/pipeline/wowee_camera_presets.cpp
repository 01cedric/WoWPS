#include "pipeline/wowee_camera_presets.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'A', 'M'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wcam";

} // namespace

const WoweeCameraPresets::Entry*
WoweeCameraPresets::findById(uint32_t presetId) const {
    for (const auto& e : entries)
        if (e.presetId == presetId) return &e;
    return nullptr;
}

std::vector<const WoweeCameraPresets::Entry*>
WoweeCameraPresets::findByPurpose(uint8_t purposeKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.purposeKind == purposeKind) out.push_back(&e);
    return out;
}

bool WoweeCameraPresetsLoader::save(const WoweeCameraPresets& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeCameraPresets::Entry& e) {
        writePOD(os, e.presetId);
        writeStr(os, e.name);
        writePOD(os, e.purposeKind);
        writePOD(os, e.motionDamping);
        writePOD(os, e.pad0);
        writePOD(os, e.fovDegrees);
        writePOD(os, e.distanceFromTarget);
        writePOD(os, e.pitchDegrees);
        writePOD(os, e.yawOffsetDegrees);
        writePOD(os, e.shoulderOffsetMeters);
        writePOD(os, e.focusBoneId);
                       });
}

WoweeCameraPresets WoweeCameraPresetsLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeCameraPresets>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeCameraPresets::Entry& e) {
        if (!readPOD(is, e.presetId)) { return false; }
        if (!readStr(is, e.name)) { return false; }
        if (!readPOD(is, e.purposeKind) ||
            !readPOD(is, e.motionDamping) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.fovDegrees) ||
            !readPOD(is, e.distanceFromTarget) ||
            !readPOD(is, e.pitchDegrees) ||
            !readPOD(is, e.yawOffsetDegrees) ||
            !readPOD(is, e.shoulderOffsetMeters) ||
            !readPOD(is, e.focusBoneId)) { return false; }
                                  return true;
                              });
}

bool WoweeCameraPresetsLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

namespace {

WoweeCameraPresets::Entry makePreset(
    uint32_t presetId, const char* name,
    uint8_t purposeKind, uint8_t motionDamping,
    float fov, float distance, float pitch,
    float yawOffset, float shoulderOffset,
    uint32_t focusBoneId) {
    WoweeCameraPresets::Entry e;
    e.presetId = presetId; e.name = name;
    e.purposeKind = purposeKind;
    e.motionDamping = motionDamping;
    e.fovDegrees = fov;
    e.distanceFromTarget = distance;
    e.pitchDegrees = pitch;
    e.yawOffsetDegrees = yawOffset;
    e.shoulderOffsetMeters = shoulderOffset;
    e.focusBoneId = focusBoneId;
    return e;
}

} // namespace

WoweeCameraPresets WoweeCameraPresetsLoader::makeCombatPresets(
    const std::string& catalogName) {
    using C = WoweeCameraPresets;
    WoweeCameraPresets c;
    c.name = catalogName;
    // Default combat camera: 75° FOV, 12m back,
    // -15° pitch (slightly looking down). Low
    // damping (10) for responsive tracking.
    c.entries.push_back(makePreset(
        1, "Combat Default", C::Combat, 10,
        75.f, 12.f, -15.f, 0.f, 0.f,
        C::kFollowRootBoneId));
    // Wide FOV ranged-DPS camera: 90° FOV, 18m back,
    // good for hunters/mages tracking enemies at
    // range. Slight shoulder offset for visibility
    // past the player model.
    c.entries.push_back(makePreset(
        2, "Combat Ranged Wide", C::Combat, 15,
        90.f, 18.f, -12.f, 0.f, 0.5f,
        C::kFollowRootBoneId));
    // Tight melee shoulder-cam: 60° FOV, 5m back,
    // strong over-the-shoulder offset (1.0m right).
    // Tracks chest bone (12) for steady framing.
    c.entries.push_back(makePreset(
        3, "Combat Melee Shoulder", C::Combat, 8,
        60.f, 5.f, -10.f, 0.f, 1.0f,
        12));
    return c;
}

WoweeCameraPresets WoweeCameraPresetsLoader::makeMountedPresets(
    const std::string& catalogName) {
    using C = WoweeCameraPresets;
    WoweeCameraPresets c;
    c.name = catalogName;
    // Ground mount: pulled back 16m to fit the mount
    // model into frame, normal pitch. Medium damping
    // (60) smooths turning that would otherwise feel
    // jerky on a horse/wolf at gallop speed.
    c.entries.push_back(makePreset(
        10, "Mount Ground", C::Mounted, 60,
        80.f, 16.f, -18.f, 0.f, 0.f,
        C::kFollowRootBoneId));
    // Flying mount: more pitch (looking down at
    // landscape) and pulled back further to show
    // wings. Higher damping (90) for cinematic
    // soaring feel.
    c.entries.push_back(makePreset(
        11, "Mount Flying", C::Mounted, 90,
        85.f, 22.f, -28.f, 0.f, 0.f,
        C::kFollowRootBoneId));
    return c;
}

WoweeCameraPresets WoweeCameraPresetsLoader::makeCinematicPresets(
    const std::string& catalogName) {
    using C = WoweeCameraPresets;
    WoweeCameraPresets c;
    c.name = catalogName;
    // Over-shoulder dialogue: 50° FOV (telephoto),
    // 4m back, strong shoulder offset (1.5m right),
    // tracks head bone (50) for face-level framing.
    // High damping (180) for film-quality motion.
    c.entries.push_back(makePreset(
        20, "Cinematic Dialogue", C::Cinematic, 180,
        50.f, 4.f, 0.f, 0.f, 1.5f,
        50 /* head bone */));
    // Wide establishing: 100° FOV, 30m back, looking
    // down at -30°. Used for "you arrive at a new
    // zone" reveals. No shoulder offset.
    c.entries.push_back(makePreset(
        21, "Cinematic Establishing", C::Cinematic, 200,
        100.f, 30.f, -30.f, 0.f, 0.f,
        C::kFollowRootBoneId));
    // Close-up portrait: 35° (very telephoto),
    // 2m back at face level (head bone 50),
    // slight 0.3m shoulder offset for dramatic
    // composition.
    c.entries.push_back(makePreset(
        22, "Cinematic Portrait", C::Cinematic, 220,
        35.f, 2.f, 5.f /* slightly looking up */,
        15.f /* yawed for 3/4 face */,
        0.3f, 50));
    return c;
}

} // namespace pipeline
} // namespace wowee
