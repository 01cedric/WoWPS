#ifdef WOWEE_PS4
#include "platform/ps4/ps4_platform.hpp"
#include "platform/ps4/cpu_memory.hpp"
#include <chrono>
#endif
#include "pipeline/asset_manager.hpp"
#include <new>
#include <cstdio>
#include "pipeline/mpq_asset_source.hpp"
#include "pipeline/base_fallback.hpp"
#include "pipeline/asset_read_bounds.hpp"
#include "core/logger.hpp"
#include "core/config_paths.hpp"
#include "core/file_probe.hpp"
#include "core/memory_monitor.hpp"
#include "core/profiler.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <unordered_set>

#include "stb_image.h"

namespace wowee {
namespace pipeline {

namespace {
size_t parseEnvSizeMB(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) {
        return 0;
    }
    char* end = nullptr;
    unsigned long long mb = std::strtoull(v, &end, 10);
    if (end == v || mb == 0) {
        return 0;
    }
    if (mb > (std::numeric_limits<size_t>::max() / (1024ull * 1024ull))) {
        return 0;
    }
    return static_cast<size_t>(mb);
}

size_t parseEnvCount(const char* name, size_t defValue) {
    const char* v = std::getenv(name);
    if (!v || !*v) {
        return defValue;
    }
    char* end = nullptr;
    unsigned long long n = std::strtoull(v, &end, 10);
    if (end == v || n == 0) {
        return defValue;
    }
    return static_cast<size_t>(n);
}
} // namespace

AssetManager::AssetManager() = default;
AssetManager::~AssetManager() {
    shutdown();
}

bool AssetManager::initialize(const std::string& dataPath_) {
    if (initialized) {
        LOG_WARNING("AssetManager already initialized");
        return true;
    }

    dataPath = dataPath_;
    overridePath_ = dataPath + "/override";
    LOG_INFO("Initializing asset manager with data path: ", dataPath);

    setupFileCacheBudget();

#ifdef WOWEE_PS4
    // The console streams from the user's archives, as the real client does.
    // No extracted tree is read or required (B4 test 7); the manifest path
    // below is the desktop's.
    {
        std::string error;
        for (const std::string& dir : {dataPath + "/Data", dataPath}) {
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec)) continue;
            mpqSource_ = MpqAssetSource::open(dir, &error);
            if (mpqSource_) break;
        }
        if (!mpqSource_) {
            LOG_ERROR("No readable MPQ archives under ", dataPath, "/Data (", error, ")");
            LOG_ERROR("Put your WotLK client's Data directory, locale folder included, at that path");
            return false;
        }
        initialized = true;
        LOG_INFO("Asset manager streaming from ", mpqSource_->archiveCount(), " archive(s) (",
                 mpqSource_->expansion(), ", ", mpqSource_->locale(), "; file cache ",
                 fileCacheBudget / (1024 * 1024), " MB)");
        return true;
    }
#endif

    std::string manifestPath = dataPath + "/manifest.json";
    if (!std::filesystem::exists(manifestPath)) {
        LOG_ERROR("manifest.json not found in: ", dataPath);
        LOG_ERROR("Run asset_extract to extract MPQ archives first");
        return false;
    }

    if (!manifest_.load(manifestPath)) {
        LOG_ERROR("Failed to load manifest");
        return false;
    }

    if (std::filesystem::is_directory(overridePath_)) {
        LOG_INFO("Override directory found: ", overridePath_);
    }

    initialized = true;
    LOG_INFO("Asset manager initialized: ", manifest_.getEntryCount(),
             " files indexed (file cache: ", fileCacheBudget / (1024 * 1024), " MB)");
    return true;
}

bool AssetManager::switchDataPath(const std::string& newDataPath) {
    if (newDataPath.empty()) return false;
    if (initialized && newDataPath == dataPath) return true;

    const std::string manifestPath = newDataPath + "/manifest.json";
    AssetManifest nextManifest;
    if (!std::filesystem::exists(manifestPath) || !nextManifest.load(manifestPath)) {
        LOG_ERROR("Cannot switch asset path; valid manifest not found in: ", newDataPath);
        return false;
    }

    clearCache();
    manifest_ = std::move(nextManifest);
    baseFallbackManifest_ = AssetManifest{};
    baseFallbackDataPath_.clear();
    dataPath = newDataPath;
    overridePath_ = dataPath + "/override";
    initialized = true;
    LOG_INFO("Switched asset manager to: ", dataPath, " (",
             manifest_.getEntryCount(), " files indexed)");
    return true;
}

void AssetManager::setupFileCacheBudget() {
    const size_t envFixedMB = parseEnvSizeMB("WOWEE_FILE_CACHE_MB");

#ifdef WOWEE_PS4
    // The console does not size this cache once and keep it. Its budget is a
    // function of measured flexible headroom, re-taken on the read path, so
    // this only seeds a first value - see refreshPs4CacheBudget.
    //
    // The clamp this replaced was the reason the measurement could not shrink
    // anything: a 32 MiB minimum against a getRecommendedCacheBudget that is
    // itself capped at 32 MiB meant three quarters of the cap always landed
    // under the floor and was raised back to it. The console therefore ran a
    // 32 MiB budget at 400 MiB free and a 32 MiB budget at 12 MiB free, and
    // the only thing that reacted to pressure was a separate switch that
    // turned the cache off entirely.
    if (envFixedMB > 0) {
        fileCacheBudget = envFixedMB * 1024ull * 1024ull;
        LOG_WARNING("Asset file cache fixed via WOWEE_FILE_CACHE_MB=", envFixedMB,
                    " (effective ", fileCacheBudget / (1024 * 1024), " MB); "
                    "measured headroom will not resize it");
        return;
    }
    const auto seed = platform::ps4::queryAvailableCpuMemory();
    fileCacheBudget = platform::ps4::cpuFileCacheBudgetFor(seed.bytes, seed.measured);
    LOG_INFO("Asset file cache budget ", fileCacheBudget / (1024 * 1024),
             " MB from flexible free ", seed.bytes / (1024 * 1024),
             " MiB (measured=", seed.measured, ")");
    return;
#else
    auto& memMonitor = core::MemoryMonitor::getInstance();
    size_t recommendedBudget = memMonitor.getRecommendedCacheBudget();
    size_t dynamicBudget = (recommendedBudget * 3) / 4;

    const size_t envMaxMB = parseEnvSizeMB("WOWEE_FILE_CACHE_MAX_MB");
    const size_t minBudgetBytes = 256ull * 1024ull * 1024ull;
#ifdef __ANDROID__
    // Half of available RAM is a desktop rule. Android does not let one app
    // have that: it enforces a per-app limit far below the machine's memory and
    // kills the process rather than swapping when it is passed. A phone with
    // 8 GB was handing this cache 840 MB, which is both more than the app may
    // hold and a good way to be killed the moment it goes to the background.
    const size_t defaultMaxBudgetBytes = 384ull * 1024ull * 1024ull;
#else
    const size_t defaultMaxBudgetBytes = 12288ull * 1024ull * 1024ull;  // 12 GB max for file cache
#endif
    const size_t maxBudgetBytes = (envMaxMB > 0)
        ? (envMaxMB * 1024ull * 1024ull)
        : defaultMaxBudgetBytes;

    if (envFixedMB > 0) {
        fileCacheBudget = envFixedMB * 1024ull * 1024ull;
        if (fileCacheBudget < minBudgetBytes) {
            fileCacheBudget = minBudgetBytes;
        }
        LOG_WARNING("Asset file cache fixed via WOWEE_FILE_CACHE_MB=", envFixedMB,
                    " (effective ", fileCacheBudget / (1024 * 1024), " MB)");
    } else {
        fileCacheBudget = std::clamp(dynamicBudget, minBudgetBytes, maxBudgetBytes);
    }
#endif
}

#ifdef WOWEE_PS4
AssetManager::Ps4Headroom AssetManager::refreshPs4CacheBudget() const {
    // sceKernelAvailableFlexibleMemorySize on every read is what the pressure
    // check used to cost: the shipped session made 38,634 of them, each one a
    // kernel call from a streaming worker, and each answer was then thrown
    // away. Sample it on a short interval instead and let every read in that
    // window share the answer.
    static constexpr uint64_t kSampleIntervalMs = 250;
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    const uint64_t last = ps4HeadroomSampledAtMs_.load(std::memory_order_relaxed);
    if (last != 0 && now - last < kSampleIntervalMs) {
        return {ps4HeadroomBytes_.load(std::memory_order_relaxed),
                ps4HeadroomMeasured_.load(std::memory_order_relaxed)};
    }
    // A racing worker may take the same sample. That is one redundant kernel
    // call, not a correctness problem, and it is cheaper than a lock here.
    ps4HeadroomSampledAtMs_.store(now, std::memory_order_relaxed);

    const auto available = platform::ps4::queryAvailableCpuMemory();
    ps4HeadroomBytes_.store(available.bytes, std::memory_order_relaxed);
    ps4HeadroomMeasured_.store(available.measured, std::memory_order_relaxed);

    // WOWEE_FILE_CACHE_MB is an explicit instruction, not a recommendation.
    // Read once: this runs on the asset path from several worker threads, and
    // getenv is neither free nor safe to keep asking beside a setenv.
    static const bool budgetFixedByEnv = parseEnvSizeMB("WOWEE_FILE_CACHE_MB") > 0;
    if (budgetFixedByEnv) return {available.bytes, available.measured};

    const size_t wanted =
        platform::ps4::cpuFileCacheBudgetFor(available.bytes, available.measured);
    size_t trimTo = 0;
    bool shrink = false;
    {
        std::lock_guard<std::shared_mutex> lock(cacheMutex);
        if (fileCacheBudget != wanted) {
            fileCacheBudget = wanted;
            shrink = fileCache.bytes() > wanted;
            trimTo = wanted;
        }
    }
    // Trimmed *to* the new budget rather than emptied. Clearing the cache does
    // not return arena pages to the kernel - musl keeps them - so the only
    // effect of emptying it is that the same bytes are read out of the archive
    // and decompressed again on the next lookup, which is a larger transient
    // allocation than the copy that was discarded. Keeping the hot set inside
    // a smaller budget is what actually lowers pressure.
    if (shrink) {
        const size_t freed = trimFileCache(trimTo);
        LOG_INFO("Asset file cache trimmed to ", trimTo / (1024 * 1024), " MB (freed ",
                 freed, " bytes) at flexible free ", available.bytes / (1024 * 1024), " MiB");
    }
    return {available.bytes, available.measured};
}
#endif

void AssetManager::shutdown() {
    mpqSource_.reset();
    if (!initialized) {
        return;
    }

    LOG_INFO("Shutting down asset manager");

    if (fileCacheHits + fileCacheMisses > 0) {
        float hitRate = static_cast<float>(fileCacheHits) / (fileCacheHits + fileCacheMisses) * 100.0f;
        LOG_INFO("File cache stats: ", fileCacheHits, " hits, ", fileCacheMisses, " misses (",
                 static_cast<int>(hitRate), "% hit rate), ", fileCache.bytes() / 1024 / 1024,
                 " MB cached, budget ", fileCacheBudget / 1024 / 1024, " MB");
    }
    // Beside the hit rate because they explain it, and because they are the
    // only record that assets the archives do hold were not drawn. Zero on a
    // healthy session; the shipped crash log had no line like this at all.
    const size_t allocFailures = readAllocationFailures_.load(std::memory_order_relaxed);
    const size_t declined = optionalReadsDeclined_.load(std::memory_order_relaxed);
    if (allocFailures || declined) {
        LOG_WARNING("Asset degradation: ", allocFailures,
                    " read(s) lost to allocation failure, ", declined,
                    " optional read(s) declined for headroom");
    }

    clearCache();
    initialized = false;
}

std::string AssetManager::resolveFile(const std::string& normalizedPath) const {
    // Check override directory first (for HD upgrades, custom textures)
    if (!overridePath_.empty()) {
        const auto* entry = manifest_.lookup(normalizedPath);
        if (entry && !entry->filesystemPath.empty()) {
            std::string overrideFsPath = overridePath_ + "/" + entry->filesystemPath;
            if (LooseFileReader::fileExists(overrideFsPath)) {
                return overrideFsPath;
            }
        }
    }
    // Primary manifest
    std::string primaryPath = manifest_.resolveFilesystemPath(normalizedPath);
    if (!primaryPath.empty()) return primaryPath;

    // If a base-path fallback is configured (expansion-specific primary that only
    // holds DBC overrides), retry against the base extraction.
    if (!baseFallbackDataPath_.empty()) {
        std::string baseFallbackPath = baseFallbackManifest_.resolveFilesystemPath(normalizedPath);
        if (!baseFallbackPath.empty()) {
            baseFallbackHits_.fetch_add(1, std::memory_order_relaxed);
            return baseFallbackPath;
        }
    }

    // Last resort: some files (e.g. DBFilesClient\TransportAnimation.dbc) can end up
    // present on disk under dataPath without ever having been captured by whatever
    // extraction produced manifest.json. Try the loose file directly at its expected
    // location before giving up, so a missing manifest entry doesn't silently mean
    // "file doesn't exist" when it plainly does.
    std::string looseCandidate = normalizedPath;
    std::replace(looseCandidate.begin(), looseCandidate.end(), '\\', '/');
    looseCandidate = dataPath + "/" + looseCandidate;
    if (LooseFileReader::fileExists(looseCandidate)) {
        return looseCandidate;
    }
    return {};
}

bool AssetManager::setBaseFallbackPath(const std::string& basePath,
                                       const std::string& expansionId) {
    previewModels_.clear();
    if (baseFallbackHits_.load(std::memory_order_relaxed) > 0) {
        // Said on the way out rather than per lookup. Next to a warning about
        // an unlabelled or forced base, this is how much of what was on screen
        // came from it.
        LOG_INFO("AssetManager: previous base fallback '", baseFallbackDataPath_,
                 "' answered ", baseFallbackHits_.load(std::memory_order_relaxed),
                 " lookups");
    }
    baseFallbackDataPath_.clear();
    baseFallbackHits_.store(0, std::memory_order_relaxed);
    if (basePath.empty() || basePath == dataPath) return false;  // nothing to do
    std::string manifestPath = basePath + "/manifest.json";
    if (!std::filesystem::exists(manifestPath)) {
        LOG_DEBUG("AssetManager: base fallback manifest not found at ", manifestPath,
                  " - fallback disabled");
        return false;
    }
    if (!baseFallbackManifest_.load(manifestPath)) return false;

    const std::string& baseExpansion = baseFallbackManifest_.getExpansion();
    const char* forcedEnv = std::getenv("WOWEE_ASSET_BASE_FALLBACK");
    const bool forced = forcedEnv && forcedEnv[0] == '1';

    switch (decideBaseFallback(baseExpansion, expansionId, forced)) {
        case BaseFallbackDecision::Use:
            break;
        case BaseFallbackDecision::UseUnlabelled:
            // Nothing can be concluded, so it is used and said: an unlabelled
            // tree is how the wrong client's assets reach the screen without
            // anybody having chosen that.
            LOG_WARNING("AssetManager: base fallback '", basePath,
                        "' does not record which client it was extracted from, so it "
                        "cannot be checked against '", expansionId,
                        "'. Re-extract to label it. Files it holds and '", expansionId,
                        "' does not will be used as they are");
            break;
        case BaseFallbackDecision::Refuse:
            LOG_ERROR("AssetManager: base fallback '", basePath, "' was extracted from '",
                      baseExpansion, "' and the client is running '", expansionId,
                      "'. Refusing it: every file '", expansionId,
                      "' does not cover would otherwise be drawn from '", baseExpansion,
                      "' with nothing to say so. Extract '", expansionId,
                      "' into its own data root, or set WOWEE_ASSET_BASE_FALLBACK=1 "
                      "to use it anyway");
            return false;
        case BaseFallbackDecision::UseForced:
            LOG_WARNING("AssetManager: using base fallback '", basePath, "' from '",
                        baseExpansion, "' while running '", expansionId,
                        "' because WOWEE_ASSET_BASE_FALLBACK=1");
            break;
    }

    baseFallbackDataPath_ = basePath;
    LOG_INFO("AssetManager: base fallback path set to '", basePath, "' (",
             baseFallbackManifest_.getEntryCount(), " files, expansion '",
             baseExpansion.empty() ? std::string("unrecorded") : baseExpansion, "')");
    return true;
}

BLPImage AssetManager::loadTexture(const std::string& path, bool keepCompressed) {
    ZoneScopedN("AssetManager::loadTexture");
    // Callers ask for the blocks to save the decompression and the memory. On a
    // GPU that cannot sample them the saving is a blank surface, so the answer
    // is no and the loader unpacks to RGBA8 instead.
#ifndef WOWEE_PS4
    keepCompressed = keepCompressed && blockCompressionSupported();
#endif
    // PS4 keeps requested DXT blocks in CPU staging even though its GPU
    // upload path uses RGBA8. VkTexture decodes one texture at upload time.
    // Expanding every terrain/WMO texture here retained an entire tile's
    // decoded pixels and exhausted the flexible heap in the B12 console test.
    if (!initialized) {
        LOG_ERROR("AssetManager not initialized");
        return BLPImage();
    }

    std::string normalizedPath = normalizePath(path);

    LOG_DEBUG("Loading texture: ", normalizedPath);

    // Check for PNG override
    BLPImage pngImage = tryLoadPngOverride(normalizedPath);
    if (pngImage.isValid()) {
        return pngImage;
    }

    std::vector<uint8_t> blpData = readFile(normalizedPath);
    if (blpData.empty()) {
        static std::mutex logMtx;
        static std::unordered_set<std::string> loggedMissingTextures;
        static bool missingTextureLogSuppressed = false;
        static const size_t kMaxMissingTextureLogKeys =
            parseEnvCount("WOWEE_TEXTURE_MISS_LOG_KEYS", 400);
        std::lock_guard<std::mutex> lock(logMtx);
        if (loggedMissingTextures.size() < kMaxMissingTextureLogKeys &&
            loggedMissingTextures.insert(normalizedPath).second) {
            LOG_WARNING("Texture not found: ", normalizedPath);
        } else if (!missingTextureLogSuppressed && loggedMissingTextures.size() >= kMaxMissingTextureLogKeys) {
            LOG_WARNING("Texture-not-found warning key cache reached ", kMaxMissingTextureLogKeys,
                        " entries; suppressing new unique texture-miss logs");
            missingTextureLogSuppressed = true;
        }
        return BLPImage();
    }

    // The decode is the largest single CPU allocation on the streaming path: a
    // 1024x1024 BLP unpacks to four MiB of RGBA8, and a tile asks for dozens.
    // A texture that will not fit is a surface drawn without it, which is what
    // an invalid BLPImage already means to every caller here; it is not a
    // reason to end the session.
    BLPImage image;
    try {
        image = BLPLoader::load(blpData, keepCompressed);
    } catch (const std::bad_alloc&) {
        blpData.clear();
        blpData.shrink_to_fit();
        trimFileCache(0);
        readAllocationFailures_.fetch_add(1, std::memory_order_relaxed);
        char note[256];
        std::snprintf(note, sizeof(note),
                      "texture decode allocation failed: %.180s; surface drawn untextured",
                      normalizedPath.c_str());
#ifdef WOWEE_PS4
        platform::ps4::reportBootStage(note);
#endif
        std::fprintf(stderr, "%s\n", note);
        return BLPImage();
    }
    if (!image.isValid()) {
        static std::mutex logMtx;
        static std::unordered_set<std::string> loggedDecodeFails;
        static bool decodeFailLogSuppressed = false;
        static const size_t kMaxDecodeFailLogKeys =
            parseEnvCount("WOWEE_TEXTURE_DECODE_LOG_KEYS", 200);
        std::lock_guard<std::mutex> lock(logMtx);
        if (loggedDecodeFails.size() < kMaxDecodeFailLogKeys &&
            loggedDecodeFails.insert(normalizedPath).second) {
            LOG_ERROR("Failed to load texture: ", normalizedPath);
        } else if (!decodeFailLogSuppressed && loggedDecodeFails.size() >= kMaxDecodeFailLogKeys) {
            LOG_WARNING("Texture-decode warning key cache reached ", kMaxDecodeFailLogKeys,
                        " entries; suppressing new unique decode-failure logs");
            decodeFailLogSuppressed = true;
        }
        return BLPImage();
    }

    LOG_DEBUG("Loaded texture: ", normalizedPath, " (", image.width, "x", image.height, ")");
    return image;
}

BLPImage AssetManager::tryLoadPngOverride(const std::string& normalizedPath) const {
    if (normalizedPath.size() < 4) return BLPImage();

    std::string ext = normalizedPath.substr(normalizedPath.size() - 4);
    if (ext != ".blp") return BLPImage();

    // Try the standard sidecar path first: extracted .blp's directory + .png.
    std::string fsPath = resolveFile(normalizedPath);
    std::string pngPath;
    if (!fsPath.empty() && fsPath.size() >= 4) {
        pngPath = fsPath.substr(0, fsPath.size() - 4) + ".png";
        if (!LooseFileReader::fileExists(pngPath)) pngPath.clear();
    }

    // Fallback: probe well-known custom-zone texture roots so that PNG-only
    // assets ship without needing a phantom BLP manifest entry. Path is
    // forward-slash + lowercase to match the editor's PNG export convention.
    if (pngPath.empty()) {
        std::string norm = normalizedPath;
        std::replace(norm.begin(), norm.end(), '\\', '/');
        std::transform(norm.begin(), norm.end(), norm.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::string candidate = norm.substr(0, norm.size() - 4) + ".png";
        for (const char* root : {"custom_zones/textures/", "output/textures/"}) {
            std::string p = std::string(root) + candidate;
            if (LooseFileReader::fileExists(p)) { pngPath = p; break; }
        }
    }
    if (pngPath.empty()) return BLPImage();

    int w, h, channels;
    unsigned char* pixels = stbi_load(pngPath.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        LOG_WARNING("PNG override exists but failed to load: ", pngPath);
        return BLPImage();
    }
    // Cap texture dimensions. WoW textures top out at 4K; stbi can return
    // 32K x 32K which would allocate 4GB on a malicious PNG.
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
        LOG_WARNING("PNG override dimensions out of range (", w, "x", h, "): ", pngPath);
        stbi_image_free(pixels);
        return BLPImage();
    }

    BLPImage image;
    image.width = w;
    image.height = h;
    image.channels = 4;
    image.format = BLPFormat::BLP2;
    image.compression = BLPCompression::ARGB8888;
    image.data.assign(pixels, pixels + (static_cast<size_t>(w) * h * 4));
    stbi_image_free(pixels);

    LOG_INFO("PNG override loaded: ", pngPath, " (", w, "x", h, ")");
    return image;
}

void AssetManager::setExpansionDataPath(const std::string& path) {
    previewModels_.clear();
    expansionDataPath_ = path;
    LOG_INFO("Expansion data path for CSV DBCs: ", expansionDataPath_);
}

std::shared_ptr<DBCFile> AssetManager::loadDBC(const std::string& name) try {
    ZoneScopedN("AssetManager::loadDBC");
    if (!initialized) {
        LOG_ERROR("AssetManager not initialized");
        return nullptr;
    }

    {
        std::shared_lock<std::shared_mutex> lock(cacheMutex);
        auto it = dbcCache.find(name);
        if (it != dbcCache.end()) return it->second;
    }

    LOG_DEBUG("Loading DBC: ", name);

    std::vector<uint8_t> dbcData;

    // Try binary DBC from extracted MPQs first (preferred source).
    std::string dbcPath = "DBFilesClient\\" + name;
    {
        dbcData = readFile(dbcPath);
    }

    // If asset_extract was run with --emit-json-dbc, the DBC's directory
    // also contains a JSON sidecar. Use it when the binary is missing
    // (lets users run with PNG/JSON-only extractions for testing the
    // open-format end-to-end path). Server-mode never reads via this
    // code path, so private-server compat is unaffected.
    if (dbcData.empty()) {
        std::string normalizedDbc = normalizePath(dbcPath);
        std::string fsPath = resolveFile(normalizedDbc);
        if (!fsPath.empty() && fsPath.size() >= 4) {
            std::string sidecar = fsPath.substr(0, fsPath.size() - 4) + ".json";
            if (core::optionalFileExists(sidecar)) {
                std::ifstream jf(sidecar, std::ios::binary | std::ios::ate);
                if (jf) {
                    auto sz = jf.tellg();
                    if (sz > 0) {
                        dbcData.resize(static_cast<size_t>(sz));
                        jf.seekg(0);
                        jf.read(reinterpret_cast<char*>(dbcData.data()), sz);
                        LOG_INFO("Loading JSON DBC sidecar: ", sidecar);
                    }
                }
            }
        }
    }

    // Try Data/db/ directory (pre-extracted binary DBCs shared across expansions)
    if (dbcData.empty()) {
        // Expansion overlay first (e.g. Data/expansions/tbc/overlay/db/), then
        // expansion db/, then shared Data/db/.
        std::vector<std::string> dbDirs;
        if (!expansionDataPath_.empty())
            dbDirs.push_back(expansionDataPath_ + "/overlay/db");
        dbDirs.push_back(dataPath + "/db");
        dbDirs.push_back(dataPath + "/../../db");
#ifdef WOWEE_PS4
        dbDirs.emplace_back(core::resolveRelativeAssetPath("Data/db"));
#else
        dbDirs.emplace_back("Data/db");
#endif
        // Try exact-case first, then case-insensitive scan (Linux is case-sensitive
        // but DBC filenames in Data/db/ are often all-lowercase).
        std::string nameLower = name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        for (const auto& dir : dbDirs) {
            if (!core::optionalDirectoryExists(dir)) continue;
            std::string exact = dir + "/" + name;
            std::string resolved;
            if (core::optionalFileExists(exact)) {
                resolved = exact;
            } else {
                std::error_code ec;
                for (std::filesystem::directory_iterator it(dir, ec), end;
                     it != end && !ec; it.increment(ec)) {
                    const auto& entry = *it;
                    if (!entry.is_regular_file(ec) || ec) continue;
                    std::string fn = entry.path().filename().string();
                    std::string fnLower = fn;
                    std::transform(fnLower.begin(), fnLower.end(), fnLower.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
                    if (fnLower == nameLower) {
                        resolved = entry.path().string();
                        break;
                    }
                }
            }
            if (resolved.empty()) continue;
            std::ifstream f(resolved, std::ios::binary | std::ios::ate);
            if (f) {
                auto size = f.tellg();
                if (size > 0) {
                    f.seekg(0);
                    dbcData.resize(static_cast<size_t>(size));
                    f.read(reinterpret_cast<char*>(dbcData.data()), size);
                    LOG_INFO("Loaded binary DBC from: ", resolved, " (", size, " bytes)");
                    break;
                }
            }
        }
    }

    // Check for JSON DBC from custom zones (wowee open format)
    if (dbcData.empty()) {
        std::string baseName = name;
        auto dot = baseName.rfind('.');
        if (dot != std::string::npos) baseName = baseName.substr(0, dot);
        for (const char* dir : {"custom_zones", "output"}) {
            std::error_code ec;
            if (!std::filesystem::exists(dir, ec) || ec) continue;
            for (std::filesystem::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
                const auto& entry = *it;
                if (!entry.is_directory(ec)) continue;
                std::string jsonPath = entry.path().string() + "/data/" + baseName + ".json";
                if (std::filesystem::exists(jsonPath, ec) && !ec) {
                    std::ifstream jf(jsonPath, std::ios::binary | std::ios::ate);
                    if (jf) {
                        auto sz = jf.tellg();
                        if (sz > 0) {
                            dbcData.resize(static_cast<size_t>(sz));
                            jf.seekg(0);
                            jf.read(reinterpret_cast<char*>(dbcData.data()), sz);
                            LOG_INFO("Loading JSON DBC override: ", jsonPath);
                        }
                    }
                    break;
                }
            }
            if (!dbcData.empty()) break;
        }
    }

    // Fall back to expansion-specific CSV (e.g. Data/expansions/wotlk/db/Spell.csv)
    if (dbcData.empty() && !expansionDataPath_.empty()) {
        std::string baseName = name;
        auto dot = baseName.rfind('.');
        if (dot != std::string::npos) {
            baseName = baseName.substr(0, dot);
        }
        std::string csvPath = expansionDataPath_ + "/db/" + baseName + ".csv";
        if (core::optionalFileExists(csvPath)) {
            std::ifstream f(csvPath, std::ios::binary | std::ios::ate);
            if (f) {
                auto size = f.tellg();
                if (size > 0) {
                    f.seekg(0);
                    dbcData.resize(static_cast<size_t>(size));
                    f.read(reinterpret_cast<char*>(dbcData.data()), size);
                    LOG_INFO("Binary DBC not found, using CSV fallback: ", csvPath);
                }
            }
        }
    }

    if (dbcData.empty()) {
        LOG_WARNING("DBC not found: ", name);
        return nullptr;
    }

    auto dbc = std::make_shared<DBCFile>();
    if (!dbc->load(dbcData)) {
        LOG_ERROR("Failed to load DBC: ", name);
        return nullptr;
    }

    {
        std::lock_guard<std::shared_mutex> lock(cacheMutex);
        const auto [it, inserted] = dbcCache.emplace(name, dbc);
        if (!inserted) return it->second;
    }

    LOG_INFO("Loaded DBC: ", name, " (", dbc->getRecordCount(), " records)");
    return dbc;
} catch (const std::bad_alloc&) {
    // Avoid allocating even for diagnostics while handling heap pressure.
    char note[240]; std::snprintf(note,sizeof(note),"DBC allocation failed: %.160s; lookup unavailable",name.c_str());
#ifdef WOWEE_PS4
    platform::ps4::reportBootStage(note);
#endif
    std::fprintf(stderr,"%s\n",note);
    return nullptr;
}

std::shared_ptr<DBCFile> AssetManager::loadDBCOptional(const std::string& name) try {
    // Check cache first
    {
        std::shared_lock<std::shared_mutex> lock(cacheMutex);
        auto it = dbcCache.find(name);
        if (it != dbcCache.end()) return it->second;
    }

    // Try binary DBC
    std::vector<uint8_t> dbcData;
    {
        std::string dbcPath = "DBFilesClient\\" + name;
        dbcData = readFile(dbcPath);
    }

    // Fall back to expansion-specific CSV
    if (dbcData.empty() && !expansionDataPath_.empty()) {
        std::string baseName = name;
        auto dot = baseName.rfind('.');
        if (dot != std::string::npos) baseName = baseName.substr(0, dot);
        std::string csvPath = expansionDataPath_ + "/db/" + baseName + ".csv";
        if (core::optionalFileExists(csvPath)) {
            std::ifstream f(csvPath, std::ios::binary | std::ios::ate);
            if (f) {
                auto size = f.tellg();
                if (size > 0) {
                    f.seekg(0);
                    dbcData.resize(static_cast<size_t>(size));
                    f.read(reinterpret_cast<char*>(dbcData.data()), size);
                    LOG_INFO("Binary DBC not found, using CSV fallback: ", csvPath);
                }
            }
        }
    }

    if (dbcData.empty()) {
        // Expected on some expansions - log at debug level only.
        LOG_DEBUG("Optional DBC not found (expected on some expansions): ", name);
        return nullptr;
    }

    auto dbc = std::make_shared<DBCFile>();
    if (!dbc->load(dbcData)) {
        LOG_ERROR("Failed to load DBC: ", name);
        return nullptr;
    }

    {
        std::lock_guard<std::shared_mutex> lock(cacheMutex);
        const auto [it, inserted] = dbcCache.emplace(name, dbc);
        if (!inserted) return it->second;
    }
    LOG_INFO("Loaded optional DBC: ", name, " (", dbc->getRecordCount(), " records)");
    return dbc;
} catch (const std::bad_alloc&) {
    char note[240]; std::snprintf(note,sizeof(note),"Optional DBC allocation failed: %.150s; lookup unavailable",name.c_str());
#ifdef WOWEE_PS4
    platform::ps4::reportBootStage(note);
#endif
    std::fprintf(stderr,"%s\n",note);
    return nullptr;
}
bool AssetManager::fileExists(const std::string& path) const {
    if (!initialized) {
        return false;
    }
    std::string normalized = normalizePath(path);
    // Resolved the same way a read is, not looked up in the primary manifest.
    //
    // A read walks override, primary manifest, base fallback manifest, then the
    // loose file; this asked the primary manifest alone. With one asset source
    // the two agree, which is why it stood. The moment an expansion overlay
    // becomes the primary - an overlay holding a few thousand models over a
    // two-hundred-thousand file base - this answered NO for every file in the
    // base game, while the read that follows would have found it.
    //
    // Fifty-eight callers ask this before deciding what to read, so every one
    // of them took the wrong branch at once: a weapon texture that is present
    // under Weapon\ was declared missing and looked for under Shield\, where
    // it has never been, and the weapon drew white.
    if (!resolveFile(normalized).empty()) return true;
    return mpqSource_ && mpqSource_->hasFile(normalized);
}

std::vector<uint8_t> AssetManager::readFile(const std::string& path) const {
    return readFileBounded(path, std::numeric_limits<size_t>::max());
}

std::vector<uint8_t> AssetManager::readFileBounded(const std::string& path, size_t maxBytes, bool useCache) const {
    if (!initialized) {
        return {};
    }

#ifdef WOWEE_PS4
    // Move the cache budget to whatever headroom the console actually has, and
    // trim down to it. This replaced an unconditional trimFileCache(0) taken
    // whenever isMemoryPressure() was true, which on the shipped session meant
    // every read: the pressure test compares *unmapped* flexible pages against
    // 32 MiB, musl never returns arena pages, so once the heap had grown the
    // reading sat at 12 MiB and the valve latched open for the rest of the
    // run. Every read then emptied the cache, took the exclusive lock to do
    // it, and cached nothing - 34,722 misses against 3,912 hits, and 0 MB
    // held, with each miss re-reading and re-decompressing bytes the client
    // had already had.
    refreshPs4CacheBudget();
#endif
    std::string normalized = normalizePath(path);

    // Promote under lock, copy outside it. A concurrent eviction cannot
    // invalidate this read and no worker holds the cache lock while copying MBs.
    try {
        ByteLruCache::Handle cached;
        {
            std::lock_guard<std::shared_mutex> cacheLock(cacheMutex);
            if (useCache) cached = fileCache.get(normalized);
        }
        if (cached) {
            if (cached->size() > maxBytes) {
                logAssetReadLimit("AssetManager cache", normalized, cached->size(), maxBytes);
                return {};
            }
            auto data = *cached;
            fileCacheHits++;
            return data;
        }
    } catch (const std::bad_alloc&) {
        // The copy handed to the caller is itself an allocation, so even a hit
        // can throw when the heap is gone. Drop the copies we are holding and
        // fall through to read it again; the read path below is fallible too.
        trimFileCache(0);
    }

    fileCacheMisses++;
    // Read from filesystem (override dir first, then base manifest), or
    // straight out of the archives when streaming.
    //
    // This is where the console's std::bad_alloc came from and where it has to
    // stop. MpqAssetSource sizes a vector from the archive's own header before
    // decompressing into it, and LooseFileReader does the same from the file
    // size; on a heap with no room left either throws, and nothing between
    // here and main() caught it. An asset that cannot be allocated is reported
    // to the caller the same way an asset that is not in the archives is -
    // every one of them already draws a fallback for that - and counted, so a
    // white texture is distinguishable from a missing one afterwards.
    std::vector<uint8_t> data;
    // Resolved inside the guard, not before it: it builds paths, and a heap
    // with no room for the file has none for the string naming it either. It
    // stays empty if that is what threw, which sends the retry to the archives
    // - the source that needs no resolved path.
    std::string fsPath;
    try {
        fsPath = resolveFile(normalized);
        if (!fsPath.empty()) {
            data = LooseFileReader::readFileBounded(fsPath, maxBytes);
            if (data.empty()) {
                // Bounded rejections already have a rate-limited diagnostic.
                if (maxBytes == std::numeric_limits<size_t>::max())
                    LOG_WARNING("Manifest entry exists but file unreadable: ", fsPath);
                return data;
            }
        } else if (mpqSource_) {
            data = mpqSource_->readFileBounded(normalized, maxBytes);
            if (data.empty()) return data;
        } else {
            return {};
        }
    } catch (const std::bad_alloc&) {
        // One retry, with the cache emptied first. A repeat asset is often a
        // few hundred KiB and the copies we are holding are worth more to the
        // caller as room for it than as a cache.
        data.clear();
        data.shrink_to_fit();
        trimFileCache(0);
        try {
            data = !fsPath.empty() ? LooseFileReader::readFileBounded(fsPath, maxBytes)
                 : (mpqSource_ ? mpqSource_->readFileBounded(normalized, maxBytes)
                               : std::vector<uint8_t>{});
        } catch (const std::bad_alloc&) {
            data.clear();
            data.shrink_to_fit();
        }
        if (data.empty()) {
            readAllocationFailures_.fetch_add(1, std::memory_order_relaxed);
            // Deliberately not LOG_*: the logger formats through std::string,
            // and asking for one while handling an exhausted heap is how a
            // diagnostic becomes the next throw. loadDBC's handler below takes
            // the same route for the same reason.
            char note[256];
            std::snprintf(note, sizeof(note),
                          "asset allocation failed: %.180s; drawing without it",
                          normalized.c_str());
#ifdef WOWEE_PS4
            platform::ps4::reportBootStage(note);
#endif
            std::fprintf(stderr, "%s\n", note);
            return {};
        }
    }

    // Caching must never turn a successful read into a fatal allocation error.
    // The returned vector already owns the requested bytes; this second copy is
    // only an optimization.
    //
    // What decides whether to take it is now the budget alone. The budget
    // shrinks with measured headroom, so a console under pressure holds a
    // small hot set instead of holding nothing: the "skip caching under
    // pressure" test that used to be here read the same latched signal as the
    // trim above and, once it stuck, guaranteed that every repeat lookup paid
    // a full archive read and decompression for the rest of the session.
    const size_t fileSize = data.size();
    const size_t budget = getFileCacheBudget();
    const bool mayCache = useCache && fileSize > 0 && fileSize < budget / 2;
    if (mayCache) {
        try {
            auto cached = std::make_shared<const std::vector<uint8_t>>(data);
            std::lock_guard<std::shared_mutex> cacheLock(cacheMutex);
            // Recheck after allocation: another worker may have shrunk the
            // budget or inserted this path. Neither can oversubscribe the cache.
            if (fileSize < fileCacheBudget / 2)
                fileCache.put(normalized, std::move(cached), fileCacheBudget);
        } catch (const std::bad_alloc&) {
            trimFileCache(0);
        }
    }

    return data;
}

std::vector<uint8_t> AssetManager::readFileOptional(const std::string& path) const {
    if (!initialized) {
        return {};
    }
#ifdef WOWEE_PS4
    // The headroom check on the loading path, at the one place every optional
    // asset passes through.
    //
    // This entry point exists for reads whose absence the caller already draws
    // around - an .anim variant, a .skin, a zone map sidecar - so declining
    // one costs detail and nothing else. Below the stop line the next mapping
    // the allocator asks the kernel for is the one likely to fail, and the
    // console's choice at that point is between a doodad and the session.
    //
    // Required geometry does not come through here and is not affected: an ADT
    // and a WMO are read with readFile, which still tries and now fails
    // gracefully if it cannot.
    {
        const auto headroom = refreshPs4CacheBudget();
        const bool holding = ps4StreamHolding_.load(std::memory_order_relaxed);
        const auto admission = platform::ps4::worldStreamAdmissionFor(
            headroom.freeBytes, headroom.measured, holding);
        ps4StreamHolding_.store(admission != platform::ps4::WorldStreamAdmission::Admit,
                                std::memory_order_relaxed);
        if (!platform::ps4::admitsOptionalDetail(admission)) {
            const size_t declined = optionalReadsDeclined_.fetch_add(
                1, std::memory_order_relaxed) + 1;
            // Once, and then on powers of ten: this fires per asset, and a
            // console with no memory left has none for a log line per doodad.
            if ((declined & (declined - 1)) == 0) {
                LOG_WARNING("Optional asset declined for CPU headroom: flexible free ",
                            headroom.freeBytes / (1024 * 1024), " MiB below ",
                            platform::ps4::kCpuStreamStopHeadroom / (1024 * 1024),
                            " MiB; declined=", declined, " (latest ", path, ")");
            }
            return {};
        }
    }
#endif
    if (!fileExists(path)) {
        return {};
    }
    return readFile(path);
}

size_t AssetManager::trimFileCache(size_t targetBytes) const {
    std::lock_guard<std::shared_mutex> lock(cacheMutex);
    return fileCache.trim(targetBytes);
}

void AssetManager::evictDBC(const std::string& name) {
    previewModels_.clear();
    const auto fileKey = normalizePath("DBFilesClient\\" + name);
    std::lock_guard<std::shared_mutex> lock(cacheMutex);
    dbcCache.erase(name);
    fileCache.erase(fileKey);
}

void AssetManager::clearDBCCache() {
    previewModels_.clear();
    std::lock_guard<std::shared_mutex> lock(cacheMutex);
    dbcCache.clear();
    LOG_INFO("Cleared DBC cache");
}

void AssetManager::clearCache() {
    previewModels_.clear();
    std::lock_guard<std::shared_mutex> lock(cacheMutex);
    dbcCache.clear();
    fileCache.clear();
    LOG_INFO("Cleared asset cache (DBC + file cache)");
}
std::string AssetManager::normalizePath(const std::string& path) const {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Reject path traversal sequences
    if (normalized.find("..\\") != std::string::npos ||
        normalized.find("../") != std::string::npos ||
        normalized == "..") {
        LOG_WARNING("Path traversal rejected: ", path);
        return {};
    }

    return normalized;
}

} // namespace pipeline
} // namespace wowee

namespace wowee {
namespace pipeline {
std::vector<std::string> AssetManager::listFilesWithPrefix(const std::string& normalizedPrefix) const {
    std::vector<std::string> out;
    for (const auto& [path, entry] : manifest_.getEntries()) {
        (void)entry;
        if (path.compare(0, normalizedPrefix.size(), normalizedPrefix) == 0) out.push_back(path);
    }
    if (mpqSource_) {
        auto fromArchives = mpqSource_->listFiles(normalizedPrefix);
        out.insert(out.end(), fromArchives.begin(), fromArchives.end());
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
} // namespace pipeline
} // namespace wowee
