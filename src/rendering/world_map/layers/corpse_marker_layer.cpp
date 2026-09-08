// corpse_marker_layer.cpp - Death corpse tombstone marker on the world map.
// Uses Rotating-MinimapCorpseArrow.blp from the game data.
#include "rendering/imgui_texture.hpp"
#include "rendering/world_map/layers/corpse_marker_layer.hpp"
#include "rendering/world_map/coordinate_projection.hpp"
#include "rendering/vk_texture.hpp"
#include "rendering/vk_context.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

namespace wowee {
namespace rendering {
namespace world_map {

CorpseMarkerLayer::~CorpseMarkerLayer() {
    clearTexture();
}

void CorpseMarkerLayer::initialize(VkContext* ctx, pipeline::AssetManager* am) {
    vkCtx_ = ctx;
    assetManager_ = am;
}

void CorpseMarkerLayer::clearTexture() {
    if (vkCtx_) marker_.destroy(vkCtx_->getDevice(), vkCtx_->getAllocator());
    loadAttempted_ = false;
}

void CorpseMarkerLayer::ensureTexture() {
    if (loadAttempted_ || !vkCtx_ || !assetManager_) return;
    loadAttempted_ = true;

    auto loaded = loadImGuiTexture(*assetManager_, *vkCtx_,
                                   "Interface\\Minimap\\Rotating-MinimapCorpseArrow.blp");
    if (!loaded) {
        LOG_WARNING("CorpseMarkerLayer: icon texture unavailable");
        return;
    }
    marker_ = std::move(loaded);
    LOG_INFO("CorpseMarkerLayer: loaded corpse icon ", marker_.texture->getWidth(), "x",
             marker_.texture->getHeight());
}

void CorpseMarkerLayer::render(const LayerContext& ctx) {
    if (!hasCorpse_ && !hasGraveyard_) return;
    const auto projection = currentProjection(ctx);
    if (!projection) return;

    // Where a release would put the player. Drawn first so the corpse sits on
    // top where the two coincide - the body is the thing being navigated to.
    if (hasGraveyard_) {
        glm::vec2 gv = renderPosToMapUV(graveyardRenderPos_, projection->bounds, projection->isContinent);
        if (gv.x >= 0.0f && gv.x <= 1.0f && gv.y >= 0.0f && gv.y <= 1.0f) {
            const float gx = ctx.imgMin.x + gv.x * ctx.displayW;
            const float gy = ctx.imgMin.y + gv.y * ctx.displayH;
            constexpr float H = 7.0f;      // half-height of the upright
            constexpr float W = 4.5f;      // half-width of the crossbar
            constexpr float T = 2.6f;
            const ImU32 halo = IM_COL32(0, 0, 0, 200);
            const ImU32 pale = IM_COL32(180, 215, 255, 245);
            // A cross, drawn with a dark pass under a pale one so it reads on
            // both the parchment and the darker continent art.
            for (int pass = 0; pass < 2; ++pass) {
                const ImU32 col = pass == 0 ? halo : pale;
                const float th = pass == 0 ? T + 1.6f : T;
                ctx.drawList->AddLine(ImVec2(gx, gy - H), ImVec2(gx, gy + H), col, th);
                ctx.drawList->AddLine(ImVec2(gx - W, gy - H * 0.25f),
                                      ImVec2(gx + W, gy - H * 0.25f), col, th);
            }
            ImVec2 gmp = ImGui::GetMousePos();
            const float gdx = gmp.x - gx, gdy = gmp.y - gy;
            if (gdx * gdx + gdy * gdy < (H + 2.0f) * (H + 2.0f)) {
                ImGui::SetTooltip("Spirit healer");
            }
        }
    }

    if (!hasCorpse_) return;

    glm::vec2 uv = renderPosToMapUV(corpseRenderPos_, projection->bounds, projection->isContinent);
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) return;

    float cx = ctx.imgMin.x + uv.x * ctx.displayW;
    float cy = ctx.imgMin.y + uv.y * ctx.displayH;

    ensureTexture();

    constexpr float ICON_HALF = 12.0f;

    if (marker_.descriptorSet) {
        ctx.drawList->AddImage(
            reinterpret_cast<ImTextureID>(marker_.descriptorSet),
            ImVec2(cx - ICON_HALF, cy - ICON_HALF),
            ImVec2(cx + ICON_HALF, cy + ICON_HALF),
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32_WHITE);
    } else {
        // Fallback: bone-white X if texture failed to load
        constexpr float R = 5.0f;
        constexpr float T = 1.8f;
        ctx.drawList->AddLine(ImVec2(cx - R, cy - R), ImVec2(cx + R, cy + R),
                              IM_COL32(0, 0, 0, 220), T + 1.5f);
        ctx.drawList->AddLine(ImVec2(cx + R, cy - R), ImVec2(cx - R, cy + R),
                              IM_COL32(0, 0, 0, 220), T + 1.5f);
        ctx.drawList->AddLine(ImVec2(cx - R, cy - R), ImVec2(cx + R, cy + R),
                              IM_COL32(230, 220, 200, 240), T);
        ctx.drawList->AddLine(ImVec2(cx + R, cy - R), ImVec2(cx - R, cy + R),
                              IM_COL32(230, 220, 200, 240), T);
    }

    // Tooltip on hover
    ImVec2 mp = ImGui::GetMousePos();
    float dx = mp.x - cx, dy = mp.y - cy;
    if (dx * dx + dy * dy < ICON_HALF * ICON_HALF) {
        ImGui::SetTooltip("Your corpse");
    }
}

} // namespace world_map
} // namespace rendering
} // namespace wowee
