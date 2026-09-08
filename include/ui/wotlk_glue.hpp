#pragma once

#include <imgui.h>
#include <memory>

namespace wowee::pipeline { class AssetManager; }
namespace wowee::rendering { class VkContext; }
namespace wowee::ui {

// Original client glue artwork is loaded from the user's MPQs. Preparation is
// deliberately separate from drawing: no upload or destruction during a frame.
class WotlkGlue {
public:
    WotlkGlue();
    ~WotlkGlue();
    WotlkGlue(const WotlkGlue&) = delete;
    WotlkGlue& operator=(const WotlkGlue&) = delete;
    void ensureLoaded(pipeline::AssetManager* assets, rendering::VkContext* context);
    bool drawAsset(ImDrawList* list, const char* path, ImVec2 a, ImVec2 b,
                   ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1),
                   ImU32 tint = IM_COL32_WHITE) const;
    bool drawLogo(ImDrawList* list, ImVec2 a, ImVec2 b) const;
    void drawButton(ImDrawList* list, ImVec2 a, ImVec2 b,
                    bool hovered, bool held, bool enabled) const;
    void drawField(ImDrawList* list, ImVec2 a, ImVec2 b, bool focused) const;
    void drawFrame(ImDrawList* list, ImVec2 a, ImVec2 b) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
