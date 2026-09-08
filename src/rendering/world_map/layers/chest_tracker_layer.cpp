// chest_tracker_layer.cpp - Nearby spawned chest markers on the world map.
#include "rendering/world_map/layers/chest_tracker_layer.hpp"
#include "rendering/world_map/coordinate_projection.hpp"
#include <imgui.h>

namespace wowee {
namespace rendering {
namespace world_map {

void ChestTrackerLayer::render(const LayerContext& ctx) {
    if (!chests_ || chests_->empty()) return;
    const auto projection = currentProjection(ctx);
    if (!projection) return;

    constexpr ImU32 fill = IM_COL32(205, 125, 35, 255);
    constexpr ImU32 outline = IM_COL32(45, 25, 5, 230);
    ImFont* font = ImGui::GetFont();
    for (const auto& chest : *chests_) {
        glm::vec2 uv = renderPosToMapUV(chest.renderPos, projection->bounds, projection->isContinent);
        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) continue;
        const float px = ctx.imgMin.x + uv.x * ctx.displayW;
        const float py = ctx.imgMin.y + uv.y * ctx.displayH;

        // Compact chest silhouette: box, domed lid line, and bright latch.
        constexpr float halfW = 6.0f;
        constexpr float halfH = 4.5f;
        ctx.drawList->AddRectFilled(ImVec2(px - halfW, py - halfH),
                                    ImVec2(px + halfW, py + halfH), fill, 1.5f);
        ctx.drawList->AddRect(ImVec2(px - halfW, py - halfH),
                              ImVec2(px + halfW, py + halfH), outline, 1.5f, 0, 1.5f);
        ctx.drawList->AddLine(ImVec2(px - halfW, py - 1.0f),
                              ImVec2(px + halfW, py - 1.0f), outline, 1.2f);
        ctx.drawList->AddRectFilled(ImVec2(px - 1.2f, py - 1.5f),
                                    ImVec2(px + 1.2f, py + 1.5f),
                                    IM_COL32(255, 220, 80, 255), 0.5f);

        ImVec2 mouse = ImGui::GetMousePos();
        if (mouse.x >= px - halfW - 2.0f && mouse.x <= px + halfW + 2.0f &&
            mouse.y >= py - halfH - 2.0f && mouse.y <= py + halfH + 2.0f &&
            !chest.name.empty()) {
            ImGui::SetTooltip("%s\nChest", chest.name.c_str());
        }
        if (!chest.name.empty()) {
            ImVec2 nameSize = font->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f,
                                                  chest.name.c_str());
            const float tx = px - nameSize.x * 0.5f;
            const float ty = py - nameSize.y - halfH - 3.0f;
            ctx.drawList->AddText(ImVec2(tx + 1.0f, ty + 1.0f),
                                  IM_COL32(0, 0, 0, 190), chest.name.c_str());
            ctx.drawList->AddText(ImVec2(tx, ty), fill, chest.name.c_str());
        }
    }
}

} // namespace world_map
} // namespace rendering
} // namespace wowee
