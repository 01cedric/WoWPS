// rare_tracker_layer.cpp - Nearby rare / rare-elite creature markers on the world map.
// Only creatures the client currently has loaded (i.e. spawned and near the player) are
// fed in, so a marker appearing means that rare is out right now.
#include "rendering/world_map/layers/rare_tracker_layer.hpp"
#include "rendering/world_map/coordinate_projection.hpp"
#include <imgui.h>

namespace wowee {
namespace rendering {
namespace world_map {

void RareTrackerLayer::render(const LayerContext& ctx) {
    if (!rares_ || rares_->empty()) return;
    const auto projection = currentProjection(ctx);
    if (!projection) return;

    ImFont* font = ImGui::GetFont();
    for (const auto& rare : *rares_) {
        glm::vec2 uv = renderPosToMapUV(rare.renderPos, projection->bounds, projection->isContinent);
        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) continue;
        float px = ctx.imgMin.x + uv.x * ctx.displayW;
        float py = ctx.imgMin.y + uv.y * ctx.displayH;

        // Rare = gold, Rare Elite = silver (matching the in-game portrait dragon colors).
        const bool isElite = (rare.rank == 2);
        const ImU32 fill = isElite ? IM_COL32(210, 210, 225, 255) : IM_COL32(255, 190, 60, 255);

        // Diamond marker with a dark outline so it reads over any map art.
        constexpr float H = 6.0f;
        const ImVec2 top(px, py - H), right(px + H, py), bot(px, py + H), left(px - H, py);
        ctx.drawList->AddQuadFilled(top, right, bot, left, fill);
        ctx.drawList->AddQuad(top, right, bot, left, IM_COL32(0, 0, 0, 220), 1.5f);

        // Tooltip and name label on hover proximity.
        ImVec2 mp = ImGui::GetMousePos();
        float dx = mp.x - px, dy = mp.y - py;
        bool hovered = (dx * dx + dy * dy <= (H + 2.0f) * (H + 2.0f));
        if (hovered && !rare.name.empty()) {
            ImGui::SetTooltip("%s\n%s", rare.name.c_str(), isElite ? "Rare Elite" : "Rare");
        }
        if (!rare.name.empty()) {
            ImVec2 nameSz = font->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, rare.name.c_str());
            float tx = px - nameSz.x * 0.5f;
            float ty = py - nameSz.y - H - 3.0f;
            ctx.drawList->AddText(ImVec2(tx + 1.0f, ty + 1.0f), IM_COL32(0, 0, 0, 190), rare.name.c_str());
            ctx.drawList->AddText(ImVec2(tx, ty), fill, rare.name.c_str());
        }
    }
}

} // namespace world_map
} // namespace rendering
} // namespace wowee
