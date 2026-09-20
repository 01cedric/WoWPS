#pragma once

namespace wowee::rendering {
// Non-owning literals only: diagnostics must not allocate in a failed frame.
inline const char* volumetricFrameStatus(int quality, bool world, bool camera,
    bool temporalUpscaling, bool multisampled, bool allowed, const char* blockedReason,
    bool failed, bool pipeline, bool sceneTarget, bool hasLight) {
    if (quality == 0) return "off";
    if (!world) return "not-world";
    if (!camera) return "no-camera";
    if (temporalUpscaling) return "temporal-upscaling";
    if (multisampled) return "multisampled-depth";
    if (!allowed) return blockedReason;
    if (!hasLight) return "no-key-light";
    if (failed) return "resource-failure";
    if (!pipeline) return "pipeline-not-ready";
    if (!sceneTarget) return "scene-target-not-ready";
    return "ready";
}
} // namespace wowee::rendering
