#include "rendering/sky_system.hpp"
#include "rendering/skybox.hpp"
#include "rendering/celestial.hpp"
#include "rendering/starfield.hpp"
#include "rendering/clouds.hpp"
#include "rendering/lens_flare.hpp"
#include "rendering/camera.hpp"
#include "rendering/vk_context.hpp"
#include "core/logger.hpp"

namespace wowee {
namespace rendering {

SkySystem::SkySystem() = default;

SkySystem::~SkySystem() {
    shutdown();
}

bool SkySystem::initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout) {
    if (initialized_) {
        LOG_WARNING("SkySystem already initialized");
        return true;
    }

    LOG_INFO("Initializing sky system");

    // Skybox (Vulkan)
    skybox_ = std::make_unique<Skybox>();
    if (!skybox_->initialize(ctx, perFrameLayout)) {
        LOG_ERROR("Failed to initialize skybox");
        return false;
    }

    // Celestial bodies - sun + 2 moons (Vulkan)
    celestial_ = std::make_unique<Celestial>();
    if (!celestial_->initialize(ctx, perFrameLayout)) {
        LOG_ERROR("Failed to initialize celestial bodies");
        return false;
    }

    // Procedural stars - fallback / debug (Vulkan)
    starField_ = std::make_unique<StarField>();
    if (!starField_->initialize(ctx, perFrameLayout)) {
        LOG_ERROR("Failed to initialize star field");
        return false;
    }
    starField_->setEnabled(false); // Off by default; skybox is authoritative

    // Clouds (Vulkan)
    clouds_ = std::make_unique<Clouds>();
    if (!clouds_->initialize(ctx, perFrameLayout)) {
        LOG_ERROR("Failed to initialize clouds");
        return false;
    }

    // Lens flare (Vulkan)
    lensFlare_ = std::make_unique<LensFlare>();
    if (!lensFlare_->initialize(ctx, perFrameLayout)) {
        LOG_ERROR("Failed to initialize lens flare");
        return false;
    }

    initialized_ = true;
    LOG_INFO("Sky system initialized successfully");
    return true;
}

void SkySystem::shutdown() {
    if (!initialized_) {
        return;
    }

    LOG_INFO("Shutting down sky system");

    if (lensFlare_)  lensFlare_->shutdown();
    if (clouds_)     clouds_->shutdown();
    if (starField_)  starField_->shutdown();
    if (celestial_)  celestial_->shutdown();
    if (skybox_)     skybox_->shutdown();

    lensFlare_.reset();
    clouds_.reset();
    starField_.reset();
    celestial_.reset();
    skybox_.reset();

    initialized_ = false;
}

void SkySystem::update(float deltaTime) {
    if (!initialized_) {
        return;
    }

    if (skybox_)    skybox_->update(deltaTime);
    if (celestial_) celestial_->update(deltaTime);
    if (starField_) starField_->update(deltaTime);
    if (clouds_)    clouds_->update(deltaTime);
}

void SkySystem::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet,
                        const Camera& camera, const SkyParams& params) {
    if (!initialized_) {
        return;
    }

    // --- Skybox (authoritative sky gradient, DBC-driven colors) ---
    if (skybox_) {
        skybox_->render(cmd, perFrameSet, params);
    }

    // Original client sky M2s supply their own celestial bodies, clouds, and
    // nebula layers. The caller draws that model over the gradient underlay.

    // --- Procedural stars ---
    // Not only a fallback any more: with sharp stars on, the sky model keeps
    // its clouds and planets while M2Renderer drops its star layer, and these
    // are drawn in that layer's place. So the original-skybox path continues
    // here rather than returning, and skyboxHasStars stops meaning "do not".
    bool renderProceduralStars = false;
    if (debugSkyMode_) {
        renderProceduralStars = true;
    } else if (proceduralStarsEnabled_) {
        renderProceduralStars = true;
    } else if (!params.useOriginalSkybox) {
        renderProceduralStars = !params.skyboxHasStars;
    }

    if (params.useOriginalSkybox && !params.originalSkyboxAllowsAtmosphere && !renderProceduralStars) {
        if (starField_) starField_->setEnabled(false);
        return;
    }

    if (starField_) {
        starField_->setEnabled(renderProceduralStars);
        if (renderProceduralStars) {
            const float cloudDensity = params.cloudDensity;
            const float fogDensity   = params.fogDensity;
            starField_->render(cmd, perFrameSet, params.timeOfDay, cloudDensity, fogDensity);
        }
    }

    if (params.useOriginalSkybox && !params.originalSkyboxAllowsAtmosphere) return;

    // --- Celestial bodies (sun + White Lady + Blue Child) ---
    if (celestial_) {
        // Gate moon visibility on how dark the DBC sky actually is. The
        // hardcoded 19:00 night window can precede sky darkening by hours,
        // and full-brightness moons on a daylight sky read as extra suns.
        float skyLum = glm::dot(params.skyTopColor, glm::vec3(0.2126f, 0.7152f, 0.0722f));
        float nightFactor = 1.0f - glm::smoothstep(0.08f, 0.25f, skyLum);
        celestial_->render(cmd, perFrameSet, params.timeOfDay,
                           &params.directionalDir, &params.sunColor, params.gameTime,
                           nightFactor);
    }

    // --- Clouds (DBC-driven colors + sun lighting) ---
    if (clouds_) {
        // Sync cloud density with weather/DBC-driven cloud coverage.
        // Active weather (rain/snow/storm) increases cloud density for visual consistency.
        float effectiveDensity = params.cloudDensity;
        if (params.weatherIntensity > 0.05f) {
            float weatherBoost = params.weatherIntensity * 0.4f;  // storms add up to 0.4 density
            effectiveDensity = glm::min(1.0f, effectiveDensity + weatherBoost);
        }
        clouds_->setDensity(effectiveDensity);
        clouds_->render(cmd, perFrameSet, params);
    }

    // --- Lens flare (attenuated by atmosphere) ---
    if (lensFlare_) {
        glm::vec3 sunPos = getSunPosition(params);
        lensFlare_->render(cmd, camera, sunPos, params.timeOfDay,
                           params.fogDensity, params.cloudDensity,
                           params.weatherIntensity);
    }
}

glm::vec3 SkySystem::getSunPosition(const SkyParams& params) const {
    float dirLenSq = glm::dot(params.directionalDir, params.directionalDir);
    glm::vec3 dir = (dirLenSq > 1e-8f) ? params.directionalDir * glm::inversesqrt(dirLenSq) : glm::vec3(0.0f);
    if (dirLenSq < 1e-8f) {
        dir = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    glm::vec3 sunDir = -dir;
    if (sunDir.z < 0.0f) {
        sunDir = dir;
    }
    return sunDir * 800.0f;
}

void SkySystem::setMoonPhaseCycling(bool enabled) {
    if (celestial_) celestial_->setMoonPhaseCycling(enabled);
}
void SkySystem::setBlueChildPhase(float phase) {
    if (celestial_) celestial_->setBlueChildPhase(phase);
}
float SkySystem::getBlueChildPhase() const {
    return celestial_ ? celestial_->getBlueChildPhase() : 0.25f;
}

} // namespace rendering
} // namespace wowee
