#pragma once
#include "imgui.h"
#include <cmath>
#include <climits>
#include <cstring>

// Same transform as upstream ImGui's vertex push constants, applied only to
// the GPU upload copy on PS4. DisplayPos/DisplaySize use logical UI units;
// FramebufferScale changes viewport/scissors, not this NDC conversion.
struct ImGui_ImplVulkan_PS4NdcTransform {
    float scale_x, scale_y, translate_x, translate_y;
};
static inline bool ImGui_ImplVulkan_PS4MakeNdcTransform(
    const ImVec2& position, const ImVec2& size, const ImVec2& framebuffer_scale,
    ImGui_ImplVulkan_PS4NdcTransform& out, int& width, int& height)
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(size.x) || !std::isfinite(size.y) ||
        !std::isfinite(framebuffer_scale.x) || !std::isfinite(framebuffer_scale.y) ||
        size.x <= 0 || size.y <= 0 || framebuffer_scale.x <= 0 || framebuffer_scale.y <= 0)
        return false;
    const float fw = size.x * framebuffer_scale.x, fh = size.y * framebuffer_scale.y;
    if (!std::isfinite(fw) || !std::isfinite(fh) || fw < 1 || fh < 1 ||
        fw >= static_cast<float>(INT_MAX) || fh >= static_cast<float>(INT_MAX))
        return false;
    out.scale_x = 2.0f / size.x;
    out.scale_y = 2.0f / size.y;
    out.translate_x = -1.0f - position.x * out.scale_x;
    out.translate_y = -1.0f - position.y * out.scale_y;
    if (!std::isfinite(out.scale_x) || !std::isfinite(out.scale_y) ||
        !std::isfinite(out.translate_x) || !std::isfinite(out.translate_y))
        return false;
    width = static_cast<int>(fw);
    height = static_cast<int>(fh);
    return true;
}
static inline bool ImGui_ImplVulkan_PS4CopyNdcVertices(ImDrawVert* destination,
    const ImDrawVert* source, size_t count, const ImGui_ImplVulkan_PS4NdcTransform& transform)
{
    if (count && (!source || !destination)) return false;
    for (size_t i = 0; i < count; ++i)
        if (!std::isfinite(source[i].pos.x) || !std::isfinite(source[i].pos.y) ||
            !std::isfinite(source[i].pos.x * transform.scale_x + transform.translate_x) ||
            !std::isfinite(source[i].pos.y * transform.scale_y + transform.translate_y))
            return false;
    if (count) std::memcpy(destination, source, count * sizeof(ImDrawVert));
    for (size_t i = 0; i < count; ++i) {
        destination[i].pos.x = source[i].pos.x * transform.scale_x + transform.translate_x;
        destination[i].pos.y = source[i].pos.y * transform.scale_y + transform.translate_y;
    }
    return true;
}
