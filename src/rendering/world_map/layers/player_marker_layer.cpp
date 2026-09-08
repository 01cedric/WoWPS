// player_marker_layer.cpp - Directional player arrow on the world map.
// Uses the WoW worldmapplayericon.blp texture, rendered as a rotated quad.
#include "rendering/imgui_texture.hpp"
#include "rendering/world_map/layers/player_marker_layer.hpp"
#include "rendering/world_map/coordinate_projection.hpp"
#include "rendering/vk_texture.hpp"
#include "rendering/vk_context.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <cmath>
#include <algorithm>

namespace wowee {
namespace rendering {
namespace world_map {

PlayerMarkerLayer::~PlayerMarkerLayer() {
    clearTexture();
}

void PlayerMarkerLayer::initialize(VkContext* ctx, pipeline::AssetManager* am) {
    vkCtx_ = ctx;
    assetManager_ = am;
}

void PlayerMarkerLayer::clearTexture() {
    if (vkCtx_) marker_.destroy(vkCtx_->getDevice(), vkCtx_->getAllocator());
    loadAttempted_ = false;
}

void PlayerMarkerLayer::ensureTexture() {
    if (loadAttempted_ || !vkCtx_ || !assetManager_) return;
    loadAttempted_ = true;

    auto loaded = loadImGuiTexture(*assetManager_, *vkCtx_,
                                   "Interface\\Minimap\\MinimapArrow.blp");
    if (!loaded) {
        LOG_WARNING("PlayerMarkerLayer: icon texture unavailable");
        return;
    }
    marker_ = std::move(loaded);
    LOG_INFO("PlayerMarkerLayer: loaded MinimapArrow.blp ", marker_.texture->getWidth(), "x",
             marker_.texture->getHeight());
}

void PlayerMarkerLayer::render(const LayerContext& ctx) {
    const auto projection = currentProjection(ctx);
    if (!projection) return;

    // The one extra question this layer asks: on a continent, the player is
    // only drawn when they are actually somewhere on it. The marker layers
    // around it draw things whose position is already known to belong here.
    if (projection->isContinent) {
        const int playerZone =
            findZoneForPlayer(*ctx.zones, ctx.playerRenderPos, ctx.playerZoneId);
        if (playerZone < 0 || !zoneBelongsToContinent(*ctx.zones, playerZone, ctx.currentZoneIdx))
            return;
    }

    glm::vec2 playerUV = renderPosToMapUV(ctx.playerRenderPos, projection->bounds, projection->isContinent);
    if (playerUV.x < 0.0f || playerUV.x > 1.0f ||
        playerUV.y < 0.0f || playerUV.y > 1.0f) return;

    float px = ctx.imgMin.x + playerUV.x * ctx.displayW;
    float py = ctx.imgMin.y + playerUV.y * ctx.displayH;

    // WoW yaw: 0° = North (+X in WoW = +Y render), increases counter-clockwise.
    // Screen: +X = right, +Y = down. North on map = up = -Y screen.
    // The BLP arrow points up (north) at 0 rotation, so we rotate by -yaw.
    float yawRad = glm::radians(ctx.playerYawDeg);
    float cosA = std::cos(-yawRad);
    float sinA = std::sin(-yawRad);

    ensureTexture();

    if (marker_.descriptorSet) {
        constexpr float ARROW_HALF = 16.0f;

        // 4 corners of the unrotated quad (TL, TR, BR, BL)
        float cx[4] = { -ARROW_HALF,  ARROW_HALF,  ARROW_HALF, -ARROW_HALF };
        float cy[4] = { -ARROW_HALF, -ARROW_HALF,  ARROW_HALF,  ARROW_HALF };

        ImVec2 p[4];
        for (int i = 0; i < 4; i++) {
            p[i].x = px + cx[i] * cosA - cy[i] * sinA;
            p[i].y = py + cx[i] * sinA + cy[i] * cosA;
        }

        ctx.drawList->AddImageQuad(
            reinterpret_cast<ImTextureID>(marker_.descriptorSet),
            p[0], p[1], p[2], p[3],
            ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),
            IM_COL32_WHITE);
    } else {
        // Fallback: red triangle if texture failed to load
        float adx = -std::cos(yawRad);
        float ady = -std::sin(yawRad);
        float apx_ = -ady, apy_ = adx;
        constexpr float TIP  = 9.0f;
        constexpr float TAIL = 4.0f;
        constexpr float FHALF = 5.0f;
        ImVec2 tip(px + adx * TIP,  py + ady * TIP);
        ImVec2 bl (px - adx * TAIL + apx_ * FHALF,  py - ady * TAIL + apy_ * FHALF);
        ImVec2 br (px - adx * TAIL - apx_ * FHALF,  py - ady * TAIL - apy_ * FHALF);
        ctx.drawList->AddTriangleFilled(tip, bl, br, IM_COL32(255, 40, 40, 255));
        ctx.drawList->AddTriangle(tip, bl, br, IM_COL32(0, 0, 0, 200), 1.5f);
    }
}

} // namespace world_map
} // namespace rendering
} // namespace wowee
