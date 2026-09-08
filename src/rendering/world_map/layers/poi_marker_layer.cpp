// poi_marker_layer.cpp - Town/dungeon/capital POI icons on the world map.
// Extracted from WorldMap::renderPOIMarkers (Phase 8 of refactoring plan).
#include "rendering/world_map/layers/poi_marker_layer.hpp"
#include "rendering/world_map/coordinate_projection.hpp"
#include "core/coordinates.hpp"
#include <imgui.h>

namespace wowee {
namespace rendering {
namespace world_map {

void POIMarkerLayer::render(const LayerContext& ctx) {
    if (!markers_ || markers_->empty()) return;
    const auto projection = currentProjection(ctx);
    if (!projection) return;

    ImVec2 mp = ImGui::GetMousePos();
    ImFont* font = ImGui::GetFont();

    for (const auto& poi : *markers_) {
        if (static_cast<int>(poi.mapId) != ctx.currentMapId) continue;

        glm::vec3 rPos = core::coords::canonicalToRender(
            glm::vec3(poi.wowX, poi.wowY, poi.wowZ));
        glm::vec2 uv = renderPosToMapUV(rPos, projection->bounds, projection->isContinent);
        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) continue;

        float px = ctx.imgMin.x + uv.x * ctx.displayW;
        float py = ctx.imgMin.y + uv.y * ctx.displayH;

        float iconSize = (poi.importance >= 2) ? 7.0f :
                         (poi.importance >= 1) ? 5.0f : 3.0f;

        ImU32 fillColor, borderColor;
        if (poi.factionId == 469) {
            fillColor = IM_COL32(60, 120, 255, 200);
            borderColor = IM_COL32(20, 60, 180, 220);
        } else if (poi.factionId == 67) {
            fillColor = IM_COL32(255, 60, 60, 200);
            borderColor = IM_COL32(180, 20, 20, 220);
        } else {
            fillColor = IM_COL32(255, 215, 0, 200);
            borderColor = IM_COL32(180, 150, 0, 220);
        }

        if (poi.importance >= 2) {
            ctx.drawList->AddCircleFilled(ImVec2(px, py), iconSize + 2.0f,
                                           IM_COL32(255, 255, 200, 30));
            ctx.drawList->AddCircleFilled(ImVec2(px, py), iconSize, fillColor);
            ctx.drawList->AddCircle(ImVec2(px, py), iconSize, borderColor, 0, 2.0f);
        } else if (poi.importance >= 1) {
            float H = iconSize;
            ImVec2 top2(px,     py - H);
            ImVec2 right2(px + H, py    );
            ImVec2 bot2(px,     py + H);
            ImVec2 left2(px - H, py    );
            ctx.drawList->AddQuadFilled(top2, right2, bot2, left2, fillColor);
            ctx.drawList->AddQuad(top2, right2, bot2, left2, borderColor, 1.2f);
        } else {
            ctx.drawList->AddCircleFilled(ImVec2(px, py), iconSize, fillColor);
            ctx.drawList->AddCircle(ImVec2(px, py), iconSize, borderColor, 0, 1.0f);
        }

        if (poi.importance >= 1 && ctx.viewLevel == ViewLevel::ZONE && !poi.name.empty()) {
            float fontSize = (poi.importance >= 2) ? ImGui::GetFontSize() * 0.85f :
                             ImGui::GetFontSize() * 0.75f;
            ImVec2 nameSz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, poi.name.c_str());
            float tx = px - nameSz.x * 0.5f;
            float ty = py + iconSize + 2.0f;
            ctx.drawList->AddText(font, fontSize,
                                  ImVec2(tx + 1.0f, ty + 1.0f),
                                  IM_COL32(0, 0, 0, 180), poi.name.c_str());
            ctx.drawList->AddText(font, fontSize,
                                  ImVec2(tx, ty), IM_COL32(255, 255, 255, 210),
                                  poi.name.c_str());
        }

        float dx = mp.x - px, dy = mp.y - py;
        float hitRadius = iconSize + 4.0f;
        if (dx * dx + dy * dy < hitRadius * hitRadius && !poi.name.empty()) {
            if (!poi.description.empty())
                ImGui::SetTooltip("%s\n%s", poi.name.c_str(), poi.description.c_str());
            else
                ImGui::SetTooltip("%s", poi.name.c_str());
        }
    }
}

} // namespace world_map
} // namespace rendering
} // namespace wowee
