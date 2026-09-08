#include "core/future_wait_guard.hpp"
#include "rendering/shadow_instances.hpp"
#include "rendering/shadow_params.hpp"
#include "rendering/m2_renderer.hpp"
#include "rendering/m2_renderer_internal.h"
#include "rendering/m2_blend_mode.hpp"
#include "rendering/m2_glow_card.hpp"
#include "rendering/m2_texture_transform.hpp"
#include "core/thread_pool.hpp"
#include "core/memory_monitor.hpp"
#ifdef WOWEE_PS4
#include "platform/ps4/ps4_platform.hpp"
#endif
#include "rendering/m2_model_classifier.hpp"
#include "rendering/hiz_system.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_buffer.hpp"
#include "rendering/vk_texture.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/vk_utils.hpp"
#include "rendering/vk_frame_data.hpp"
#include "rendering/camera.hpp"
#include "rendering/frustum.hpp"
#include "rendering/render_constants.hpp"
#include "rendering/m2_view_distance.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/blp_loader.hpp"
#include "core/logger.hpp"
#include "core/profiler.hpp"
#include <chrono>
#include <cctype>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <unordered_set>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>
#include <limits>
#include <future>
#include <thread>
#include <type_traits>

namespace wowee {
namespace rendering {

/// Starts a new instance's animation and gives it bones to draw with now.
///
/// Both spawn paths need this: the one that takes a position and the one that
/// takes a whole matrix, and each had its own copy.
///
/// The bone seed is what keeps a new instance from being invisible for a
/// frame. Bones are computed in update(), so an instance spawned mid-frame has
/// none until the next one; copying them from a sibling of the same model
/// draws it immediately. A seed entry pointing at an instance that has since
/// gone is dropped rather than followed.
void M2Renderer::seedInstanceAnimation(const M2ModelGPU& model, uint32_t modelId,
                                       M2Instance& instance) {
        if (!model.sequences.empty()) {
            instance.currentSequenceIndex = 0;
            instance.idleSequenceIndex = 0;
            instance.animDuration = static_cast<float>(model.sequences[0].duration);
            instance.animTime = static_cast<float>(randRange(std::max(1u, model.sequences[0].duration)));
            instance.variationTimer = randFloat(rendering::M2_VARIATION_TIMER_MIN_MS, rendering::M2_VARIATION_TIMER_MAX_MS);
        }

    auto seedIt = boneSeedInstanceByModel_.find(modelId);
    if (seedIt != boneSeedInstanceByModel_.end()) {
        auto idxIt = instanceIndexById.find(seedIt->second);
        if (idxIt != instanceIndexById.end() && idxIt->second < instances.size()) {
            const auto& existing = instances[idxIt->second];
            if (existing.modelId == modelId && !existing.boneMatrices.empty()) {
                instance.boneMatrices = existing.boneMatrices;
                instance.bonesDirty[0] = instance.bonesDirty[1] = true;
            } else {
                boneSeedInstanceByModel_.erase(seedIt);  // stale entry
            }
        } else {
            boneSeedInstanceByModel_.erase(seedIt);  // that instance is gone
        }
    }

    // No sibling to copy from, so pay for the bones now.
    if (instance.boneMatrices.empty()) {
        computeBoneMatrices(model, instance, &cachedCamPos_);
    }
    if (!instance.boneMatrices.empty()) {
        boneSeedInstanceByModel_.emplace(modelId, instance.id);
    }
}

uint32_t M2Renderer::commitInstance(M2Instance&& instance) {
    static_assert(std::is_nothrow_move_constructible_v<M2Instance>);
    const uint32_t id = instance.id;
    const size_t idx = instances.size();
    const bool smoke = instance.cachedIsSmoke;
    const bool portal = instance.cachedIsInstancePortal;
    const bool particles = instance.cachedHasParticleEmitters;
    const bool animated = instance.cachedHasAnimation && !instance.cachedDisableAnimation;
    const DedupKey key{.modelId = instance.modelId,
        .qx = static_cast<int32_t>(std::round(instance.position.x * 10.0f)),
        .qy = static_cast<int32_t>(std::round(instance.position.y * 10.0f)),
        .qz = static_cast<int32_t>(std::round(instance.position.z * 10.0f))};
    const auto room = [](auto& v) {
        if (v.size() == v.capacity()) v.reserve(std::max<size_t>(8, v.size() * 2));
    };
    try {
        room(instances);
        if (smoke) room(smokeInstanceIndices_);
        if (portal) room(portalInstanceIndices_);
        if (particles) room(particleInstanceIndices_);
        if (animated) room(animatedInstanceIndices_);
        else if (particles) room(particleOnlyInstanceIndices_);
        // insertBounds may have filed only part of a large box on OOM. Roll
        // that partial insertion back before allowing a retry of this child.
        insertBounds(spatialGrid, instance.worldBoundsMin, instance.worldBoundsMax, id);
        instanceIndexById.emplace(id, idx);
        if (instance.participatesInPositionDedup) instanceDedupMap_.emplace(key, id);
        // Capacity is ready and M2Instance moves without allocating.
        instances.push_back(std::move(instance));
    } catch (...) {
        eraseBounds(spatialGrid, instance.worldBoundsMin, instance.worldBoundsMax, id);
        instanceIndexById.erase(id);
        auto dedup = instanceDedupMap_.find(key);
        if (dedup != instanceDedupMap_.end() && dedup->second == id) instanceDedupMap_.erase(dedup);
        auto seed = boneSeedInstanceByModel_.find(instance.modelId);
        if (seed != boneSeedInstanceByModel_.end() && seed->second == id) boneSeedInstanceByModel_.erase(seed);
        throw;
    }
    if (smoke) smokeInstanceIndices_.push_back(idx);
    if (portal) portalInstanceIndices_.push_back(idx);
    if (particles) particleInstanceIndices_.push_back(idx);
    if (animated) animatedInstanceIndices_.push_back(idx);
    else if (particles) particleOnlyInstanceIndices_.push_back(idx);
    return id;
}

uint32_t M2Renderer::createInstance(uint32_t modelId, const glm::vec3& position,
                                     const glm::vec3& rotation, float scale,
                                     bool allowPositionDedup) {
    // Reject NaN inputs at the boundary - std::round of NaN is implementation-
    // defined and a NaN instance position propagates into the GPU model matrix,
    // either tripping Vulkan validation or rendering at the world origin.
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z) || !std::isfinite(rotation.x) ||
        !std::isfinite(rotation.y) || !std::isfinite(rotation.z) ||
        !std::isfinite(scale) || scale <= 0.0f) {
        return 0;
    }
    auto modelIt = models.find(modelId);
    if (modelIt == models.end()) {
        LOG_WARNING("Cannot create instance: model ", modelId, " not loaded");
        return 0;
    }
    const auto& mdlRef = modelIt->second;
    modelUnusedSince_.erase(modelId);


    // Deduplicate: skip if same model already at nearly the same position.
    // Uses hash map for O(1) lookup instead of O(N) scan.
    // Spell effects are exempt - transient visuals must always create fresh instances.
    if (allowPositionDedup && !mdlRef.isGroundDetail && !mdlRef.isSpellEffect) {
        DedupKey dk{.modelId = modelId,
                    .qx = static_cast<int32_t>(std::round(position.x * 10.0f)),
                    .qy = static_cast<int32_t>(std::round(position.y * 10.0f)),
                    .qz = static_cast<int32_t>(std::round(position.z * 10.0f))};
        auto dit = instanceDedupMap_.find(dk);
        if (dit != instanceDedupMap_.end()) {
            return dit->second;
        }
    }

    M2Instance instance;
    instance.id = nextInstanceId++;
    instance.modelId = modelId;
    instance.position = position;
    if (mdlRef.isGroundDetail) {
        instance.position.z -= computeGroundDetailDownOffset(mdlRef, scale);
    }
    instance.rotation = rotation;
    instance.scale = scale;
    instance.updateModelMatrix();
    glm::vec3 localMin, localMax;
    getTightCollisionBounds(mdlRef, localMin, localMax);
    transformAABB(instance.modelMatrix, localMin, localMax, instance.worldBoundsMin, instance.worldBoundsMax);

    // Cache model flags on instance to avoid per-frame hash lookups
    instance.cachedHasAnimation = mdlRef.hasAnimation;
    instance.cachedDisableAnimation = mdlRef.disableAnimation;
    instance.cachedIsSmoke = mdlRef.isSmoke;
    instance.cachedHasParticleEmitters = !mdlRef.particleEmitters.empty();
    instance.cachedBoundRadius = mdlRef.boundRadius;
    instance.cachedIsGroundDetail = mdlRef.isGroundDetail;
    instance.participatesInPositionDedup = allowPositionDedup && !mdlRef.isGroundDetail && !mdlRef.isSpellEffect;
    instance.cachedIsInvisibleTrap = mdlRef.isInvisibleTrap;
    instance.cachedIsInstancePortal = mdlRef.isInstancePortal;
    instance.cachedIsSkyBird = mdlRef.isSkyBird;
    instance.cachedIsLightBeam = mdlRef.isLightBeam;
    instance.cachedIsTransportDoodad = mdlRef.isTransportDoodad;
    instance.cachedIsValid = mdlRef.isValid();
    instance.cachedModel = &mdlRef;
    instance.recomputeCachedCullFactors();

    // Initialize animation: play first sequence (usually Stand/Idle)
    const auto& mdl = mdlRef;
    if (mdl.hasAnimation && !mdl.disableAnimation) {
        seedInstanceAnimation(mdlRef, modelId, instance);
    }

    return commitInstance(std::move(instance));
}

uint32_t M2Renderer::createInstanceWithMatrix(uint32_t modelId, const glm::mat4& modelMatrix,
                                                const glm::vec3& position) {
    // Reject NaN inputs at the boundary. position feeds the dedup hash
    // (std::round of NaN is implementation-defined); the matrix goes
    // straight to the GPU UBO and would crash validation.
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z)) {
        return 0;
    }
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            if (!std::isfinite(modelMatrix[c][r])) return 0;
    if (models.find(modelId) == models.end()) {
        LOG_WARNING("Cannot create instance: model ", modelId, " not loaded");
        return 0;
    }
    modelUnusedSince_.erase(modelId);

    // Deduplicate: O(1) hash lookup
    {
        DedupKey dk{.modelId = modelId,
                    .qx = static_cast<int32_t>(std::round(position.x * 10.0f)),
                    .qy = static_cast<int32_t>(std::round(position.y * 10.0f)),
                    .qz = static_cast<int32_t>(std::round(position.z * 10.0f))};
        auto dit = instanceDedupMap_.find(dk);
        if (dit != instanceDedupMap_.end()) {
            return dit->second;
        }
    }

    M2Instance instance;
    instance.id = nextInstanceId++;
    instance.modelId = modelId;
    instance.position = position;  // Used for frustum culling
    instance.rotation = glm::vec3(0.0f);
    instance.scale = 1.0f;
    instance.modelMatrix = modelMatrix;
    instance.invModelMatrix = glm::inverse(modelMatrix);
    glm::vec3 localMin, localMax;
    getTightCollisionBounds(models[modelId], localMin, localMax);
    transformAABB(instance.modelMatrix, localMin, localMax, instance.worldBoundsMin, instance.worldBoundsMax);
    // Cache model flags on instance to avoid per-frame hash lookups
    const auto& mdl2 = models[modelId];
    instance.cachedHasAnimation = mdl2.hasAnimation;
    instance.cachedDisableAnimation = mdl2.disableAnimation;
    instance.cachedIsSmoke = mdl2.isSmoke;
    instance.cachedHasParticleEmitters = !mdl2.particleEmitters.empty();
    instance.cachedBoundRadius = mdl2.boundRadius;
    instance.cachedIsGroundDetail = mdl2.isGroundDetail;
    instance.cachedIsInstancePortal = mdl2.isInstancePortal;
    instance.cachedIsInvisibleTrap = mdl2.isInvisibleTrap;
    instance.cachedIsSkyBird = mdl2.isSkyBird;
    instance.cachedIsLightBeam = mdl2.isLightBeam;
    instance.cachedIsTransportDoodad = mdl2.isTransportDoodad;
    instance.cachedIsValid = mdl2.isValid();
    instance.cachedModel = &mdl2;
    instance.recomputeCachedCullFactors();

    // Initialize animation
    if (mdl2.hasAnimation && !mdl2.disableAnimation) {
        seedInstanceAnimation(mdl2, modelId, instance);
    } else {
        // A model with no skeleton can still have particle emitters, and their
        // rate and lifespan tracks are sampled at animTime. Starting every
        // instance at zero puts a courtyard of identical torches in lockstep,
        // so the phase is spread.
        //
        // createInstance above does not do this, so doodads spawned by
        // position keep the lockstep this avoids. Which of the two is right is
        // a question for whoever next looks at particle timing; they differ
        // today and this is the difference.
        instance.animTime = randFloat(0.0f, 10000.0f);
    }

    return commitInstance(std::move(instance));
}

// WOWEE_SKY_M2_MAX_BATCH=<n> draws only the sky model's first n layers.
//
// Thirty-four of them, and every measurement says the set drawn is the same
// every frame - so if the flicker is one layer's doing, bisecting the count
// finds it in about five runs. That has worked three times on this fault where
// reading the code has worked none.
static bool skyBatchAllowed(bool skyMode, std::size_t index) {
    if (!skyMode) return true;
    static const int maxBatch = [] {
        const char* set = std::getenv("WOWEE_SKY_M2_MAX_BATCH");
        return set ? std::atoi(set) : -1;
    }();
    return maxBatch < 0 || static_cast<int>(index) < maxBatch;
}

void M2Renderer::update(float deltaTime, const glm::vec3& cameraPos, const glm::mat4& viewProjection) {
    ZoneScopedN("M2Renderer::update");
    if (spatialIndexDirty_) {
        rebuildSpatialIndex();
    }

    float dtMs = deltaTime * 1000.0f;

    // Cache camera state for frustum-culling bone computation
    cachedCamPos_ = cameraPos;
    // Never past the ground. The density constants are how far models are
    // worth drawing, not how far there is anything to draw them on: the
    // terrain and the WMOs stop at the view distance itself, so a doodad
    // beyond it is a tree standing on nothing.
#ifdef WOWEE_PS4
    const float maxRenderDistance = rendering::m2StableRenderDistance(
        viewDistanceAbsolute_, environmentDetail_);
#else
    const float maxRenderDistance = std::min(
        viewDistanceAbsolute_,
        viewDistanceScale_ *
            ((instances.size() > rendering::M2_HIGH_DENSITY_INSTANCE_THRESHOLD)
                 ? rendering::M2_MAX_RENDER_DISTANCE_HIGH_DENSITY
                 : rendering::M2_MAX_RENDER_DISTANCE_LOW_DENSITY));
#endif
    cachedMaxRenderDistSq_ = maxRenderDistance * maxRenderDistance;

    // Build frustum for culling bones
    Frustum updateFrustum;
    updateFrustum.extractFromMatrix(viewProjection);

    // --- Smoke particle spawning (only iterate tracked smoke instances) ---
    std::uniform_real_distribution<float> distXY(rendering::SMOKE_OFFSET_XY_MIN, rendering::SMOKE_OFFSET_XY_MAX);
    std::uniform_real_distribution<float> distVelXY(-0.3f, 0.3f);
    std::uniform_real_distribution<float> distVelZ(rendering::SMOKE_VEL_Z_MIN, rendering::SMOKE_VEL_Z_MAX);
    std::uniform_real_distribution<float> distLife(rendering::SMOKE_LIFETIME_MIN, rendering::SMOKE_LIFETIME_MAX);
    std::uniform_real_distribution<float> distDrift(-0.2f, 0.2f);

    smokeEmitAccum += deltaTime;
    constexpr float emitInterval = kSmokeEmitInterval;  // 48 particles per second per emitter

    if (smokeEmitAccum >= emitInterval &&
        static_cast<int>(smokeParticles.size()) < MAX_SMOKE_PARTICLES) {
        for (size_t si : smokeInstanceIndices_) {
            if (si >= instances.size()) continue;
            auto& instance = instances[si];

            glm::vec3 emitWorld = glm::vec3(instance.modelMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            bool spark = (smokeRng() % rendering::SPARK_PROBABILITY_DENOM == 0);

            SmokeParticle p;
            p.position = emitWorld + glm::vec3(distXY(smokeRng), distXY(smokeRng), 0.0f);
            if (spark) {
                p.velocity = glm::vec3(distVelXY(smokeRng) * 2.0f, distVelXY(smokeRng) * 2.0f, distVelZ(smokeRng) * 1.5f);
                p.maxLife = rendering::SPARK_LIFE_BASE + static_cast<float>(smokeRng() % 100) / 100.0f * rendering::SPARK_LIFE_RANGE;
                p.size = 0.5f;
                p.isSpark = 1.0f;
            } else {
                p.velocity = glm::vec3(distVelXY(smokeRng), distVelXY(smokeRng), distVelZ(smokeRng));
                p.maxLife = distLife(smokeRng);
                p.size = 1.0f;
                p.isSpark = 0.0f;
            }
            p.life = 0.0f;
            p.instanceId = instance.id;
            smokeParticles.push_back(p);
            if (static_cast<int>(smokeParticles.size()) >= MAX_SMOKE_PARTICLES) break;
        }
        smokeEmitAccum = 0.0f;
    }

    // --- Update existing smoke particles (swap-and-pop for O(1) removal) ---
    for (size_t i = 0; i < smokeParticles.size(); ) {
        auto& p = smokeParticles[i];
        p.life += deltaTime;
        if (p.life >= p.maxLife) {
            smokeParticles[i] = smokeParticles.back();
            smokeParticles.pop_back();
            continue;
        }
        p.position += p.velocity * deltaTime;
        p.velocity.z *= rendering::SMOKE_Z_VEL_DAMPING;  // Slight deceleration
        p.velocity.x += distDrift(smokeRng) * deltaTime;
        p.velocity.y += distDrift(smokeRng) * deltaTime;
        // Grow from 1.0 to 3.5 over lifetime
        float t = p.life / p.maxLife;
        p.size = rendering::SMOKE_SIZE_START + t * rendering::SMOKE_SIZE_GROWTH;
        ++i;
    }

    // Instance portals are not spun here.
    //
    // A yaw of 1.2 rad/s was written into inst.rotation.z every frame, which
    // does two things once the swirl is actually drawn rather than replaced by
    // a glow card. It overwrites the placement yaw the ADT gave the doodad, so
    // the portal no longer faces out of its doorway; and a portal is a flat
    // vertical disc, so turning it about the world's up axis presents it
    // edge-on twice a revolution - it would vanish and reappear twice a second.
    // While the mesh was substituted away neither was visible, which is how it
    // survived.
    //
    // The animation the portal should have is its own: InstancePortal-style
    // models carry a looping texture animation and particle and ribbon
    // emitters, all of which are already driven from animTime below.

    // --- Normal M2 animation update ---
    // Advance animTime for ALL instances (needed for texture UV animation on static doodads).
    // This is a tight loop touching only one float per instance - no hash lookups.
    for (auto& instance : instances) {
        instance.animTime += dtMs;
        instance.globalSequenceTime += dtMs;
    }

    // The sky model's clock, when this is the renderer that draws one.
    //
    // A report of the sky "playing an animation that brightens and dims, faster
    // when walking" survived three fixes to what chooses the sky, and the
    // lighting diagnostic then showed every input to it holding still while it
    // happened - the zone, the volumes, the model and the target colour. So
    // what is moving is this, and the two things worth telling apart are
    // whether the instance is being rebuilt (animTime back to zero) and
    // whether the clock runs at wall speed (animTime should advance by dtMs and
    // by nothing else).
    //
    // Only this renderer has skyMode_, and only on a change worth seeing, so it
    // is quiet unless asked for with WOWEE_LOG_LEVEL=info.
    if (skyMode_ && !instances.empty()) {
        const auto& sky = instances.front();
        const bool restarted = sky.animTime < skyDiagAnimTime_;
        if (restarted || sky.id != skyDiagInstanceId_ ||
            sky.animTime - skyDiagAnimTime_ > 1000.0f) {
            LOG_INFO("skyM2: instance=", sky.id, " instances=", instances.size(),
                     " animTime=", sky.animTime, " duration=", sky.animDuration,
                     " gsTime=", sky.globalSequenceTime, " dtMs=", dtMs,
                     restarted ? " RESTARTED" : "");
            skyDiagInstanceId_ = sky.id;
            skyDiagAnimTime_ = sky.animTime;
        }
    }
    // Wrap animTime for particle-only instances so emission rate tracks keep looping.
    // 3333ms chosen as a safe wrap period: long enough to cover the longest known M2
    // particle emission cycle (~3s for torch/campfire effects) while preventing float
    // precision loss that accumulates over hours of runtime.
    static constexpr float kParticleWrapMs = 3333.0f;
    for (size_t idx : particleOnlyInstanceIndices_) {
        if (idx >= instances.size()) continue;
        auto& instance = instances[idx];
        // Use iterative subtraction instead of fmod() to preserve precision
        while (instance.animTime > kParticleWrapMs) {
            instance.animTime -= kParticleWrapMs;
        }
    }

    boneWorkIndices_.clear();
    boneWorkIndices_.reserve(animatedInstanceIndices_.size());

    // Update animated instances (full animation state + bone computation culling)
    // Note: animTime was already advanced by dtMs in the global loop above.
    // Here we apply the speed factor: subtract the base dtMs and add dtMs*speed.
    // Ground clutter stops being stepped once it is past the distance it draws
    // at. There are hundreds of tufts to a tile and each one plays a sequence of
    // its own, so this list is mostly grass that nothing can see; the sequences
    // loop, so one that resumes from a stale time is indistinguishable from one
    // that never stopped.
    const float clutterAnimCutoffSq = (groundDetailMaxDistance_ > 0.0f)
        ? (groundDetailMaxDistance_ * groundDetailMaxDistance_) : 0.0f;

    for (size_t idx : animatedInstanceIndices_) {
        if (idx >= instances.size()) continue;
        auto& instance = instances[idx];

        if (clutterAnimCutoffSq > 0.0f && instance.cachedIsGroundDetail) {
            const glm::vec3 toCam = instance.position - cachedCamPos_;
            if (glm::dot(toCam, toCam) > clutterAnimCutoffSq) continue;
        }

        instance.animTime += dtMs * (instance.animSpeed - 1.0f);

        // For animation looping/variation, we need the actual model data.
        if (!instance.cachedModel) continue;
        const M2ModelGPU& model = *instance.cachedModel;

        // Validate sequence index
        if (instance.currentSequenceIndex < 0 ||
            instance.currentSequenceIndex >= static_cast<int>(model.sequences.size())) {
            instance.currentSequenceIndex = 0;
            if (!model.sequences.empty()) {
                instance.animDuration = static_cast<float>(model.sequences[0].duration);
            }
        }

        // Handle animation looping / variation transitions
        if (instance.animDuration <= 0.0f && instance.cachedHasParticleEmitters) {
            instance.animDuration = rendering::M2_DEFAULT_PARTICLE_ANIM_MS;
        }
        if (instance.animDuration > 0.0f && instance.animTime >= instance.animDuration) {
            if (instance.playingVariation) {
                instance.playingVariation = false;
                instance.currentSequenceIndex = instance.idleSequenceIndex;
                if (instance.idleSequenceIndex < static_cast<int>(model.sequences.size())) {
                    instance.animDuration = static_cast<float>(model.sequences[instance.idleSequenceIndex].duration);
                }
                instance.animTime = 0.0f;
                instance.variationTimer = randFloat(rendering::M2_LOOP_VARIATION_TIMER_MIN_MS, rendering::M2_LOOP_VARIATION_TIMER_MAX_MS);
            } else {
                // Use iterative subtraction instead of fmod() to preserve precision
                float duration = std::max(1.0f, instance.animDuration);
                while (instance.animTime >= duration) {
                    instance.animTime -= duration;
                }
            }
        }

        // Idle variation timer
        if (!instance.playingVariation && model.idleVariationIndices.size() > 1) {
            instance.variationTimer -= dtMs;
            if (instance.variationTimer <= 0.0f) {
                int pick = static_cast<int>(randRange(static_cast<uint32_t>(model.idleVariationIndices.size())));
                int newSeq = model.idleVariationIndices[pick];
                if (newSeq != instance.currentSequenceIndex && newSeq < static_cast<int>(model.sequences.size())) {
                    instance.playingVariation = true;
                    instance.currentSequenceIndex = newSeq;
                    instance.animDuration = static_cast<float>(model.sequences[newSeq].duration);
                    instance.animTime = 0.0f;
                } else {
                    instance.variationTimer = randFloat(rendering::M2_IDLE_VARIATION_TIMER_MIN_MS, rendering::M2_IDLE_VARIATION_TIMER_MAX_MS);
                }
            }
        }

        // Frustum + distance cull: skip expensive bone computation for off-screen instances.
        // Both effectiveMaxDistSq and paddedRadius are precomputed per instance in
        // recomputeCachedCullFactors(); we only need the per-frame distance and frustum test.
        glm::vec3 toCam = instance.position - cachedCamPos_;
        float distSq = glm::dot(toCam, toCam);
        float effectiveMaxDistSq = rendering::m2InstanceMaxDistSq(
            cachedMaxRenderDistSq_, instance.cachedEffectiveMaxDistSqFactor,
            false, 0.0f, viewDistanceAbsolute_,
            instance.cachedIsGroundDetail, groundDetailMaxDistance_);
        if (instance.cachedIsSkyBird) {
            constexpr float kBirdMaxDistSq =
                rendering::M2_SKY_BIRD_MAX_RENDER_DISTANCE *
                rendering::M2_SKY_BIRD_MAX_RENDER_DISTANCE;
            effectiveMaxDistSq = std::min(effectiveMaxDistSq, kBirdMaxDistSq);
        }
        if (distSq > effectiveMaxDistSq) continue;
        float paddedRadius = instance.cachedPaddedRadius;
        if (paddedRadius > 0.0f && !updateFrustum.intersectsSphere(instance.cachedCullCenter, paddedRadius)) continue;

        // LOD 3 skip: models beyond 150 units use the lowest LOD mesh which has
        // no visible skeletal animation.  Keep their last-computed bone matrices
        // (always valid - seeded on spawn) and avoid the expensive per-bone work.
        // Sky birds, light beams, and ship machinery are exempt: their visible
        // motion is baked entirely into bone animation.
        constexpr float kLOD3DistSq = rendering::M2_LOD3_DISTANCE * rendering::M2_LOD3_DISTANCE;
        const bool needsDistantBones = instance.cachedIsSkyBird || instance.cachedIsLightBeam ||
                                       instance.cachedIsTransportDoodad;
        if (distSq > kLOD3DistSq && !needsDistantBones) continue;

        // Distance-based frame skipping: update distant bones less frequently
        uint32_t boneInterval = 1;
        if (!needsDistantBones) {
            if (distSq > rendering::M2_BONE_SKIP_DIST_FAR * rendering::M2_BONE_SKIP_DIST_FAR) boneInterval = 4;
            else if (distSq > rendering::M2_BONE_SKIP_DIST_MID * rendering::M2_BONE_SKIP_DIST_MID) boneInterval = 2;
        }
        instance.frameSkipCounter++;
        if ((instance.frameSkipCounter % boneInterval) != 0) continue;

        boneWorkIndices_.push_back(idx);
    }

    // Compute bone matrices (expensive, parallel if enough work)
    const size_t animCount = boneWorkIndices_.size();
    if (animCount > 0) {
        static const size_t minParallelAnimInstances = std::max<size_t>(
            8, envSizeOrDefault("WOWEE_M2_ANIM_MT_MIN", 96));
        if (animCount < minParallelAnimInstances || numAnimThreads_ <= 1) {
            // Sequential - not enough work to justify thread overhead
            for (size_t i : boneWorkIndices_) {
                if (i >= instances.size()) continue;
                auto& inst = instances[i];
                if (!inst.cachedModel) continue;
                computeBoneMatrices(*inst.cachedModel, inst, &cachedCamPos_);
            }
        } else {
            // Parallel - dispatch across worker threads
            static const size_t minAnimWorkPerThread = std::max<size_t>(
                16, envSizeOrDefault("WOWEE_M2_ANIM_WORK_PER_THREAD", 64));
            const size_t maxUsefulThreads = std::max<size_t>(
                1, (animCount + minAnimWorkPerThread - 1) / minAnimWorkPerThread);
            const size_t numThreads = std::min(static_cast<size_t>(numAnimThreads_), maxUsefulThreads);
            if (numThreads <= 1) {
                for (size_t i : boneWorkIndices_) {
                    if (i >= instances.size()) continue;
                    auto& inst = instances[i];
                    if (!inst.cachedModel) continue;
                    computeBoneMatrices(*inst.cachedModel, inst, &cachedCamPos_);
                }
            } else {
                const size_t chunkSize = animCount / numThreads;
                const size_t remainder = animCount % numThreads;

                auto processRange = [this](size_t begin, size_t end) {
                    for (size_t j = begin; j < end; ++j) {
                        size_t idx = boneWorkIndices_[j];
                        if (idx >= instances.size()) continue;
                        auto& inst = instances[idx];
                        if (!inst.cachedModel) continue;
                        computeBoneMatrices(*inst.cachedModel, inst, &cachedCamPos_);
                    }
                };

                // Reuse persistent futures vector to avoid allocation
                animFutures_.clear();
                // Join every submitted range if another range/submit/get
                // throws. The outer renderer may catch the exception and
                // continue immediately with collision or scene cleanup.
                core::FutureGroupWaitGuard animationRangesScope(animFutures_);
                if (animFutures_.capacity() < numThreads) {
                    animFutures_.reserve(numThreads);
                }

                // Dispatch all but the last chunk to the shared pool; process the
                // last chunk on this thread so this call always makes progress even
                // when it itself runs on a pool worker (see ThreadPool docs).
                size_t start = 0;
                for (size_t t = 0; t + 1 < numThreads; ++t) {
                    size_t end = start + chunkSize + (t < remainder ? 1 : 0);
                    animFutures_.push_back(core::ThreadPool::frameWorkers().submit(
                        [processRange, start, end]() { processRange(start, end); }));
                    start = end;
                }
                processRange(start, animCount);

                for (auto& f : animFutures_) {
                    f.get();
                }
            }
        }
    }

    // Particle update (sequential - uses RNG, not thread-safe)
    // Only iterate instances that have particle emitters (pre-built list).
    //
    // The frame's budget is settled first, because emitParticles caps each
    // instance against its share of it. The emitter count it is divided by is
    // last frame's, which is what makes it free: the number falls out of the
    // loop below rather than needing a pass of its own, and a scene that gets
    // busier tightens over one frame instead of spending that frame building
    // particles it would only have to throw away.
    //
    // Memory pressure is re-read on a counter, not per frame. On the console
    // each of those two calls is sceKernelAvailableFlexibleMemorySize - a
    // kernel round trip - and headroom moves on the scale of streaming a tile,
    // not of a frame. Everything else here is arithmetic.
    if (particleBudgetSample_ == 0) {
        const auto& memMon = core::MemoryMonitor::getInstance();
        particleFrameBudget_ = m2particles::frameBudget(
            PARTICLE_FRAME_BUDGET, memMon.isMemoryPressure(),
            memMon.isSevereMemoryPressure());
    }
    if (++particleBudgetSample_ >= kParticleBudgetSampleInterval)
        particleBudgetSample_ = 0;
    particleInstanceShare_ = m2particles::instanceShare(
        particleFrameBudget_, std::max<size_t>(particleEmittersSmoothed_, 1));
    const float particleCullDistSq = m2particles::emitterCullDistSq(
        cachedMaxRenderDistSq_, particleEmittersSmoothed_,
        PARTICLE_EMITTER_SOFT_LIMIT);
    // Where a culled instance actually gives its particles back. Freeing them
    // at the cull distance itself would malloc and free once a frame for every
    // emitter sitting on the boundary - and the boundary moves, because the
    // cull is driven by the count. A fifth further out is far enough that the
    // two edges cannot both be crossed by ordinary walking speed in a frame.
    const float particleReleaseDistSq = particleCullDistSq * 1.44f;
    const auto effectPriority=[&](size_t idx) {
        if(idx>=instances.size() || !instances[idx].cachedModel)return false;
        const auto& instance=instances[idx];const auto& model=*instance.cachedModel;
        const glm::vec3 d=instance.position-cachedCamPos_;
        return (model.isInstancePortal || model.isSpellEffect) && glm::dot(d,d)<80.f*80.f;
    };
    std::stable_sort(particleInstanceIndices_.begin(),particleInstanceIndices_.end(),
        [&](size_t a,size_t b){return effectPriority(a)>effectPriority(b);});
    const size_t protectedCount=std::count_if(particleInstanceIndices_.begin(),particleInstanceIndices_.end(),effectPriority);
    const size_t baseShare=particleInstanceShare_;
    size_t emittersThisFrame = 0;
    for (size_t idx : particleInstanceIndices_) {
        if (idx >= instances.size()) continue;
        auto& instance = instances[idx];
        // Distance cull: only update particles within visible range
        glm::vec3 toCam = instance.position - cachedCamPos_;
        float distSq = glm::dot(toCam, toCam);
        if (distSq > particleCullDistSq) {
            // Well past it and still holding particles: give the memory back,
            // or a walk through a city leaves every brazier it passed paying
            // for a vector nothing will draw.
            if (distSq > particleReleaseDistSq && !instance.particles.empty()) {
                decltype(instance.particles){}.swap(instance.particles);
            }
            continue;
        }
        if (!instance.cachedModel) continue;
        ++emittersThisFrame;
        particleInstanceShare_=effectPriority(idx)
            ? std::max(baseShare,std::min<size_t>(512,particleFrameBudget_/std::max<size_t>(protectedCount,1)))
            : baseShare;
        emitParticles(instance, *instance.cachedModel, deltaTime);
        updateParticles(instance, deltaTime);
        if (!instance.cachedModel->ribbonEmitters.empty()) {
            updateRibbons(instance, *instance.cachedModel, deltaTime);
        }
    }
    particleInstanceShare_=baseShare;
    particleEmittersLastFrame_ = emittersThisFrame;
    particleEmittersSmoothed_ =
        m2particles::smoothEmitterCount(particleEmittersSmoothed_, emittersThisFrame);
}

// Diagnostic: WOWEE_M2_NO_SKINNING=1 renders every M2 in its bind pose by
// telling the shader to ignore bones, separating a skinning artifact from one
// drawn by the particle or ribbon systems.
static const bool kM2NoSkinning = envFlagEnabled("WOWEE_M2_NO_SKINNING");

void M2Renderer::prepareRender(uint32_t frameIndex, const Camera& camera) {
    if (!initialized_ || instances.empty()) return;
    (void)camera;  // reserved for future frustum-based culling

    // --- Mega bone SSBO: assign ranges and upload all animated instance bones ---
    // Offset 0 is reserved as the identity/no-bones sentinel; animated instances
    // are packed after it at their own bone count, so a 300-bone creature gets
    // all 300 matrices instead of being truncated into a fixed stride and
    // reading into its neighbour's range.
    uint32_t nextOffset = 1;
    for (size_t idx : animatedInstanceIndices_) {
        if (idx >= instances.size()) continue;
        auto& instance = instances[idx];

        if (instance.boneMatrices.empty()) {
            instance.megaBoneOffset = 0;  // Use identity slot
            continue;
        }

        const uint32_t boneCount = static_cast<uint32_t>(instance.boneMatrices.size());
        if (boneCount > MEGA_BONE_MATRIX_CAPACITY - nextOffset) {
            instance.megaBoneOffset = 0;  // Overflow - use identity
            continue;
        }

        instance.megaBoneOffset = nextOffset;

        // Upload bone matrices to mega buffer - only when they were recomputed
        // since the last upload into this frame's buffer, or the instance's
        // slot moved (animated set changed). Most animated instances are
        // distance/frustum/frame-skip culled and keep their previous bones, so
        // skipping their memcpy avoids megabytes of redundant writes per frame.
        if (megaBoneMapped_[frameIndex] &&
            (instance.bonesDirty[frameIndex] ||
             instance.megaBoneUploadedSlot[frameIndex] != instance.megaBoneOffset)) {
            auto* dst = static_cast<glm::mat4*>(megaBoneMapped_[frameIndex]) + instance.megaBoneOffset;
            memcpy(dst, instance.boneMatrices.data(), boneCount * sizeof(glm::mat4));
            instance.bonesDirty[frameIndex] = false;
            instance.megaBoneUploadedSlot[frameIndex] = instance.megaBoneOffset;
        }

        nextOffset += boneCount;
    }
}

// Dispatch GPU frustum culling compute shader into the primary frame command
// buffer. render() consumes the completed output left in this frame slot from
// its previous use; this dispatch produces results for the slot's next reuse.
void M2Renderer::dispatchCullCompute(VkCommandBuffer cmd, uint32_t frameIndex, const Camera& camera) {
    if (frameIndex >= 2 || !cullPipeline_ || instances.empty()) return;

    const uint32_t numInstances = std::min(static_cast<uint32_t>(instances.size()), MAX_CULL_INSTANCES);

    // --- Compute per-instance adaptive distances (same formula as old CPU cull) ---
    const float targetRenderDist = viewDistanceScale_ *
        ((instances.size() > 2000) ? 300.0f
         : (instances.size() > 1000) ? 500.0f
                                     : 1000.0f);
    const float shrinkRate = 0.005f;
    const float growRate   = 0.05f;
    float blendRate = (targetRenderDist < smoothedRenderDist_) ? shrinkRate : growRate;
    smoothedRenderDist_ = glm::mix(smoothedRenderDist_, targetRenderDist, blendRate);
    const float maxRenderDistance = smoothedRenderDist_;
    const float maxRenderDistanceSq = maxRenderDistance * maxRenderDistance;
    // The shader rejects on this bound before it ever reads the per-instance
    // distance, so it has to clear the game-object floor as well - otherwise
    // that floor is silently capped at 2x the ambient doodad distance.
    const float maxPossibleDistSq = std::max(
        maxRenderDistanceSq * 4.0f,  // 2x safety margin
        rendering::M2_GAME_OBJECT_MIN_RENDER_DISTANCE *
        rendering::M2_GAME_OBJECT_MIN_RENDER_DISTANCE);

    // --- Upload frustum planes + camera (UBO, binding 0) ---
    const glm::mat4 vp = camera.getProjectionMatrix() * camera.getViewMatrix();
    Frustum frustum;
    frustum.extractFromMatrix(vp);
    const glm::vec3 camPos = camera.getPosition();

    if (cullUniformMapped_[frameIndex]) {
        auto* ubo = static_cast<CullUniformsGPU*>(cullUniformMapped_[frameIndex]);
        for (int i = 0; i < 6; i++) {
            const auto& p = frustum.getPlane(static_cast<Frustum::Side>(i));
            ubo->frustumPlanes[i] = glm::vec4(p.normal, p.distance);
        }
        ubo->cameraPos = glm::vec4(camPos, maxPossibleDistSq);
        ubo->instanceCount = numInstances;

        // HiZ occlusion culling fields
        const bool hizReady = hizSystem_ && hizSystem_->isReady();

        // Auto-disable HiZ when the camera has moved/rotated significantly.
        // Large VP changes make the depth pyramid unreliable because the
        // reprojected screen positions diverge from the actual pyramid data.
        bool hizSafe = hizReady;
        if (hizReady) {
            // Compare current VP against previous VP - Frobenius-style max diff.
            float maxDiff = 0.0f;
            const float* curM  = &vp[0][0];
            const float* prevM = &prevVP_[0][0];
            for (int k = 0; k < 16; ++k)
                maxDiff = std::max(maxDiff, std::abs(curM[k] - prevM[k]));
            // Threshold: typical tracking-camera motion (following a walking
            // character) produces diffs of 0.05–0.25.  A fast rotation or
            // zoom easily exceeds 0.5.  The previous threshold (0.15) caused
            // the HiZ pass to toggle on/off every other frame during normal
            // gameplay, which produced global M2 doodad flicker.
            if (maxDiff > rendering::HIZ_VP_DIFF_THRESHOLD) hizSafe = false;
        }

        ubo->hizEnabled = hizSafe ? 1u : 0u;
        ubo->hizMipLevels = hizReady ? hizSystem_->getMipLevels() : 0u;
        ubo->_pad2 = 0;
        if (hizReady) {
            ubo->hizParams = glm::vec4(
                static_cast<float>(hizSystem_->getPyramidWidth()),
                static_cast<float>(hizSystem_->getPyramidHeight()),
                camera.getNearPlane(),
                0.0f
            );
            ubo->viewProj = vp;
            // Use previous frame's VP for HiZ reprojection - the HiZ pyramid
            // was built from the previous frame's depth, so we must project
            // into the same screen space to sample the correct depths.
            ubo->prevViewProj = prevVP_;
        } else {
            ubo->hizParams = glm::vec4(0.0f);
            ubo->viewProj = glm::mat4(1.0f);
            ubo->prevViewProj = glm::mat4(1.0f);
        }

        // Save current VP for next frame's temporal reprojection
        prevVP_ = vp;
    }

    // Rotate this slot's ID log: the dispatch recorded below replaces the one
    // whose results render() is about to consume, so the IDs it was built from
    // become the readable set.  Done before the upload loop overwrites them, and
    // after the early-out above, so "readable" always describes the last dispatch
    // actually recorded on this slot.
    if (frameIndex < 2) {
        cullReadableIds_[frameIndex].swap(cullSubmittedIds_[frameIndex]);
        cullSubmittedIds_[frameIndex].clear();
        cullSubmittedIds_[frameIndex].reserve(numInstances);
        for (uint32_t i = 0; i < numInstances; i++)
            cullSubmittedIds_[frameIndex].push_back(instances[i].id);
    }

    // --- Upload per-instance cull data (SSBO, binding 1) ---
    // The per-instance radius math used to be recomputed here every frame; it's
    // now precomputed once by recomputeCachedCullFactors() since it depends only
    // on static instance state (scale, bound radius, animation/ground flags).
    if (cullInputMapped_[frameIndex]) {
        auto* input = static_cast<CullInstanceGPU*>(cullInputMapped_[frameIndex]);
        for (uint32_t i = 0; i < numInstances; i++) {
            const auto& inst = instances[i];
            float effectiveMaxDistSq = rendering::m2InstanceMaxDistSq(
                maxRenderDistanceSq, inst.cachedEffectiveMaxDistSqFactor,
                inst.isGameObject, rendering::M2_GAME_OBJECT_MIN_RENDER_DISTANCE,
                viewDistanceAbsolute_,
                inst.cachedIsGroundDetail, groundDetailMaxDistance_);
            if (inst.cachedIsSkyBird && inst.cachedHasAnimation && !inst.cachedDisableAnimation) {
                constexpr float kBirdMaxDistSq =
                    rendering::M2_SKY_BIRD_MAX_RENDER_DISTANCE *
                    rendering::M2_SKY_BIRD_MAX_RENDER_DISTANCE;
                effectiveMaxDistSq = std::min(effectiveMaxDistSq, kBirdMaxDistSq);
            }

            uint32_t flags = 0;
            if (inst.cachedIsValid)          flags |= 1u;
            if (inst.cachedIsSmoke)           flags |= 2u;
            if (inst.cachedIsInvisibleTrap)   flags |= 4u;
            // Bit 3: previouslyVisible - the shader runs the HiZ occlusion test
            // ONLY when this bit is set (an object with no depth in last frame's
            // pyramid can't be tested reliably). Hysteresis: keep it set unless
            // culled for 2+ consecutive frames, preventing single-frame false-cull
            // flicker. The counter lives on the instance, so streaming churn can
            // never pair it with a different object's history.
            //
            // Server game objects (mailboxes, chests, ...) opt out of HiZ entirely
            // by never setting this bit: they are small gameplay props that sit
            // flush against walls and doorframes, exactly where the coarse depth
            // pyramid reports false occlusions. Such a false-cull would persist
            // (the prop is then not rendered, so it never regains depth to clear
            // itself) - the "mailbox went invisible in place" report. Frustum +
            // distance culling still bound them; only the unreliable occlusion
            // test is waived.
            if (inst.hizPrevCulledFrames < 2 && !inst.isGameObject)
                flags |= 8u;

            input[i].sphere = glm::vec4(inst.cachedCullCenter, inst.cachedPaddedRadius);
            input[i].effectiveMaxDistSq = effectiveMaxDistSq;
            input[i].flags = flags;
        }
    }

    // --- Dispatch compute shader ---
    const bool useHiZ = (cullHiZPipeline_ != VK_NULL_HANDLE)
                     && hizSystem_ && hizSystem_->isReady();
    if (useHiZ) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullHiZPipeline_);
        // Set 0: cull UBO + input/output SSBOs
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                cullHiZPipelineLayout_, 0, 1, &cullSet_[frameIndex], 0, nullptr);
        // Set 1: HiZ pyramid sampler
        VkDescriptorSet hizSet = hizSystem_->getDescriptorSet(frameIndex);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                cullHiZPipelineLayout_, 1, 1, &hizSet, 0, nullptr);
    } else {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                cullPipelineLayout_, 0, 1, &cullSet_[frameIndex], 0, nullptr);
    }

    const uint32_t groupCount = (numInstances + 63) / 64;
    vkCmdDispatch(cmd, groupCount, 1, 1);

    // Make writes available to the host after this frame's fence signals. The
    // CPU invalidates and reads them when this frame slot is reused.
    VkMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_HOST_BIT;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    cmdPipelineBarrier2(cmd, dep);
}

bool M2Renderer::ensureInstanceCapacity(uint32_t frameIndex, uint64_t required) {
    if (frameIndex >= 2) return false;
    if (required <= instanceCapacity_[frameIndex]) return true;
    uint64_t capacity = instanceCapacity_[frameIndex];
    while (capacity < required && capacity <= UINT32_MAX / 2u) capacity *= 2u;
    if (capacity < required || capacity > UINT32_MAX / sizeof(M2InstanceGPU)) {
        LOG_ERROR("M2 instance storage: requested capacity too large: ", required);
        return false;
    }
    VkBufferCreateInfo bci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = capacity * sizeof(M2InstanceGPU);
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};
    ::VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    const auto allocator = vkCtx_->getAllocator();
    const VkResult result = vmaCreateBuffer(allocator, &bci, &aci, &buffer, &allocation, &info);
    if (result != VK_SUCCESS || !buffer || !allocation || !info.pMappedData) {
        if (buffer || allocation) vmaDestroyBuffer(allocator, buffer, allocation);
        LOG_ERROR("M2 instance storage: growth failed bytes=", bci.size, " result=", int(result));
        return false;
    }
    // beginFrame has waited for this slot; no draws have bound its set yet.
    VkDescriptorBufferInfo bi{.buffer = buffer, .offset = 0, .range = bci.size};
    VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = instanceSet_[frameIndex];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bi;
    vkUpdateDescriptorSets(vkCtx_->getDevice(), 1, &write, 0, nullptr);
    vmaDestroyBuffer(allocator, instanceBuffer_[frameIndex], instanceAlloc_[frameIndex]);
    instanceBuffer_[frameIndex] = buffer;
    instanceAlloc_[frameIndex] = allocation;
    instanceMapped_[frameIndex] = info.pMappedData;
    instanceCapacity_[frameIndex] = static_cast<uint32_t>(capacity);
    LOG_INFO("M2 instance storage grew: frame=", frameIndex, " slots=", capacity, " bytes=", bci.size);
    return true;
}

void M2Renderer::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet, const Camera& camera) {
    if (instances.empty() || !opaquePipeline_) {
        return;
    }

    // Debug: log once when we start rendering
    static bool loggedOnce = false;
    const bool traceFirstRender = !loggedOnce;
    if (!loggedOnce) {
        loggedOnce = true;
        LOG_INFO("M2 render: ", instances.size(), " instances, ", models.size(), " models");
    }

    auto firstFrameStage = [&](const char* stage) {
#ifdef WOWEE_PS4
        if (traceFirstRender) platform::ps4::reportBootStage(stage);
#else
        (void)stage;
#endif
    };
    firstFrameStage("M2 first frame: cull results begin");

    // Periodic diagnostic: report render pipeline stats every 10 seconds
    static int diagCounter = 0;
    if (++diagCounter == 600) { // ~10s at 60fps
        diagCounter = 0;
        uint32_t totalValid = 0, totalAnimated = 0, totalBonesReady = 0, totalMegaBoneOk = 0;
        for (const auto& inst : instances) {
            if (inst.cachedIsValid) totalValid++;
            if (inst.cachedHasAnimation && !inst.cachedDisableAnimation) {
                totalAnimated++;
                if (!inst.boneMatrices.empty()) totalBonesReady++;
                if (inst.megaBoneOffset != 0) totalMegaBoneOk++;
            }
        }
        LOG_INFO("M2 diag: total=", instances.size(),
                 " valid=", totalValid,
                 " animated=", totalAnimated,
                 " bonesReady=", totalBonesReady,
                 " megaBoneOk=", totalMegaBoneOk,
                 " visible=", sortedVisible_.size(),
                 " draws=", lastDrawCallCount);
    }

    // Reuse persistent buffers (clear instead of reallocating)
    glowSprites_.clear();

    lastDrawCallCount = 0;
    const float lavaAnimSeconds = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - kLavaAnimStart).count();

    // GPU cull results - dispatchCullCompute() already updated smoothedRenderDist_.
    // Use the cached value (set by dispatchCullCompute or fallback below).
    const uint32_t frameIndex = vkCtx_->getCurrentFrame();
    if (frameIndex >= 2 || !instanceMapped_[frameIndex] || !instanceSet_[frameIndex]) {
        LOG_ERROR("M2 render: missing mapped instance buffer or descriptor for frame ", frameIndex);
        firstFrameStage("M2 first frame: instance resources unavailable");
        return;
    }

    const uint32_t numInstances = std::min(static_cast<uint32_t>(instances.size()), MAX_CULL_INSTANCES);
    const uint32_t* visibility = static_cast<const uint32_t*>(cullOutputMapped_[frameIndex]);
#ifdef WOWEE_PS4
    const bool gpuCullAvailable = false; // current-camera visibility; no stale frame-slot verdict
#else
    const bool gpuCullAvailable = (cullPipeline_ != VK_NULL_HANDLE && visibility != nullptr
                                   && !cullReadableIds_[frameIndex].empty());
#endif

    // Scatter the GPU visibility results back onto the instances they were
    // computed for.  The results belong to the dispatch recorded on this slot
    // ~2 frames ago, so they are keyed by that dispatch's instance IDs, never by
    // the current array index: createInstance() appends and removeInstance()
    // swap-removes, so a respawned game object lands at the volatile tail of the
    // array and would otherwise inherit the verdict of whatever transient object
    // (spell visual, streamed doodad, creature) held that index two frames back
    // - leaving it culled for as long as the churn continued.
    //
    // Instances with no entry in the readable set were created after that
    // dispatch and keep their defaults: visible, and exempt from the HiZ test.
    //
    // hizPrevCulledFrames is a hysteresis counter rather than a binary flag: an
    // object must be culled for 2 consecutive frames before it stops counting as
    // "previously visible", which prevents the 1-frame-on / 1-frame-off
    // oscillation that showed up as doodad flicker near moving characters.
    if (gpuCullAvailable) {
        const auto& ids = cullReadableIds_[frameIndex < 2 ? frameIndex : 0];
        for (size_t k = 0; k < ids.size(); ++k) {
            // Fast path: with no churn since that dispatch the ordering still
            // matches, so skip the hash lookup.
            M2Instance* inst = nullptr;
            if (k < instances.size() && instances[k].id == ids[k]) {
                inst = &instances[k];
            } else {
                auto idxIt = instanceIndexById.find(ids[k]);
                if (idxIt == instanceIndexById.end() || idxIt->second >= instances.size())
                    continue;  // instance was removed since the dispatch
                inst = &instances[idxIt->second];
            }
            if (visibility[k]) {
                inst->lastCullVisible = 1;
                inst->hizPrevCulledFrames = 0;
            } else {
                inst->lastCullVisible = 0;
                inst->hizPrevCulledFrames =
                    std::min<uint8_t>(inst->hizPrevCulledFrames + 1, 3);
            }
        }
    } else {
        // No GPU cull data - conservatively treat everything as visible.
        for (auto& inst : instances) {
            inst.lastCullVisible = 1;
            inst.hizPrevCulledFrames = 0;
        }
    }

    // If GPU culling was not dispatched, fallback: compute distances on CPU
#ifdef WOWEE_PS4
    smoothedRenderDist_ = rendering::m2StableRenderDistance(viewDistanceAbsolute_, environmentDetail_);
    const float maxRenderDistanceSq = smoothedRenderDist_ * smoothedRenderDist_;
#else
    float maxRenderDistanceSq;
    if (!gpuCullAvailable) {
        const float targetRenderDist = viewDistanceScale_ *
            ((instances.size() > 2000) ? 300.0f
             : (instances.size() > 1000) ? 500.0f
                                         : 1000.0f);
        const float shrinkRate = 0.005f;
        const float growRate = 0.05f;
        float blendRate = (targetRenderDist < smoothedRenderDist_) ? shrinkRate : growRate;
        smoothedRenderDist_ = glm::mix(smoothedRenderDist_, targetRenderDist, blendRate);
        maxRenderDistanceSq = smoothedRenderDist_ * smoothedRenderDist_;
    } else {
        maxRenderDistanceSq = smoothedRenderDist_ * smoothedRenderDist_;
    }

#endif

    const float fadeStartFraction = 0.75f;
    const glm::vec3 camPos = camera.getPosition();

    // Build sorted visible instance list
    sortedVisible_.clear();
    transparentVisible_.clear();
    skyDiagDrawsOpaque_ = 0;
    skyDiagDrawsTransparent_ = 0;
    const size_t expectedVisible = std::min(instances.size() / 3, size_t(600));
    if (sortedVisible_.capacity() < expectedVisible) {
        sortedVisible_.reserve(expectedVisible);
    }
    if (transparentVisible_.capacity() < expectedVisible / 4)
        transparentVisible_.reserve(expectedVisible / 4);

    // GPU frustum culling - build frustum for CPU fallback path and overflow instances
    Frustum frustum;
    {
        const glm::mat4 vp = camera.getProjectionMatrix() * camera.getViewMatrix();
        frustum.extractFromMatrix(vp);
    }
    // Matches the bound uploaded to the cull shader, including the headroom the
    // game-object floor needs (see dispatchCullCompute).
    const float maxPossibleDistSq = std::max(
        maxRenderDistanceSq * 4.0f,
        rendering::M2_GAME_OBJECT_MIN_RENDER_DISTANCE *
        rendering::M2_GAME_OBJECT_MIN_RENDER_DISTANCE);

    const uint32_t totalInstances = static_cast<uint32_t>(instances.size());
    firstFrameStage("M2 first frame: visibility begin");
    auto classifyRange = [&](std::vector<VisibleEntry>& opaque,
                             std::vector<VisibleEntry>& transparent,
                             uint32_t begin, uint32_t end) {
        for (uint32_t i = begin; i < end; ++i) {
            const auto& instance = instances[i];
            float distSq;
            float effectiveMaxDistSq;

            // Server game objects keep a distance floor instead of following the
            // ambient doodad distance down; it also feeds the fade curve below,
            // so a mailbox doesn't fade out at the doodad boundary either.
            const float instanceMaxDistSq = rendering::m2InstanceMaxDistSq(
                maxRenderDistanceSq, instance.cachedEffectiveMaxDistSqFactor,
                instance.isGameObject, rendering::M2_GAME_OBJECT_MIN_RENDER_DISTANCE,
                viewDistanceAbsolute_,
                instance.cachedIsGroundDetail, groundDetailMaxDistance_);

            if (forceNoCull_) {
                if (!instance.cachedIsValid) continue;
                glm::vec3 toCam = instance.position - camPos;
                distSq = glm::dot(toCam, toCam);
                effectiveMaxDistSq = instanceMaxDistSq;
            } else {
                // A GPU verdict is delayed by a frame-slot round trip. Always
                // apply current-camera geometry checks, including when no
                // readback exists yet or a new instance defaults to visible.
                if (gpuCullAvailable && i < numInstances && !instance.lastCullVisible) continue;
                if (!instance.cachedIsValid || instance.cachedIsSmoke || instance.cachedIsInvisibleTrap) continue;
                // Measure distance to the visual sphere, not the placement
                // pivot. Offset/large tree canopies can cross the draw boundary
                // while their origin lies beyond it. This must agree with the
                // bounds used for frustum tests and the fade below.
#ifdef WOWEE_PS4
                glm::vec3 toCam = instance.cachedCullCenter - camPos;
                const float centerDistance = glm::length(toCam);
                const float surfaceDistance = std::max(0.0f, centerDistance - instance.cachedVisualRadius);
                distSq = surfaceDistance * surfaceDistance;
#else
                glm::vec3 toCam = instance.position - camPos;
                distSq = glm::dot(toCam, toCam);
#endif
                if (distSq > maxPossibleDistSq) continue;
                effectiveMaxDistSq = instanceMaxDistSq;
                if (distSq > effectiveMaxDistSq) continue;
                float paddedRadius = instance.cachedPaddedRadius;
                if (paddedRadius > 0.0f && !frustum.intersectsSphere(instance.cachedCullCenter, paddedRadius)) continue;
            }

            if (instance.cachedIsSkyBird && instance.cachedHasAnimation && !instance.cachedDisableAnimation) {
                constexpr float kBirdMaxDistSq =
                    rendering::M2_SKY_BIRD_MAX_RENDER_DISTANCE *
                    rendering::M2_SKY_BIRD_MAX_RENDER_DISTANCE;
                effectiveMaxDistSq = std::min(effectiveMaxDistSq, kBirdMaxDistSq);
                if (distSq > effectiveMaxDistSq) continue;
            }

            VisibleEntry visible{.index = i, .modelId = instance.modelId, .distSq = distSq, .effectiveMaxDistSq = effectiveMaxDistSq};
            opaque.push_back(visible);
            if (instance.cachedModel &&
                (instance.cachedModel->hasTransparentBatches || instance.cachedModel->isSpellEffect)) {
                transparent.push_back(visible);
            }
        }
    };

#ifdef WOWEE_PS4
    // Command recording is serial on PS4. Classify directly into persistent
    // lists, without a new set of chunk vectors/futures for the first dense
    // world frame. Every instance uses the same culling predicate below.
    classifyRange(sortedVisible_, transparentVisible_, 0, totalInstances);
#else
    struct VisibleChunk {
        std::vector<VisibleEntry> opaque;
        std::vector<VisibleEntry> transparent;
    };
    const uint32_t chunkCount = totalInstances >= 2048
        ? std::min<uint32_t>(4, (totalInstances + 1023) / 1024) : 1;
    std::vector<VisibleChunk> chunks(chunkCount);
    std::vector<std::future<void>> visibilityFutures;
    visibilityFutures.reserve(chunkCount > 0 ? chunkCount - 1 : 0);
    const uint32_t chunkSize = (totalInstances + chunkCount - 1) / chunkCount;
    for (uint32_t chunk = 0; chunk + 1 < chunkCount; ++chunk) {
        const uint32_t begin = chunk * chunkSize;
        const uint32_t end = std::min(totalInstances, begin + chunkSize);
        visibilityFutures.push_back(core::ThreadPool::frameWorkers().submit(
            [&, chunk, begin, end]() { classifyRange(chunks[chunk].opaque, chunks[chunk].transparent, begin, end); }));
    }
    const uint32_t lastChunk = chunkCount - 1;
    classifyRange(chunks[lastChunk].opaque, chunks[lastChunk].transparent, lastChunk * chunkSize, totalInstances);
    for (auto& future : visibilityFutures) future.get();

    for (auto& chunk : chunks) {
        sortedVisible_.insert(sortedVisible_.end(),
                              std::make_move_iterator(chunk.opaque.begin()),
                              std::make_move_iterator(chunk.opaque.end()));
        transparentVisible_.insert(transparentVisible_.end(),
                                   std::make_move_iterator(chunk.transparent.begin()),
                                   std::make_move_iterator(chunk.transparent.end()));
    }

#endif
    if (traceFirstRender) LOG_INFO("M2 first visibility: total=", totalInstances,
        " opaque=", sortedVisible_.size(), " transparent=", transparentVisible_.size());
    firstFrameStage("M2 first frame: visibility complete");

    // The furthest doodad actually drawn, for the diagnostic that sets it
    // against the furthest terrain chunk. See Renderer::logViewDistanceDiag.
    furthestDrawnSq_ = 0.0f;
    for (const auto& e : sortedVisible_)
        if (e.distSq > furthestDrawnSq_) furthestDrawnSq_ = e.distSq;
    for (const auto& e : transparentVisible_)
        if (e.distSq > furthestDrawnSq_) furthestDrawnSq_ = e.distSq;

    // Whether the sky model survived culling this frame, when this is the
    // renderer that draws one.
    //
    // Reported as flickering while the camera turns and steady while it does
    // not, with everything upstream measured and holding still: the lighting
    // inputs, the model's clock, the frame time. What that leaves is the dome
    // being drawn on some frames and not others, and this says so in one line
    // per change rather than one per frame.
    // WOWEE_SKY_M2_OPAQUE_ONLY=1 drops the sky model's blended layers.
    //
    // The model is the culprit - the flicker goes with WOWEE_NO_SKY_M2 - and it
    // is not a small one: hellfireskybox.m2 carries 23 textures over 34 render
    // flags, with five transparency tracks and seven UV animations driven by
    // six global sequences. The brightening and dimming is those alpha tracks.
    // This says whether the flicker is in the blended layers or in the base the
    // opaque pass draws, which halves what is left to read.
    static const bool skyOpaqueOnly = std::getenv("WOWEE_SKY_M2_OPAQUE_ONLY") != nullptr;
    if (skyMode_ && skyOpaqueOnly) transparentVisible_.clear();

    if (skyMode_) {
        const bool drawn = !sortedVisible_.empty() || !transparentVisible_.empty();
        if (drawn != skyDiagWasDrawn_) {
            skyDiagWasDrawn_ = drawn;
            LOG_INFO("skyM2 cull: ", drawn ? "DRAWN" : "CULLED",
                     " opaque=", sortedVisible_.size(),
                     " transparent=", transparentVisible_.size(),
                     " instances=", instances.size());
        }
    }

    // Two-pass rendering: opaque/alpha-test first (depth write ON), then transparent/additive
    // (depth write OFF, sorted back-to-front) so transparent geometry composites correctly
    // against all opaque geometry rather than only against what was rendered before it.

    // Pass 1: sort by modelId for minimum buffer rebinds (opaque batches)
    std::sort(sortedVisible_.begin(), sortedVisible_.end(),
              [](const VisibleEntry& a, const VisibleEntry& b) { return a.modelId < b.modelId; });


    uint32_t currentModelId = UINT32_MAX;
    const M2ModelGPU* currentModel = nullptr;
    bool currentModelValid = false;

    // State tracking
    VkPipeline currentPipeline = VK_NULL_HANDLE;
    VkDescriptorSet currentMaterialSet = VK_NULL_HANDLE;

    // Push constants now carry per-batch data only; per-instance data is in instance SSBO.
    struct M2PushConstants {
        int32_t texCoordSet;        // UV set index (0 or 1)
        int32_t isFoliage;          // -1 = sky, 0 = none, 1 = wind foliage, 2 = ground clutter
        int32_t instanceDataOffset; // Base index into instance SSBO for this draw group
        float swayRefHeight;        // Model-space height the wind normalises against
        float swayAmp;              // Wind amplitude scale; 1.0 = the tree-sized default
        float plantHeight;          // The model's own height, for the player brush
    };

    // Fill the sway half of the push constants for one model.
    //
    // Two modes, and the split is about who owns the idle motion. Ground clutter
    // plays a sequence of its own, so mode 2 asks the shader for the player
    // brush and no wind - two swings of one plant at two rates reads as a
    // glitch. Everything else the wind picks up has its animation disabled by
    // the classifier and gets mode 1, wind and brush both.
    //
    // The wind itself was written for trees: it normalised height against 20
    // yards and displaced by an absolute number of model units, so a one-yard
    // tuft travelled a fraction of a millimetre. Every model normalises against
    // its own height now, with an amplitude interpolated between the two ends
    // rather than switched at a threshold - a bush a foot taller than its
    // neighbour should not sway ten times less. Both ends reproduce the numbers
    // that were there: a 20-yard tree still throws 0.35 model units at the tip.
    auto fillSway = [](M2PushConstants& pc, const M2ModelGPU& mdl, bool sky) {
        pc.swayRefHeight = 20.0f;
        pc.swayAmp = 1.0f;
        pc.plantHeight = 0.0f;
        if (sky || !mdl.shadowWindFoliage) {
            pc.isFoliage = sky ? -1 : 0;
            return;
        }
        pc.isFoliage = mdl.isGroundDetail ? 2 : 1;

        // Height above the model's own base, not above the origin: a few
        // detail doodads sit with geometry below z=0.
        const float height = std::max(mdl.boundMax.z - std::min(mdl.boundMin.z, 0.0f), 0.05f);
        pc.plantHeight = height;
        pc.swayRefHeight = height;

        // How far the tip travels as a fraction of the plant's own height:
        // about a tenth for grass, a fiftieth for a tree, and the blend between
        // them for everything in the middle. 0.35 is the trunk layer's throw in
        // the shader, so dividing by it turns a fraction back into that scale.
        const float t = std::clamp((height - 1.0f) / 19.0f, 0.0f, 1.0f);
        const float relativeThrow = 0.105f + (0.0175f - 0.105f) * t;
        pc.swayAmp = relativeThrow * height / 0.35f;
    };

    // What is left of a portal once it is too far away to be a portal.
    //
    // These two sprites used to *be* the dungeon entrance: the opaque pass drew
    // them and skipped the model, and the transparent pass skipped it a second
    // time, so the swirl was never recorded at any distance and a doorway was a
    // bright blue point. That is the screenshot. The model is drawn now, and the
    // card is only what its own comment in emitParticles already claimed for it -
    // the read from across a zone, past where the particles stop.
    //
    // The near edge is the emitter cull distance rather than a constant, because
    // that is the distance being stood in for: inside it the swirl and its
    // particles are both live and a seven-yard additive sprite laid over them is
    // the wash that hid the mesh even where the mesh was drawn.
    const float portalCardNearDistSq = m2particles::emitterCullDistSq(
        cachedMaxRenderDistSq_, particleEmittersSmoothed_, PARTICLE_EMITTER_SOFT_LIMIT);
    auto appendInstancePortalGlow = [&](const M2Instance& instance, float distSq) {
        if (distSq < portalCardNearDistSq || distSq >= 400.0f * 400.0f) return;
        glm::vec3 center = glm::vec3(instance.modelMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        GlowSprite core;
        core.worldPos = center;
        core.color = glm::vec4(0.35f, 0.55f, 1.0f, 1.25f);
        core.size = instance.scale * 7.0f;
        glowSprites_.push_back(core);

        GlowSprite halo = core;
        halo.color.a *= 0.35f;
        halo.size *= 2.4f;
        glowSprites_.push_back(halo);
    };

    firstFrameStage("M2 first frame: draw recording begin");

    // Validate per-frame descriptor set before any Vulkan commands
    if (!perFrameSet) {
        LOG_ERROR("M2Renderer::render: perFrameSet is VK_NULL_HANDLE - skipping M2 render");
        return;
    }

    // Conservative upper bound: base transforms plus every possible animated
    // opaque batch copy and every transparent batch. Filtered batches only
    // reduce usage. Reserve before any draw references this slot's descriptor.
    uint64_t requiredSlots = sortedVisible_.size();
    for (const auto& entry : sortedVisible_) {
        const auto* model = instances[entry.index].cachedModel;
        if (!model) continue;
        for (const auto& batch : model->batches)
            if ((batch.textureAnimIndex != 0xFFFF && model->hasTextureAnimation) || model->isLavaModel)
                ++requiredSlots;
    }
    for (const auto& entry : transparentVisible_) {
        const auto* model = instances[entry.index].cachedModel;
        if (model) requiredSlots += model->batches.size();
    }
    if (!ensureInstanceCapacity(frameIndex, requiredSlots)) {
        firstFrameStage("M2 first frame: draw storage growth failed");
        return;
    }

    // Bind per-frame descriptor set (set 0) - shared across all draws
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_, 0, 1, &perFrameSet, 0, nullptr);

    // Start with opaque pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, opaquePipeline_);
    currentPipeline = opaquePipeline_;

    // Bind dummy bone set (set 2) so non-animated draws have a valid binding.
    // Bind mega bone SSBO instead - all instances index into one buffer via boneBase.
    if (megaBoneSet_[frameIndex]) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout_, 2, 1, &megaBoneSet_[frameIndex], 0, nullptr);
    } else if (dummyBoneSet_) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout_, 2, 1, &dummyBoneSet_, 0, nullptr);
    }

    // Bind instance data SSBO (set 3) - per-instance transforms, fade, bones
    if (instanceSet_[frameIndex]) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout_, 3, 1, &instanceSet_[frameIndex], 0, nullptr);
    }

    // Reset instance SSBO write cursor for this frame
    instanceDataCount_ = 0;
    auto* instSSBO = static_cast<M2InstanceGPU*>(instanceMapped_[frameIndex]);

    // =====================================================================
    // Opaque pass - instanced draws grouped by (modelId, LOD)
    // =====================================================================
    // sortedVisible_ is already sorted by modelId so consecutive entries share
    // the same vertex/index buffer.  Within each model group we sub-group by
    // targetLOD to guarantee all instances in one vkCmdDrawIndexed use the
    // same batch set.  Per-instance data (model matrix, fade, bones) is
    // written to the instance SSBO; the shader reads it via gl_InstanceIndex.
    {
        struct PendingInstance {
            uint32_t instanceIdx;
            float fadeAlpha;
            bool useBones;
            uint16_t targetLOD;
        };
        std::vector<PendingInstance> pending;
        pending.reserve(128);

        size_t visStart = 0;
        while (visStart < sortedVisible_.size()) {
            // Find group of consecutive entries with same modelId
            uint32_t groupModelId = sortedVisible_[visStart].modelId;
            size_t groupEnd = visStart;
            while (groupEnd < sortedVisible_.size() && sortedVisible_[groupEnd].modelId == groupModelId)
                groupEnd++;

            // Pull the model through the first entry's instance.cachedModel pointer
            // (set at addInstance) instead of doing models.find(groupModelId) per group.
            const auto& firstEntry = sortedVisible_[visStart];
            if (firstEntry.index >= instances.size() || !instances[firstEntry.index].cachedModel) {
                visStart = groupEnd;
                continue;
            }
            const M2ModelGPU& model = *instances[firstEntry.index].cachedModel;
            if (model.isInstancePortal) {
                for (size_t vi = visStart; vi < groupEnd; vi++) {
                    const auto& entry = sortedVisible_[vi];
                    if (entry.index >= instances.size()) continue;
                    appendInstancePortalGlow(instances[entry.index], entry.distSq);
                }
                // The swirl is drawn in pass 2 with the rest of the additive
                // work. Leaving the group here is not the substitution this used
                // to be: a portal is classified as a spell effect, and the
                // opaque gate below treats every batch of a spell effect as
                // transparent, so pass 1 has nothing of it to draw either way.
                visStart = groupEnd;
                continue;
            }
            if (!model.vertexBuffer || !model.indexBuffer) {
                visStart = groupEnd;
                continue;
            }

            bool modelNeedsAnimation = model.hasAnimation && !model.disableAnimation;
            const bool foliageLikeModel = model.isFoliageLike;
            // Same rule as pass 2 - see the note there for why a portal is not
            // one of these.
            const bool particleDominantEffect = model.isSpellEffect && !model.isInstancePortal &&
                !model.particleEmitters.empty() && model.batches.size() <= 2;

            // Collect per-instance data for this model group
            pending.clear();
            for (size_t vi = visStart; vi < groupEnd; vi++) {
                const auto& entry = sortedVisible_[vi];
                if (entry.index >= instances.size()) continue;
                auto& instance = instances[entry.index];

                // Distance-based fade alpha
                float fadeFrac = model.disableAnimation ? 0.55f : fadeStartFraction;
                float fadeStartDistSq = entry.effectiveMaxDistSq * fadeFrac * fadeFrac;
                float fadeAlpha = 1.0f;
                if (entry.distSq > fadeStartDistSq) {
                    fadeAlpha = std::clamp((entry.effectiveMaxDistSq - entry.distSq) /
                                          (entry.effectiveMaxDistSq - fadeStartDistSq), 0.0f, 1.0f);
                }
                float instanceFadeAlpha = fadeAlpha;
                if (model.isGroundDetail) instanceFadeAlpha *= 0.82f;

                // Bone readiness check
                if (modelNeedsAnimation && instance.boneMatrices.empty()) continue;
                bool needsBones = modelNeedsAnimation && !instance.boneMatrices.empty();
                if (needsBones && instance.megaBoneOffset == 0) continue;

                // LOD selection
                uint16_t desiredLOD = 0;
                if (entry.distSq > 150.0f * 150.0f) desiredLOD = 3;
                else if (entry.distSq > 80.0f * 80.0f) desiredLOD = 2;
                else if (entry.distSq > 40.0f * 40.0f) desiredLOD = 1;
                uint16_t targetLOD = desiredLOD;
                if (desiredLOD > 0 && !(model.availableLODs & (1u << desiredLOD))) targetLOD = 0;

                pending.push_back({.instanceIdx = entry.index, .fadeAlpha = instanceFadeAlpha, .useBones = needsBones, .targetLOD = targetLOD});
            }

            if (pending.empty()) { visStart = groupEnd; continue; }

            // Sort by targetLOD so each sub-group occupies a contiguous SSBO range
            std::sort(pending.begin(), pending.end(),
                      [](const PendingInstance& a, const PendingInstance& b) { return a.targetLOD < b.targetLOD; });

            // Bind vertex/index buffers once per model group
            VkDeviceSize vbOffset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &model.vertexBuffer, &vbOffset);
            vkCmdBindIndexBuffer(cmd, model.indexBuffer, 0, VK_INDEX_TYPE_UINT16);

            // Write base instance data to SSBO (uvOffset=0 - overridden for tex-anim batches)
            uint32_t baseSSBOOffset = instanceDataCount_;
            size_t writtenInstances = 0;
            for (const auto& p : pending) {
                if (instanceDataCount_ >= instanceCapacity_[frameIndex]) break;
                auto& inst = instances[p.instanceIdx];
                auto& e = instSSBO[instanceDataCount_];
                e.model = inst.modelMatrix;
                e.uvOffset = glm::vec2(0.0f);
                e.uvLinear = glm::vec4(1,0,0,1);
                e.fadeAlpha = p.fadeAlpha;
                e.useBones = (p.useBones && !kM2NoSkinning) ? 1 : 0;
                e.boneBase = p.useBones ? static_cast<int32_t>(inst.megaBoneOffset) : 0;
                e.boneCount = static_cast<int32_t>(inst.boneMatrices.size());
                std::memset(e._pad, 0, sizeof(e._pad));
                instanceDataCount_++;
                ++writtenInstances;
            }

            // Drop what did not fit. The loop above stops writing at the cap,
            // but the LOD sub-groups below are ranges over `pending` and were
            // still being drawn in full - groupSSBOOffset then runs past the
            // end of the buffer and the vertex shader reads instance data that
            // is not there. That is a real out-of-bounds read on the GPU, not a
            // missing model: it fires once per instance past the cap, hundreds
            // of times a frame, and the device is lost seconds later.
            //
            // Truncating is safe here precisely because `pending` was sorted by
            // LOD before the write: the sub-groups are contiguous and in the
            // same order, so cutting the tail cuts whole instances rather than
            // splitting a range.
            if (writtenInstances < pending.size()) {
                static bool warnedInstanceCap = false;
                if (!warnedInstanceCap) {
                    warnedInstanceCap = true;
                    LOG_WARNING("M2Renderer: instance buffer full at ", instanceCapacity_[frameIndex],
                                "; dropping ", pending.size() - writtenInstances,
                                " instances of '", model.name, "' this frame");
                }
                pending.resize(writtenInstances);
            }

            // Process LOD sub-groups within this model group
            size_t lodIdx = 0;
            while (lodIdx < pending.size()) {
                uint16_t lod = pending[lodIdx].targetLOD;
                size_t lodEnd = lodIdx + 1;
                while (lodEnd < pending.size() && pending[lodEnd].targetLOD == lod) lodEnd++;
                uint32_t groupSize = static_cast<uint32_t>(lodEnd - lodIdx);
                uint32_t groupSSBOOffset = baseSSBOOffset + static_cast<uint32_t>(lodIdx);

                for (size_t bi = 0; bi < model.batches.size(); bi++) {
                    const auto& batch = model.batches[bi];
                    if (batch.indexCount == 0) continue;
                    if (!model.isGroundDetail && !model.isInstancePortal && !model.isSpellEffect && batch.submeshLevel != lod) continue;
                    if (batch.batchOpacity < 0.01f) continue;
                    if (!skyBatchAllowed(skyMode_, bi)) continue;
                    if (suppressBakedStars_ && batch.starLayer) continue;
                    const bool batchUnlit = (batch.materialFlags & 0x01) != 0;
                    M2GlowCardBatch glowCard;
                    glowCard.glowSize = batch.glowSize;
                    glowCard.blendMode = batch.blendMode;
                    glowCard.lanternGlowHint = batch.lanternGlowHint;
                    glowCard.glowCardLike = batch.glowCardLike;
                    glowCard.colorKeyBlack = batch.colorKeyBlack;
                    glowCard.unlit = batchUnlit;
                    glowCard.preserveGlowMesh = batch.preserveGlowMesh;
                    glowCard.modelIsElvenLike = model.isElvenLike;
                    glowCard.modelIsLanternLike = model.isLanternLike;
                    glowCard.modelIsTorch = model.isTorch;
                    glowCard.modelIsBrazierOrFire = model.isBrazierOrFire;
                    glowCard.modelIsSpellEffect = model.isSpellEffect;
                    glowCard.modelIsKoboldFlame = model.isKoboldFlame;
                    const bool shouldUseGlowSprite = m2WantsGlowSprite(glowCard);
                    if (shouldUseGlowSprite) {
                        // Generate glow sprites for each instance in the group
                        for (size_t j = lodIdx; j < lodEnd; j++) {
                            auto& inst = instances[pending[j].instanceIdx];
                            glm::vec3 worldPos;
                            if (model.isGroundFire &&
                                !model.particleEmitters.empty()) {
                                worldPos = glm::vec3(std::numeric_limits<float>::max());
                                for (const auto& emitter : model.particleEmitters) {
                                    glm::mat4 boneXform(1.0f);
                                    if (emitter.bone < inst.boneMatrices.size()) {
                                        boneXform = inst.boneMatrices[emitter.bone];
                                    }
                                    const glm::vec3 emitterWorld = glm::vec3(
                                        inst.modelMatrix * boneXform * glm::vec4(emitter.position, 1.0f));
                                    if (emitterWorld.z < worldPos.z) worldPos = emitterWorld;
                                }
                            } else {
                                worldPos = animatedBatchWorldCenter(inst, batch);
                            }
                            // Preserved emissive glass writes opaque depth before
                            // this additive point sprite. Move only the visual
                            // halo just beyond the camera-facing glass surface so
                            // depth testing does not reject it; the associated
                            // local light remains at the true batch center.
                            if (batch.preserveGlowMesh) {
                                const glm::vec3 towardCamera = camPos - worldPos;
                                const float lenSq = glm::dot(towardCamera, towardCamera);
                                if (lenSq > 0.0001f) {
                                    worldPos += towardCamera * glm::inversesqrt(lenSq) *
                                        (batch.glowSize * inst.scale * 1.25f);
                                }
                            }
                            GlowSprite gs;
                            gs.worldPos = worldPos;
                            if (batch.glowTint == 1 || model.isElvenLike)
                                gs.color = glm::vec4(0.48f, 0.72f, 1.0f, 1.05f);
                            else if (batch.glowTint == 2)
                                gs.color = glm::vec4(1.0f, 0.28f, 0.22f, 1.10f);
                            else
                                gs.color = glm::vec4(1.0f, 0.82f, 0.46f, 1.15f);
                            // Match the parent M2's distance fade instead of a separate
                            // hard 180-unit cutoff, which made tunnel lights pop on.
                            gs.color.a *= pending[j].fadeAlpha;
                            gs.size = batch.glowSize * inst.scale *
                                (batch.preserveGlowMesh ? 2.0f : 1.45f);
                            if (batch.preserveGlowMesh) gs.color.a *= 1.25f;

                            // A fixture with real particle flames should read as
                            // flames with a halo behind them. The sprite is sized
                            // from its glow card's geometric radius, which on a
                            // chandelier spans the whole fixture - an additive
                            // blob about a unit across, against candle flames of
                            // 0.15, so the glow swallowed them entirely. Cap it
                            // just above what a small glow card already produces
                            // (the 0.5 floor times 1.45), so candles, lanterns
                            // and torches are untouched and only oversized cards
                            // are clamped.
                            if (!model.particleEmitters.empty() && model.isLanternLike) {
                                constexpr float kMaxHaloRadius = 0.75f;
                                gs.size = std::min(gs.size, kMaxHaloRadius * inst.scale);
                            }

                            // Fire burning inside a hearth. The sprite is a point
                            // billboard carrying one depth value for the whole
                            // quad, so as soon as its centre shows through the
                            // fireplace opening the entire square draws - brick
                            // surround included, which reads as the fire glowing
                            // through the masonry. Sized from the glow card's
                            // geometric radius these spheres are wider than the
                            // opening, so keep them inside it.
                            const bool hearthFire = model.isBrazierOrFire ||
                                                    model.isGroundFire ||
                                                    model.isForge;
                            if (hearthFire) {
                                constexpr float kMaxFireGlowRadius = 0.5f;
                                gs.size = std::min(gs.size, kMaxFireGlowRadius * inst.scale);
                            }

                            // Flame guttering. The phase comes from the lamp's own
                            // world position, so two lanterns on the same street
                            // never pulse together - a synchronised row of lamps
                            // reads as a rendering artifact, not firelight. Two
                            // detuned sines keep any single lamp from looping
                            // visibly. Alpha and size move together, since a
                            // brighter flame also looks slightly larger.
                            {
                                // Matches the phase used for this lamp's local
                                // light, so the sprite and the pool of light it
                                // casts breathe together.
                                // Same clock and parameters as the local light in
                                // gatherLocalLights, so the sprite and the pool of
                                // light it casts rise and fall together.
                                const float flicker = lampFlicker(
                                    inst.position, lampFlickerClockSeconds(),
                                    0.82f, 0.12f, 0.06f);
                                gs.color.a *= flicker;
                                gs.size    *= 0.98f + 0.02f * flicker;
                            }
                            glowSprites_.push_back(gs);
                            GlowSprite halo = gs;
                            halo.color.a *= batch.preserveGlowMesh ? 0.34f : 0.42f;
                            halo.size *= batch.preserveGlowMesh ? 2.2f : 1.8f;
                            // The halo is nearly twice the sprite, so capping only
                            // the sprite would leave the bleed to the halo.
                            if (hearthFire) {
                                constexpr float kMaxFireHaloRadius = 0.85f;
                                halo.size = std::min(halo.size, kMaxFireHaloRadius * inst.scale);
                            }
                            glowSprites_.push_back(halo);
                        }
                        if (m2GlowSpriteReplacesMesh(glowCard)) continue;
                    }

                    // Opaque gate - transparent glow cards were handled above so their
                    // sprites are generated before the mesh moves to pass 2.
                    const bool rawTransparent = (batch.blendMode >= 2) || model.isSpellEffect;
                    if (rawTransparent) continue;

                    // Particle-dominant effects: emission geometry - skip opaque
                    if (particleDominantEffect && batch.blendMode <= 1) continue;

                    // Handle texture animation: if this batch has per-instance uvOffset,
                    // write a separate SSBO range with the correct offsets.
                    bool hasBatchTexAnim = (batch.textureAnimIndex != 0xFFFF && model.hasTextureAnimation)
                                           || model.isLavaModel;
                    uint32_t drawOffset = groupSSBOOffset;
                    if (hasBatchTexAnim && instanceDataCount_ + groupSize <= instanceCapacity_[frameIndex]) {
                        drawOffset = instanceDataCount_;
                        // Hoist per-batch lookups: the transform pointer is fixed for
                        // every instance in this group; only the sampled translation
                        // varies (per-instance animTime).
                        const pipeline::M2TextureTransform* tt = nullptr;
                        if (batch.textureAnimIndex != 0xFFFF && model.hasTextureAnimation) {
                            uint16_t lookupIdx = batch.textureAnimIndex;
                            if (lookupIdx < model.textureTransformLookup.size()) {
                                uint16_t transformIdx = model.textureTransformLookup[lookupIdx];
                                if (transformIdx < model.textureTransforms.size()) {
                                    tt = &model.textureTransforms[transformIdx];
                                }
                            }
                        }
                        for (size_t j = lodIdx; j < lodEnd; j++) {
                            const auto& p = pending[j];
                            auto& inst = instances[p.instanceIdx];
                            const auto uv=sampleM2Uv(tt,inst.currentSequenceIndex,inst.animTime,inst.globalSequenceTime,model.globalSequenceDurations);
                            glm::vec2 uvOffset=uv.offset;
                            if (model.isLavaModel && uvOffset == glm::vec2(0.0f)) {
                                uvOffset = glm::vec2(lavaAnimSeconds * 0.03f,
                                                     -lavaAnimSeconds * 0.08f);
                            }
                            // Rebuild the entry from CPU-side data rather than copying it
                            // out of the base entry. instSSBO lives in write-combined
                            // upload memory: writing it is cheap, but reading it back is
                            // uncached and costs microseconds per instance.
                            auto& e = instSSBO[instanceDataCount_];
                            e.model = inst.modelMatrix;
                            e.uvOffset = uvOffset;
                            e.uvLinear = uv.linear;
                            e.fadeAlpha = p.fadeAlpha;
                            e.useBones = (p.useBones && !kM2NoSkinning) ? 1 : 0;
                            e.boneBase = p.useBones ? static_cast<int32_t>(inst.megaBoneOffset) : 0;
                            e.boneCount = static_cast<int32_t>(inst.boneMatrices.size());
                            std::memset(e._pad, 0, sizeof(e._pad));
                            instanceDataCount_++;
                        }
                    }

                    // Pipeline selection (per-model/batch, not per-instance)
                    const bool foliageCutout = foliageLikeModel && !model.isSpellEffect && batch.blendMode <= 3;
                    // The fire burning in the hearth is an effect overlay on a
                    // black background, the same shape as a spell visual; drawn
                    // opaque it fills the forge opening with a black rectangle
                    // instead of flame. That is true of the flame cards only -
                    // treating the whole model this way turned the masonry and
                    // ironwork additive, which is to say translucent.
                    const bool fireEffectModel = batch.forgeFireCard;
                    // A batch the artist marked additive is already doing
                    // what the cutout and the colour key are approximations
                    // of: black adds nothing, so it disappears on its own.
                    // Forcing it opaque and then keying the black out leaves
                    // the bright middle of a glow card as a solid disc, which
                    // is what Orgrimmar's bonfires were.
                    const bool forceCutout =
                        !model.isSpellEffect && !fireEffectModel &&
                        !m2BlendIsAdditive(batch.blendMode) &&
                        (model.isGroundDetail || foliageCutout ||
                         m2BatchNeedsAlphaTest(batch.blendMode, batch.hasAlpha) ||
                         batch.colorKeyBlack);

                    uint8_t effectiveBlendMode = batch.blendMode;
                    if (model.isSpellEffect || fireEffectModel) {
                        if (effectiveBlendMode <= 1) effectiveBlendMode = 3;
                        else if (effectiveBlendMode == 4 || effectiveBlendMode == 5) effectiveBlendMode = 3;
                    }
                    if (forceCutout) effectiveBlendMode = 1;

                    VkPipeline desiredPipeline;
                    if (forceCutout) {
                        desiredPipeline = opaquePipeline_;
                    } else {
                        switch (effectiveBlendMode) {
                            case 0: desiredPipeline = opaquePipeline_; break;
                            case 1: desiredPipeline = alphaTestPipeline_; break;
                            case 2: desiredPipeline = alphaPipeline_; break;
                            default: desiredPipeline = additivePipeline_; break;
                        }
                    }
                    if (desiredPipeline != currentPipeline) {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, desiredPipeline);
                        currentPipeline = desiredPipeline;
                    }

                    // Update material UBO
                    if (batch.materialUBOMapped) {
                        auto* mat = static_cast<M2MaterialUBO*>(batch.materialUBOMapped);
                        // interiorDarken is a camera-based flag - it darkens ALL M2s (incl.
                        // outdoor trees) when the camera is inside a WMO.  Disable it; indoor
                        // M2s already look correct from the darker ambient/lighting.
                        mat->interiorDarken = 0.0f;
                        if (batch.colorKeyBlack)
                            mat->colorKeyThreshold = (effectiveBlendMode == 4 || effectiveBlendMode == 5) ? 0.7f : 0.08f;
                        if (forceCutout) {
                            mat->alphaTest = model.isGroundDetail ? 3 : (foliageCutout ? 2 : 1);
                            if (model.isGroundDetail) mat->unlit = 0;
                        }
                    }

                    // Bind material descriptor set (set 1)
                    if (!batch.materialSet) continue;
                    if (batch.materialSet != currentMaterialSet) {
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayout_, 1, 1, &batch.materialSet, 0, nullptr);
                        currentMaterialSet = batch.materialSet;
                    }

                    // Push constants + instanced draw
                    M2PushConstants pc;
                    pc.texCoordSet = static_cast<int32_t>(batch.textureUnit);
                    fillSway(pc, model, skyMode_);
                    pc.instanceDataOffset = static_cast<int32_t>(drawOffset);
                    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
                    if (traceFirstRender && lastDrawCallCount < 8)
                        LOG_INFO("M2 first draw ", lastDrawCallCount, " model=", model.name,
                                 " indices=", batch.indexCount, " instances=", groupSize,
                                 " ssboOffset=", drawOffset);
                    vkCmdDrawIndexed(cmd, batch.indexCount, groupSize, batch.indexStart, 0, 0);
                    if (skyMode_) ++skyDiagDrawsOpaque_;
                    lastDrawCallCount++;
                }

                lodIdx = lodEnd;
            }

            visStart = groupEnd;
        }
    }

    // =====================================================================
    // Pass 2: Transparent/additive batches - back-to-front per instance
    // =====================================================================
    // Transparent geometry must be drawn individually per instance in back-to-
    // front order for correct alpha compositing.  Each draw writes one
    // M2InstanceGPU entry and issues a single-instance indexed draw.
    std::sort(transparentVisible_.begin(), transparentVisible_.end(),
              [](const VisibleEntry& a, const VisibleEntry& b) { return a.distSq > b.distSq; });

    firstFrameStage("M2 first frame: opaque draws complete");
    currentModelId = UINT32_MAX;
    currentModel = nullptr;
    currentModelValid = false;
    currentPipeline = opaquePipeline_;
    currentMaterialSet = VK_NULL_HANDLE;

    for (const auto& entry : transparentVisible_) {
        if (entry.index >= instances.size()) continue;
        auto& instance = instances[entry.index];

        // Model boundary: read cachedModel off the instance - was doing a
        // per-boundary models.find() even though every instance already has
        // the pointer cached at addInstance time.
        if (entry.modelId != currentModelId) {
            currentModelId = entry.modelId;
            currentModelValid = false;
            currentModel = instance.cachedModel;
            if (!currentModel) continue;
            // A portal was skipped here as well as in pass 1, which is why
            // removing only the particle exclusion did not bring the swirl
            // back: this is the pass that would have drawn it.
            if (!currentModel->hasTransparentBatches && !currentModel->isSpellEffect) continue;
            if (!currentModel->vertexBuffer || !currentModel->indexBuffer) continue;
            currentModelValid = true;
            VkDeviceSize vbOff = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &currentModel->vertexBuffer, &vbOff);
            vkCmdBindIndexBuffer(cmd, currentModel->indexBuffer, 0, VK_INDEX_TYPE_UINT16);
        }
        if (!currentModelValid) continue;

        const M2ModelGPU& model = *currentModel;

        // Fade alpha
        float fadeAlpha = 1.0f;
        float fadeFrac = model.disableAnimation ? 0.55f : fadeStartFraction;
        float fadeStartDistSq = entry.effectiveMaxDistSq * fadeFrac * fadeFrac;
        if (entry.distSq > fadeStartDistSq) {
            fadeAlpha = std::clamp((entry.effectiveMaxDistSq - entry.distSq) /
                                  (entry.effectiveMaxDistSq - fadeStartDistSq), 0.0f, 1.0f);
        }
        float instanceFadeAlpha = fadeAlpha;
        if (model.isGroundDetail) instanceFadeAlpha *= 0.82f;

        bool modelNeedsAnimation = model.hasAnimation && !model.disableAnimation;
        if (modelNeedsAnimation && instance.boneMatrices.empty()) continue;
        bool needsBones = modelNeedsAnimation && !instance.boneMatrices.empty();
        if (needsBones && instance.megaBoneOffset == 0) continue;

        uint16_t desiredLOD = 0;
        if (entry.distSq > 150.0f * 150.0f) desiredLOD = 3;
        else if (entry.distSq > 80.0f * 80.0f) desiredLOD = 2;
        else if (entry.distSq > 40.0f * 40.0f) desiredLOD = 1;
        uint16_t targetLOD = desiredLOD;
        if (desiredLOD > 0 && !(model.availableLODs & (1u << desiredLOD))) targetLOD = 0;

        // A spell effect whose geometry is one or two quads carrying emitters is
        // emission scaffolding and the particles are the effect - except for a
        // portal, where the one or two batches ARE the swirl and the particles
        // are the sparks around it. Dropping its mesh here is the second way the
        // portal loses its geometry, and the one that survives removing the
        // isInstancePortal skips above.
        const bool particleDominantEffect = model.isSpellEffect && !model.isInstancePortal &&
            !model.particleEmitters.empty() && model.batches.size() <= 2;

        for (const auto& batch : model.batches) {
            if (batch.indexCount == 0) continue;
            if (!model.isGroundDetail && !model.isInstancePortal && !model.isSpellEffect && batch.submeshLevel != targetLOD) continue;
            if (batch.batchOpacity < 0.01f) continue;
            if (!skyBatchAllowed(skyMode_, static_cast<std::size_t>(&batch - model.batches.data()))) continue;

            // Pass 2 gate: only transparent/additive batches
            {
                const bool rawTransparent = (batch.blendMode >= 2) || model.isSpellEffect;
                if (!rawTransparent) continue;
            }

            // WOWEE_SKY_M2_SKIP_BLEND=<n> drops the sky's layers of one blend
            // mode. Its thirty-four batches are six opaque, ten alpha-blended
            // (2) and eighteen additive (4), and the two behave differently in
            // a way that matters: additive is order-independent and alpha
            // blending is not. Dropping ten or dropping eighteen says which
            // half the flicker lives in, the same way NO_SKY_M2 and
            // OPAQUE_ONLY narrowed it to the blended layers at all.
            if (skyMode_) {
                static const int skipBlend = [] {
                    const char* set = std::getenv("WOWEE_SKY_M2_SKIP_BLEND");
                    return set ? std::atoi(set) : -1;
                }();
                if (skipBlend >= 0 && batch.blendMode == skipBlend) continue;
            }

            // Skip glow sprites (handled in opaque pass)
            const bool batchUnlit = (batch.materialFlags & 0x01) != 0;
            M2GlowCardBatch glowCard;
            glowCard.glowSize = batch.glowSize;
            glowCard.blendMode = batch.blendMode;
            glowCard.lanternGlowHint = batch.lanternGlowHint;
            glowCard.glowCardLike = batch.glowCardLike;
            glowCard.colorKeyBlack = batch.colorKeyBlack;
            glowCard.unlit = batchUnlit;
            glowCard.preserveGlowMesh = batch.preserveGlowMesh;
            glowCard.modelIsElvenLike = model.isElvenLike;
            glowCard.modelIsLanternLike = model.isLanternLike;
            glowCard.modelIsTorch = model.isTorch;
            glowCard.modelIsBrazierOrFire = model.isBrazierOrFire;
            glowCard.modelIsSpellEffect = model.isSpellEffect;
            glowCard.modelIsKoboldFlame = model.isKoboldFlame;
            if (m2WantsGlowSprite(glowCard) && m2GlowSpriteReplacesMesh(glowCard)) {
                continue;
            }

            if (particleDominantEffect) continue; // emission-only mesh

            // Compute UV offset for this instance + batch
            //
            // WOWEE_SKY_M2_NO_TEXANIM=1 freezes the sky's scrolling. Nine of
            // its eighteen additive layers carry a texture animation, and
            // those offsets are the one thing about those layers that changes
            // between frames at all - so if the flicker survives freezing them
            // it is not in what the layers sample, it is in the geometry they
            // are drawn on.
            static const bool skyNoTexAnim =
                std::getenv("WOWEE_SKY_M2_NO_TEXANIM") != nullptr;
            M2UvTransform uv;
            glm::vec2 uvOffset(0.0f);
            if (batch.textureAnimIndex != 0xFFFF && model.hasTextureAnimation &&
                !(skyMode_ && skyNoTexAnim)) {
                uint16_t lookupIdx = batch.textureAnimIndex;
                if (lookupIdx < model.textureTransformLookup.size()) {
                    uint16_t transformIdx = model.textureTransformLookup[lookupIdx];
                    if (transformIdx < model.textureTransforms.size()) {
                        const auto& tt = model.textureTransforms[transformIdx];
                        uv=sampleM2Uv(&tt,instance.currentSequenceIndex,instance.animTime,
                            instance.globalSequenceTime,model.globalSequenceDurations);
                        uvOffset=uv.offset;
                    }
                }
            }
            if (model.isLavaModel && uvOffset == glm::vec2(0.0f)) {
                uvOffset = glm::vec2(lavaAnimSeconds * 0.03f,
                                     -lavaAnimSeconds * 0.08f);
            }

            // Write single instance entry to SSBO
            if (instanceDataCount_ >= instanceCapacity_[frameIndex]) continue;
            uint32_t drawOffset = instanceDataCount_;
            auto& e = instSSBO[instanceDataCount_];
            e.model = instance.modelMatrix;
            e.uvOffset = uvOffset;
            e.uvLinear = uv.linear;
            e.fadeAlpha = instanceFadeAlpha;
            e.useBones = (needsBones && !kM2NoSkinning) ? 1 : 0;
            e.boneBase = needsBones ? static_cast<int32_t>(instance.megaBoneOffset) : 0;
            e.boneCount = static_cast<int32_t>(instance.boneMatrices.size());
            std::memset(e._pad, 0, sizeof(e._pad));
            instanceDataCount_++;

            // Pipeline selection
            uint8_t effectiveBlendMode = batch.blendMode;
            if (model.isSpellEffect || batch.forgeFireCard) {
                // Matches the opaque pass: a forge's flame cards are additive,
                // the forge itself is not.
                if (effectiveBlendMode <= 1) effectiveBlendMode = 3;
                else if (effectiveBlendMode == 4 || effectiveBlendMode == 5) effectiveBlendMode = 3;
            }

            VkPipeline desiredPipeline;
            switch (effectiveBlendMode) {
                case 2: desiredPipeline = alphaPipeline_; break;
                default: desiredPipeline = additivePipeline_; break;
            }
            if (desiredPipeline != currentPipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, desiredPipeline);
                currentPipeline = desiredPipeline;
            }

            if (batch.materialUBOMapped) {
                auto* mat = static_cast<M2MaterialUBO*>(batch.materialUBOMapped);
                mat->interiorDarken = 0.0f;
                if (batch.colorKeyBlack)
                    mat->colorKeyThreshold = (effectiveBlendMode == 4 || effectiveBlendMode == 5) ? 0.7f : 0.08f;

                // A fire's own glow breathes. The same clock and parameters as
                // the lamp sprites and the local light they cast, so a brazier
                // and the pool of light under it rise and fall together
                // instead of beating against each other.
                //
                // Never for a sky. lampFlicker is keyed on the instance
                // position and says a seed that drifts re-rolls its phase every
                // frame and strobes; a sky dome's position IS the camera's,
                // rewritten every frame, so it is the one instance that can
                // never be a valid seed. The classifier no longer calls
                // HellfireSkyBox a brazier, and this makes sure the next model
                // that gets miscalled one cannot strobe the sky either.
                if (!skyMode_ && m2BlendIsAdditive(batch.blendMode) &&
                    (model.isBrazierOrFire || model.isTorch || model.isLanternLike)) {
                    const float flicker = lampFlicker(
                        instance.position, lampFlickerClockSeconds(),
                        0.82f, 0.12f, 0.06f);
                    mat->tintR = batch.tint.r * flicker;
                    mat->tintG = batch.tint.g * flicker;
                    mat->tintB = batch.tint.b * flicker;
                }
            }

            if (!batch.materialSet) continue;
            if (batch.materialSet != currentMaterialSet) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout_, 1, 1, &batch.materialSet, 0, nullptr);
                currentMaterialSet = batch.materialSet;
            }

            // Push constants + single-instance draw
            M2PushConstants pc;
            pc.texCoordSet = static_cast<int32_t>(batch.textureUnit);
            fillSway(pc, model, skyMode_);
            pc.instanceDataOffset = static_cast<int32_t>(drawOffset);
            vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cmd, batch.indexCount, 1, batch.indexStart, 0, 0);
            if (skyMode_) ++skyDiagDrawsTransparent_;
            lastDrawCallCount++;
        }
    }

    // Render glow sprites as billboarded additive point lights
    if (!glowSprites_.empty() && particleAdditivePipeline_ && glowVB_ && glowTexDescSet_) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particleAdditivePipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                particlePipelineLayout_, 0, 1, &perFrameSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                particlePipelineLayout_, 1, 1, &glowTexDescSet_, 0, nullptr);

        // Push constants for particle: tileCount(vec2) + alphaKey(int)
        struct { float tileX, tileY; int alphaKey; } particlePush = {.tileX = 1.0f, .tileY = 1.0f, .alphaKey = 0};
        vkCmdPushConstants(cmd, particlePipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(particlePush), &particlePush);

        // Write glow vertex data directly to mapped buffer (no temp vector)
        size_t uploadCount = std::min(glowSprites_.size(), MAX_GLOW_SPRITES);
        float* dst = static_cast<float*>(glowVBMapped_);
        for (size_t gi = 0; gi < uploadCount; gi++) {
            const auto& gs = glowSprites_[gi];
            *dst++ = gs.worldPos.x;
            *dst++ = gs.worldPos.y;
            *dst++ = gs.worldPos.z;
            *dst++ = gs.color.r;
            *dst++ = gs.color.g;
            *dst++ = gs.color.b;
            *dst++ = gs.color.a;
            *dst++ = gs.size;
            *dst++ = 0.0f;
        }

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &glowVB_, &offset);
        vkCmdDraw(cmd, static_cast<uint32_t>(uploadCount), 1, 0, 0);
    }

    // How many of the sky's layers were actually drawn this frame.
    //
    // The instance-level report says DRAWN every frame, which is a different
    // question: it counts instances that survived culling, not batches that
    // reached a draw call. Every per-batch gate between the two is meant to be
    // constant here - the material flags are static, the LOD is chosen by a
    // distance that does not change for a dome centred on the camera, and the
    // batch opacity is baked at load - so this number should never move. If it
    // moves while the camera turns, the flicker is layers appearing and
    // disappearing and one of those gates is not as constant as it reads.
    if (skyMode_ && (skyDiagDrawsOpaque_ != skyDiagLastOpaque_ ||
                     skyDiagDrawsTransparent_ != skyDiagLastTransparent_)) {
        LOG_INFO("skyM2 draws: opaque=", skyDiagDrawsOpaque_,
                 " transparent=", skyDiagDrawsTransparent_,
                 " (was ", skyDiagLastOpaque_, "/", skyDiagLastTransparent_, ")");
        skyDiagLastOpaque_ = skyDiagDrawsOpaque_;
        skyDiagLastTransparent_ = skyDiagDrawsTransparent_;
    }
    firstFrameStage("M2 first frame: all draws complete");
}

bool M2Renderer::initializeShadow(VkRenderPass shadowRenderPass) {
    if (!vkCtx_ || shadowRenderPass == VK_NULL_HANDLE) return false;
    VkDevice device = vkCtx_->getDevice();

    // The set the shadow pass binds, built the same way for all four renderers.
    if (!createShadowParamsSet(device, vkCtx_->getAllocator(), sizeof(ShadowParamsUBO),
                               whiteTexture_->getImageView(),
                              whiteTexture_->getSampler(), "M2Renderer", shadowParams_)) {
        return false;
    }

    // Rigid casters never change parameters. Foliage parameters are frame-local:
    // overwriting one UBO between draws changes *all* already recorded draws.
    ShadowParamsUBO rigid{};
    rigid.foliageMotionDamp = 1.0f;
    VmaAllocationInfo rigidInfo{};
    vmaGetAllocationInfo(vkCtx_->getAllocator(), shadowParams_.alloc, &rigidInfo);
    std::memcpy(rigidInfo.pMappedData, &rigid, sizeof(rigid));
    for (auto& foliage : shadowFoliageParams_) {
        if (!createShadowParamsSet(device, vkCtx_->getAllocator(), sizeof(ShadowParamsUBO),
                                   whiteTexture_->getImageView(), whiteTexture_->getSampler(),
                                   "M2 foliage shadow", foliage)) return false;
    }

    // Per-frame pools for foliage shadow texture sets (one per frame-in-flight, reused after its fence)
    {
        VkDescriptorPoolSize texPoolSizes[2]{};
        texPoolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texPoolSizes[0].descriptorCount = 256;
        texPoolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        texPoolSizes[1].descriptorCount = 256;
        VkDescriptorPoolCreateInfo texPoolCI{};
        texPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        texPoolCI.maxSets = 256;
        texPoolCI.poolSizeCount = 2;
        texPoolCI.pPoolSizes = texPoolSizes;
        for (uint32_t f = 0; f < kShadowTexPoolFrames; ++f) {
            if (vkCreateDescriptorPool(device, &texPoolCI, nullptr, &shadowTexPool_[f]) != VK_SUCCESS) {
                LOG_ERROR("M2Renderer: failed to create shadow texture pool ", f);
                return false;
            }
        }
    }

    // Create shadow pipeline layout: set 1 = shadowParams_.layout, push constants = 128 bytes
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc.offset = 0;
    pc.size = 128;  // lightSpaceMatrix (64) + model (64)
    shadowPipelineLayout_ = createPipelineLayout(device, {shadowParams_.layout}, {pc});
    if (!shadowPipelineLayout_) {
        LOG_ERROR("M2Renderer: failed to create shadow pipeline layout");
        return false;
    }

    // Load shadow shaders
    VkShaderModule vertShader, fragShader;
    if (!vertShader.loadFromFile(device, "assets/shaders/shadow.vert.spv")) {
        LOG_ERROR("M2Renderer: failed to load shadow vertex shader");
        return false;
    }
    if (!fragShader.loadFromFile(device, "assets/shaders/shadow.frag.spv")) {
        LOG_ERROR("M2Renderer: failed to load shadow fragment shader");
        return false;
    }

    // M2 vertex layout: 18 floats = 72 bytes stride
    // loc0=pos(off0), loc1=normal(off12), loc2=texCoord0(off24), loc5=texCoord1(off32),
    // loc3=boneWeights(off40), loc4=boneIndices(off56)
    // Shadow shader locations: 0=aPos, 1=aTexCoord, 2=aBoneWeights, 3=aBoneIndicesF
    // useBones=0 so locations 2,3 are never used
    VkVertexInputBindingDescription vertBind{};
    vertBind.binding = 0;
    vertBind.stride = 20 * sizeof(float);   // the model VBO's layout, see M2Renderer::loadModel
    vertBind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::vector<VkVertexInputAttributeDescription> vertAttrs = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,    .offset = 0},                     // aPos       -> position
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,       .offset = 6 * sizeof(float)},     // aTexCoord  -> texCoord0
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 12 * sizeof(float)},    // aBoneWeights
        {.location = 3, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 16 * sizeof(float)},    // aBoneIndicesF
    };

    shadowPipeline_ = buildShadowPipeline(
        device, vkCtx_->getPipelineCache(),
        vertShader.stageInfo(VK_SHADER_STAGE_VERTEX_BIT),
        fragShader.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT),
        vertBind, vertAttrs, shadowPipelineLayout_, shadowRenderPass);

    vertShader.destroy();
    fragShader.destroy();

    if (!shadowPipeline_) {
        LOG_ERROR("M2Renderer: failed to create shadow pipeline");
        return false;
    }
    if (!initializeShadowInstancing(shadowRenderPass, vertBind, vertAttrs)) {
        destroyShadowInstancing();
        LOG_WARNING("M2 shadow instancing unavailable; retaining all casters with individual draws");
    }
    LOG_INFO("M2Renderer shadow pipeline initialized");
    return true;
}

void M2Renderer::destroyShadowInstancing() {
    if (!vkCtx_) return;
    const VkDevice device = vkCtx_->getDevice();
    if (shadowInstancedPipeline_) vkDestroyPipeline(device, shadowInstancedPipeline_, nullptr);
    shadowInstancedPipeline_ = VK_NULL_HANDLE;
    if (shadowInstancedLayout_) vkDestroyPipelineLayout(device, shadowInstancedLayout_, nullptr);
    shadowInstancedLayout_ = VK_NULL_HANDLE;
    if (shadowInstancePool_) vkDestroyDescriptorPool(device, shadowInstancePool_, nullptr);
    shadowInstancePool_ = VK_NULL_HANDLE;
    if (shadowInstanceSetLayout_) vkDestroyDescriptorSetLayout(device, shadowInstanceSetLayout_, nullptr);
    shadowInstanceSetLayout_ = VK_NULL_HANDLE;
    for (uint32_t frame = 0; frame < 2; ++frame) {
        if (shadowInstanceBuffer_[frame] || shadowInstanceAlloc_[frame])
            vmaDestroyBuffer(vkCtx_->getAllocator(), shadowInstanceBuffer_[frame], shadowInstanceAlloc_[frame]);
        shadowInstanceBuffer_[frame] = VK_NULL_HANDLE;
        shadowInstanceAlloc_[frame] = VK_NULL_HANDLE;
        shadowInstanceMapped_[frame] = nullptr;
        shadowInstanceSet_[frame] = VK_NULL_HANDLE;
    }
}

bool M2Renderer::initializeShadowInstancing(VkRenderPass renderPass,
        const VkVertexInputBindingDescription& binding,
        const std::vector<VkVertexInputAttributeDescription>& attributes) {
    const VkDevice device = vkCtx_->getDevice();
    VkDescriptorSetLayoutBinding matrixBinding{};
    matrixBinding.binding = 0;
    matrixBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    matrixBinding.descriptorCount = 1;
    matrixBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    shadowInstanceSetLayout_ = createDescriptorSetLayout(device, {matrixBinding});
    if (!shadowInstanceSetLayout_) return false;
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
    VkDescriptorPoolCreateInfo pool{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 2;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    if (vkCreateDescriptorPool(device, &pool, nullptr, &shadowInstancePool_) != VK_SUCCESS) return false;
    constexpr VkDeviceSize bytes = MAX_SHADOW_INSTANCE_DATA * sizeof(glm::mat4);
    static_assert(sizeof(glm::mat4) == 64, "Shadow matrix must match std430 mat4");
    for (uint32_t frame = 0; frame < 2; ++frame) {
        VkBufferCreateInfo buffer{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size = bytes;
        buffer.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VmaAllocationCreateInfo memory{};
        memory.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        memory.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        if (vmaCreateBuffer(vkCtx_->getAllocator(), &buffer, &memory,
                &shadowInstanceBuffer_[frame], &shadowInstanceAlloc_[frame], &info) != VK_SUCCESS)
            return false;
        shadowInstanceMapped_[frame] = info.pMappedData;
        if (!shadowInstanceMapped_[frame]) return false;
        VkDescriptorSetAllocateInfo alloc{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = shadowInstancePool_;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &shadowInstanceSetLayout_;
        if (vkAllocateDescriptorSets(device, &alloc, &shadowInstanceSet_[frame]) != VK_SUCCESS) return false;
        VkDescriptorBufferInfo storage{shadowInstanceBuffer_[frame], 0, bytes};
        VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = shadowInstanceSet_[frame];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &storage;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.size = 128;
    shadowInstancedLayout_ = createPipelineLayout(device,
        {shadowParams_.layout, shadowInstanceSetLayout_}, {push});
    if (!shadowInstancedLayout_) return false;
    VkShaderModule vertex, fragment;
    if (!vertex.loadFromFile(device, "assets/shaders/shadow_instanced.vert.spv") ||
        !fragment.loadFromFile(device, "assets/shaders/shadow.frag.spv")) return false;
    shadowInstancedPipeline_ = buildShadowPipeline(device, vkCtx_->getPipelineCache(),
        vertex.stageInfo(VK_SHADER_STAGE_VERTEX_BIT), fragment.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT),
        binding, attributes, shadowInstancedLayout_, renderPass);
    vertex.destroy();
    fragment.destroy();
    if (!shadowInstancedPipeline_) return false;
    LOG_INFO("[SHADOW_INSTANCING] M2 capacity=", MAX_SHADOW_INSTANCE_DATA,
             " bytesPerFrame=", bytes, " frames=2 sameCasterCoverage=1");
    return true;
}

void M2Renderer::renderShadow(VkCommandBuffer cmd, const glm::mat4& lightSpaceMatrix, float globalTime,
                              const glm::vec3& /*shadowCenter*/, float shadowRadius) {
    if (!shadowPipeline_ || !shadowParams_.set) return;
    if (instances.empty() || models.empty()) return;

    const uint32_t frameIdx = vkCtx_->getCurrentFrame();
    if (frameIdx >= kShadowTexPoolFrames || !shadowFoliageParams_[frameIdx].set) return;
    const auto& foliageSet = shadowFoliageParams_[frameIdx];
    shadowTextureCache_.beginFrameSlot(frameIdx);
    auto getTexDescSet = [&](VkTexture* tex) -> VkDescriptorSet {
        if (!tex || !tex->isValid()) return foliageSet.set;
        return shadowTextureCache_.get(vkCtx_->getDevice(), frameIdx,
            shadowTexPool_[frameIdx], shadowParams_.layout, foliageSet.ubo,
            sizeof(ShadowParamsUBO), tex->getImageView(), tex->getSampler(),
            foliageSet.set);
    };
    if ((++shadowPerfFrames_ % 300u) == 1u) {
        LOG_INFO("[SHADOW_CACHE] M2 allocations=", shadowTextureCache_.allocations(),
                 " reused=", shadowTextureCache_.reuses(),
                 " hits=", shadowTextureCache_.hits(),
                 " fallback=", shadowTextureCache_.fallbacks(), " poolReset=0");
    }

    ShadowParamsUBO foliage{};
    foliage.foliageSway = 1;
    foliage.windTime = globalTime;
    foliage.foliageMotionDamp = 1.0f;
    foliage.useTexture = 1;
    foliage.alphaTest = 1;
    VmaAllocationInfo foliageInfo{};
    vmaGetAllocationInfo(vkCtx_->getAllocator(), foliageSet.alloc, &foliageInfo);
    std::memcpy(foliageInfo.pMappedData, &foliage, sizeof(foliage));

    shadowCasters_.clear();
    for (const auto& instance : instances) {
            // Use cached flags to skip early without hash lookup
            if (!instance.cachedIsValid || instance.cachedIsSmoke || instance.cachedIsInvisibleTrap) continue;

            if (!instance.cachedModel) continue;
            const M2ModelGPU& model = *instance.cachedModel;

            // Cull casters against the light-space ortho footprint, not a
            // world-space sphere around the player. The shadow frustum extends
            // ~2000 units toward the sun, so a distant tree can legitimately
            // cast across the whole view while sitting far outside any player
            // sphere - sphere culling made such shadows pop on/off at the cull
            // boundary as the player moved (large-area flicker at low sun).
            {
                const glm::vec4 clip = lightSpaceMatrix * glm::vec4(instance.position, 1.0f);
                // Orthographic projection: w == 1, NDC directly comparable.
                // Inflate by the model's bounding sphere converted to NDC
                // (shadowRadius ≈ frustum half-extent; overshoot is harmless).
                const float margin = (model.boundRadius * instance.scale) / shadowRadius * 1.5f;
                if (std::abs(clip.x) > 1.0f + margin || std::abs(clip.y) > 1.0f + margin) continue;
                if (clip.z < -margin || clip.z > 1.0f + margin) continue;
            }

        shadowCasters_.push_back(&instance);
    }
    std::sort(shadowCasters_.begin(), shadowCasters_.end(), [](auto* a, auto* b) { return a->modelId < b->modelId; });

    // The buffer is separate from the main pass, which resets and rewrites its
    // instance SSBO later in this same command buffer. This slot's fence was
    // waited in beginFrame; no recorded draw is allowed to observe a rewrite.
    bool useInstancing = shadowInstancedPipeline_ && shadowInstanceMapped_[frameIdx] &&
        shadowInstanceSet_[frameIdx] && shadowInstanceStorageFits(shadowCasters_.size(), MAX_SHADOW_INSTANCE_DATA);
    if (useInstancing) {
        auto* matrices = static_cast<glm::mat4*>(shadowInstanceMapped_[frameIdx]);
        for (size_t i = 0; i < shadowCasters_.size(); ++i) matrices[i] = shadowCasters_[i]->modelMatrix;
        useInstancing = vmaFlushAllocation(vkCtx_->getAllocator(), shadowInstanceAlloc_[frameIdx], 0,
                           shadowCasters_.size() * sizeof(glm::mat4)) == VK_SUCCESS;
    }
    if (useInstancing) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowInstancedPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowInstancedLayout_,
            1, 1, &shadowInstanceSet_[frameIdx], 0, nullptr);
        struct InstancedPush { glm::mat4 lightSpaceMatrix; uint32_t instanceDataOffset; };
        static_assert(offsetof(InstancedPush, instanceDataOffset) == 64);
        uint32_t draws = 0, individualDraws = 0;
        VkDescriptorSet currentSet = VK_NULL_HANDLE;
        for (size_t begin = 0; begin < shadowCasters_.size();) {
            const size_t end = shadowInstanceGroupEnd(begin, shadowCasters_.size(),
                [&](size_t index) { return shadowCasters_[index]->modelId; });
            const auto& model = *shadowCasters_[begin]->cachedModel;
            const uint32_t count = static_cast<uint32_t>(end - begin);
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &model.vertexBuffer, &offset);
            vkCmdBindIndexBuffer(cmd, model.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
            InstancedPush push{lightSpaceMatrix, static_cast<uint32_t>(begin)};
            vkCmdPushConstants(cmd, shadowInstancedLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(push), &push);
            for (const auto& batch : model.shadowBatches) {
                VkDescriptorSet set = model.shadowWindFoliage
                    ? (batch.texture ? getTexDescSet(batch.texture) : foliageSet.set)
                    : shadowParams_.set;
                if (set != currentSet) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowInstancedLayout_,
                        0, 1, &set, 0, nullptr);
                    currentSet = set;
                }
                for (const auto& range : batch.ranges) {
                    vkCmdDrawIndexed(cmd, range.indexCount, count, range.firstIndex, 0, 0);
                    ++draws;
                    individualDraws += count;
                }
            }
            begin = end;
        }
        if (shadowPerfFrames_ % 300u == 1u)
            LOG_INFO("[SHADOW_SUBMIT] M2 casters=", shadowCasters_.size(), " draws=", draws,
                     " uninstanced=", individualDraws, " instanced=1 matrixBytes=",
                     shadowCasters_.size() * sizeof(glm::mat4));
        return;
    }

    uint32_t mergedDraws=0, originalDraws=0;
    auto drawPass = [&](bool foliagePass) {
        VkDescriptorSet baseSet = foliagePass ? foliageSet.set : shadowParams_.set;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout_,
            0, 1, &baseSet, 0, nullptr);
        VkDescriptorSet currentTexSet = baseSet;
        uint32_t currentModelId = UINT32_MAX;
        const M2ModelGPU* currentModel = nullptr;

        for (const auto* caster : shadowCasters_) {
            const auto& instance = *caster;
            const auto& model = *instance.cachedModel;
            if (model.shadowWindFoliage != foliagePass) continue;
            // Bind vertex/index buffers when model changes
            if (instance.modelId != currentModelId) {
                currentModelId = instance.modelId;
                currentModel = &model;
                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &currentModel->vertexBuffer, &offset);
                vkCmdBindIndexBuffer(cmd, currentModel->indexBuffer, 0, VK_INDEX_TYPE_UINT16);
            }

            ShadowPush push{.lightSpaceMatrix = lightSpaceMatrix, .model = instance.modelMatrix};
            vkCmdPushConstants(cmd, shadowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                               0, 128, &push);

            if (shadowPerfFrames_ % 300u == 1u)
                for (const auto& b : model.batches) if (b.submeshLevel == 0 && b.indexCount) ++originalDraws;
            for (const auto& batch : model.shadowBatches) {
                // For foliage: bind per-batch texture for alpha-tested shadows
                if (foliagePass) {
                    VkDescriptorSet texSet = batch.texture
                        ? getTexDescSet(batch.texture) : foliageSet.set;
                    if (texSet != currentTexSet) {
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            shadowPipelineLayout_, 0, 1, &texSet, 0, nullptr);
                        currentTexSet = texSet;
                    }
                }
                for (const auto& range : batch.ranges) {
                    vkCmdDrawIndexed(cmd, range.indexCount, 1, range.firstIndex, 0, 0);
                    ++mergedDraws;
                }
            }
        }
    };

    // Pass 1: non-foliage (no wind displacement)
    drawPass(false);
    // Pass 2: foliage (wind displacement enabled, per-batch alpha-tested textures)
    drawPass(true);
    if (shadowPerfFrames_ % 300u == 1u)
        LOG_INFO("[SHADOW_SUBMIT] M2 casters=", shadowCasters_.size(), " draws=", mergedDraws, " unmerged=", originalDraws, " instanced=0");
}

} // namespace rendering
} // namespace wowee
