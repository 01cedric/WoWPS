// party_dot_layer.cpp - Party member position dots on the world map.
// Extracted from WorldMap::renderImGuiOverlay (Phase 8 of refactoring plan).
#include "rendering/world_map/layers/party_dot_layer.hpp"
#include "rendering/world_map/coordinate_projection.hpp"
#include <imgui.h>

namespace wowee {
namespace rendering {
namespace world_map {

void PartyDotLayer::render(const LayerContext& ctx) {
    if (!dots_ || dots_->empty()) return;
    const auto projection = currentProjection(ctx);
    if (!projection) return;

    ImFont* font = ImGui::GetFont();
    for (const auto& dot : *dots_) {
        glm::vec2 uv = renderPosToMapUV(dot.renderPos, projection->bounds, projection->isContinent);
        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) continue;
        float px = ctx.imgMin.x + uv.x * ctx.displayW;
        float py = ctx.imgMin.y + uv.y * ctx.displayH;
        ctx.drawList->AddCircleFilled(ImVec2(px, py), 5.0f, dot.color);
        ctx.drawList->AddCircle(ImVec2(px, py), 5.0f, IM_COL32(0, 0, 0, 200), 0, 1.5f);
        if (!dot.name.empty()) {
            ImVec2 mp = ImGui::GetMousePos();
            float dx = mp.x - px, dy = mp.y - py;
            if (dx * dx + dy * dy <= 49.0f) {
                ImGui::SetTooltip("%s", dot.name.c_str());
            }
            ImVec2 nameSz = font->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, dot.name.c_str());
            float tx = px - nameSz.x * 0.5f;
            float ty = py - nameSz.y - 7.0f;
            ctx.drawList->AddText(ImVec2(tx + 1.0f, ty + 1.0f), IM_COL32(0, 0, 0, 180), dot.name.c_str());
            ctx.drawList->AddText(ImVec2(tx, ty), IM_COL32(255, 255, 255, 220), dot.name.c_str());
        }
    }
}

} // namespace world_map
} // namespace rendering
} // namespace wowee
