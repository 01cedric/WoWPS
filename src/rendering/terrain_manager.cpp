#include <string_view>
#include "rendering/terrain_manager.hpp"
#ifdef WOWEE_PS4
#include "platform/ps4/cpu_memory.hpp"
#endif

#include <vector>

#include "pipeline/adt_alpha.hpp"
#include "pipeline/grass_profile.hpp"
#include "rendering/terrain_renderer.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/water_renderer.hpp"
#include "rendering/m2_renderer.hpp"
#include "rendering/m2_model_classifier.hpp"
#include "rendering/wmo_renderer.hpp"
#include "rendering/camera.hpp"
#include "audio/ambient_sound_manager.hpp"
#include "core/coordinates.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include "pipeline/wowee_model.hpp"
#include "pipeline/wowee_building.hpp"
#include "pipeline/wowee_collision.hpp"
#include "core/memory_monitor.hpp"
#include "core/profiler.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/adt_loader.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/wmo_loader.hpp"
#include "pipeline/wmo_group_path.hpp"
#include "pipeline/terrain_mesh.hpp"
#include "core/logger.hpp"
#include "core/config_paths.hpp"
#ifdef WOWEE_PS4
#include "platform/ps4/ps4_platform.hpp"
#include "rendering/ps4_world_budget.hpp"
#endif
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <unordered_set>

#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <array>
#include <cstring>

#endif

namespace wowee {
namespace rendering {

namespace {
/// The euler triple a placement's three degrees become, in render axes.
///
/// MDDF and MODF store the rotation identically and this was written out twice,
/// once for each; it is one function. Both are composed X, Y, Z - see the note
/// in WMOInstance::updateModelMatrix for how the buildings came to be composed
/// the other way round and what it took to settle it.
glm::vec3 placementEuler(const float rotation[3]) {
    // MDDF and MODF store the rotation identically, this was written out once
    // for each, and both are composed X, Y, Z - see the note in
    // WMOInstance::updateModelMatrix for how the buildings came to be composed
    // the other way and what it took to settle it.
    //
    // What is *not* wrong: this mapping. Darkshore's bridges are still slightly
    // askew, and every dial that could be turned here has been turned - all six
    // composition orders, all four source permutations, the sign of each
    // component, and the yaw offset. None of them stands the bridges up, and
    // the closest compromise anyone found was multiplying one component by
    // four, which is not a thing a placement convention ever does: a convention
    // is a sign and a right angle. A factor of four is a small wrong number
    // stretched until it resembles a different one, and it is wrong differently
    // for every placement with a different roll.
    //
    // So the remaining error is not in the euler mapping, and the next thing to
    // suspect is what the bridges are being judged against - the terrain they
    // span. A correctly placed bridge over a slightly wrong heightmap looks
    // exactly like a wrongly placed bridge, and it would explain the same
    // pattern turning up on other objects that sit against ground.
    constexpr float kDeg = core::coords::PI / 180.0f;
    return glm::vec3(-rotation[2] * kDeg,
                     -rotation[0] * kDeg,
                     (rotation[1] + 180.0f) * kDeg);
}


// Alpha map format constants live with the loader now - see
// pipeline/adt_alpha.hpp - because two copies of the decoder had grown and the
// grass terrain adapter would have made a third.
using pipeline::ALPHA_MAP_SIZE;

// Random float normalization: mask to 16-bit then divide by max value to get [0..1]
constexpr float kRand16Max = 65535.0f;

// Placement transform constants
constexpr float kInv1024  = 1.0f / 1024.0f;

int computeTerrainWorkerCount() {
    const char* raw = std::getenv("WOWEE_TERRAIN_WORKERS");
    if (raw && *raw) {
        char* end = nullptr;
        unsigned long long forced = std::strtoull(raw, &end, 10);
        if (end != raw && *end == '\0' && forced > 0 && forced <= 16) {
            return static_cast<int>(forced);
        }
    }

    unsigned hc = std::thread::hardware_concurrency();
    if (hc > 0) {
        // Keep terrain workers conservative by default. Over-subscribing loader
        // threads can starve main-thread networking/render updates on large-core CPUs.
        const unsigned reserved = (hc >= 16u) ? 4u : ((hc >= 8u) ? 2u : 1u);
        const unsigned maxDefaultWorkers = 8u;
        const unsigned targetWorkers = std::max(4u, std::min(maxDefaultWorkers, hc - reserved));
        return static_cast<int>(targetWorkers);
    }
    return 4;  // Fallback
}

using pipeline::decodeLayerAlpha;

std::string toLowerCopy(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v;
}

} // namespace

TerrainManager::TerrainManager() {
}

TerrainManager::~TerrainManager() {
    stopWorkers();
}

bool TerrainManager::initialize(pipeline::AssetManager* assets, TerrainRenderer* renderer) {
    assetManager = assets;
    terrainRenderer = renderer;

    if (!assetManager) {
        LOG_ERROR("Asset manager is null");
        return false;
    }

    if (!terrainRenderer) {
        LOG_ERROR("Terrain renderer is null");
        return false;
    }

    // Set dynamic tile cache budget.
    // Keep this lower so decompressed MPQ file cache can stay very aggressive.
    auto& memMonitor = core::MemoryMonitor::getInstance();
    tileCacheBudgetBytes_ = memMonitor.getRecommendedCacheBudget() / 4;
    LOG_INFO("Terrain tile cache budget: ", tileCacheBudgetBytes_ / (1024 * 1024), " MB (dynamic)");

    // Publish immutable ground-effect tables before worker/main render reads.
    // Lazy initialization on a worker exposed a half-filled unordered_map to
    // the grass renderer (and to other terrain workers on desktop).
    ensureGroundEffectTablesLoaded();

    // Start background worker pool (dynamic: scales with available cores)
    // Keep defaults moderate; env override can increase if streaming is bottlenecked.
    workerCount = computeTerrainWorkerCount();
    if (!startWorkers()) return false;

    LOG_INFO("Terrain manager initialized (async loading enabled)");
    LOG_INFO("  Map: ", mapName);
    LOG_INFO("  Load radius: ", loadRadius, " tiles");
    LOG_INFO("  Unload radius: ", unloadRadius, " tiles");
    LOG_INFO("  Workers: ", workerCount);
#ifdef WOWEE_PS4
    LOG_INFO("  CPU tile preparation: 1 total preparing/ready/finalizing payload; low-headroom probe interval=1000ms");
    LOG_INFO("  Resident tile cap: ", ps4budget::kMaxResidentTiles,
             "; shared WMO lifetimes; world uploads: 2 chunks/1 model per step, 4ms scheduling budget");
#endif

    return true;
}

void TerrainManager::update(const Camera& camera, float deltaTime) {
    ZoneScopedN("TerrainManager::update");
    if (!streamingEnabled || !assetManager || !terrainRenderer) {
        return;
    }

    // Residency decisions made while finalizing/unloading must use this
    // frame's camera position, including the first frame after a teleport.
    const glm::vec3 cameraPosition = camera.getPosition();
    currentTile = worldToTile(cameraPosition.x, cameraPosition.y);
    streamPosition_ = cameraPosition;
    streamViewDistance_ = camera.getFarPlane();
#ifdef WOWEE_PS4
    // Reprioritize before finalization, including camera cuts and movement
    // inside one ADT. Old intro/taxi work must not upload ahead of the spawn.
    const glm::vec3 travelled = streamPosition_ - lastStreamPosition_;
    if ((lastStreamTile.x != currentTile.x || lastStreamTile.y != currentTile.y) || glm::dot(travelled, travelled) >= 32.f * 32.f) {
        streamTiles();
        lastStreamTile = currentTile;
        lastStreamPosition_ = streamPosition_;
    }
#endif

    // Phase timing: this call stalls for ~180ms when tiles arrive, and the
    // caller only sees one number. Both processReadyTiles and
    // processPendingUnloads claim to be time-budgeted internally, so knowing
    // which phase actually runs long says whether a budget is being exceeded or
    // whether the cost is in streamTiles enumerating the world.
    using clock = std::chrono::steady_clock;
    const auto tStart = clock::now();
    auto elapsedMs = [](clock::time_point a, clock::time_point b) {
        return std::chrono::duration<float, std::milli>(b - a).count();
    };
    float reconcileMs = 0.0f, readyMs = 0.0f, unloadMs = 0.0f, streamMs = 0.0f;
    // Reports the breakdown on the way out of any long call, whichever return
    // path is taken.
    struct PhaseReport {
        clock::time_point start;
        const float *reconcile, *ready, *unload, *stream;
        ~PhaseReport() {
            const float total = std::chrono::duration<float, std::milli>(
                clock::now() - start).count();
            if (total > 50.0f) {
                LOG_WARNING("SLOW terrain update ", total, "ms: reconcile=", *reconcile,
                            " readyTiles=", *ready, " unloads=", *unload,
                            " streamTiles=", *stream);
            }
        }
    } report{.start = tStart, .reconcile = &reconcileMs, .ready = &readyMs, .unload = &unloadMs, .stream = &streamMs};

    // Reconcile the "already uploaded" cache against models the renderer reaped
    // for being instanceless. Without this, a model freed after leaving an area
    // stays marked uploaded, so the next tile prep skips its load and pushes an
    // empty placeholder - doodads (e.g. Stormwind tunnel torches) then fail to
    // spawn on revisit with "M2 model has no renderable content".
    if (m2Renderer) {
        std::vector<uint32_t> reaped = m2Renderer->drainReapedModelIds();
        if (!reaped.empty()) {
            std::lock_guard<std::mutex> lock(uploadedM2IdsMutex_);
            for (uint32_t id : reaped) uploadedM2Ids_.erase(id);
        }
    }

    // Reclaim distant resources before starting the next upload. Reversing
    // this order briefly held both working sets at tile boundaries.
    const auto tUnloadStart = clock::now();
    processPendingUnloads();
    collectUnusedSharedWmos();
    unloadMs = elapsedMs(tUnloadStart, clock::now());

    // Always process ready tiles each frame (GPU uploads from background thread)
    // Time-budgeted internally to prevent frame spikes.
    const auto tReconcile = clock::now();
    reconcileMs = elapsedMs(tStart, tUnloadStart);
    processReadyTiles();
    readyMs = elapsedMs(tReconcile, clock::now());

    timeSinceLastUpdate += deltaTime;
    proactiveStreamTimer_ += deltaTime;

    // Only update streaming periodically (not every frame)
    if (timeSinceLastUpdate < updateInterval) {
        return;
    }

    timeSinceLastUpdate = 0.0f;

    // Get current tile from camera position.
    glm::vec3 camPos = camera.getPosition();
    TileCoord newTile = worldToTile(camPos.x, camPos.y);

    // Stream tiles when player crosses a tile boundary
    if (newTile.x != lastStreamTile.x || newTile.y != lastStreamTile.y) {
        LOG_DEBUG("Streaming: cam=(", camPos.x, ",", camPos.y, ",", camPos.z,
                 ") tile=[", newTile.x, ",", newTile.y,
                 "] loaded=", loadedTiles.size());
        const auto tStream = clock::now();
        streamTiles();
        streamMs = elapsedMs(tStream, clock::now());
        lastStreamTile = newTile;
    } else {
        // Proactive loading: when workers are idle, periodically re-check for
        // unloaded tiles within range. Throttled to avoid hitching right after
        // world load when many tiles finalize simultaneously.
        if (proactiveStreamTimer_ >= 2.0f) {
            proactiveStreamTimer_ = 0.0f;
            bool workersIdle;
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                workersIdle = loadQueue.empty();
            }
            if (workersIdle) {
                const auto tStream = clock::now();
                streamTiles();
                streamMs = elapsedMs(tStream, clock::now());
            }
        }
    }
}

bool TerrainManager::enqueueTile(int x, int y, bool priority) {
    if (x < 0 || x >= 64 || y < 0 || y >= 64) return false;
    TileCoord coord = {.x = x, .y = y};
    if (loadedTiles.find(coord) != loadedTiles.end()) {
        return true;
    }
    if (failedTiles.find(coord) != failedTiles.end()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (pendingTiles.find(coord) != pendingTiles.end()) {
            if(priority) {
                auto it=std::find(loadQueue.begin(),loadQueue.end(),coord);
                if(it!=loadQueue.end()){loadQueue.erase(it);loadQueue.push_front(coord);}
            }
            return true;
        }
        if(priority)loadQueue.push_front(coord);else loadQueue.push_back(coord);
        pendingTiles[coord] = true;
    }
    queueCV.notify_all();
    return true;
}

std::shared_ptr<PendingTile> TerrainManager::prepareTile(int x, int y, bool objectsOnly) {
    TileCoord coord = {.x = x, .y = y};
    if (auto cached = objectsOnly ? nullptr : getCachedTile(coord)) {
        LOG_DEBUG("Using cached tile [", x, ",", y, "]");
        return cached;
    }

    LOG_DEBUG("Preparing tile [", x, ",", y, "] (CPU work)");

    // Early-exit check - worker should bail fast during shutdown
    if (!workerRunning.load()) return nullptr;

    // Try Wowee Open Terrain format first (custom zones)
    std::string wotBase = core::resolveRelativeAssetPath(
        "custom_zones/" + mapName + "/" + mapName + "_" +
        std::to_string(coord.x) + "_" + std::to_string(coord.y));
    auto terrainPtr = std::make_unique<pipeline::ADTTerrain>();
    bool loadedFromWot = false;

    if (pipeline::WoweeTerrainLoader::exists(wotBase)) {
        if (pipeline::WoweeTerrainLoader::load(wotBase, *terrainPtr)) {
            loadedFromWot = true;
            LOG_INFO("Loaded custom zone terrain: ", wotBase);
            // Load collision mesh if available
            if (pipeline::WoweeCollisionBuilder::exists(wotBase)) {
                auto woc = pipeline::WoweeCollisionBuilder::load(wotBase + ".woc");
                if (woc.isValid()) {
                    CollisionData cd;
                    cd.triangles.reserve(woc.triangles.size());
                    for (const auto& t : woc.triangles)
                        cd.triangles.push_back({.v0 = t.v0, .v1 = t.v1, .v2 = t.v2, .flags = t.flags});
                    cd.boundsMin = woc.bounds.min;
                    cd.boundsMax = woc.bounds.max;
                    cd.loaded = true;
                    collisionTiles_[tileKey(coord.x, coord.y)] = std::move(cd);
                    LOG_INFO("Loaded WOC collision: ", woc.triangles.size(), " triangles");
                }
            }
        }
    }

    // Also check output directory (editor exports here)
    if (!loadedFromWot) {
        std::string outputBase = core::resolveRelativeAssetPath(
            "output/" + mapName + "/" + mapName + "_" +
            std::to_string(coord.x) + "_" + std::to_string(coord.y));
        if (pipeline::WoweeTerrainLoader::exists(outputBase)) {
            if (pipeline::WoweeTerrainLoader::load(outputBase, *terrainPtr)) {
                loadedFromWot = true;
                LOG_INFO("Loaded editor output terrain: ", outputBase);
                if (pipeline::WoweeCollisionBuilder::exists(outputBase)) {
                    auto woc = pipeline::WoweeCollisionBuilder::load(outputBase + ".woc");
                    if (woc.isValid()) {
                        CollisionData cd;
                        cd.triangles.reserve(woc.triangles.size());
                        for (const auto& t : woc.triangles)
                            cd.triangles.push_back({.v0 = t.v0, .v1 = t.v1, .v2 = t.v2, .flags = t.flags});
                        cd.boundsMin = woc.bounds.min;
                        cd.boundsMax = woc.bounds.max;
                        cd.loaded = true;
                        collisionTiles_[tileKey(coord.x, coord.y)] = std::move(cd);
                        LOG_INFO("Loaded WOC collision: ", woc.triangles.size(), " triangles");
                    }
                }
            }
        }
    }

    // Try WHM/WOT sidecar from the asset tree (asset_extract --emit-terrain
    // writes one alongside the ADT). This lets the runtime use the open
    // format without copying anything into custom_zones/.
    if (!loadedFromWot) {
        std::string adtPath = getADTPath(coord);
        std::string adtFsPath = assetManager->resolveFile(adtPath);
        if (!adtFsPath.empty() && adtFsPath.size() >= 4) {
            std::string sidecarBase = adtFsPath.substr(0, adtFsPath.size() - 4);
            if (pipeline::WoweeTerrainLoader::exists(sidecarBase) &&
                pipeline::WoweeTerrainLoader::load(sidecarBase, *terrainPtr)) {
                loadedFromWot = true;
                LOG_INFO("Loaded asset-tree WHM/WOT sidecar: ", sidecarBase);
                if (pipeline::WoweeCollisionBuilder::exists(sidecarBase)) {
                    auto woc = pipeline::WoweeCollisionBuilder::load(sidecarBase + ".woc");
                    if (woc.isValid()) {
                        CollisionData cd;
                        cd.triangles.reserve(woc.triangles.size());
                        for (const auto& t : woc.triangles)
                            cd.triangles.push_back({.v0 = t.v0, .v1 = t.v1, .v2 = t.v2, .flags = t.flags});
                        cd.boundsMin = woc.bounds.min;
                        cd.boundsMax = woc.bounds.max;
                        cd.loaded = true;
                        collisionTiles_[tileKey(coord.x, coord.y)] = std::move(cd);
                        LOG_INFO("Loaded sidecar WOC collision: ",
                                 woc.triangles.size(), " triangles");
                    }
                }
            }
        }
    }

    // Fall back to ADT format
    if (!loadedFromWot) {
        std::string adtPath = getADTPath(coord);
        auto adtData = assetManager->readFile(adtPath);

        if (adtData.empty()) {
            logMissingAdtOnce(adtPath);
            return nullptr;
        }

        *terrainPtr = pipeline::ADTLoader::load(adtData);
        if (!terrainPtr->isLoaded()) {
            LOG_ERROR("Failed to parse ADT terrain: ", adtPath);
            return nullptr;
        }
    }

    if (!workerRunning.load()) return nullptr;

    // WotLK split ADTs can store placements in *_obj0.adt.
    // Only needed for ADT-loaded tiles, not for WOT custom zones.
    if (!loadedFromWot) {
    std::string objPath = "World\\Maps\\" + mapName + "\\" + mapName + "_" +
                          std::to_string(coord.x) + "_" + std::to_string(coord.y) + "_obj0.adt";
    auto objData = assetManager->readFile(objPath);
    if (!objData.empty()) {
        auto objTerrain = std::make_unique<pipeline::ADTTerrain>(pipeline::ADTLoader::load(objData));
        if (objTerrain->isLoaded()) {
            const uint32_t doodadNameBase = static_cast<uint32_t>(terrainPtr->doodadNames.size());
            const uint32_t wmoNameBase = static_cast<uint32_t>(terrainPtr->wmoNames.size());

            terrainPtr->doodadNames.insert(terrainPtr->doodadNames.end(),
                                       objTerrain->doodadNames.begin(), objTerrain->doodadNames.end());
            terrainPtr->wmoNames.insert(terrainPtr->wmoNames.end(),
                                    objTerrain->wmoNames.begin(), objTerrain->wmoNames.end());

            std::unordered_set<uint32_t> existingDoodadUniqueIds;
            existingDoodadUniqueIds.reserve(terrainPtr->doodadPlacements.size());
            for (const auto& p : terrainPtr->doodadPlacements) {
                if (p.uniqueId != 0) existingDoodadUniqueIds.insert(p.uniqueId);
            }

            size_t mergedDoodads = 0;
            for (auto placement : objTerrain->doodadPlacements) {
                if (placement.nameId >= objTerrain->doodadNames.size()) continue;
                placement.nameId += doodadNameBase;
                if (placement.uniqueId != 0 && !existingDoodadUniqueIds.insert(placement.uniqueId).second) {
                    continue;
                }
                terrainPtr->doodadPlacements.push_back(placement);
                mergedDoodads++;
            }

            std::unordered_set<uint32_t> existingWmoUniqueIds;
            existingWmoUniqueIds.reserve(terrainPtr->wmoPlacements.size());
            for (const auto& p : terrainPtr->wmoPlacements) {
                if (p.uniqueId != 0) existingWmoUniqueIds.insert(p.uniqueId);
            }

            size_t mergedWmos = 0;
            for (auto placement : objTerrain->wmoPlacements) {
                if (placement.nameId >= objTerrain->wmoNames.size()) continue;
                placement.nameId += wmoNameBase;
                if (placement.uniqueId != 0 && !existingWmoUniqueIds.insert(placement.uniqueId).second) {
                    continue;
                }
                terrainPtr->wmoPlacements.push_back(placement);
                mergedWmos++;
            }

            if (mergedDoodads > 0 || mergedWmos > 0) {
                LOG_DEBUG("Merged obj0 tile [", x, ",", y, "]: +", mergedDoodads,
                          " doodads, +", mergedWmos, " WMOs");
            }
        }
    }
    } // end if (!loadedFromWot) obj0 merge

    // Set tile coordinates so mesh knows where to position this tile in world
    terrainPtr->coord.x = x;
    terrainPtr->coord.y = y;

    // The stock Azeroth ADT leaves two terrain-hole cells exposed along the
    // exterior edge of the Westfall lumbermill. The original client hides the
    // mask beneath the building footprint, but our WMO floor ends just short
    // of it, producing a small textureless strip between the inn/prison camp
    // and lumber yard. Repair only this confirmed exterior seam; other ADT
    // holes remain intact for caves and below-ground WMO entrances.
    if (toLowerCopy(mapName) == "azeroth" && x == 29 && y == 51) {
        auto& lumbermillChunk = terrainPtr->chunks[15 * 16 + 14];
        if (lumbermillChunk.indexX == 14 && lumbermillChunk.indexY == 15 &&
            lumbermillChunk.holes == 0x0044) {
            lumbermillChunk.holes = 0;
            LOG_INFO("Repaired Westfall lumbermill terrain seam in tile [29,51]");
        }
    }

    // Generate mesh
    pipeline::TerrainMesh mesh;
    if (!objectsOnly) mesh = pipeline::TerrainMeshGenerator::generate(*terrainPtr);
    if (!objectsOnly && mesh.validChunkCount == 0) {
        LOG_ERROR("Failed to generate terrain mesh for tile [", x, ",", y, "]");
        return nullptr;
    }

    if (!workerRunning.load()) return nullptr;

    auto pending = std::make_shared<PendingTile>();
    pending->coord = coord;
    pending->objectsOnly = objectsOnly;
    pending->terrain = std::move(*terrainPtr);
    pending->mesh = std::move(mesh);

    // Pre-load terrain texture BLP data on background thread so finalizeTile
    // doesn't block the main thread with file I/O.
    if (!objectsOnly) for (const auto& texPath : pending->terrain.textures) {
        if (pending->preloadedTextures.find(texPath) != pending->preloadedTextures.end()) continue;
        // Terrain tilesets and M2 skins stay in their blocks - both are only
        // sampled, and M2's transparency now comes from the blocks too. The
        // normal-map source below still decodes: it reads the pixels.
        pending->preloadedTextures[texPath] = assetManager->loadTexture(texPath, true);
    }

    // Ground and terrain textures are complete before optional world objects.
    // A large building or clutter allocation must not throw away the spawn ADT.
    const char* preparationStage = "M2 models";
    uint32_t unfinishedWmoClaim = 0;
    size_t wmoDoodadCheckpoint = 0, wmoEmitterCheckpoint = 0;
    try {
    std::unordered_set<uint32_t> preparedModelIds;
    auto ensureModelPrepared = [&](const std::string& m2Path,
                                   uint32_t modelId,
                                   int& skippedFileNotFound,
                                   int& skippedInvalid,
                                   int& skippedSkinNotFound) -> bool {
        if (preparedModelIds.find(modelId) != preparedModelIds.end()) return true;

        // Skip file I/O + parsing for models already uploaded to GPU from previous tiles
        {
            std::lock_guard<std::mutex> lock(uploadedM2IdsMutex_);
            if (uploadedM2Ids_.count(modelId)) {
                preparedModelIds.insert(modelId);
                return true;
            }
        }

        // Check for WOM open format first (custom zone models)
        // Try open WOM format first via shared helper. Per-zone prefixes are
        // checked before the global fallback so a zone export overrides a
        // generic custom asset of the same name.
        {
            std::vector<std::string> extraPrefixes = {
                "output/" + mapName + "/models/",
                "custom_zones/" + mapName + "/models/",
            };
            // Asset extractor's --emit-wom writes WOM sidecars next to the
            // M2 in the asset tree (e.g. <data>/world/maps/foo/foo.wom).
            // Add the data path as a prefix so the runtime picks them up
            // without needing to copy them into custom_zones/.
            if (assetManager && !assetManager->getDataPath().empty()) {
                extraPrefixes.push_back(assetManager->getDataPath() + "/");
            }
            auto wom = pipeline::WoweeModelLoader::tryLoadByGamePath(m2Path, extraPrefixes);
            if (wom.isValid()) {
                auto m2Model = pipeline::WoweeModelLoader::toM2(wom);
                m2Model.name = m2Path;
                pending->m2Models.push_back({.modelId = modelId, .model = std::move(m2Model), .path = {}});
                preparedModelIds.insert(modelId);
                LOG_INFO("Loaded WOM model: ", m2Path, " (v", wom.version,
                         ", ", wom.batches.size(), " batches)");
                return true;
            }
        }

        std::vector<uint8_t> m2Data = assetManager->readFile(m2Path);
        if (m2Data.empty()) {
            skippedFileNotFound++;
            return false;
        }

        pipeline::M2Model m2Model = pipeline::M2Loader::load(m2Data);
        // The asset path carries foliage semantics; embedded names frequently
        // do not. Always classify ADT doodads using the path.
        m2Model.name = m2Path;
        std::string skinPath = pipeline::skinPathForM2(m2Path);
        std::vector<uint8_t> skinData = assetManager->readFileOptional(skinPath);
        if (!skinData.empty() && m2Model.version >= 264) {
            pipeline::M2Loader::loadSkin(skinData, m2Model);
        } else if (skinData.empty() && m2Model.version >= 264) {
            pending->objectsIncomplete = true;
            skippedSkinNotFound++;
        }

        if (!m2Model.isValid()) {
            skippedInvalid++;
            LOG_DEBUG("M2 model invalid (no verts/indices): ", m2Path);
            return false;
        }

        // Pre-decode M2 model textures on background thread
        for (const auto& tex : m2Model.textures) {
            if (tex.filename.empty()) continue;
            std::string texKey = tex.filename;
            std::replace(texKey.begin(), texKey.end(), '/', '\\');
            std::transform(texKey.begin(), texKey.end(), texKey.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (pending->preloadedM2Textures.find(texKey) != pending->preloadedM2Textures.end()) continue;
            auto blp = assetManager->loadTexture(texKey, true);
            if (blp.isValid()) {
                pending->preloadedM2Textures[texKey] = std::move(blp);
            }
        }

        PendingTile::M2Ready ready;
        ready.modelId = modelId;
        ready.model = std::move(m2Model);
        ready.path = m2Path;
        pending->m2Models.push_back(std::move(ready));
        preparedModelIds.insert(modelId);
        return true;
    };

    preparationStage = "WMO models/textures";
    // Pre-load WMOs (CPU: read files, parse models and groups)
    if (!pending->terrain.wmoPlacements.empty()) {
        for (const auto& placement : pending->terrain.wmoPlacements) {
            if (!workerRunning.load()) return nullptr;
            if (placement.nameId >= pending->terrain.wmoNames.size()) continue;

            const std::string& wmoPath = pending->terrain.wmoNames[placement.nameId];
#ifdef WOWEE_PS4
            // A live parent is pinned before reading any WMO/group/texture.
            // Its original tile can now unload during preparation without
            // removing the city, liquids or children. The pin is transferred
            // to the incoming tile; cancelled pins are retired on the main thread.
            if (pinPreparedWmo(*pending, placement.uniqueId)) {
                LOG_INFO("[WMO_STREAM_REUSE] tile=[", x, ",", y,
                         "] uniqueId=", placement.uniqueId, " path=", wmoPath);
                continue;
            }
#endif
            wmoDoodadCheckpoint = pending->wmoDoodads.size();
            wmoEmitterCheckpoint = pending->ambientEmitters.size();
            LOG_INFO("Terrain prepare WMO tile=[", x, ",", y, "] path=", wmoPath);

            // Check for WOB open format first (custom zone buildings)
            bool wobLoaded = false;
            pipeline::WMOModel wmoModel;
            {
                // Per-zone overrides win over global custom_zones/ overrides.
                std::vector<std::string> extraPrefixes = {
                    "output/" + mapName + "/buildings/",
                    "custom_zones/" + mapName + "/buildings/",
                };
                // asset_extract --emit-wob writes WOB next to the WMO in
                // the asset tree; add the data path so the runtime picks
                // them up there too.
                if (assetManager && !assetManager->getDataPath().empty()) {
                    extraPrefixes.push_back(assetManager->getDataPath() + "/");
                }
                auto wob = pipeline::WoweeBuildingLoader::tryLoadByGamePath(
                    wmoPath, extraPrefixes);
                if (wob.isValid() &&
                    pipeline::WoweeBuildingLoader::toWMOModel(wob, wmoModel)) {
                    LOG_INFO("Loaded WOB building: ", wmoPath);
                    wobLoaded = true;
                }
            }

            if (!wobLoaded) {
                std::vector<uint8_t> wmoData = assetManager->readFile(wmoPath);
                if (wmoData.empty()) { pending->objectsIncomplete = true; continue; }

                wmoModel = pipeline::WMOLoader::load(wmoData);
                std::vector<uint8_t>{}.swap(wmoData);
                if (wmoModel.nGroups > 0) {
                    bool groupsComplete = true;
                    for (uint32_t gi = 0; gi < wmoModel.nGroups; gi++) {
                        bool groupLoaded = false;
                        for (const std::string& groupPath :
                             pipeline::wmoGroupCandidates(wmoPath, gi)) {
                            std::vector<uint8_t> groupData =
                                assetManager->readFile(groupPath);
                            if (groupData.empty()) continue;
                            groupLoaded = pipeline::WMOLoader::loadGroup(groupData, wmoModel, gi);
                            if (groupLoaded) break;
                        }
                        groupsComplete = groupsComplete && groupLoaded;
                    }
                    if (!groupsComplete) {
                        pending->objectsIncomplete = true;
                        LOG_WARNING("[TERRAIN_REPAIR] incomplete WMO groups; retry required: ", wmoPath);
                        continue;
                    }
                }
            }

            if (!wmoModel.groups.empty()) {
                wmoModel.sourcePath = wmoPath;
                glm::vec3 pos = core::coords::adtToWorld(placement.position[0],
                                                       placement.position[1],
                                                       placement.position[2]);

                glm::vec3 rot = placementEuler(placement.rotation);

                // Pre-load WMO doodads (M2 models inside WMO)
                if (!workerRunning.load()) return nullptr;

                // Skip WMO doodads if this placement was already prepared by another tile's worker.
                // This prevents 15+ copies of Stormwind's ~6000 doodads from being parsed
                // simultaneously, which was the primary cause of OOM during world load.
                bool wmoAlreadyPrepared = false;
                if (placement.uniqueId != 0) {
                    std::lock_guard<std::mutex> lock(preparedWmoUniqueIdsMutex_);
                    wmoAlreadyPrepared = !preparedWmoUniqueIds_.insert(placement.uniqueId).second;
                    if (!wmoAlreadyPrepared) unfinishedWmoClaim = placement.uniqueId;
                }
#ifdef WOWEE_PS4
                // A moving camera may unload the old owner while this worker
                // is parsing. A preparation claim is not a lifetime pin.
                // Keep fallback child placements; finalization discards them
                // if a live shared parent was successfully retained.
                const bool needsIndependentFallback = wmoAlreadyPrepared && !stationaryPreload_.load();
                if (!stationaryPreload_.load()) wmoAlreadyPrepared = false;
#endif

                if (!wmoAlreadyPrepared && !wmoModel.doodadSets.empty() && !wmoModel.doodads.empty()) {
                    glm::mat4 wmoMatrix(1.0f);
                    wmoMatrix = glm::translate(wmoMatrix, pos);
                    wmoMatrix = glm::rotate(wmoMatrix, rot.z, glm::vec3(0, 0, 1));
                    wmoMatrix = glm::rotate(wmoMatrix, rot.y, glm::vec3(0, 1, 0));
                    wmoMatrix = glm::rotate(wmoMatrix, rot.x, glm::vec3(1, 0, 0));

                    // Load doodads from set 0 (global) + placement-specific set
                    std::vector<uint32_t> setsToLoad = {0};
                    if (placement.doodadSet > 0 && placement.doodadSet < wmoModel.doodadSets.size()) {
                        setsToLoad.push_back(placement.doodadSet);
                    }
                    std::unordered_set<uint32_t> loadedDoodadIndices;
                    std::unordered_set<uint32_t> wmoPreparedModelIds;  // within-WMO model dedup
                    for (uint32_t setIdx : setsToLoad) {
                        const auto& doodadSet = wmoModel.doodadSets[setIdx];
                    for (uint32_t di = 0; di < doodadSet.count; di++) {
                        uint32_t doodadIdx = doodadSet.startIndex + di;
                        if (doodadIdx >= wmoModel.doodads.size()) break;
                        if (!loadedDoodadIndices.insert(doodadIdx).second) continue;

                        const auto& doodad = wmoModel.doodads[doodadIdx];
                        auto nameIt = wmoModel.doodadNames.find(doodad.nameIndex);
                        if (nameIt == wmoModel.doodadNames.end()) continue;

                        std::string m2Path = nameIt->second;
                        if (m2Path.empty()) continue;

                        m2Path = pipeline::modelPathToM2(m2Path);

                        uint32_t doodadModelId = static_cast<uint32_t>(std::hash<std::string>{}(m2Path));

                        // Skip file I/O if model already uploaded or already prepared within this WMO
                        bool modelAlreadyUploaded = false;
                        {
                            std::lock_guard<std::mutex> lock(uploadedM2IdsMutex_);
                            modelAlreadyUploaded = uploadedM2Ids_.count(doodadModelId) > 0;
                        }
#ifdef WOWEE_PS4
                        // The old owner's model can also become an orphan
                        // during long preparation. A fallback needs its own
                        // parsed asset once per model, not an empty placeholder
                        // that only works while the old GPU model survives.
                        if (needsIndependentFallback) modelAlreadyUploaded = false;
#endif
                        // Membership check only - the id is claimed below, after a
                        // successful prep. Claiming up front meant a model whose first
                        // occurrence failed (missing/invalid file) poisoned every later
                        // occurrence into an empty placeholder push, which finalize then
                        // rejected with "no renderable content".
                        bool modelAlreadyPreparedInWmo = wmoPreparedModelIds.count(doodadModelId) > 0;

                        pipeline::M2Model m2Model;
                        if (!modelAlreadyUploaded && !modelAlreadyPreparedInWmo) {
                            std::vector<uint8_t> m2Data = assetManager->readFile(m2Path);
                            if (m2Data.empty()) continue;

                            m2Model = pipeline::M2Loader::load(m2Data);
                            m2Model.name = m2Path;
                            std::string skinPath = pipeline::skinPathForM2(m2Path);
                            std::vector<uint8_t> skinData = assetManager->readFile(skinPath);
                            if (!skinData.empty() && m2Model.version >= 264) {
                                pipeline::M2Loader::loadSkin(skinData, m2Model);
                            }
                            if (!m2Model.isValid()) continue;
                            wmoPreparedModelIds.insert(doodadModelId);

                            // Pre-decode doodad M2 textures on background thread
                            for (const auto& tex : m2Model.textures) {
                                if (tex.filename.empty()) continue;
                                std::string texKey = tex.filename;
                                std::replace(texKey.begin(), texKey.end(), '/', '\\');
                                std::transform(texKey.begin(), texKey.end(), texKey.begin(),
                                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                if (pending->preloadedM2Textures.find(texKey) != pending->preloadedM2Textures.end()) continue;
                                auto blp = assetManager->loadTexture(texKey, true);
                                if (blp.isValid()) {
                                    pending->preloadedM2Textures[texKey] = std::move(blp);
                                }
                            }
                        }

                        // Build doodad's local transform (WoW coordinates)
                        // WMO doodads use quaternion rotation
                        glm::quat fixedRotation(doodad.rotation.w, doodad.rotation.x, doodad.rotation.y, doodad.rotation.z);

                        glm::mat4 doodadLocal(1.0f);
                        doodadLocal = glm::translate(doodadLocal, doodad.position);
                        doodadLocal *= glm::mat4_cast(fixedRotation);
                        doodadLocal = glm::scale(doodadLocal, glm::vec3(doodad.scale));

                        // Full world transform = WMO world transform * doodad local transform
                        glm::mat4 worldMatrix = wmoMatrix * doodadLocal;

                        // Extract world position for frustum culling
                        glm::vec3 worldPos = glm::vec3(worldMatrix[3]);

                        // Detect ambient sound emitters from doodad model path
                        std::string m2PathLower = m2Path;
                        std::transform(m2PathLower.begin(), m2PathLower.end(), m2PathLower.begin(), ::tolower);

                        // Debug: Log all doodad paths to help identify fire-related models
                        static int doodadLogCount = 0;
                        if (doodadLogCount < 50) {  // Limit logging to first 50 doodads
                            LOG_DEBUG("WMO doodad: ", m2Path);
                            doodadLogCount++;
                        }

                        auto emitterType = rendering::classifyAmbientEmitter(m2PathLower);
                        if (emitterType != rendering::AmbientEmitterType::None) {
                            PendingTile::AmbientEmitter emitter;
                            emitter.position = worldPos;
                            // Map classifier enum to AmbientSoundManager type codes
                            switch (emitterType) {
                                case rendering::AmbientEmitterType::FireplaceSmall: emitter.type = 0; break;
                                case rendering::AmbientEmitterType::FireplaceLarge: emitter.type = 1; break;
                                case rendering::AmbientEmitterType::Torch:          emitter.type = 2; break;
                                case rendering::AmbientEmitterType::Fountain:       emitter.type = 3; break;
                                case rendering::AmbientEmitterType::Waterfall:      emitter.type = 6; break;
                                case rendering::AmbientEmitterType::Forge:          emitter.type = 1; break; // Forge → large fire
                                default: emitter.type = 0; break;
                            }
                            pending->ambientEmitters.push_back(emitter);
                        }

                        PendingTile::WMODoodadReady doodadReady;
                        doodadReady.modelId = doodadModelId;
                        doodadReady.parentWmoUniqueId = placement.uniqueId;
                        doodadReady.model = std::move(m2Model);
                        doodadReady.worldPosition = worldPos;
                        doodadReady.modelMatrix = worldMatrix;
                        pending->wmoDoodads.push_back(std::move(doodadReady));
                    }
                    }
                }

                // Pre-decode WMO textures on background thread
                for (const auto& texPath : wmoModel.textures) {
                    if (texPath.empty()) continue;
                    std::string texKey = texPath;
                    // Truncate at NUL (WMO paths can have stray bytes)
                    size_t nul = texKey.find('\0');
                    if (nul != std::string::npos) texKey.resize(nul);
                    std::replace(texKey.begin(), texKey.end(), '/', '\\');
                    std::transform(texKey.begin(), texKey.end(), texKey.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (texKey.empty()) continue;
                    if (pending->preloadedWMOTextures.find(texKey) != pending->preloadedWMOTextures.end()) continue;
                    // Try .blp variant
                    std::string blpKey = texKey;
                    if (blpKey.size() >= 4) {
                        std::string ext = blpKey.substr(blpKey.size() - 4);
                        if (ext == ".tga" || ext == ".dds") {
                            blpKey = blpKey.substr(0, blpKey.size() - 4) + ".blp";
                        }
                    }
                    // Blocks for the upload; the normal map decodes a copy
                    // here on the worker thread and lets it go, which is where
                    // that cost already was.
                    auto blp = assetManager->loadTexture(blpKey, true);
                    if (blp.isValid()) {
#ifndef WOWEE_PS4
                        float variance = 0.0f;
                        const std::vector<uint8_t> decoded =
                            blp.isBlockCompressed()
                                ? pipeline::BLPLoader::decodeBaseLevel(blp)
                                : blp.data;
                        auto normalPixels = decoded.empty()
                            ? pipeline::BLPImage{}
                            : WMORenderer::generateNormalHeightMapPixels(
                                  decoded.data(), static_cast<uint32_t>(blp.width),
                                  static_cast<uint32_t>(blp.height), variance);
                        if (normalPixels.isValid()) {
                            pending->preloadedWMONormalMaps[blpKey] = std::move(normalPixels);
                            pending->preloadedWMONormalMapVariances[blpKey] = variance;
                        }
#endif
                        pending->preloadedWMOTextures[blpKey] = std::move(blp);
                    }
                }

                PendingTile::WMOReady ready;
                // Cache WMO model uploads by path; placement dedup uses uniqueId separately.
                ready.modelId = static_cast<uint32_t>(std::hash<std::string>{}(wmoPath));
                if (ready.modelId == 0) ready.modelId = 1;
                ready.uniqueId = placement.uniqueId;
                ready.model = std::move(wmoModel);
                ready.position = pos;
                ready.rotation = rot;
                ready.scale = placement.scale > 0
                    ? static_cast<float>(placement.scale) / 1024.0f : 1.0f;
                pending->wmoModels.push_back(std::move(ready));
                unfinishedWmoClaim = 0;
            }
        }
    }

    if (!workerRunning.load()) return nullptr;

    // Buildings/floors take priority over ground clutter on a constrained heap.
    preparationStage = "M2 models";
    // Pre-load M2 doodads (CPU: read files, parse models)
    int skippedNameId = 0, skippedFileNotFound = 0, skippedInvalid = 0, skippedSkinNotFound = 0;
    for (const auto& placement : pending->terrain.doodadPlacements) {
        if (!workerRunning.load()) return nullptr;
        if (placement.nameId >= pending->terrain.doodadNames.size()) {
            skippedNameId++;
            continue;
        }

        std::string m2Path = pending->terrain.doodadNames[placement.nameId];
        // .mdx and .mdl both mean the .m2 that shipped. This site knew only
        // .mdx, so an ADT doodad named with .mdl was looked up as ".mdl", found
        // nothing, and did not appear.
        m2Path = pipeline::modelPathToM2(m2Path);

        uint32_t modelId = static_cast<uint32_t>(std::hash<std::string>{}(m2Path));
        if (!ensureModelPrepared(m2Path, modelId, skippedFileNotFound, skippedInvalid, skippedSkinNotFound)) {
            continue;
        }

        float wowX = placement.position[0];
        float wowY = placement.position[1];
        float wowZ = placement.position[2];
        glm::vec3 glPos = core::coords::adtToWorld(wowX, wowY, wowZ);

        PendingTile::M2Placement p;
        p.modelId = modelId;
        p.uniqueId = placement.uniqueId;
        p.position = glPos;
        p.rotation = placementEuler(placement.rotation);
        p.scale = placement.scale * kInv1024;
        pending->m2Placements.push_back(p);
    }

    if (skippedNameId > 0 || skippedFileNotFound > 0 || skippedInvalid > 0 || skippedSkinNotFound > 0) {
        LOG_DEBUG("Tile [", x, ",", y, "] doodad issues: ",
                  skippedNameId, " bad nameId, ",
                  skippedFileNotFound, " file not found, ",
                  skippedInvalid, " invalid model, ",
                  skippedSkinNotFound, " skin not found");
    }

    preparationStage = "ground clutter";
    // Procedural ground clutter from terrain layer effectId -> GroundEffectTexture/Doodad DBCs.
    // Procedural clutter has no persistent placement IDs. An object repair
    // preserves existing clutter instead of scattering another copy on top.
    if (!objectsOnly) generateGroundClutterPlacements(pending, preparedModelIds);

    if (!workerRunning.load()) return nullptr;


    } catch (const std::bad_alloc&) {
        pending->objectsIncomplete = true;
        // Roll back only the unfinished WMO's claim and child placements.
        // Completed model/placement pairs remain usable in this tile.
        if (unfinishedWmoClaim) {
            std::lock_guard<std::mutex> lock(preparedWmoUniqueIdsMutex_);
            preparedWmoUniqueIds_.erase(unfinishedWmoClaim);
        }
        if (preparationStage == std::string_view("WMO models/textures")) {
            pending->wmoDoodads.resize(wmoDoodadCheckpoint);
            pending->ambientEmitters.resize(wmoEmitterCheckpoint);
            pending->preloadedWMOTextures.clear();
            pending->preloadedWMONormalMaps.clear();
            pending->preloadedWMONormalMapVariances.clear();
        }
        const size_t reclaimed = assetManager->trimFileCache();
        LOG_ERROR("Terrain objects incomplete tile=[", x, ",", y, "] stage=", preparationStage,
                  "; keeping terrain/collision and completed objects; raw cache reclaimed=", reclaimed,
                  ". Missing objects will retry in place; existing terrain is retained.");
    }

    LOG_DEBUG("Prepared tile [", x, ",", y, "]: ",
             pending->m2Models.size(), " M2 models, ",
             pending->m2Placements.size(), " M2 placements, ",
             pending->wmoModels.size(), " WMOs, ",
             pending->wmoDoodads.size(), " WMO doodads, ",
             pending->preloadedTextures.size(), " textures");

    return pending;
}

void TerrainManager::logMissingAdtOnce(const std::string& adtPath) {
    std::string normalized = adtPath;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::lock_guard<std::mutex> lock(missingAdtWarningsMutex_);
    if (missingAdtWarnings_.insert(normalized).second) {
        LOG_WARNING("Failed to load ADT file: ", adtPath);
    }
}

bool TerrainManager::advanceFinalization(FinalizingTile& ft) {
    auto& pending = ft.pending;
    int x = pending->coord.x;
    int y = pending->coord.y;
    TileCoord coord = pending->coord;

    switch (ft.phase) {

    case FinalizationPhase::TERRAIN: {
        // Check if tile was already loaded or failed
        if (loadedTiles.find(coord) != loadedTiles.end() || failedTiles.find(coord) != failedTiles.end()) {
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                pendingTiles.erase(coord);
            }
            ft.phase = FinalizationPhase::DONE;
            return true;
        }

        // Upload pre-loaded textures (once)
        if (!ft.terrainPreloaded) {
            // Retain spanning buildings before evicting their old owner for
            // the resident cap, including stationary-preload duplicates for
            // which preparation intentionally did not reparse the WMO.
            retainSharedWmos(ft);
            retainSharedDoodads(ft);
#ifdef WOWEE_PS4
            if (!makeResidentRoom(coord)) return false;
            // A full tile's diffuse uploads used to be a single unbounded
            // step. Move one node, preserving its pixel storage on retries.
            if (ft.terrainTextureUpload.empty() && !pending->preloadedTextures.empty()) {
                ft.terrainTextureUpload.insert(
                    pending->preloadedTextures.extract(pending->preloadedTextures.begin()));
            }
            if (!ft.terrainTextureUpload.empty()) {
                terrainRenderer->uploadPreloadedTextures(ft.terrainTextureUpload);
                ft.terrainTextureUpload.clear();
                return false;
            }
#else
            LOG_DEBUG("Finalizing tile [", x, ",", y, "] (incremental)");
            if (!pending->preloadedTextures.empty()) {
                terrainRenderer->uploadPreloadedTextures(pending->preloadedTextures);
            }
#endif
            ft.terrainPreloaded = true;
            // Yield after preload to give time budget a chance to interrupt
            return false;
        }

        // Upload terrain chunks incrementally (16 per call to spread across frames)
        if (!ft.terrainMeshDone) {
            if (pending->mesh.validChunkCount == 0) {
                LOG_ERROR("Failed to upload terrain to GPU for tile [", x, ",", y, "]");
                failedTiles[coord] = true;
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    pendingTiles.erase(coord);
                }
                ft.phase = FinalizationPhase::DONE;
                return true;
            }
            // 16 chunks measured 43-57ms on hardware. The running world
            // uploads one: two chunks exceeded 4ms repeatedly in the 01.57 log.
            // The stationary load screen can use 16. Geometry is unchanged.
#ifdef WOWEE_PS4
            const int chunksPerStep = stationaryPreload_.load() ? 16 : 1;
#else
            constexpr int chunksPerStep = 16;
#endif
            const int firstPendingChunk = ft.terrainChunkNext;
            bool allDone = terrainRenderer->loadTerrainIncremental(
                pending->mesh, pending->terrain.textures, x, y,
                ft.terrainChunkNext, chunksPerStep);
#ifdef WOWEE_PS4
            // uploadChunk owns staging copies by the time it returns. Drop
            // completed CPU vertices/indices/duplicate alpha maps now instead
            // of retaining all 256 chunks during a slow incremental upload.
            // The retry cursor excludes a chunk whose descriptors were full;
            // that chunk must retain its payload for the next attempt.
            for (int chunk = firstPendingChunk; chunk < ft.terrainChunkNext; ++chunk)
                pending->mesh.chunks[chunk] = {};
#else
            (void)firstPendingChunk;
#endif
            if (!allDone) {
                return false; // More chunks remain - yield to time budget
            }
            ft.terrainMeshDone = true;
#ifdef WOWEE_PS4
            pending->mesh = {}; // GPU owns copies; collision uses terrain.
#endif
        }

        // Load water after all terrain chunks are uploaded
        if (waterRenderer) {
            size_t beforeSurfaces = waterRenderer->getSurfaceCount();
            waterRenderer->loadFromTerrain(pending->terrain, true, x, y);
            size_t afterSurfaces = waterRenderer->getSurfaceCount();
            if (afterSurfaces > beforeSurfaces) {
                LOG_INFO("Water: tile [", x, ",", y, "] added ", afterSurfaces - beforeSurfaces,
                         " surfaces (total: ", afterSurfaces, ")");
            }
        } else {
            LOG_WARNING("Water: waterRenderer is null during tile [", x, ",", y, "] finalization!");
        }

        // Ensure M2 renderer has asset manager
        if (m2Renderer && assetManager) {
            if (!m2Renderer->initialize(nullptr, VK_NULL_HANDLE, assetManager))
                LOG_WARNING("M2Renderer terrain re-init failed");
        }

        ft.phase = FinalizationPhase::M2_MODELS;
        return false;
    }

    case FinalizationPhase::M2_MODELS: {
        // Upload multiple M2 models per call (batched GPU uploads).
        // When no more tiles are queued for background parsing, increase the
        // per-frame budget so idle workers don't waste time waiting for the
        // main thread to trickle-upload models.
        if (m2Renderer && ft.m2ModelIndex < pending->m2Models.size()) {
            // Set pre-decoded BLP cache so loadTexture() skips main-thread BLP decode
            m2Renderer->setPredecodedBLPCache(&pending->preloadedM2Textures);
#ifndef WOWEE_PS4
            bool workersIdle;
            {
                std::lock_guard<std::mutex> lk(queueMutex);
                workersIdle = loadQueue.empty() && readyQueue.empty();
            }
#endif
#ifdef WOWEE_PS4
            constexpr size_t kModelsPerStep = 1;
#else
            const size_t kModelsPerStep = workersIdle ? 6 : 4;
#endif
            size_t uploaded = 0;
            while (ft.m2ModelIndex < pending->m2Models.size() && uploaded < kModelsPerStep) {
                auto& m2Ready = pending->m2Models[ft.m2ModelIndex];
                if (m2Renderer->loadModel(m2Ready.model, m2Ready.modelId)) {
                    ft.uploadedM2ModelIds.insert(m2Ready.modelId);
                    // Track uploaded model IDs so background threads can skip re-reading
                    std::lock_guard<std::mutex> lock(uploadedM2IdsMutex_);
                    uploadedM2Ids_.insert(m2Ready.modelId);
                }
#ifdef WOWEE_PS4
                m2Ready.model = {}; // Renderer owns animation/collision data.
#endif
                ft.m2ModelIndex++;
                uploaded++;
            }
            m2Renderer->setPredecodedBLPCache(nullptr);
            // Stay in this phase until all models uploaded
            if (ft.m2ModelIndex < pending->m2Models.size()) {
                return false;
            }
        }
        if (!ft.uploadedM2ModelIds.empty()) {
            LOG_DEBUG("  Uploaded ", ft.uploadedM2ModelIds.size(), " M2 models for tile [", x, ",", y, "]");
        }
#ifdef WOWEE_PS4
        decltype(pending->m2Models){}.swap(pending->m2Models);
#endif
        ft.phase = FinalizationPhase::M2_INSTANCES;
        return false;
    }

    case FinalizationPhase::M2_INSTANCES: {
        // Create M2 instances incrementally to avoid main-thread stalls.
        // createInstance includes an O(n) bone-sibling scan that becomes expensive
        // on dense tiles with many placements and a large existing instance list.
        if (m2Renderer && ft.m2InstanceIndex < pending->m2Placements.size()) {
            // A fixed count was overshooting: 32 instances measured at 19ms
            // against this phase's 8ms budget, because createInstance carries an
            // O(n) bone-sibling scan whose cost grows with the instances already
            // present. Bound by time instead, which holds regardless of how
            // heavy each one turns out to be.
#ifdef WOWEE_PS4
            constexpr size_t kInstancesPerStep = 8;
            constexpr float kInstanceBudgetMs = 2.0f;
#else
            constexpr size_t kInstancesPerStep = 32;
            constexpr float kInstanceBudgetMs = 4.0f;
#endif
            const auto instanceStart = std::chrono::steady_clock::now();
            size_t created = 0;
            while (ft.m2InstanceIndex < pending->m2Placements.size() && created < kInstancesPerStep) {
                if (created > 0 && std::chrono::duration<float, std::milli>(
                        std::chrono::steady_clock::now() - instanceStart).count() >= kInstanceBudgetMs) {
                    break;
                }
                const auto& p = pending->m2Placements[ft.m2InstanceIndex++];
                if (p.uniqueId != 0 && placedDoodadIds.count(p.uniqueId)) {
                    continue;
                }
                if (!m2Renderer->hasModel(p.modelId)) {
                    continue;
                }
                uint32_t instId = m2Renderer->createInstance(p.modelId, p.position, p.rotation, p.scale);
                if (instId) {
                    ft.m2InstanceIds.push_back(instId);
                    if (p.uniqueId != 0) {
                        placedDoodadIds.insert(p.uniqueId);
                        ft.tileUniqueIds.push_back(p.uniqueId);
                        auto shared = std::make_shared<SharedTerrainDoodad>();
                        shared->uniqueId = p.uniqueId;
                        shared->instanceId = instId;
                        ft.sharedDoodads.emplace(instId, shared);
                        sharedDoodads_[p.uniqueId] = shared;
                        for (auto& [tileCoord, tile] : loadedTiles) {
                            const auto& placements = tile->terrain.doodadPlacements;
                            if (std::any_of(placements.begin(), placements.end(), [&](const auto& other) {
                                    return other.uniqueId == p.uniqueId;
                                }) && tile->sharedDoodads.emplace(instId, shared).second) {
                                tile->m2InstanceIds.push_back(instId);
                            }
                        }
                    }
                    created++;
                }
            }
            if (ft.m2InstanceIndex < pending->m2Placements.size()) {
                return false; // More instances to create - yield
            }
            LOG_DEBUG("  Loaded doodads for tile [", x, ",", y, "]: ",
                     ft.m2InstanceIds.size(), " instances (", ft.uploadedM2ModelIds.size(), " new models)");
        }
#ifdef WOWEE_PS4
        decltype(pending->m2Placements){}.swap(pending->m2Placements);
#endif
        ft.phase = FinalizationPhase::WMO_MODELS;
        return false;
    }

    case FinalizationPhase::WMO_MODELS: {
        // Upload multiple WMO models per call (batched GPU uploads)
        if (wmoRenderer && assetManager) {
            if (!wmoRenderer->initialize(nullptr, VK_NULL_HANDLE, assetManager))
                LOG_WARNING("WMORenderer terrain re-init failed");
            // Diffuse decode and normal/height generation were completed by the
            // terrain worker. The main thread only uploads those prepared pixels.
            wmoRenderer->setPredecodedBLPCache(&pending->preloadedWMOTextures);
            wmoRenderer->setPredecodedNormalMapCache(
                &pending->preloadedWMONormalMaps,
                &pending->preloadedWMONormalMapVariances);
#ifdef WOWEE_PS4
            // Generate/upload one normal map at a time; never retain a whole
            // tile's expanded normal maps alongside its diffuse staging data.
            wmoRenderer->setDeferNormalMaps(false);
#else
            wmoRenderer->setDeferNormalMaps(true);
#endif

            // One model per step, and a large model spread across several
            // steps: a 286-group WMO took 131ms to upload in one go, against
            // this phase's 8ms budget. Uploading two per step when the workers
            // were idle only doubled that worst case.
#ifdef WOWEE_PS4
            constexpr float kWmoGroupBudgetMs = 2.0f;
#else
            constexpr float kWmoGroupBudgetMs = 6.0f;
#endif
            while (ft.wmoModelIndex < pending->wmoModels.size()) {
                auto& wmoReady = pending->wmoModels[ft.wmoModelIndex];
                if (wmoReady.uniqueId != 0 && placedWmoIds.count(wmoReady.uniqueId)) {
                    ft.wmoModelIndex++;
                    continue;
                }
                const auto result = wmoRenderer->loadModelIncremental(
                    wmoReady.model, wmoReady.modelId, kWmoGroupBudgetMs, /*terrainManaged=*/true);
#ifdef WOWEE_PS4
                wmoRenderer->releaseUploadedGeometry(wmoReady.model, wmoReady.modelId);
#endif
                if (result == WMORenderer::ModelLoadResult::InProgress) {
                    break;  // same model resumes on the next call
                }
                ft.wmoModelIndex++;  // Complete or Failed - either way, move on
                break;               // one model per step
            }
            wmoRenderer->setDeferNormalMaps(false);
            wmoRenderer->setPredecodedBLPCache(nullptr);
            wmoRenderer->setPredecodedNormalMapCache(nullptr, nullptr);
            if (ft.wmoModelIndex < pending->wmoModels.size()) return false;
        }
#ifdef WOWEE_PS4
        pending->preloadedWMOTextures.clear();
        pending->preloadedWMONormalMaps.clear();
        pending->preloadedWMONormalMapVariances.clear();
#endif
        ft.phase = FinalizationPhase::WMO_INSTANCES;
        return false;
    }

    case FinalizationPhase::WMO_INSTANCES: {
        // Create WMO instances incrementally to avoid stalls on tiles with many WMOs.
        // Liquid group loading is also budgeted (max 4 per call) to prevent stalls
        // on WMOs with many liquid groups (e.g. Stormwind canals).
        if (wmoRenderer && ft.wmoInstanceIndex < pending->wmoModels.size()) {
            constexpr size_t kWmoInstancesPerStep = 4;
            constexpr size_t kLiquidGroupsPerStep = 4;
            size_t created = 0;
            size_t liquidGroupsLoaded = 0;
            while (ft.wmoInstanceIndex < pending->wmoModels.size() && created < kWmoInstancesPerStep) {
                auto& wmoReady = pending->wmoModels[ft.wmoInstanceIndex];
                // Skip duplicates and unloaded models
                if (ft.wmoLiquidGroupIndex == 0 && wmoReady.uniqueId != 0 && placedWmoIds.count(wmoReady.uniqueId)) {
#ifdef WOWEE_PS4
                    wmoReady.model = {}; // Liquids consumed; renderer owns group resources.
#endif
                    ft.wmoInstanceIndex++;
                    ft.wmoLiquidGroupIndex = 0;
                    continue;
                }
                if (!wmoRenderer->isModelLoaded(wmoReady.modelId)) {
#ifdef WOWEE_PS4
                    wmoReady.model = {}; // Liquids consumed; renderer owns group resources.
#endif
                    ft.wmoInstanceIndex++;
                    ft.wmoLiquidGroupIndex = 0;
                    continue;
                }
                // Create the instance on first visit (liquidGroupIndex == 0)
                if (ft.wmoLiquidGroupIndex == 0) {
                    uint32_t wmoInstId = wmoRenderer->createInstance(
                        wmoReady.modelId, wmoReady.position, wmoReady.rotation, wmoReady.scale);
                    if (!wmoInstId) {
#ifdef WOWEE_PS4
                        wmoReady.model = {}; // Liquids consumed; renderer owns group resources.
#endif
                        ft.wmoInstanceIndex++;
                        continue;
                    }
                    ft.wmoInstanceIds.push_back(wmoInstId);
                    if (wmoReady.uniqueId != 0) {
                        std::lock_guard<std::mutex> lock(preparedWmoUniqueIdsMutex_);
                        placedWmoIds.insert(wmoReady.uniqueId);
                        ft.tileWmoUniqueIds.push_back(wmoReady.uniqueId);
                    }
                    if (wmoReady.uniqueId != 0) {
                        auto shared = std::make_shared<SharedTerrainWmo>();
                        shared->uniqueId = wmoReady.uniqueId;
                        shared->instanceId = wmoInstId;
                        ft.sharedWmos.push_back(shared);
                        {
                            std::lock_guard<std::mutex> lock(preparedWmoUniqueIdsMutex_);
                            sharedWmos_[shared->uniqueId] = shared;
                        }
                    }
                }
                // Load WMO liquids incrementally (canals, pools, etc.)
                if (waterRenderer) {
                    uint32_t wmoInstId = ft.wmoInstanceIds.back();
                    glm::mat4 modelMatrix = glm::mat4(1.0f);
                    modelMatrix = glm::translate(modelMatrix, wmoReady.position);
                    modelMatrix = glm::rotate(modelMatrix, wmoReady.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
                    modelMatrix = glm::rotate(modelMatrix, wmoReady.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
                    modelMatrix = glm::rotate(modelMatrix, wmoReady.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
                    const auto& groups = wmoReady.model.groups;
                    while (ft.wmoLiquidGroupIndex < groups.size() && liquidGroupsLoaded < kLiquidGroupsPerStep) {
                        const auto& group = groups[ft.wmoLiquidGroupIndex];
                        ft.wmoLiquidGroupIndex++;
                        if (!group.liquid.hasLiquid()) continue;
                        if (group.flags & 0x2000) {
                            uint16_t lt = group.liquid.materialId;
                            uint8_t basicType = (lt == 0) ? 0 : ((lt - 1) % 4);
                            if (basicType < 2) continue;
                        }
                        waterRenderer->loadFromWMO(group.liquid, modelMatrix, wmoInstId);
                        liquidGroupsLoaded++;
                    }
                    // More liquid groups remain on this WMO - yield
                    if (ft.wmoLiquidGroupIndex < groups.size()) {
                        return false;
                    }
                }
#ifdef WOWEE_PS4
                wmoReady.model = {}; // Liquids consumed; renderer owns group resources.
#endif
                ft.wmoInstanceIndex++;
                ft.wmoLiquidGroupIndex = 0;
                created++;
            }
            if (ft.wmoInstanceIndex < pending->wmoModels.size()) {
                return false; // More WMO instances to create - yield
            }
            LOG_DEBUG("  Loaded WMOs for tile [", x, ",", y, "]: ", ft.wmoInstanceIds.size(), " instances");
        }
#ifdef WOWEE_PS4
        // A retained parent already owns its children. The worker's fallback
        // placements are only needed if the previous owner vanished before
        // we could retain it. Remove duplicate children in one linear pass,
        // rather than spending a frame per duplicate or spawning them twice.
        pending->wmoDoodads.erase(std::remove_if(pending->wmoDoodads.begin(), pending->wmoDoodads.end(),
            [&](const auto& doodad) {
                return doodad.parentWmoUniqueId != 0 &&
                    std::find(ft.tileWmoUniqueIds.begin(), ft.tileWmoUniqueIds.end(), doodad.parentWmoUniqueId)
                        == ft.tileWmoUniqueIds.end() &&
                    std::any_of(ft.sharedWmos.begin(), ft.sharedWmos.end(), [&](const auto& shared) {
                        return shared->uniqueId == doodad.parentWmoUniqueId;
                    });
            }), pending->wmoDoodads.end());
#endif
        ft.phase = FinalizationPhase::WMO_DOODADS;
        return false;
    }

    case FinalizationPhase::WMO_DOODADS: {
        // Upload multiple WMO doodad M2s per call (batched GPU uploads)
        if (m2Renderer && ft.wmoDoodadIndex < pending->wmoDoodads.size()) {
            // Set pre-decoded BLP cache for doodad M2 textures
            m2Renderer->setPredecodedBLPCache(&pending->preloadedM2Textures);
#ifdef WOWEE_PS4
            constexpr size_t kDoodadsPerStep = 1;
#else
            constexpr size_t kDoodadsPerStep = 4;
#endif
            size_t uploaded = 0;
            while (ft.wmoDoodadIndex < pending->wmoDoodads.size() && uploaded < kDoodadsPerStep) {
                auto& doodad = pending->wmoDoodads[ft.wmoDoodadIndex];
                if (!m2Renderer->loadModel(doodad.model, doodad.modelId)) {
#ifdef WOWEE_PS4
                    doodad.model = {};
#endif
                    ft.wmoDoodadIndex++;
                    uploaded++;
                    continue;
                }
                {
                    std::lock_guard<std::mutex> lock(uploadedM2IdsMutex_);
                    uploadedM2Ids_.insert(doodad.modelId);
                }
                uint32_t wmoDoodadInstId = m2Renderer->createInstanceWithMatrix(
                    doodad.modelId, doodad.modelMatrix, doodad.worldPosition);
                if (wmoDoodadInstId) {
                    // WMO doodads should not add duplicate/over-aggressive wall
                    // blocking, but structural doodads such as Exodarplatform01
                    // carry the only authored floor for their walkable ramps.
                    m2Renderer->setSkipWallCollision(wmoDoodadInstId, true);
                    bool sharedChild = false;
                    if (doodad.parentWmoUniqueId != 0) {
                        for (const auto& shared : ft.sharedWmos) {
                            if (shared->uniqueId == doodad.parentWmoUniqueId) {
                                shared->doodadInstanceIds.push_back(wmoDoodadInstId);
                                sharedChild = true;
                                break;
                            }
                        }
                    }
                    if (!sharedChild) ft.m2InstanceIds.push_back(wmoDoodadInstId);
                }
#ifdef WOWEE_PS4
                doodad.model = {};
#endif
                ft.wmoDoodadIndex++;
                uploaded++;
            }
            m2Renderer->setPredecodedBLPCache(nullptr);
            if (ft.wmoDoodadIndex < pending->wmoDoodads.size()) return false;
        }
#ifdef WOWEE_PS4
        decltype(pending->wmoDoodads){}.swap(pending->wmoDoodads);
        pending->preloadedM2Textures.clear();
#endif
        completeSharedWmos(ft);
        ft.phase = FinalizationPhase::WATER;
        return false;
    }

    case FinalizationPhase::WATER: {
        if (pending->objectsOnly) { ft.phase = FinalizationPhase::AMBIENT; return false; }
        // Terrain water was already loaded in TERRAIN phase.
        // Generate water ambient emitters here.
        if (ambientSoundManager) {
            for (size_t chunkIdx = 0; chunkIdx < pending->terrain.waterData.size(); chunkIdx++) {
                const auto& chunkWater = pending->terrain.waterData[chunkIdx];
                if (!chunkWater.hasWater()) continue;

                int chunkX = chunkIdx % 16;
                int chunkY = chunkIdx / 16;
                float tileOriginX = (32.0f - x) * core::coords::TILE_SIZE;
                float tileOriginY = (32.0f - y) * core::coords::TILE_SIZE;
                float chunkCenterX = tileOriginX + (chunkX + 0.5f) * 33.333333f;
                float chunkCenterY = tileOriginY + (chunkY + 0.5f) * 33.333333f;

                if (!chunkWater.layers.empty()) {
                    const auto& layer = chunkWater.layers[0];
                    float waterHeight = layer.minHeight;
                    if (layer.liquidType == 0 && chunkIdx % 32 == 0) {
                        PendingTile::AmbientEmitter emitter;
                        emitter.position = glm::vec3(chunkCenterX, chunkCenterY, waterHeight);
                        emitter.type = 4;
                        pending->ambientEmitters.push_back(emitter);
                    } else if (layer.liquidType == 1 && chunkIdx % 64 == 0) {
                        PendingTile::AmbientEmitter emitter;
                        emitter.position = glm::vec3(chunkCenterX, chunkCenterY, waterHeight);
                        emitter.type = 4;
                        pending->ambientEmitters.push_back(emitter);
                    }
                }
            }
        }

        ft.phase = FinalizationPhase::AMBIENT;
        return false;
    }

    case FinalizationPhase::AMBIENT: {
        // Register ambient sound emitters
        if (ambientSoundManager && !pending->ambientEmitters.empty()) {
            for (const auto& emitter : pending->ambientEmitters) {
                auto type = static_cast<audio::AmbientSoundManager::AmbientType>(emitter.type);
                ambientSoundManager->addEmitter(emitter.position, type, tileKey(x, y) + 1);
            }
        }

        // Commit tile to loadedTiles
        retainSharedWmos(ft);
        retainSharedDoodads(ft);
        auto tile = std::make_unique<TerrainTile>();
        tile->coord = coord;
        tile->terrain = std::move(pending->terrain);
#ifndef WOWEE_PS4
        tile->mesh = std::move(pending->mesh);
#endif
        // PS4 keeps the ADT height/alpha data for collision and grass. No
        // runtime consumer reads TerrainTile::mesh; its uploaded vertex,
        // index and duplicate alpha arrays otherwise cost several MiB per
        // tile. Let the pending payload release them after the upload.
        tile->loaded = true;
        tile->objectsIncomplete = pending->objectsIncomplete;
        tile->nextObjectRetry = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        tile->m2InstanceIds = std::move(ft.m2InstanceIds);
        tile->wmoInstanceIds = std::move(ft.wmoInstanceIds);
        tile->wmoUniqueIds = std::move(ft.tileWmoUniqueIds);
        tile->doodadUniqueIds = std::move(ft.tileUniqueIds);
        tile->sharedWmos = std::move(ft.sharedWmos);
        tile->sharedDoodads = std::move(ft.sharedDoodads);
        getTileBounds(coord, tile->minX, tile->minY, tile->maxX, tile->maxY);
        if (pending->objectsOnly && loadedTiles.count(coord)) {
            // Repair missing objects in place. Do not replace visible ground,
            // water, collision data or successfully loaded buildings.
            auto& existing = *loadedTiles.at(coord);
            const auto append = [](auto& dst, auto& src) {
                for (const auto id : src) if (std::find(dst.begin(), dst.end(), id) == dst.end()) dst.push_back(id);
            };
            append(existing.m2InstanceIds, tile->m2InstanceIds);
            append(existing.wmoInstanceIds, tile->wmoInstanceIds);
            append(existing.wmoUniqueIds, tile->wmoUniqueIds);
            append(existing.doodadUniqueIds, tile->doodadUniqueIds);
            for (const auto& shared : tile->sharedWmos)
                if (std::find(existing.sharedWmos.begin(), existing.sharedWmos.end(), shared) == existing.sharedWmos.end())
                    existing.sharedWmos.push_back(shared);
            existing.sharedDoodads.insert(tile->sharedDoodads.begin(), tile->sharedDoodads.end());
            existing.objectsIncomplete = pending->objectsIncomplete;
            existing.nextObjectRetry = tile->nextObjectRetry;
            LOG_INFO("[TERRAIN_REPAIR] tile=[", x, ",", y, "] incomplete=", existing.objectsIncomplete);
        } else {
            loadedTiles[coord] = std::move(tile);
        }
        // NOTE: Don't cache pending here - std::move above empties terrain/mesh,
        // so the cached tile would have 0 valid chunks on reuse.  Tiles are
        // re-parsed from ADT files (file-cache hit) when they re-enter range.

        // Now safe to remove from pendingTiles (tile is in loadedTiles)
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            pendingTiles.erase(coord);
        }

        LOG_DEBUG("  Finalized tile [", x, ",", y, "]");

        ft.phase = FinalizationPhase::DONE;
        return true;
    }

    case FinalizationPhase::DONE:
        return true;
    }
    return true;
}

void TerrainManager::workerLoop() {
#ifdef WOWEE_PS4
    platform::ps4::registerCrashReportingThread("terrain worker");
#endif
    // Leave placement to the OS scheduler. Artificially reserving CPU 0 made
    // this worker policy depend on the old main-thread pin and reduced the
    // scheduler's ability to balance streaming with render workers.
    LOG_INFO("Terrain worker thread started");

    while (workerRunning.load()) {
        TileCoord coord;
        bool hasWork = false;
        bool objectsOnly = false;
        bool pressureProbe = false;
        TerrainPreparationBudget::Lease preparationLease;

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCV.wait(lock, [this]() {
                return !loadQueue.empty() || !workerRunning.load();
            });

            if (!workerRunning.load()) {
                break;
            }

            const auto& memMon = core::MemoryMonitor::getInstance();
#ifdef WOWEE_PS4
            // One decoded payload across ALL phases, not one per worker or
            // per queue. Draining readyQueue into finalizingTiles_ must not
            // admit another tile while the first still retains its textures.
            const uint64_t nowMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            const auto admission = preparationBudget_.acquire(1, memMon.isMemoryPressure(), nowMs);
            if (admission == TerrainPreparationBudget::Admission::Wait) {
                queueCV.wait_for(lock, std::chrono::milliseconds(100));
                continue;
            }
            pressureProbe = admission == TerrainPreparationBudget::Admission::PressureProbe;
#else
            // Preserve desktop pressure behavior and worker parallelism.
            if (memMon.isSevereMemoryPressure() || readyQueue.size() >= maxReadyQueueSize_ ||
                memMon.isMemoryPressure()) {
                queueCV.wait_for(lock, std::chrono::milliseconds(100));
                continue;
            }
            preparationBudget_.acquire(static_cast<size_t>(-1), false, 0);
#endif
            preparationLease = preparationBudget_.lease();

            if (!loadQueue.empty()) {
                coord = loadQueue.front();
                objectsOnly = objectRepairRequests_.erase(coord) != 0;
                loadQueue.pop_front();
                hasWork = true;
            }
        }

        if (hasWork) {
            // A throw here (bad_alloc on the console's small heap, a
            // filesystem_error) would leave the thread with no handler and
            // std::terminate the whole client. A failed tile is just a failed tile.
            decltype(prepareTile(coord.x, coord.y)) pending{};
            try {
                if (pressureProbe && assetManager) {
                    const size_t reclaimed = assetManager->trimFileCache();
                    LOG_WARNING("Terrain CPU pressure: single allocation probe tile=[", coord.x, ",", coord.y,
                                "] cacheReclaimed=", reclaimed, " bytes; free-page telemetry excludes malloc reuse");
                }
                pending = prepareTile(coord.x, coord.y, objectsOnly);
            } catch (const std::exception& e) {
                LOG_ERROR("Terrain worker: tile [", coord.x, ",", coord.y, "] failed: ", e.what());
            } catch (...) {
                LOG_ERROR("Terrain worker: tile [", coord.x, ",", coord.y, "] failed with an unknown exception");
            }

            std::lock_guard<std::mutex> lock(queueMutex);
            bool queued = false;
            if (pending) {
                pending->preparationLease = std::move(preparationLease);
                try {
                    readyQueue.push(std::move(pending));
                    queued = true;
                } catch (const std::exception& e) {
                    LOG_ERROR("Terrain ready queue: tile [", coord.x, ",", coord.y, "] failed: ", e.what());
                }
            }
            if (!queued) {
                pending.reset();
                pendingTiles.erase(coord);
            }
            queueCV.notify_all();
        }
    }

    LOG_INFO("Terrain worker thread stopped");
}

void TerrainManager::processReadyTiles() {
    ZoneScopedN("TerrainManager::processReadyTiles");
    // Move newly ready tiles into the finalizing deque.
    // Keep them in pendingTiles so streamTiles() won't re-enqueue them.
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        while (!readyQueue.empty()) {
            auto pending = readyQueue.front();
            readyQueue.pop();
            if (pending) {
                FinalizingTile ft;
                ft.pending = std::move(pending);
                if (ft.pending->objectsOnly) ft.phase = FinalizationPhase::M2_MODELS;
                finalizingTiles_.push_back(std::move(ft));
            }
        }
    }

    VkContext* vkCtx = terrainRenderer ? terrainRenderer->getVkContext() : nullptr;

    // Reclaim completed async uploads from previous frames (non-blocking)
    if (vkCtx) vkCtx->pollUploadBatches();

    // Nothing to finalize - done.
    if (finalizingTiles_.empty()) return;

#ifdef WOWEE_PS4
    // A teleport or a long taxi step can finish CPU preparation for a tile
    // which is no longer part of the working set. Discard its partial upload
    // through the ordinary cleanup path, never commit stale terrain.
    if (!stationaryPreload_.load() && currentTile.x >= 0 && currentTile.y >= 0) {
        const TileCoord coord = finalizingTiles_.front().pending->coord;
        if (!retainStreamTile(coord) ||
            (finalizingTiles_.front().pending->objectsOnly && !loadedTiles.count(coord))) {
            unloadTile(coord.x, coord.y);
            return;
        }
    }
#endif

    // Async upload batch: record GPU copies into a command buffer, submit with
    // a fence, but DON'T wait.  The fence is polled on subsequent frames.
    // This eliminates the main-thread stall from vkWaitForFences entirely.
    //
    // Time-budgeted: yield after 8ms to prevent main-loop stalls. Each
    // advanceFinalization step is designed to be small, but texture uploads
    // and M2 model loads can occasionally spike. The budget ensures we
    // spread heavy tiles across multiple frames instead of blocking.
    const auto budgetStart = std::chrono::steady_clock::now();
#ifdef WOWEE_PS4
    constexpr float budgetMs = 4.0f;
#else
    const float budgetMs = taxiStreamingMode_ ? 16.0f : 8.0f;
#endif

    if (vkCtx) vkCtx->beginUploadBatch();

    // The budget is checked between steps, so it bounds how many run - not how
    // long one takes. Most phases handle a single model per call, but TERRAIN
    // uploads a whole tile's chunks and textures, and the INSTANCES phases build
    // every instance at once, so one step can overrun the whole budget on its
    // own. Name the phase when it does, rather than leaving one opaque number.
    auto phaseName = [](FinalizationPhase p) {
        switch (p) {
            case FinalizationPhase::TERRAIN:       return "TERRAIN";
            case FinalizationPhase::M2_MODELS:     return "M2_MODELS";
            case FinalizationPhase::M2_INSTANCES:  return "M2_INSTANCES";
            case FinalizationPhase::WMO_MODELS:    return "WMO_MODELS";
            case FinalizationPhase::WMO_INSTANCES: return "WMO_INSTANCES";
            case FinalizationPhase::WMO_DOODADS:   return "WMO_DOODADS";
            case FinalizationPhase::WATER:         return "WATER";
            case FinalizationPhase::AMBIENT:       return "AMBIENT";
            case FinalizationPhase::DONE:          return "DONE";
        }
        return "?";
    };

    while (!finalizingTiles_.empty()) {
        auto& ft = finalizingTiles_.front();
        const auto phaseBefore = ft.phase;
        const auto stepStart = std::chrono::steady_clock::now();
        bool done = advanceFinalization(ft);
        const float stepMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - stepStart).count();
        if (stepMs > budgetMs) {
            ++finalizeOverrunCount_;
            finalizeOverrunWorstMs_ = std::max(finalizeOverrunWorstMs_, stepMs);
            // Keep severe stalls and periodic aggregate evidence, not a disk
            // write for every terrain chunk while streaming.
            if (finalizeOverrunCount_ <= 3 || finalizeOverrunCount_ % 60 == 0 || stepMs >= 40.0f) {
            LOG_WARNING("Terrain finalize step overran: ", phaseName(phaseBefore),
                        " took ", stepMs, "ms (budget ", budgetMs, "ms) tile=[",
                        ft.pending ? ft.pending->coord.x : -1, ",",
                        ft.pending ? ft.pending->coord.y : -1, "] count=", finalizeOverrunCount_,
                        " worstMs=", finalizeOverrunWorstMs_);
            }
        }
        if (done) {
            finalizingTiles_.pop_front();
        }
        float elapsed = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - budgetStart).count();
        if (elapsed >= budgetMs) break;
    }

    if (vkCtx) vkCtx->endUploadBatch();  // Async - submits but doesn't wait
}

void TerrainManager::processPendingUnloads() {
    ZoneScopedN("TerrainManager::processPendingUnloads");
    if (pendingUnloadQueue_.empty()) return;

    // Time-budgeted rather than count-capped (see pendingUnloadQueue_'s comment) so
    // throughput scales with whatever time is actually available this frame instead
    // of a fixed tile count that can't keep pace when frame rate drops or the queue
    // is unusually large (e.g. after a long, fast taxi flight). Taxi mode gets a
    // larger budget, matching processReadyTiles()'s existing taxi-aware budget.
    const auto budgetStart = std::chrono::steady_clock::now();
#ifdef WOWEE_PS4
    constexpr float budgetMs = 4.0f;
#else
    const float budgetMs = taxiStreamingMode_ ? 16.0f : 8.0f;
#endif

    size_t unloaded = 0;
    while (!pendingUnloadQueue_.empty()) {
        TileCoord coord = pendingUnloadQueue_.front();
        pendingUnloadQueue_.pop_front();

        // Skip stale entries: the player may have reversed course since this
        // tile was queued, bringing it back within range. Unloading it now
        // would pop visible terrain out from under them.
        if (retainStreamTile(coord)) continue;

        unloadTile(coord.x, coord.y);
        unloaded++;

        const float elapsed = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - budgetStart).count();
        if (elapsed >= budgetMs) break;
    }

    if (unloaded > 0) {
        LOG_DEBUG("Unloaded ", unloaded, " distant tiles (", pendingUnloadQueue_.size(),
                 " queued), ", loadedTiles.size(), " remain; unused models scheduled for retirement");
    }
}

void TerrainManager::processOneReadyTile() {
    // Move ready tiles into finalizing deque
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        while (!readyQueue.empty()) {
            auto pending = readyQueue.front();
            readyQueue.pop();
            if (pending) {
                FinalizingTile ft;
                ft.pending = std::move(pending);
                if (ft.pending->objectsOnly) ft.phase = FinalizationPhase::M2_MODELS;
                finalizingTiles_.push_back(std::move(ft));
            }
        }
    }
    // PS4 finalizes one phase step per call; desktop finalizes one full tile.
    if (!finalizingTiles_.empty()) {
        VkContext* vkCtx = terrainRenderer ? terrainRenderer->getVkContext() : nullptr;
        if (vkCtx) vkCtx->beginUploadBatch();

        auto& ft = finalizingTiles_.front();
#ifdef WOWEE_PS4
        // Submit and release staging after one phase step. Keeping every
        // decoded texture/model upload for a whole city tile in one outer
        // batch makes staging memory grow until the final instance is built.
        if (advanceFinalization(ft)) finalizingTiles_.pop_front();
#else
        while (!advanceFinalization(ft)) {}
        finalizingTiles_.pop_front();
#endif

        if (vkCtx) vkCtx->endUploadBatchSync();  // Sync - load screen needs data ready
    }
}

std::shared_ptr<PendingTile> TerrainManager::getCachedTile(const TileCoord& coord) {
    std::lock_guard<std::mutex> lock(tileCacheMutex_);
    auto it = tileCache_.find(coord);
    if (it == tileCache_.end()) return nullptr;
    tileCacheLru_.erase(it->second.lruIt);
    tileCacheLru_.push_front(coord);
    it->second.lruIt = tileCacheLru_.begin();
    return it->second.tile;
}

bool TerrainManager::makeResidentRoom(const TileCoord& incoming) {
#ifdef WOWEE_PS4
    // How many tiles the console can actually hold right now, rather than the
    // fixed ceiling. The ceiling was chosen for a console with room; a session
    // that has been running long enough to sit at 12 MiB free cannot honour it,
    // and going on loading tiles at that point is what turns "the interface has
    // no memory left" into std::bad_alloc a minute later. The cap never falls
    // below the ring the player is standing in, so this drops detail at the
    // edges instead of unloading the ground under them.
    const auto cpu = platform::ps4::queryAvailableCpuMemory();
    const auto admission = platform::ps4::worldStreamAdmissionFor(
        cpu.bytes, cpu.measured, streamHolding_);
    const bool holdingNow = admission != platform::ps4::WorldStreamAdmission::Admit;
    if (holdingNow != streamHolding_) {
        LOG_WARNING("Terrain streaming ", holdingNow ? "held back" : "resumed",
                    ": free=", cpu.bytes / (1024 * 1024), " MiB measured=", cpu.measured);
    }
    streamHolding_ = holdingNow;
    const unsigned residentCap = platform::ps4::residentTileCapFor(
        admission, ps4budget::kMaxResidentTiles, loadRadius);
    if (loadedTiles.count(incoming) || loadedTiles.size() < residentCap) return true;
    if (currentTile.x < 0 || currentTile.y < 0) return false;

    // Required collision/terrain always wins. Only the extra hysteresis ring
    // is eligible, and the farthest resident is retired first. Camera
    // direction is deliberately absent from this policy.
    auto victim = loadedTiles.end();
    int farthestSquared = 2 * loadRadius * loadRadius;
    for (auto it = loadedTiles.begin(); it != loadedTiles.end(); ++it) {
        const int dx = it->first.x - currentTile.x;
        const int dy = it->first.y - currentTile.y;
        const int distanceSquared = dx * dx + dy * dy;
        if (distanceSquared > farthestSquared) {
            victim = it;
            farthestSquared = distanceSquared;
        }
    }
    if (victim == loadedTiles.end()) return false;
    const TileCoord coord = victim->first;
    LOG_INFO("Terrain resident limit: retiring [", coord.x, ",", coord.y,
             "] before [", incoming.x, ",", incoming.y, "]; limit=", residentCap);
    unloadTile(coord.x, coord.y);
    return loadedTiles.size() < residentCap;
#else
    (void)incoming;
    return true;
#endif
}

bool TerrainManager::pinPreparedWmo(PendingTile& pending, uint32_t uniqueId) {
    if (!uniqueId) return false;
    std::lock_guard<std::mutex> lock(preparedWmoUniqueIdsMutex_);
    const auto it = sharedWmos_.find(uniqueId);
    if (it == sharedWmos_.end() || !it->second->assemblyComplete || it->second->retiring) return false;
    if (std::none_of(pending.sharedWmoPins.begin(), pending.sharedWmoPins.end(),
                    [uniqueId](const auto& shared) { return shared->uniqueId == uniqueId; })) {
        pending.sharedWmoPins.push_back(it->second);
    }
    return true;
}

void TerrainManager::collectUnusedSharedWmos() {
    // Keep the registry's last reference until main-thread retirement. A
    // worker may drop its last pin after cancellation; shared_ptr destruction
    // on that worker must never call any renderer or touch placement tables.
    std::vector<std::shared_ptr<SharedTerrainWmo>> retired;
    std::vector<uint32_t> parents, children;
    {
        std::lock_guard<std::mutex> lock(preparedWmoUniqueIdsMutex_);
        for (const auto& [uniqueId, shared] : sharedWmos_) {
            if (shared.use_count() != 1) continue;
            retired.push_back(shared);
            parents.push_back(shared->instanceId);
            children.insert(children.end(), shared->doodadInstanceIds.begin(), shared->doodadInstanceIds.end());
        }
        // Keep registry ownership through renderer destruction, which can
        // itself allocate. A propagated exception must not orphan live GPU
        // resources. Retiring records cannot be pinned or reused meanwhile.
        for (const auto& shared : retired) shared->retiring = true;
    }
    // Retire all children in one batch. Rebuilding the M2 spatial index once
    // per building made one tile unload take 157ms in the B28 travel log.
    for (uint32_t parent : parents) {
        if (waterRenderer) waterRenderer->removeWMO(parent);
    }
    if (wmoRenderer && !parents.empty()) wmoRenderer->removeInstances(parents);
    if (m2Renderer && !children.empty()) m2Renderer->removeInstances(children);
    {
        std::lock_guard<std::mutex> lock(preparedWmoUniqueIdsMutex_);
        for (const auto& shared : retired) {
            placedWmoIds.erase(shared->uniqueId);
            preparedWmoUniqueIds_.erase(shared->uniqueId);
            sharedWmos_.erase(shared->uniqueId);
        }
    }
}

void TerrainManager::completeSharedWmos(FinalizingTile& ft) {
    std::lock_guard<std::mutex> lock(preparedWmoUniqueIdsMutex_);
    for (const auto& shared : ft.sharedWmos) {
        if (shared->assemblyComplete || shared->retiring) continue;
        // Publish neighboring ownership only after the whole assembly is
        // ready. Cancelling a partial creator must not leave an incomplete
        // building pinned by an otherwise completed neighboring ADT.
        for (auto& [tileCoord, tile] : loadedTiles) {
            const auto& placements = tile->terrain.wmoPlacements;
            if (std::any_of(placements.begin(), placements.end(), [&](const auto& p) {
                    return p.uniqueId == shared->uniqueId;
                }) && std::none_of(tile->sharedWmos.begin(), tile->sharedWmos.end(), [&](const auto& p) {
                    return p->uniqueId == shared->uniqueId;
                })) tile->sharedWmos.push_back(shared);
        }
        shared->assemblyComplete = true;
    }
}

void TerrainManager::retainSharedWmos(FinalizingTile& ft) {
    std::lock_guard<std::mutex> lock(preparedWmoUniqueIdsMutex_);
    for (const auto& placement : ft.pending->terrain.wmoPlacements) {
        if (!placement.uniqueId) continue;
        if (std::any_of(ft.sharedWmos.begin(), ft.sharedWmos.end(), [&](const auto& shared) {
                return shared->uniqueId == placement.uniqueId;
            })) continue;
        const auto it = sharedWmos_.find(placement.uniqueId);
        if (it != sharedWmos_.end() && it->second->assemblyComplete && !it->second->retiring)
            ft.sharedWmos.push_back(it->second);
    }
    // Finalization now owns each matching reference; its normal unload path
    // also covers a partially uploaded tile. There is no unpinned interval.
    ft.pending->sharedWmoPins.clear();
}

void TerrainManager::releaseSharedWmos(
    std::vector<std::shared_ptr<SharedTerrainWmo>>& references,
    std::vector<uint32_t>& wmoIds, std::vector<uint32_t>& uniqueIds) {
    for (const auto& shared : references) {
        // Shared parents bypass ordinary per-tile cleanup. Registry retirement
        // handles the last tile or preparation pin and destroys every child once.
        wmoIds.erase(std::remove(wmoIds.begin(), wmoIds.end(), shared->instanceId), wmoIds.end());
        uniqueIds.erase(std::remove(uniqueIds.begin(), uniqueIds.end(), shared->uniqueId), uniqueIds.end());
    }
    references.clear();
    collectUnusedSharedWmos();
}

void TerrainManager::retainSharedDoodads(FinalizingTile& ft) {
    for (const auto& placement : ft.pending->terrain.doodadPlacements) {
        if (!placement.uniqueId) continue;
        const auto it = sharedDoodads_.find(placement.uniqueId);
        if (it == sharedDoodads_.end()) continue;
        if (auto shared = it->second.lock()) {
            if (ft.sharedDoodads.emplace(shared->instanceId, shared).second) {
                ft.m2InstanceIds.push_back(shared->instanceId);
            }
        }
    }
}

void TerrainManager::releaseSharedDoodads(SharedTerrainDoodads& references,
                                         std::vector<uint32_t>& instanceIds,
                                         std::vector<uint32_t>& uniqueIds) {
    // Keep last-reference IDs in the existing batch so removal rebuilds the
    // renderer's spatial/index tables once, not once per tree in a dense tile.
    instanceIds.erase(std::remove_if(instanceIds.begin(), instanceIds.end(), [&](uint32_t id) {
        const auto it = references.find(id);
        return it != references.end() && it->second.use_count() > 1;
    }), instanceIds.end());
    uniqueIds.erase(std::remove_if(uniqueIds.begin(), uniqueIds.end(), [&](uint32_t uid) {
        return sharedDoodads_.count(uid) != 0;
    }), uniqueIds.end());
    for (const auto& [id, shared] : references) {
        if (shared.use_count() != 1) continue;
        placedDoodadIds.erase(shared->uniqueId);
        sharedDoodads_.erase(shared->uniqueId);
    }
    references.clear();
}

void TerrainManager::collectPendingWmoModels(std::unordered_set<uint32_t>& ids) const {
    for (const auto& ft : finalizingTiles_) if (ft.pending)
        for (const auto& model : ft.pending->wmoModels) ids.insert(model.modelId);
}

void TerrainManager::collectPendingM2Models(std::unordered_set<uint32_t>& ids) const {
    // Uploaded models can be instanceless for more than five seconds while a
    // large tile finalizes. Their GPU data must survive until placement ends.
    for (const auto& ft : finalizingTiles_) if (ft.pending) {
        for (const auto& model : ft.pending->m2Models) ids.insert(model.modelId);
        for (const auto& place : ft.pending->m2Placements) ids.insert(place.modelId);
        for (const auto& child : ft.pending->wmoDoodads) ids.insert(child.modelId);
    }
}

void TerrainManager::unloadTile(int x, int y) {
    TileCoord coord = {.x = x, .y = y};

    // Also remove from pending if it was queued but not yet loaded
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        pendingTiles.erase(coord);
    }

    // Remove from finalizingTiles_ if it's being incrementally finalized.
    // Water may have already been loaded in TERRAIN phase, so clean it up.
    for (auto fit = finalizingTiles_.begin(); fit != finalizingTiles_.end(); ++fit) {
        if (fit->pending && fit->pending->coord == coord) {
            releaseSharedWmos(fit->sharedWmos, fit->wmoInstanceIds, fit->tileWmoUniqueIds);
            releaseSharedDoodads(fit->sharedDoodads, fit->m2InstanceIds, fit->tileUniqueIds);
            // If terrain chunks were already uploaded, free their descriptor sets
            if ((fit->terrainMeshDone || fit->terrainChunkNext > 0) && terrainRenderer) {
                terrainRenderer->removeTile(x, y);
            }
            // If past TERRAIN phase, water was already loaded - remove it
            if (!fit->pending->objectsOnly && fit->phase != FinalizationPhase::TERRAIN && waterRenderer) {
                waterRenderer->removeTile(x, y);
            }
            // Clean up any M2/WMO instances that were already created
            if (m2Renderer && !fit->m2InstanceIds.empty()) {
                m2Renderer->removeInstances(fit->m2InstanceIds);
            }
            if (wmoRenderer && !fit->wmoInstanceIds.empty()) {
                for (uint32_t id : fit->wmoInstanceIds) {
                    if (waterRenderer) waterRenderer->removeWMO(id);
                }
                wmoRenderer->removeInstances(fit->wmoInstanceIds);
            }
            for (uint32_t uid : fit->tileUniqueIds) placedDoodadIds.erase(uid);
            for (uint32_t uid : fit->tileWmoUniqueIds) {
                std::lock_guard<std::mutex> lock(preparedWmoUniqueIdsMutex_);
                placedWmoIds.erase(uid);
                preparedWmoUniqueIds_.erase(uid);
            }
#ifdef WOWEE_PS4
            // Single-payload preparation means these unpublished claims
            // belong to this discarded tile. Keep claims for live instances.
            {
                std::lock_guard<std::mutex> lock(preparedWmoUniqueIdsMutex_);
                for (const auto& wmo : fit->pending->wmoModels) {
                    if (wmo.uniqueId && !placedWmoIds.count(wmo.uniqueId)) {
                        preparedWmoUniqueIds_.erase(wmo.uniqueId);
                    }
                }
            }
#endif
            finalizingTiles_.erase(fit);
            if (!loadedTiles.count(coord)) collisionTiles_.erase(tileKey(x, y));
            break; // A canceled repair may also have a resident tile to unload.
        }
    }

    auto it = loadedTiles.find(coord);
    if (it == loadedTiles.end()) {
        return;
    }

    LOG_INFO("Unloading terrain tile [", x, ",", y, "]");

    const auto& tile = it->second;

    if (ambientSoundManager) ambientSoundManager->removeEmitters(tileKey(x, y) + 1);

    releaseSharedWmos(tile->sharedWmos, tile->wmoInstanceIds, tile->wmoUniqueIds);
    releaseSharedDoodads(tile->sharedDoodads, tile->m2InstanceIds, tile->doodadUniqueIds);

    // Remove doodad unique IDs from dedup set
    for (uint32_t uid : tile->doodadUniqueIds) {
        placedDoodadIds.erase(uid);
    }
    for (uint32_t uid : tile->wmoUniqueIds) {
        std::lock_guard<std::mutex> lock(preparedWmoUniqueIdsMutex_);
        placedWmoIds.erase(uid);
        preparedWmoUniqueIds_.erase(uid);
    }

    // Remove M2 doodad instances
    if (m2Renderer) {
        m2Renderer->removeInstances(tile->m2InstanceIds);
        LOG_DEBUG("  Removed ", tile->m2InstanceIds.size(), " M2 instances");
    }

    // Remove WMO instances and their liquids
    if (wmoRenderer) {
        for (uint32_t id : tile->wmoInstanceIds) {
            // Remove WMO liquids associated with this instance
            if (waterRenderer) {
                waterRenderer->removeWMO(id);
            }
        }
        wmoRenderer->removeInstances(tile->wmoInstanceIds);
        LOG_DEBUG("  Removed ", tile->wmoInstanceIds.size(), " WMO instances");
    }

    // Remove terrain chunks for this tile
    if (terrainRenderer) {
        terrainRenderer->removeTile(x, y);
    }

    // Remove water surfaces for this tile
    if (waterRenderer) {
        waterRenderer->removeTile(x, y);
    }

    loadedTiles.erase(it);
    collisionTiles_.erase(tileKey(x, y));
}

bool TerrainManager::startWorkers() {
    if (workerRunning.load()) return true;
    workerRunning.store(true);
    try {
        workerThreads.reserve(workerCount);
        for (int i = 0; i < workerCount; ++i) {
            workerThreads.emplace_back(&TerrainManager::workerLoop, this);
        }
    } catch (const std::exception& e) {
        // A partially constructed pool still owns joinable threads. Join it
        // before returning failure instead of terminating in their destructors.
        stopWorkers();
        LOG_ERROR("Terrain worker pool could not start: ", e.what());
        return false;
    }
    return true;
}

void TerrainManager::stopWorkers() {
    if (!workerRunning.load() && workerThreads.empty()) {
        LOG_DEBUG("stopWorkers: already stopped");
        return;
    }
    LOG_DEBUG("stopWorkers: signaling ", workerThreads.size(), " workers to stop...");
    // Serialize the predicate transition with queueCV.wait(). An atomic store
    // alone can land between the worker's predicate check and its sleep,
    // losing the only wake-up and hanging logout/quit in join().
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        workerRunning.store(false);
    }
    queueCV.notify_all();

    // Workers check workerRunning at each I/O point in prepareTile() and bail
    // out quickly.  Use plain join() which is safe with std::thread - no
    // pthread_timedjoin_np (which silently joins the pthread but leaves the
    // std::thread object thinking it's still joinable → std::terminate on dtor).
    for (size_t i = 0; i < workerThreads.size(); i++) {
        if (workerThreads[i].joinable()) {
            LOG_DEBUG("stopWorkers: joining worker ", i, "...");
            workerThreads[i].join();
        }
    }
    workerThreads.clear();
    LOG_DEBUG("stopWorkers: done");
}

void TerrainManager::softReset() {
    // A worker may still own old-map data even after its queue becomes empty.
    // Join before resetting map state, caches or render resources.
    stopWorkers();
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        loadQueue.clear();
        objectRepairRequests_.clear();
        while (!readyQueue.empty()) readyQueue.pop();
        finalizingTiles_.clear();
    }
    queueCV.notify_all();
    pendingTiles.clear();
    placedDoodadIds.clear();
    placedWmoIds.clear();
    sharedWmos_.clear();
    sharedDoodads_.clear();
    {
        std::lock_guard<std::mutex> lock(uploadedM2IdsMutex_);
        uploadedM2Ids_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(preparedWmoUniqueIdsMutex_);
        preparedWmoUniqueIds_.clear();
    }

    // Clear tile cache - keys are (x,y) without map name, so stale entries from
    // a different map with overlapping coordinates would produce wrong geometry.
    {
        std::lock_guard<std::mutex> lock(tileCacheMutex_);
        tileCache_.clear();
        tileCacheLru_.clear();
    }

    LOG_INFO("Resetting terrain (workers joined; clearing tiles, water and caches)");
    if (ambientSoundManager) for (const auto& [coord, tile] : loadedTiles)
        ambientSoundManager->removeEmitters(tileKey(coord.x, coord.y) + 1);
    loadedTiles.clear();
    failedTiles.clear();
    collisionTiles_.clear();
    pendingUnloadQueue_.clear();

    currentTile = {.x = -1, .y = -1};
    lastStreamTile = {.x = -1, .y = -1};

    if (terrainRenderer) {
        terrainRenderer->clear();
    }
    if (waterRenderer) {
        waterRenderer->clear();
    }
    if (workerCount > 0 && !startWorkers()) {
        throw std::runtime_error("Terrain streaming workers could not restart");
    }
}

TileCoord TerrainManager::worldToTile(float glX, float glY) const {
    auto [tileX, tileY] = core::coords::worldToTile(glX, glY);
    return {.x = tileX, .y = tileY};
}

bool TerrainManager::retainStreamTile(const TileCoord& coord) const {
#ifdef WOWEE_PS4
    if (!stationaryPreload_.load() && currentTile.x >= 0 && currentTile.y >= 0)
        return ps4budget::tileInRange(coord.x, coord.y, streamPosition_.x, streamPosition_.y,
                                      streamViewDistance_, true);
#endif
    const int dx = coord.x - currentTile.x, dy = coord.y - currentTile.y;
    return dx * dx + dy * dy <= unloadRadius * unloadRadius;
}

void TerrainManager::getTileBounds(const TileCoord& coord, float& minX, float& minY,
                                    float& maxX, float& maxY) const {
    // Calculate world bounds for this tile
    // Tile (32, 32) is at origin
    float offsetX = (32 - coord.x) * TILE_SIZE;
    float offsetY = (32 - coord.y) * TILE_SIZE;

    minX = offsetX - TILE_SIZE;
    minY = offsetY - TILE_SIZE;
    maxX = offsetX;
    maxY = offsetY;
}

std::string TerrainManager::getADTPath(const TileCoord& coord) const {
    // Format: World\Maps\{MapName}\{MapName}_{X}_{Y}.adt
    return "World\\Maps\\" + mapName + "\\" + mapName + "_" +
           std::to_string(coord.x) + "_" + std::to_string(coord.y) + ".adt";
}

void TerrainManager::ensureGroundEffectTablesLoaded() {
    if (groundEffectsLoaded_ || !assetManager) return;
    groundEffectsLoaded_ = true;

    auto groundEffectTex = assetManager->loadDBC("GroundEffectTexture.dbc");
    auto groundEffectDoodad = assetManager->loadDBC("GroundEffectDoodad.dbc");
    if (!groundEffectTex || !groundEffectDoodad) {
        LOG_WARNING("Ground clutter DBCs missing; skipping procedural ground effects");
        return;
    }

    // GroundEffectTexture: id + 4 doodad IDs + 4 weights + density + sound
    for (uint32_t i = 0; i < groundEffectTex->getRecordCount(); ++i) {
        uint32_t effectId = groundEffectTex->getUInt32(i, 0);
        if (effectId == 0) continue;

        GroundEffectEntry e;
        e.doodadIds[0] = groundEffectTex->getUInt32(i, 1);
        e.doodadIds[1] = groundEffectTex->getUInt32(i, 2);
        e.doodadIds[2] = groundEffectTex->getUInt32(i, 3);
        e.doodadIds[3] = groundEffectTex->getUInt32(i, 4);
        e.weights[0] = groundEffectTex->getUInt32(i, 5);
        e.weights[1] = groundEffectTex->getUInt32(i, 6);
        e.weights[2] = groundEffectTex->getUInt32(i, 7);
        e.weights[3] = groundEffectTex->getUInt32(i, 8);
        e.density = groundEffectTex->getUInt32(i, 9);
        groundEffectById_[effectId] = e;
    }

    // GroundEffectDoodad: id + modelName(offset) + flags
    for (uint32_t i = 0; i < groundEffectDoodad->getRecordCount(); ++i) {
        uint32_t doodadId = groundEffectDoodad->getUInt32(i, 0);
        std::string modelName = groundEffectDoodad->getString(i, 1);
        if (doodadId == 0 || modelName.empty()) continue;

        std::string lower = toLowerCopy(modelName);
        if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".mdl") {
            lower = lower.substr(0, lower.size() - 4) + ".m2";
        }
        if (lower.find('\\') != std::string::npos || lower.find('/') != std::string::npos) {
            groundDoodadModelById_[doodadId] = lower;
        } else {
            groundDoodadModelById_[doodadId] = "World\\NoDXT\\Detail\\" + lower;
        }
    }

    LOG_INFO("Ground clutter tables loaded: ", groundEffectById_.size(),
             " effects, ", groundDoodadModelById_.size(), " doodad models");
}

void TerrainManager::generateGroundClutterPlacements(std::shared_ptr<PendingTile>& pending,
                                                     std::unordered_set<uint32_t>& preparedModelIds) {
    if (taxiStreamingMode_) return;  // Skip clutter while on taxi flights.
    if (!pending || groundEffectById_.empty() || groundDoodadModelById_.empty()) return;

    static const std::string kGroundClutterProxyModel = "World\\NoDXT\\Detail\\ElwGra01.m2";
    static bool loggedProxy = false;
    if (!loggedProxy) {
        LOG_INFO("Ground clutter: forcing proxy model ", kGroundClutterProxyModel);
        loggedProxy = true;
    }

    std::unordered_set<uint32_t> unavailableModels;
    size_t modelMissing = 0;
    size_t modelInvalid = 0;
    // How many placements ended up as the Elwynn proxy because the doodad the
    // texture actually asked for would not load.
    size_t proxyFallbackUsed = 0;
    auto ensureModelPrepared = [&](const std::string& m2Path, uint32_t modelId) -> bool {
        if (preparedModelIds.count(modelId)) return true;
        if (unavailableModels.count(modelId)) return false;
        // Insert before work so exceptions or pressure cannot make later
        // placements thrash the same file. This set dies with this tile job.
        unavailableModels.insert(modelId);

        std::vector<uint8_t> m2Data = assetManager->readFileOptional(m2Path);
        if (m2Data.empty()) {
            modelMissing++;
            return false;
        }

        pipeline::M2Model m2Model = pipeline::M2Loader::load(m2Data);
        m2Model.name = m2Path;
        std::string skinPath = pipeline::skinPathForM2(m2Path);
        std::vector<uint8_t> skinData = assetManager->readFileOptional(skinPath);
        if (!skinData.empty() && m2Model.version >= 264) {
            pipeline::M2Loader::loadSkin(skinData, m2Model);
        }
        if (!m2Model.isValid()) {
            modelInvalid++;
            return false;
        }

        PendingTile::M2Ready ready;
        ready.modelId = modelId;
        ready.model = std::move(m2Model);
        ready.path = m2Path;
        pending->m2Models.push_back(std::move(ready));
        preparedModelIds.insert(modelId);
        return true;
    };

    constexpr float unitSize = CHUNK_SIZE / 8.0f;
    constexpr float pi = core::coords::PI;
    // The ceiling for a whole tile, and how many placements one texture layer
    // of one chunk may try for.
    //
    // The ceiling used to be spent in chunk scan order: the loops below run
    // cy 0..15, cx 0..15 and break the moment the running total reaches it, so
    // a tile's whole allowance went to the first few rows of chunks and the
    // rest of the tile got nothing at all. Measured against Mulgore's own
    // layers that was about six of sixteen rows filled and ten empty, which is
    // why standing in the wrong part of a tile showed no ground cover
    // whatsoever. The ceiling is now shared out as a per-chunk budget, so it
    // bounds the tile the same way while covering all of it.
    constexpr size_t kBaseMaxGroundClutterPerTile = 880;
    constexpr uint32_t kBaseMaxAttemptsPerLayer = 12;
    const float densityScaleRaw = glm::clamp(groundClutterDensityScale_, 0.0f, 1.5f);
    // Keep runtime density bounded to avoid large streaming spikes in dense tiles.
    const float densityScale = std::min(densityScaleRaw, 1.0f);
    const size_t kMaxGroundClutterPerTile = std::max<size_t>(
        0, static_cast<size_t>(std::lround(static_cast<float>(kBaseMaxGroundClutterPerTile) * densityScale)));
    const uint32_t kMaxAttemptsPerLayer = std::max<uint32_t>(
        1u, static_cast<uint32_t>(std::lround(static_cast<float>(kBaseMaxAttemptsPerLayer) * densityScale)));
    // A tile is 16x16 chunks. Dividing rather than rounding up keeps the sum of
    // the chunk budgets under the tile ceiling, so the ceiling stays a backstop
    // and never becomes the thing that decides where the cover stops.
    const size_t kMaxGroundClutterPerChunk =
        std::max<size_t>(1, kMaxGroundClutterPerTile / 256);
    std::vector<uint8_t> alphaScratch;
    std::vector<uint8_t> alphaScratchTex;
    size_t added = 0;
    size_t attemptsTotal = 0;
    size_t alphaRejected = 0;
    size_t roadRejected = 0;
    size_t noEffectMatch = 0;
    size_t textureIdFallbackMatch = 0;
    size_t noDoodadModel = 0;
    std::array<uint16_t, 256> perChunkAdded{};

    // pipeline::isRoadLikeTexture - shared with grass, which needs the same
    // test for the same reason and used to lack it.
    auto isRoadLikeTexture = [](const std::string& texPath) -> bool {
        return pipeline::isRoadLikeTexture(texPath);
    };

    auto layerWeightAt = [&](const pipeline::MapChunk& chunk, size_t layerIdx, int alphaIndex) -> int {
        if (layerIdx >= chunk.layers.size()) return 0;
        if (layerIdx == 0) {
            int accum = 0;
            size_t numLayers = std::min(chunk.layers.size(), static_cast<size_t>(4));
            for (size_t i = 1; i < numLayers; ++i) {
                int a = 0;
                if (decodeLayerAlpha(chunk, i, alphaScratchTex) &&
                    alphaIndex >= 0 &&
                    alphaIndex < static_cast<int>(alphaScratchTex.size())) {
                    a = alphaScratchTex[alphaIndex];
                }
                accum += a;
            }
            return glm::clamp(255 - accum, 0, 255);
        }
        if (decodeLayerAlpha(chunk, layerIdx, alphaScratchTex) &&
            alphaIndex >= 0 &&
            alphaIndex < static_cast<int>(alphaScratchTex.size())) {
            return alphaScratchTex[alphaIndex];
        }
        return 0;
    };

    auto hasRoadLikeTextureAt = [&](const pipeline::MapChunk& chunk, float fracX, float fracY) -> bool {
        if (chunk.layers.empty()) return false;
        int alphaX = glm::clamp(static_cast<int>((fracX / 8.0f) * 63.0f), 0, 63);
        int alphaY = glm::clamp(static_cast<int>((fracY / 8.0f) * 63.0f), 0, 63);
        int alphaIndex = alphaY * 64 + alphaX;

        size_t numLayers = std::min(chunk.layers.size(), static_cast<size_t>(4));
        for (size_t layerIdx = 0; layerIdx < numLayers; ++layerIdx) {
            uint32_t texId = chunk.layers[layerIdx].textureId;
            if (texId >= pending->terrain.textures.size()) continue;
            const std::string& texPath = pending->terrain.textures[texId];
            if (!isRoadLikeTexture(texPath)) continue;
            // Treat meaningful blend contribution as road occupancy.
            int w = layerWeightAt(chunk, layerIdx, alphaIndex);
            if (w >= 24) return true;
        }
        return false;
    };

    for (int cy = 0; cy < 16; ++cy) {
        if (added >= kMaxGroundClutterPerTile) break;
        for (int cx = 0; cx < 16; ++cx) {
            if (added >= kMaxGroundClutterPerTile) break;
            const auto& chunk = pending->terrain.getChunk(cx, cy);
            if (!chunk.hasHeightMap() || chunk.layers.empty()) continue;

            size_t chunkAdded = 0;
            for (size_t layerIdx = 0; layerIdx < chunk.layers.size(); ++layerIdx) {
                if (added >= kMaxGroundClutterPerTile) break;
                if (chunkAdded >= kMaxGroundClutterPerChunk) break;
                const auto& layer = chunk.layers[layerIdx];
                if (layer.effectId == 0) continue;

                auto geIt = groundEffectById_.find(layer.effectId);
                if (geIt == groundEffectById_.end() && layer.textureId != 0) {
                    geIt = groundEffectById_.find(layer.textureId);
                    if (geIt != groundEffectById_.end()) {
                        textureIdFallbackMatch++;
                    }
                }
                if (geIt == groundEffectById_.end()) {
                    noEffectMatch++;
                    continue;
                }
                const GroundEffectEntry& ge = geIt->second;

                uint32_t totalWeight = ge.weights[0] + ge.weights[1] + ge.weights[2] + ge.weights[3];
                if (totalWeight == 0) totalWeight = 4;

                uint32_t density = std::min<uint32_t>(ge.density, 16u);
                density = static_cast<uint32_t>(std::lround(static_cast<float>(density) * densityScale));
                if (density == 0) continue;
                uint32_t attempts = std::max<uint32_t>(3u, density * 2u);
                attempts = std::min<uint32_t>(attempts, kMaxAttemptsPerLayer);
                attemptsTotal += attempts;

                bool hasAlpha = decodeLayerAlpha(chunk, layerIdx, alphaScratch);
                uint32_t seed = static_cast<uint32_t>(
                    ((pending->coord.x & 0xFF) << 24) ^
                    ((pending->coord.y & 0xFF) << 16) ^
                    ((cx & 0x1F) << 8) ^
                    ((cy & 0x1F) << 3) ^
                    (layerIdx & 0x7));
                auto nextRand = [&seed]() -> uint32_t {
                    seed = seed * 1664525u + 1013904223u;
                    return seed;
                };

                for (uint32_t a = 0; a < attempts; ++a) {
                    float fracX = (nextRand() & 0xFFFFu) / kRand16Max * 8.0f;
                    float fracY = (nextRand() & 0xFFFFu) / kRand16Max * 8.0f;

                    if (hasAlpha && !alphaScratch.empty()) {
                        int alphaX = glm::clamp(static_cast<int>((fracX / 8.0f) * 63.0f), 0, 63);
                        int alphaY = glm::clamp(static_cast<int>((fracY / 8.0f) * 63.0f), 0, 63);
                        int alphaIndex = alphaY * 64 + alphaX;
                        if (alphaIndex < 0 || alphaIndex >= static_cast<int>(alphaScratch.size())) continue;
                        if (alphaScratch[alphaIndex] < 64) {
                            alphaRejected++;
                            continue;
                        }
                    }

                    if (hasRoadLikeTextureAt(chunk, fracX, fracY)) {
                        roadRejected++;
                        continue;
                    }

                    uint32_t roll = nextRand() % totalWeight;
                    int pick = 0;
                    uint32_t acc = 0;
                    for (int i = 0; i < 4; ++i) {
                        uint32_t w = ge.weights[i] > 0 ? ge.weights[i] : 1;
                        acc += w;
                        if (roll < acc) { pick = i; break; }
                    }
                    uint32_t doodadId = ge.doodadIds[pick];
                    if (doodadId == 0) continue;

                    auto doodadIt = groundDoodadModelById_.find(doodadId);
                    if (doodadIt == groundDoodadModelById_.end()) {
                        noDoodadModel++;
                        continue;
                    }
                    const std::string& doodadModelPath = doodadIt->second;
                    uint32_t modelId = static_cast<uint32_t>(std::hash<std::string>{}(doodadModelPath));
                    if (!ensureModelPrepared(doodadModelPath, modelId)) {
                        modelId = static_cast<uint32_t>(std::hash<std::string>{}(kGroundClutterProxyModel));
                        if (!ensureModelPrepared(kGroundClutterProxyModel, modelId)) {
                            continue;
                        }
                        ++proxyFallbackUsed;
                    }

                    const glm::vec3 surfacePoint = pipeline::TerrainMeshGenerator::chunkSurfacePoint(
                        chunk.position, chunk.heightMap, fracX, fracY, unitSize);
                    const float worldX = surfacePoint.x;
                    const float worldY = surfacePoint.y;
                    const float worldZ = surfacePoint.z;

                    PendingTile::M2Placement p;
                    p.modelId = modelId;
                    p.uniqueId = 0;
                    // MCNK chunk.position is already in terrain/render world space.
                    // Do not convert via ADT placement mapping (that is for MDDF/MODF records).
                    p.rotation = glm::vec3(0.0f, 0.0f, (nextRand() & 0xFFFFu) / kRand16Max * (2.0f * pi));
                    p.scale = 0.80f + ((nextRand() & 0xFFFFu) / kRand16Max) * 0.35f;
                    // Snap directly to sampled terrain height.
                    p.position = glm::vec3(worldX, worldY, worldZ + 0.01f);
                    pending->m2Placements.push_back(p);
                    added++;
                    chunkAdded++;
                    perChunkAdded[cy * 16 + cx]++;
                    if (added >= kMaxGroundClutterPerTile) break;
                    if (chunkAdded >= kMaxGroundClutterPerChunk) break;
                }
            }
        }
    }

    size_t fallbackAdded = 0;
    const size_t kMinGroundClutterPerTile = static_cast<size_t>(std::lround(40.0f * densityScale));
    size_t fallbackNeeded = (added < kMinGroundClutterPerTile) ? (kMinGroundClutterPerTile - added) : 0;
    if (fallbackNeeded > 0) {
        const uint32_t proxyModelId = static_cast<uint32_t>(std::hash<std::string>{}(kGroundClutterProxyModel));
        if (ensureModelPrepared(kGroundClutterProxyModel, proxyModelId)) {
            constexpr uint32_t kFallbackPerChunk = 2;
            for (int cy = 0; cy < 16; ++cy) {
                for (int cx = 0; cx < 16; ++cx) {
                    if (fallbackAdded >= fallbackNeeded || added >= kMaxGroundClutterPerTile) break;
                    const auto& chunk = pending->terrain.getChunk(cx, cy);
                    if (!chunk.hasHeightMap()) continue;

                    for (uint32_t i = 0; i < kFallbackPerChunk; ++i) {
                        if (fallbackAdded >= fallbackNeeded || added >= kMaxGroundClutterPerTile) break;
                        // Deterministic scatter so the tile stays visually stable.
                        uint32_t seed = static_cast<uint32_t>(
                            ((pending->coord.x & 0xFF) << 24) ^
                            ((pending->coord.y & 0xFF) << 16) ^
                            ((cx & 0x1F) << 8) ^
                            ((cy & 0x1F) << 3) ^
                            (i & 0x7));
                        auto nextRand = [&seed]() -> uint32_t {
                            seed = seed * 1664525u + 1013904223u;
                            return seed;
                        };

                        float fracX = (nextRand() & 0xFFFFu) / kRand16Max * 8.0f;
                        float fracY = (nextRand() & 0xFFFFu) / kRand16Max * 8.0f;
                        if (hasRoadLikeTextureAt(chunk, fracX, fracY)) {
                            roadRejected++;
                            continue;
                        }
                        const glm::vec3 surfacePoint = pipeline::TerrainMeshGenerator::chunkSurfacePoint(
                            chunk.position, chunk.heightMap, fracX, fracY, unitSize);
                        const float worldX = surfacePoint.x;
                        const float worldY = surfacePoint.y;
                        const float worldZ = surfacePoint.z;

                        PendingTile::M2Placement p;
                        p.modelId = proxyModelId;
                        p.uniqueId = 0;
                        p.rotation = glm::vec3(0.0f, 0.0f, (nextRand() & 0xFFFFu) / kRand16Max * (2.0f * pi));
                        p.scale = 0.75f + ((nextRand() & 0xFFFFu) / kRand16Max) * 0.40f;
                        p.position = glm::vec3(worldX, worldY, worldZ + 0.01f);
                        pending->m2Placements.push_back(p);
                        fallbackAdded++;
                        added++;
                        perChunkAdded[cy * 16 + cx]++;
                    }
                }
                if (fallbackAdded >= fallbackNeeded || added >= kMaxGroundClutterPerTile) break;
            }
        }
    }

    // Baseline pass disabled: one-per-chunk fill caused large instance spikes and hitches
    // when streaming tiles around the player.
    size_t baselineAdded = 0;

    if (added > 0) {
        static int clutterLogCount = 0;
        if (clutterLogCount < 12) {
            // At warning, with the counts beside it. Elwynn grass was
            // reported growing in Hellfire Peninsula, and the two places that
            // can put it there - a doodad whose model will not load, and the
            // minimum-per-tile floor below - both report only here.
            LOG_WARNING("Ground clutter tile [", pending->coord.x, ",", pending->coord.y,
                     "] added=", added, " attempts=", attemptsTotal,
                     " proxyFallback=", proxyFallbackUsed,
                     " fallbackAdded=", fallbackAdded,
                     " baselineAdded=", baselineAdded,
                     " roadRejected=", roadRejected);
            clutterLogCount++;
        }
    } else {
        static int noClutterLogCount = 0;
        if (noClutterLogCount < 8) {
            LOG_WARNING("Ground clutter tile [", pending->coord.x, ",", pending->coord.y,
                     "] added=0 attempts=", attemptsTotal,
                     " alphaRejected=", alphaRejected,
                     " roadRejected=", roadRejected,
                     " noEffect=", noEffectMatch,
                     " textureFallback=", textureIdFallbackMatch,
                     " noDoodadModel=", noDoodadModel,
                     " modelMissing=", modelMissing,
                     " modelInvalid=", modelInvalid);
            noClutterLogCount++;
        }
    }
}

TerrainManager::TerrainTextureTones
TerrainManager::getTerrainTextureTones(const std::string& texturePath) {
    const auto it = terrainTextureTones_.find(texturePath);
    if (it != terrainTextureTones_.end()) return it->second;

    TerrainTextureTones tones;  // greys, which tint nothing much either way
    if (assetManager) {
        const pipeline::BLPImage blp = assetManager->loadTexture(texturePath, false);
        if (blp.isValid() && !blp.data.empty()) {
            // Every sixteenth pixel, sorted by luminance. Percentiles rather
            // than a mean because a grass texture is blades over earth, and
            // the mean is the two averaged into neither.
            struct Sample { float luma; uint8_t r, g, b; };
            std::vector<Sample> samples;
            samples.reserve(blp.data.size() / 64 + 1);
            for (size_t i = 0; i + 3 < blp.data.size(); i += 64) {
                const uint8_t r = blp.data[i];
                const uint8_t g = blp.data[i + 1];
                const uint8_t b = blp.data[i + 2];
                samples.push_back({0.299f * static_cast<float>(r) +
                                       0.587f * static_cast<float>(g) +
                                       0.114f * static_cast<float>(b),
                                   r, g, b});
            }
            if (samples.size() >= 8) {
                std::sort(samples.begin(), samples.end(),
                          [](const Sample& a, const Sample& b) { return a.luma < b.luma; });
                auto at = [&](float pct) {
                    const auto idx = static_cast<size_t>(
                        static_cast<float>(samples.size() - 1) * pct);
                    const Sample& sm = samples[idx];
                    return glm::vec3(static_cast<float>(sm.r), static_cast<float>(sm.g),
                                     static_cast<float>(sm.b)) / 255.0f;
                };
                tones.shadow = at(0.25f);
                tones.highlight = at(0.85f);
            }
        }
    }
    terrainTextureTones_[texturePath] = tones;
    return tones;
}

void TerrainManager::getGroundEffectDoodads(uint32_t effectId,
                                            std::vector<std::string>& outModels,
                                            std::vector<uint32_t>& outWeights) const {
    outModels.clear();
    outWeights.clear();
    const auto it = groundEffectById_.find(effectId);
    if (it == groundEffectById_.end()) return;

    for (size_t i = 0; i < it->second.doodadIds.size(); ++i) {
        const uint32_t doodadId = it->second.doodadIds[i];
        if (doodadId == 0) continue;
        const auto model = groundDoodadModelById_.find(doodadId);
        if (model == groundDoodadModelById_.end()) continue;
        outModels.push_back(model->second);
        outWeights.push_back(it->second.weights[i]);
    }
}

uint32_t TerrainManager::getGroundEffectDensity(uint32_t effectId) const {
    if (effectId == 0) return 0;
    const auto it = groundEffectById_.find(effectId);
    return (it == groundEffectById_.end()) ? 0 : it->second.density;
}

const pipeline::MapChunk* TerrainManager::findChunkAt(float glX, float glY,
                                                      float& fracX, float& fracY,
                                                      const TerrainTile** outTile) const {
    // Terrain mesh vertices use chunk.position directly (WoW coordinates) and
    // the terrain is rendered without a model transform, so the camera's own
    // coordinates index it unchanged. A chunk spans
    //   X: [position[0] - 8 * unitSize, position[0]]
    //   Y: [position[1] - 8 * unitSize, position[1]]
    // and the two fractions handed back are the offsets within it, in the same
    // order the mesh builder walks its grid: fracY down the 17-stride rows,
    // fracX across.
    //
    // One finder for everyone who needs "which chunk is under this point".
    // isHoleAt used to carry its own copy and the copy was missing the full
    // scan below, so a chunk the guess did not land on read as no-hole rather
    // than as look-harder - which is every hole whose tile is indexed a little
    // differently from the guess. That is the whole reason the Gadgetzan
    // stairwell still reported hole=0 with 0x1000 sitting in its MCNK header.
    const float unitSize = CHUNK_SIZE / 8.0f;

    auto inChunk = [&](const TerrainTile* tile, int cx, int cy) -> const pipeline::MapChunk* {
        if (!tile || cx < 0 || cx >= 16 || cy < 0 || cy >= 16) return nullptr;
        const auto& chunk = tile->terrain.getChunk(cx, cy);
        if (!chunk.hasHeightMap()) return nullptr;

        if (!pipeline::TerrainMeshGenerator::chunkFractionsAt(chunk.position, glX, glY,
                                                              unitSize, fracX, fracY)) {
            return nullptr;
        }
        return &chunk;
    };

    auto inTile = [&](const TerrainTile* tile) -> const pipeline::MapChunk* {
        if (!tile || !tile->loaded) return nullptr;
        // Recorded per attempt rather than per hit: whichever tile the found
        // chunk came from was necessarily the last one tried.
        if (outTile) *outTile = tile;

        // Fast path: infer the likely chunk index and probe a 3x3 neighbourhood.
        const int guessCy = glm::clamp(
            static_cast<int>(std::floor((tile->maxX - glX) / CHUNK_SIZE)), 0, 15);
        const int guessCx = glm::clamp(
            static_cast<int>(std::floor((tile->maxY - glY) / CHUNK_SIZE)), 0, 15);
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (auto* c = inChunk(tile, guessCx + dx, guessCy + dy)) return c;
            }
        }

        // Fallback full scan for robustness at seams/unusual coords.
        for (int cy = 0; cy < 16; cy++) {
            for (int cx = 0; cx < 16; cx++) {
                if (auto* c = inChunk(tile, cx, cy)) return c;
            }
        }
        return nullptr;
    };

    // Fast path: the expected containing tile first.
    const TileCoord tc = worldToTile(glX, glY);
    auto it = loadedTiles.find(tc);
    if (it != loadedTiles.end()) {
        if (auto* c = inTile(it->second.get())) return c;
    }

    // Fallback: every loaded tile (handles seam/edge coordinate ambiguity).
    for (const auto& [coord, tile] : loadedTiles) {
        if (coord == tc) continue;
        if (auto* c = inTile(tile.get())) return c;
    }
    if (outTile) *outTile = nullptr;
    return nullptr;
}

std::optional<float> TerrainManager::getHeightAt(float glX, float glY) const {
    float fracX = 0.0f, fracY = 0.0f;
    const pipeline::MapChunk* chunk = findChunkAt(glX, glY, fracX, fracY);
    if (!chunk) return std::nullopt;

    // The one sampler, shared with the mesh builder and the clutter scatterer.
    //
    // This was a second bilinear interpolation of the four outer corners, which
    // is not the surface that gets drawn: the mesh fans four triangles from each
    // quad's centre vertex, and MCVT puts that vertex wherever the artist needed
    // it. The two answers differed by whatever the centre was offset by, so the
    // floor came out below the visible ground and the player sank into a slope.
    const glm::vec3 surface = pipeline::TerrainMeshGenerator::chunkSurfacePoint(
        chunk->position, chunk->heightMap, fracX, fracY, CHUNK_SIZE / 8.0f);
    return surface.z;
}

bool TerrainManager::isTileLoadedAt(float glX, float glY) const {
    return loadedTiles.find(worldToTile(glX, glY)) != loadedTiles.end();
}

bool TerrainManager::isHoleAt(float glX, float glY) const {
    float fracX = 0.0f, fracY = 0.0f;
    const pipeline::MapChunk* chunk = findChunkAt(glX, glY, fracX, fracY);
    if (!chunk || chunk->holes == 0) return false;

    // The quad, in the order the mesh builder passes to isHole: it walks
    // `for y { for x { if (chunk.isHole(y, x)) continue; ... } }` over vertices
    // at `y * 17 + x`, which is the grid getHeightAt samples as
    // `heights[gy * 17 + gx]`. Reading the quad from the same two fractions is
    // what keeps the surface stood on and the surface drawn agreeing about
    // which quads are there.
    const int qy = glm::clamp(static_cast<int>(std::floor(fracY)), 0, 7);
    const int qx = glm::clamp(static_cast<int>(std::floor(fracX)), 0, 7);
    return chunk->isHole(qy, qx);
}

bool TerrainManager::chunkHasHoles(float glX, float glY) const {
    const float unitSize = CHUNK_SIZE / 8.0f;
    (void)unitSize;

    auto tileHasHoles = [&](const TerrainTile* tile) -> std::optional<bool> {
        if (!tile || !tile->loaded) return std::nullopt;
        int cy = static_cast<int>(std::floor((tile->maxX - glX) / CHUNK_SIZE));
        int cx = static_cast<int>(std::floor((tile->maxY - glY) / CHUNK_SIZE));
        if (cx < 0 || cx >= 16 || cy < 0 || cy >= 16) return std::nullopt;
        return tile->terrain.getChunk(cx, cy).holes != 0;
    };

    const TileCoord tc = worldToTile(glX, glY);
    auto it = loadedTiles.find(tc);
    if (it != loadedTiles.end()) {
        if (auto r = tileHasHoles(it->second.get())) return *r;
    }
    for (const auto& [coord, tile] : loadedTiles) {
        if (coord == tc) continue;
        if (auto r = tileHasHoles(tile.get())) return *r;
    }
    return false;
}

std::optional<uint32_t> TerrainManager::getAreaIdAt(float glX, float glY) const {
    const TileCoord tc = worldToTile(glX, glY);
    auto it = loadedTiles.find(tc);
    if (it == loadedTiles.end() || !it->second || !it->second->loaded) {
        return std::nullopt;
    }

    const TerrainTile& tile = *it->second;
    // tileX advances along renderY while tileY advances along renderX.
    const float tileMaxRenderX = (32.0f - static_cast<float>(tc.y)) * TILE_SIZE;
    const float tileMaxRenderY = (32.0f - static_cast<float>(tc.x)) * TILE_SIZE;
    const int chunkY = glm::clamp(
        static_cast<int>(std::floor((tileMaxRenderX - glX) / CHUNK_SIZE)), 0, 15);
    const int chunkX = glm::clamp(
        static_cast<int>(std::floor((tileMaxRenderY - glY) / CHUNK_SIZE)), 0, 15);
    const uint32_t areaId = tile.terrain.getChunk(chunkX, chunkY).areaId;
    return areaId != 0 ? std::optional<uint32_t>(areaId) : std::nullopt;
}

std::optional<std::string> TerrainManager::getDominantTextureAt(float glX, float glY) const {
    const float unitSize = CHUNK_SIZE / 8.0f;
    std::vector<uint8_t> alphaScratch;
    auto sampleTileTexture = [&](const TerrainTile* tile) -> std::optional<std::string> {
        if (!tile || !tile->loaded) return std::nullopt;

        auto sampleChunkTexture = [&](int cx, int cy) -> std::optional<std::string> {
            if (cx < 0 || cx >= 16 || cy < 0 || cy >= 16) return std::nullopt;
            const auto& chunk = tile->terrain.getChunk(cx, cy);
            if (!chunk.hasHeightMap() || chunk.layers.empty()) return std::nullopt;

            float chunkMaxX = chunk.position[0];
            float chunkMinX = chunk.position[0] - 8.0f * unitSize;
            float chunkMaxY = chunk.position[1];
            float chunkMinY = chunk.position[1] - 8.0f * unitSize;
            if (glX < chunkMinX || glX > chunkMaxX || glY < chunkMinY || glY > chunkMaxY) {
                return std::nullopt;
            }

            float fracY = (chunk.position[0] - glX) / unitSize;
            float fracX = (chunk.position[1] - glY) / unitSize;
            fracX = glm::clamp(fracX, 0.0f, 8.0f);
            fracY = glm::clamp(fracY, 0.0f, 8.0f);

            int alphaX = glm::clamp(static_cast<int>((fracX / 8.0f) * 63.0f), 0, 63);
            int alphaY = glm::clamp(static_cast<int>((fracY / 8.0f) * 63.0f), 0, 63);
            int alphaIndex = alphaY * 64 + alphaX;

            int weights[4] = {0, 0, 0, 0};
            size_t numLayers = std::min(chunk.layers.size(), static_cast<size_t>(4));
            int accum = 0;
            for (size_t layerIdx = 1; layerIdx < numLayers; layerIdx++) {
                int alpha = 0;
                if (decodeLayerAlpha(chunk, layerIdx, alphaScratch) && alphaIndex < static_cast<int>(alphaScratch.size())) {
                    alpha = alphaScratch[alphaIndex];
                }
                weights[layerIdx] = alpha;
                accum += alpha;
            }
            weights[0] = glm::clamp(255 - accum, 0, 255);

            size_t bestLayer = 0;
            int bestWeight = weights[0];
            for (size_t i = 1; i < numLayers; i++) {
                if (weights[i] > bestWeight) {
                    bestWeight = weights[i];
                    bestLayer = i;
                }
            }

            uint32_t texId = chunk.layers[bestLayer].textureId;
            if (texId < tile->terrain.textures.size()) {
                return tile->terrain.textures[texId];
            }
            return std::nullopt;
        };

        int guessCy = glm::clamp(static_cast<int>(std::floor((tile->maxX - glX) / CHUNK_SIZE)), 0, 15);
        int guessCx = glm::clamp(static_cast<int>(std::floor((tile->maxY - glY) / CHUNK_SIZE)), 0, 15);
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                auto tex = sampleChunkTexture(guessCx + dx, guessCy + dy);
                if (tex) return tex;
            }
        }

        for (int cy = 0; cy < 16; cy++) {
            for (int cx = 0; cx < 16; cx++) {
                auto tex = sampleChunkTexture(cx, cy);
                if (tex) {
                    return tex;
                }
            }
        }
        return std::nullopt;
    };

    // Fast path: check expected containing tile first.
    TileCoord tc = worldToTile(glX, glY);
    auto it = loadedTiles.find(tc);
    if (it != loadedTiles.end()) {
        auto tex = sampleTileTexture(it->second.get());
        if (tex) return tex;
    }

    // Fallback: seam/edge case.
    for (const auto& [coord, tile] : loadedTiles) {
        if (coord == tc) continue;
        auto tex = sampleTileTexture(tile.get());
        if (tex) return tex;
    }

    return std::nullopt;
}

void TerrainManager::streamTiles() {
    auto shouldSkipMissingAdt = [this](const TileCoord& coord) -> bool {
        if (!assetManager) return false;
        if (failedTiles.find(coord) != failedTiles.end()) return true;
        const std::string adtPath = getADTPath(coord);
        if (!assetManager->fileExists(adtPath)) {
            // Mark permanently failed so future stream/precache passes do not retry.
            failedTiles[coord] = true;
            return true;
        }
        return false;
    };

    // Enqueue tiles in radius around current tile for async loading.
    // Collect all newly-needed tiles, then sort by distance so the closest
    // (most visible) tiles get loaded first.  This is critical during taxi
    // flight where new tiles enter the radius faster than they can load.
    {
        std::lock_guard<std::mutex> lock(queueMutex);

#ifdef WOWEE_PS4
        // Drop only work still waiting in the queue. A worker-owned payload
        // finishes safely and is checked again before its first GPU upload.
        // Keeping a stale travel backlog used to parse faraway city WMOs
        // after the camera had already left their tiles behind.
        for (auto it = loadQueue.begin(); it != loadQueue.end();) {
            if (!retainStreamTile(*it)) {
                pendingTiles.erase(*it);
                objectRepairRequests_.erase(*it);
                it = loadQueue.erase(it);
            } else {
                ++it;
            }
        }
#endif

        struct PendingEntry { TileCoord coord; int distSq; };
        std::vector<PendingEntry> newTiles;

        for (int dy = -loadRadius; dy <= loadRadius; dy++) {
            for (int dx = -loadRadius; dx <= loadRadius; dx++) {
                int tileX = currentTile.x + dx;
                int tileY = currentTile.y + dy;

                // Check valid range
                if (tileX < 0 || tileX > 63 || tileY < 0 || tileY > 63) {
                    continue;
                }

                // Circular pattern: skip corner tiles beyond radius (Euclidean distance)
#ifdef WOWEE_PS4
                if (!ps4budget::neededTile(dx, dy)) continue;
                if (!stationaryPreload_.load() &&
                    !ps4budget::tileInRange(tileX, tileY, streamPosition_.x, streamPosition_.y,
                                            streamViewDistance_, false)) continue;
#else
                if (dx*dx + dy*dy > loadRadius*loadRadius) continue;
#endif

                TileCoord coord = {.x = tileX, .y = tileY};

                // Skip if already loaded, pending, or failed
                if (loadedTiles.find(coord) != loadedTiles.end()) continue;
                if (pendingTiles.find(coord) != pendingTiles.end()) continue;
                if (failedTiles.find(coord) != failedTiles.end()) continue;
                if (shouldSkipMissingAdt(coord)) continue;

                newTiles.push_back({.coord = coord, .distSq = dx*dx + dy*dy});
                pendingTiles[coord] = true;
            }
        }

        // Sort nearest tiles first so workers service the most visible tiles
        std::sort(newTiles.begin(), newTiles.end(),
                  [](const PendingEntry& a, const PendingEntry& b) { return a.distSq < b.distSq; });

        // Insert at front so new close tiles preempt any distant tiles already queued
        for (auto it = newTiles.rbegin(); it != newTiles.rend(); ++it) {
            loadQueue.push_front(it->coord);
        }
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        const auto now = std::chrono::steady_clock::now();
        if (loadQueue.empty() && pendingTiles.empty()) {
            for (const auto& [coord, tile] : loadedTiles) {
                if (!tile->objectsIncomplete || !retainStreamTile(coord) || now < tile->nextObjectRetry) continue;
                tile->nextObjectRetry = now + std::chrono::seconds(15);
                objectRepairRequests_.insert(coord);
                pendingTiles[coord] = true;
                loadQueue.push_back(coord);
                LOG_INFO("[TERRAIN_REPAIR] queued tile=[", coord.x, ",", coord.y, "]");
                break;
            }
        }
    }

    // Notify workers that there's work
    queueCV.notify_all();

    // Unload tiles beyond unload radius (well past the camera far clip).
    // Queue them rather than unloading synchronously here - processPendingUnloads()
    // drains a time-budgeted batch per frame instead (see pendingUnloadQueue_'s comment).
    std::unordered_set<TileCoord, TileCoord::Hash> alreadyQueued(
        pendingUnloadQueue_.begin(), pendingUnloadQueue_.end());
    size_t queuedNow = 0;

    for (const auto& pair : loadedTiles) {
        const TileCoord& coord = pair.first;

        // Retire travel history outside the current spatial working set.
        if (!retainStreamTile(coord) && !alreadyQueued.count(coord)) {
            pendingUnloadQueue_.push_back(coord);
            queuedNow++;
        }
    }

    if (queuedNow > 0) {
        LOG_DEBUG("Queued ", queuedNow, " distant tiles for unload (",
                 pendingUnloadQueue_.size(), " total pending)");
    }
}

void TerrainManager::precacheTiles(const std::vector<std::pair<int, int>>& tiles) {
    std::lock_guard<std::mutex> lock(queueMutex);

    for (const auto& [x, y] : tiles) {
        if (x < 0 || x > 63 || y < 0 || y > 63) continue;

        TileCoord coord = {.x = x, .y = y};

        // Skip if already loaded, pending, or failed
        if (loadedTiles.find(coord) != loadedTiles.end()) continue;
        if (pendingTiles.find(coord) != pendingTiles.end()) continue;
        if (failedTiles.find(coord) != failedTiles.end()) continue;
        if (assetManager && !assetManager->fileExists(getADTPath(coord))) {
            failedTiles[coord] = true;
            continue;
        }

        // Precache work is prioritized so taxi-route tiles are prepared before
        // opportunistic radius streaming tiles.
        loadQueue.push_front(coord);
        pendingTiles[coord] = true;
    }

    // Notify workers to start loading
    queueCV.notify_all();
}
} // namespace rendering
} // namespace wowee
