#pragma once
// P05 line of sight, second half : the installed collision pack, and the
// test the cast and target gates ask.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33.
//   Spell::CheckCast                 Spell.cpp:6092-6118 - the cast gate, its
//       SPELL_ATTR2_IGNORE_LINE_OF_SIGHT / SPELL_ATTR5_ALWAYS_AOE_LINE_OF_SIGHT
//       escapes and SPELL_FAILED_LINE_OF_SIGHT.
//   WorldObject::IsWithinLOSInMap    Object.cpp:1412-1436 - which point each end
//       contributes.
//   WorldObject::GetHitSpherePointFor Object.cpp:1295-1302.
//   Map::isInLineOfSight             Map.cpp:1545-1582 - the static tree, and the
//       two config arms whose defaults (WorldConfig.cpp:558) leave
//       ModelIgnoreFlags::M2 in force.
//   StaticMapTree::isInLineOfSight   MapTree.cpp:129-149.
//   ObjectDefines.h:49               DEFAULT_COLLISION_HEIGHT.
//
// **No real collision data was produced or verified when this was written.** The
// environment had no client MPQ and no extracted WMO/M2/ADT asset, so the pack
// this reads cannot exist here. Without an installed pack every call below
// returns "visible", which is exactly how this build treats every other optional
// asset, and the suite proves both halves: the ray test against synthetic
// geometry authored in the test, and the inert answer with nothing installed.

#include "game/local_collision.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

namespace wowee::game {

/// DEFAULT_COLLISION_HEIGHT, ObjectDefines.h:49. The reference computes a real
/// per-model height from CreatureDisplayInfo and CreatureModelData
/// (Unit::GetCollisionHeight, Unit.cpp:16705-16728); this realm imports neither
/// table, so both ends use the constant the reference itself calls "most common
/// value in dbc" and says so.
inline constexpr float kLocalCollisionHeight = 2.03128f;

/// How many tiles stay resident. A pack tile is a 533 yd square; sixteen covers
/// a generous view of one map without holding a continent in memory.
inline constexpr size_t kLocalCollisionResidentTiles = 16;

/// The installed pack. Absent, empty, or covering a different map than the one
/// asked about, every test answers "visible".
class LocalCollisionData {
public:
    /// Read `<directory>/collision.manifest`. Tile bytes are read on first use.
    bool load(const std::string& directory, std::string& error) {
        namespace fs = std::filesystem;
        entries_.clear(); cache_.clear(); order_.clear(); pinned_.clear(); maps_.clear();
        fingerprint_ = 0; directory_.clear();
        std::error_code ec;
        const fs::path manifest = fs::path(directory) / "collision.manifest";
        if (!fs::is_regular_file(manifest, ec)) { error = "No collision manifest in " + directory; return false; }
        std::ifstream in(manifest);
        if (!in) { error = "Cannot read " + manifest.string(); return false; }
        std::string line;
        if (!std::getline(in, line) || line != "WCOLMANIFEST 1") { error = "Collision manifest header is not WCOLMANIFEST 1"; return false; }
        uint32_t declared = 0;
        bool haveFingerprint = false;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream words(line);
            std::string keyword; words >> keyword;
            if (keyword == "fingerprint") { words >> declared; haveFingerprint = true; continue; }
            if (keyword != "tile") { error = "Unknown collision manifest keyword " + keyword; return false; }
            Entry e;
            words >> e.mapId >> e.tileY >> e.tileX >> e.triangles >> e.hash
                  >> e.lo[0] >> e.lo[1] >> e.lo[2] >> e.hi[0] >> e.hi[1] >> e.hi[2] >> e.file;
            if (!words || e.file.empty() || e.file.find("..") != std::string::npos ||
                e.file.find('/') != std::string::npos || e.file.find('\\') != std::string::npos) {
                error = "Malformed collision manifest tile line"; return false;
            }
            if (e.triangles > kLocalCollisionMaxTriangles) { error = "Collision manifest tile is implausibly large"; return false; }
            entries_.push_back(std::move(e));
        }
        if (!haveFingerprint) { error = "Collision manifest carries no fingerprint"; return false; }
        if (entries_.size() > 65536) { error = "Collision manifest lists too many tiles"; return false; }
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            return std::tie(a.mapId, a.tileY, a.tileX) < std::tie(b.mapId, b.tileY, b.tileX);
        });
        const uint32_t computed = computeFingerprint(entries_);
        if (computed != declared) { error = "Collision manifest fingerprint does not match its tile list"; return false; }
        fingerprint_ = computed;
        directory_ = directory;
        for (const auto& e : entries_) maps_.insert(e.mapId);
        error.clear();
        return true;
    }

    /// The extractor and the suite build tiles in memory instead of reading them.
    void adopt(LocalCollisionTile tile) {
        Entry e;
        e.mapId = tile.mapId; e.tileX = tile.tileX; e.tileY = tile.tileY;
        e.triangles = uint32_t(tile.triangles.size());
        for (unsigned i = 0; i < 3; ++i) { e.lo[i] = tile.tree.bounds().lo[i]; e.hi[i] = tile.tree.bounds().hi[i]; }
        const auto bytes = localCollisionEncodeTile(tile);
        e.hash = localCollisionHash(2166136261U, bytes.data(), bytes.size());
        e.file = localCollisionTileFileName(tile.mapId, tile.tileY, tile.tileX);
        const auto k = key(tile.mapId, tile.tileX, tile.tileY);
        const auto existing = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& o) {
            return key(o.mapId, o.tileX, o.tileY) == k;
        });
        if (existing == entries_.end()) entries_.push_back(std::move(e)); else *existing = std::move(e);
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            return std::tie(a.mapId, a.tileY, a.tileX) < std::tie(b.mapId, b.tileY, b.tileX);
        });
        maps_.insert(tile.mapId);
        // An adopted tile has no file to be read back from, so it is pinned
        // rather than put in the eviction cache.
        pinned_[k] = std::move(tile);
        fingerprint_ = computeFingerprint(entries_);
    }

    bool empty() const { return entries_.empty(); }
    bool covers(uint32_t mapId) const { return maps_.count(mapId) != 0; }
    size_t tileCount() const { return entries_.size(); }
    uint64_t triangleCount() const {
        uint64_t total = 0; for (const auto& e : entries_) total += e.triangles; return total;
    }
    uint32_t fingerprint() const { return fingerprint_; }

    /// StaticMapTree::isInLineOfSight, MapTree.cpp:129-149, over every tile of
    /// this map the segment can enter. The reference walks its own grid and
    /// streams tiles in; this walks the installed tile list, which is the same
    /// answer for a pack whose tiles do not overlap.
    bool isInLineOfSight(uint32_t mapId, float x1, float y1, float z1,
                         float x2, float y2, float z2, bool ignoreM2) const {
        if (!covers(mapId)) return true; // inert without data
        const LocalCollisionVec3 p1{x1, y1, z1}, p2{x2, y2, z2};
        const LocalCollisionVec3 delta = p2 - p1;
        const float maxDist = std::sqrt(localCollisionDot(delta, delta));
        if (!std::isfinite(maxDist) || maxDist == std::numeric_limits<float>::max()) return false;
        if (maxDist < 1e-10f) return true;
        const LocalCollisionVec3 dir = delta * (1.f / maxDist);
        for (const auto& e : entries_) {
            if (e.mapId != mapId || !e.triangles) continue;
            if (!segmentEntersBox(p1, dir, maxDist, e)) continue;
            const LocalCollisionTile* tile = resident(e);
            if (!tile) continue; // a tile that will not load cannot occlude
            float distance = maxDist;
            if (tile->intersectRay(p1, dir, distance, true, ignoreM2)) return false;
        }
        return true;
    }

    static std::string localCollisionTileFileName(uint32_t mapId, uint32_t tileY, uint32_t tileX) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%03u_%02u_%02u.wcol", mapId, tileY, tileX);
        return buffer;
    }

    struct Entry {
        uint32_t mapId = 0, tileX = 0, tileY = 0, triangles = 0, hash = 0;
        float lo[3]{}, hi[3]{};
        std::string file;
    };
    const std::vector<Entry>& entries() const { return entries_; }

    static uint32_t computeFingerprint(const std::vector<Entry>& entries) {
        uint32_t f = 2166136261U;
        const auto hash = [&](uint32_t value) { f = localCollisionHash(f, &value, 4); };
        hash(0x4C4F5330); hash(uint32_t(entries.size()));
        for (const auto& e : entries) { hash(e.mapId); hash(e.tileY); hash(e.tileX); hash(e.triangles); hash(e.hash); }
        return f;
    }

private:
    static uint64_t key(uint32_t mapId, uint32_t tileX, uint32_t tileY) {
        return (uint64_t(mapId) << 32) | (uint64_t(tileY) << 16) | tileX;
    }
    static bool segmentEntersBox(const LocalCollisionVec3& org, const LocalCollisionVec3& dir,
                                 float maxDist, const Entry& e) {
        float tmin = 0.f, tmax = maxDist;
        for (unsigned i = 0; i < 3; ++i) {
            if (std::fabs(dir[i]) < 1e-9f) { if (org[i] < e.lo[i] || org[i] > e.hi[i]) return false; continue; }
            const float inv = 1.f / dir[i];
            float t1 = (e.lo[i] - org[i]) * inv, t2 = (e.hi[i] - org[i]) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1); tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
        return true;
    }
    const LocalCollisionTile* resident(const Entry& e) const {
        const auto k = key(e.mapId, e.tileX, e.tileY);
        const auto held = pinned_.find(k);
        if (held != pinned_.end()) return &held->second;
        const auto it = cache_.find(k);
        if (it != cache_.end()) return &it->second;
        if (directory_.empty()) return nullptr;
        std::ifstream in(std::filesystem::path(directory_) / e.file, std::ios::binary);
        if (!in) return nullptr;
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        if (localCollisionHash(2166136261U, bytes.data(), bytes.size()) != e.hash) return nullptr;
        LocalCollisionTile tile; std::string error;
        if (!localCollisionDecodeTile(bytes, tile, error)) return nullptr;
        if (tile.mapId != e.mapId || tile.tileX != e.tileX || tile.tileY != e.tileY) return nullptr;
        while (order_.size() >= kLocalCollisionResidentTiles) { cache_.erase(order_.front()); order_.erase(order_.begin()); }
        order_.push_back(k);
        return &(cache_[k] = std::move(tile));
    }

    std::vector<Entry> entries_;
    std::string directory_;
    uint32_t fingerprint_ = 0;
    std::set<uint32_t> maps_;
    std::map<uint64_t, LocalCollisionTile> pinned_;
    mutable std::map<uint64_t, LocalCollisionTile> cache_;
    mutable std::vector<uint64_t> order_;
};

/// The point each end of the segment contributes.
/// WorldObject::IsWithinLOSInMap (Object.cpp:1412-1435) with
/// WorldObject::GetHitSpherePointFor (Object.cpp:1295-1302): a player is its own
/// position raised by its collision height; anything else is the point on its hit
/// sphere nearest the other end.
inline void localLineOfSightHitSpherePoint(float fromX, float fromY, float fromZ,
                                           float atX, float atY, float atZ, float reach,
                                           float& outX, float& outY, float& outZ) {
    const LocalCollisionVec3 self{atX, atY, atZ + kLocalCollisionHeight};
    const LocalCollisionVec3 dest{fromX, fromY, fromZ};
    const LocalCollisionVec3 delta = dest - self;
    const float length = std::sqrt(localCollisionDot(delta, delta));
    const float step = std::min(std::sqrt((dest.x - atX) * (dest.x - atX) + (dest.y - atY) * (dest.y - atY) +
                                          (dest.z - atZ) * (dest.z - atZ)), std::max(0.f, reach));
    const LocalCollisionVec3 unit = length > 0.f ? delta * (1.f / length) : LocalCollisionVec3{};
    const LocalCollisionVec3 contact = self + unit * step;
    outX = contact.x; outY = contact.y; outZ = contact.z;
}

/// The whole gate: a player caster against a creature target.
/// Returns true - visible - whenever no pack covers the map, which is the state
/// this build ships in.
inline bool localLineOfSightReady(const LocalCollisionData* collision, uint32_t mapId,
                                  float casterX, float casterY, float casterZ,
                                  float targetX, float targetY, float targetZ, float targetReach) {
    if (!collision || !collision->covers(mapId)) return true;
    const float eyeZ = casterZ + kLocalCollisionHeight;
    float tx = targetX, ty = targetY, tz = targetZ;
    localLineOfSightHitSpherePoint(casterX, casterY, eyeZ, targetX, targetY, targetZ, targetReach, tx, ty, tz);
    // ModelIgnoreFlags::M2: Spell.cpp:6115 asks for it and both Map::isInLineOfSight
    // config arms default to leaving it in force (WorldConfig.cpp:558).
    return collision->isInLineOfSight(mapId, casterX, casterY, eyeZ, tx, ty, tz, true);
}

/// A player caster against another player: both ends are raw position plus the
/// collision height (Object.cpp:1418-1431).
inline bool localLineOfSightReadyPlayers(const LocalCollisionData* collision, uint32_t mapId,
                                         float casterX, float casterY, float casterZ,
                                         float targetX, float targetY, float targetZ) {
    if (!collision || !collision->covers(mapId)) return true;
    return collision->isInLineOfSight(mapId, casterX, casterY, casterZ + kLocalCollisionHeight,
                                      targetX, targetY, targetZ + kLocalCollisionHeight, true);
}

} // namespace wowee::game
