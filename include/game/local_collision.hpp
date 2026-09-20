#pragma once
// P05 line of sight, first half : the collision pack this realm defines,
// its Bounding Interval Hierarchy and the ray test over it.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33.
//   BIH::build / BIH::subdivide / BIH::createNode   BoundingIntervalHierarchy.h:78-113,
//       :427-432 and BoundingIntervalHierarchy.cpp:26-271 - the Sunflow-derived
//       builder, the BVH2 empty-space cut and the leaf encoding.
//   BIH::intersectRay                               BoundingIntervalHierarchy.h:117-276.
//   VMAP::IntersectTriangle                         WorldModel.cpp:34-84 - the
//       Moeller-Trumbore test with its 1e-5 determinant cut.
//   StaticMapTree::isInLineOfSight                  MapTree.cpp:129-149 - the
//       non-finite guard, the 1e-10 early "visible", the unit direction and the
//       stop-at-first-hit intersection.
//   StaticMapTree::getTileFileName                  MapTree.cpp:76-84 - the
//       <map>_<tileY>_<tileX> tile naming this pack keeps.
//   WorldModel::IntersectRay                        WorldModel.cpp:542-565 - the
//       ModelIgnoreFlags::M2 arm.
//
// What is NOT transcribed, and why it cannot be: the reference keeps one tree of
// *model instances* per map and a second tree of *triangles* inside each model,
// with an instance transform between them (ModelInstance::intersectRay,
// ModelInstance.cpp:32-63). That indirection exists so one WMO's mesh is stored
// once and instanced many times. This pack instead bakes every triangle into
// world space per ADT tile and keeps one tree per tile, because the extractor
// that writes it runs once on the player's machine and the console that reads it
// benefits from never doing a matrix multiply per ray. The ray arithmetic, the
// tree and the triangle test are the reference's; the instancing is not.
//
// Nothing in this file was validated against real map geometry. No client MPQ
// and no extracted WMO/M2/ADT asset exists in the environment this was written
// in, so no collision pack could be produced or checked here. The suite
// tools/tests/local_spell_level_los_test.cpp drives it with synthetic
// geometry authored in the test.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace wowee::game {

/// One vertex or direction in the client's own world space.
struct LocalCollisionVec3 {
    float x = 0.f, y = 0.f, z = 0.f;
    float operator[](unsigned i) const { return i == 0 ? x : i == 1 ? y : z; }
    float& operator[](unsigned i) { return i == 0 ? x : i == 1 ? y : z; }
};
inline LocalCollisionVec3 operator-(const LocalCollisionVec3& a, const LocalCollisionVec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline LocalCollisionVec3 operator+(const LocalCollisionVec3& a, const LocalCollisionVec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline LocalCollisionVec3 operator*(const LocalCollisionVec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float localCollisionDot(const LocalCollisionVec3& a, const LocalCollisionVec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline LocalCollisionVec3 localCollisionCross(const LocalCollisionVec3& a, const LocalCollisionVec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// An axis-aligned box, G3D::AABox's role in the reference.
struct LocalCollisionBox {
    LocalCollisionVec3 lo{ std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::infinity() };
    LocalCollisionVec3 hi{ -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity() };
    void merge(const LocalCollisionBox& o) {
        for (unsigned i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], o.lo[i]); hi[i] = std::max(hi[i], o.hi[i]); }
    }
    bool empty() const { return lo.x > hi.x; }
};

/// One triangle, by index into the tile's vertex array. `flags` bit 0 marks a
/// triangle that came from an M2 doodad, which is what ModelIgnoreFlags::M2
/// skips (WorldModel.cpp:545-552).
struct LocalCollisionTriangle { uint32_t a = 0, b = 0, c = 0, flags = 0; };
inline constexpr uint32_t kLocalCollisionFromM2 = 1u;

/// Bounding Interval Hierarchy, transcribed from BoundingIntervalHierarchy.h /
/// .cpp. The node encoding is the reference's byte for byte: three uint32 per
/// node, the top two bits the axis (3 meaning "leaf"), bit 29 the BVH2 empty
/// space cut, the low 29 bits the child offset or the first object index.
class LocalCollisionBih {
public:
    LocalCollisionBih() { initEmpty(); }

    /// primitives: any array; bounds(i) must return the i-th primitive's box.
    template <class BoundsFunc>
    void build(uint32_t count, BoundsFunc bounds, uint32_t leafSize = 3) {
        if (!count) { initEmpty(); return; }
        BuildData dat;
        dat.maxPrims = int(leafSize);
        dat.numPrims = count;
        dat.indices.resize(count);
        dat.primBound.resize(count);
        bounds_ = LocalCollisionBox{};
        for (uint32_t i = 0; i < count; ++i) {
            dat.indices[i] = i;
            dat.primBound[i] = bounds(i);
            bounds_.merge(dat.primBound[i]);
        }
        std::vector<uint32_t> temp;
        temp.push_back(uint32_t(3u << 30));
        temp.insert(temp.end(), 2, 0u);
        AABound gridBox{bounds_.lo, bounds_.hi}, nodeBox = gridBox;
        subdivide(0, int(dat.numPrims) - 1, temp, dat, gridBox, nodeBox, 0, 0);
        objects_.assign(dat.indices.begin(), dat.indices.end());
        tree_ = std::move(temp);
    }

    /// BIH::intersectRay, BoundingIntervalHierarchy.h:117-276.
    /// callback(objectIndex, maxDist) returns true when it registered a hit.
    template <class Callback>
    void intersectRay(const LocalCollisionVec3& org, const LocalCollisionVec3& dir,
                      Callback&& callback, float& maxDist, bool stopAtFirstHit) const {
        if (tree_.empty() || bounds_.empty()) return;
        float intervalMin = -1.f, intervalMax = -1.f;
        LocalCollisionVec3 invDir;
        for (unsigned i = 0; i < 3; ++i) {
            invDir[i] = 1.f / dir[i];
            if (std::fabs(dir[i]) > 1e-9f) {
                float t1 = (bounds_.lo[i] - org[i]) * invDir[i];
                float t2 = (bounds_.hi[i] - org[i]) * invDir[i];
                if (t1 > t2) std::swap(t1, t2);
                if (t1 > intervalMin) intervalMin = t1;
                if (t2 < intervalMax || intervalMax < 0.f) intervalMax = t2;
                if (intervalMax <= 0.f || intervalMin >= maxDist) return;
            }
        }
        if (intervalMin > intervalMax) return;
        intervalMin = std::max(intervalMin, 0.f);
        intervalMax = std::min(intervalMax, maxDist);

        uint32_t offsetFront[3], offsetBack[3], offsetFront3[3], offsetBack3[3];
        for (unsigned i = 0; i < 3; ++i) {
            offsetFront[i] = rawBits(dir[i]) >> 31;
            offsetBack[i] = offsetFront[i] ^ 1u;
            offsetFront3[i] = offsetFront[i] * 3u;
            offsetBack3[i] = offsetBack[i] * 3u;
            ++offsetFront[i];
            ++offsetBack[i];
        }
        struct StackNode { uint32_t node; float tnear, tfar; };
        StackNode stack[kStack];
        int stackPos = 0;
        uint32_t node = 0;
        while (true) {
            while (true) {
                if (node + 2 >= tree_.size()) return;
                const uint32_t tn = tree_[node];
                const uint32_t axis = (tn & (3u << 30)) >> 30;
                const bool bvh2 = (tn & (1u << 29)) != 0;
                uint32_t offset = tn & ~(7u << 29);
                if (!bvh2) {
                    if (axis < 3) {
                        const float tf = (bitsToFloat(tree_[node + offsetFront[axis]]) - org[axis]) * invDir[axis];
                        const float tb = (bitsToFloat(tree_[node + offsetBack[axis]]) - org[axis]) * invDir[axis];
                        if (tf < intervalMin && tb > intervalMax) break;
                        const uint32_t back = offset + offsetBack3[axis];
                        node = back;
                        if (tf < intervalMin) { intervalMin = (tb >= intervalMin) ? tb : intervalMin; continue; }
                        node = offset + offsetFront3[axis];
                        if (tb > intervalMax) { intervalMax = (tf <= intervalMax) ? tf : intervalMax; continue; }
                        if (stackPos >= kStack) return;
                        stack[stackPos].node = back;
                        stack[stackPos].tnear = (tb >= intervalMin) ? tb : intervalMin;
                        stack[stackPos].tfar = intervalMax;
                        ++stackPos;
                        intervalMax = (tf <= intervalMax) ? tf : intervalMax;
                        continue;
                    }
                    int n = int(tree_[node + 1]);
                    while (n > 0) {
                        if (offset >= objects_.size()) return;
                        const bool hit = callback(objects_[offset], maxDist);
                        if (stopAtFirstHit && hit) return;
                        --n; ++offset;
                    }
                    break;
                }
                if (axis > 2) return; // should not happen
                const float tf = (bitsToFloat(tree_[node + offsetFront[axis]]) - org[axis]) * invDir[axis];
                const float tb = (bitsToFloat(tree_[node + offsetBack[axis]]) - org[axis]) * invDir[axis];
                node = offset;
                intervalMin = (tf >= intervalMin) ? tf : intervalMin;
                intervalMax = (tb <= intervalMax) ? tb : intervalMax;
                if (intervalMin > intervalMax) break;
            }
            while (true) {
                if (!stackPos) return;
                --stackPos;
                intervalMin = stack[stackPos].tnear;
                if (maxDist < intervalMin) continue;
                node = stack[stackPos].node;
                intervalMax = stack[stackPos].tfar;
                break;
            }
        }
    }

    const std::vector<uint32_t>& tree() const { return tree_; }
    const std::vector<uint32_t>& objects() const { return objects_; }
    const LocalCollisionBox& bounds() const { return bounds_; }
    /// The read half of BIH::readFromFile (BoundingIntervalHierarchy.cpp:285-305),
    /// with every field range-checked because this file comes off a memory card.
    bool adopt(LocalCollisionBox bounds, std::vector<uint32_t> tree, std::vector<uint32_t> objects,
               uint32_t primitiveCount) {
        if (tree.size() < 3 || tree.size() > (1u << 28) || objects.size() != primitiveCount) return false;
        for (auto o : objects) if (o >= primitiveCount) return false;
        bounds_ = bounds; tree_ = std::move(tree); objects_ = std::move(objects);
        return true;
    }

private:
    static constexpr int kStack = 64; // MAX_STACK_SIZE
    static uint32_t rawBits(float f) { uint32_t r; std::memcpy(&r, &f, 4); return r; }
    static float bitsToFloat(uint32_t i) { float r; std::memcpy(&r, &i, 4); return r; }
    void initEmpty() {
        tree_.clear(); objects_.clear(); bounds_ = LocalCollisionBox{};
        tree_.push_back(uint32_t(3u << 30));
        tree_.insert(tree_.end(), 2, 0u);
    }
    struct AABound { LocalCollisionVec3 lo, hi; };
    struct BuildData {
        std::vector<uint32_t> indices;
        std::vector<LocalCollisionBox> primBound;
        uint32_t numPrims = 0;
        int maxPrims = 3;
    };
    /// G3D::Vector3::primaryAxis - the axis of greatest extent. G3D is a
    /// third-party dependency of the reference and is not vendored in the pinned
    /// checkout, so this is the documented semantic rather than a line citation.
    static unsigned primaryAxis(const LocalCollisionVec3& d) {
        const float nx = std::fabs(d.x), ny = std::fabs(d.y), nz = std::fabs(d.z);
        if (nx > ny) return nx > nz ? 0u : 2u;
        return ny > nz ? 1u : 2u;
    }
    static bool fuzzyEq(float a, float b) {
        return a == b || std::fabs(a - b) <= 1e-6f * std::max(std::fabs(a), std::fabs(b));
    }
    static void createNode(std::vector<uint32_t>& tree, int nodeIndex, uint32_t left, uint32_t right) {
        tree[size_t(nodeIndex) + 0] = (3u << 30) | left;
        tree[size_t(nodeIndex) + 1] = right - left + 1;
    }
    void subdivide(int left, int right, std::vector<uint32_t>& tree, BuildData& dat,
                   AABound& gridBox, AABound& nodeBox, int nodeIndex, int depth) {
        if ((right - left + 1) <= dat.maxPrims || depth >= kStack) {
            createNode(tree, nodeIndex, uint32_t(left), uint32_t(right));
            return;
        }
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        int axis = -1, prevAxis, rightOrig;
        float clipL = nan, clipR = nan, prevClip = nan, split = nan, prevSplit;
        bool wasLeft = true;
        while (true) {
            prevAxis = axis; prevSplit = split;
            const LocalCollisionVec3 d = gridBox.hi - gridBox.lo;
            // The reference throws on these; a console reading a file off a
            // memory card degrades to a leaf instead of terminating the realm.
            if (d.x < 0 || d.y < 0 || d.z < 0) { createNode(tree, nodeIndex, uint32_t(left), uint32_t(right)); return; }
            for (unsigned i = 0; i < 3; ++i)
                if (nodeBox.hi[i] < gridBox.lo[i] || nodeBox.lo[i] > gridBox.hi[i]) {
                    createNode(tree, nodeIndex, uint32_t(left), uint32_t(right)); return;
                }
            axis = int(primaryAxis(d));
            split = 0.5f * (gridBox.lo[unsigned(axis)] + gridBox.hi[unsigned(axis)]);
            clipL = -inf; clipR = inf;
            rightOrig = right;
            float nodeL = inf, nodeR = -inf;
            for (int i = left; i <= right;) {
                const uint32_t obj = dat.indices[size_t(i)];
                const float minb = dat.primBound[obj].lo[unsigned(axis)];
                const float maxb = dat.primBound[obj].hi[unsigned(axis)];
                const float center = (minb + maxb) * 0.5f;
                if (center <= split) { ++i; if (clipL < maxb) clipL = maxb; }
                else {
                    std::swap(dat.indices[size_t(i)], dat.indices[size_t(right)]);
                    --right;
                    if (clipR > minb) clipR = minb;
                }
                nodeL = std::min(nodeL, minb); nodeR = std::max(nodeR, maxb);
            }
            if (nodeL > nodeBox.lo[unsigned(axis)] && nodeR < nodeBox.hi[unsigned(axis)]) {
                const float nodeBoxW = nodeBox.hi[unsigned(axis)] - nodeBox.lo[unsigned(axis)];
                const float nodeNewW = nodeR - nodeL;
                if (1.3f * nodeNewW < nodeBoxW) {
                    const uint32_t nextIndex = uint32_t(tree.size());
                    tree.insert(tree.end(), 3, 0u);
                    tree[size_t(nodeIndex) + 0] = (uint32_t(axis) << 30) | (1u << 29) | nextIndex;
                    tree[size_t(nodeIndex) + 1] = rawBits(nodeL);
                    tree[size_t(nodeIndex) + 2] = rawBits(nodeR);
                    nodeBox.lo[unsigned(axis)] = nodeL;
                    nodeBox.hi[unsigned(axis)] = nodeR;
                    subdivide(left, rightOrig, tree, dat, gridBox, nodeBox, int(nextIndex), depth + 1);
                    return;
                }
            }
            if (right == rightOrig) {
                if (prevAxis == axis && fuzzyEq(prevSplit, split)) {
                    createNode(tree, nodeIndex, uint32_t(left), uint32_t(right)); return;
                }
                if (clipL <= split) { gridBox.hi[unsigned(axis)] = split; prevClip = clipL; wasLeft = true; continue; }
                gridBox.hi[unsigned(axis)] = split; prevClip = nan;
            } else if (left > right) {
                right = rightOrig;
                if (prevAxis == axis && fuzzyEq(prevSplit, split)) {
                    createNode(tree, nodeIndex, uint32_t(left), uint32_t(right)); return;
                }
                if (clipR >= split) { gridBox.lo[unsigned(axis)] = split; prevClip = clipR; wasLeft = false; continue; }
                gridBox.lo[unsigned(axis)] = split; prevClip = nan;
            } else {
                if (prevAxis != -1 && !std::isnan(prevClip)) {
                    const uint32_t nextIndex = uint32_t(tree.size());
                    tree.insert(tree.end(), 3, 0u);
                    if (wasLeft) {
                        tree[size_t(nodeIndex) + 0] = (uint32_t(prevAxis) << 30) | nextIndex;
                        tree[size_t(nodeIndex) + 1] = rawBits(prevClip);
                        tree[size_t(nodeIndex) + 2] = rawBits(inf);
                    } else {
                        tree[size_t(nodeIndex) + 0] = (uint32_t(prevAxis) << 30) | (nextIndex - 3u);
                        tree[size_t(nodeIndex) + 1] = rawBits(-inf);
                        tree[size_t(nodeIndex) + 2] = rawBits(prevClip);
                    }
                    ++depth;
                    nodeIndex = int(nextIndex);
                }
                break;
            }
        }
        uint32_t nextIndex = uint32_t(tree.size());
        const int nl = right - left + 1;
        const int nr = rightOrig - (right + 1) + 1;
        if (nl > 0) tree.insert(tree.end(), 3, 0u); else nextIndex -= 3u;
        if (nr > 0) tree.insert(tree.end(), 3, 0u);
        tree[size_t(nodeIndex) + 0] = (uint32_t(axis) << 30) | nextIndex;
        tree[size_t(nodeIndex) + 1] = rawBits(clipL);
        tree[size_t(nodeIndex) + 2] = rawBits(clipR);
        AABound gridBoxL(gridBox), gridBoxR(gridBox), nodeBoxL(nodeBox), nodeBoxR(nodeBox);
        gridBoxL.hi[unsigned(axis)] = gridBoxR.lo[unsigned(axis)] = split;
        nodeBoxL.hi[unsigned(axis)] = clipL;
        nodeBoxR.lo[unsigned(axis)] = clipR;
        if (nl > 0) subdivide(left, right, tree, dat, gridBoxL, nodeBoxL, int(nextIndex), depth + 1);
        if (nr > 0) subdivide(right + 1, rightOrig, tree, dat, gridBoxR, nodeBoxR, int(nextIndex) + 3, depth + 1);
    }

    std::vector<uint32_t> tree_;
    std::vector<uint32_t> objects_;
    LocalCollisionBox bounds_;
};

/// VMAP::IntersectTriangle, WorldModel.cpp:34-84 - Moeller-Trumbore with the
/// reference's own 1e-5 determinant cut and its "closer than the previous hit"
/// arm.
inline bool localCollisionIntersectTriangle(const LocalCollisionTriangle& tri,
                                            const std::vector<LocalCollisionVec3>& points,
                                            const LocalCollisionVec3& origin, const LocalCollisionVec3& direction,
                                            float& distance) {
    static constexpr float kEps = 1e-5f;
    const LocalCollisionVec3 v0 = points[tri.a];
    const LocalCollisionVec3 e1 = points[tri.b] - v0;
    const LocalCollisionVec3 e2 = points[tri.c] - v0;
    const LocalCollisionVec3 p = localCollisionCross(direction, e2);
    const float a = localCollisionDot(e1, p);
    if (std::fabs(a) < kEps) return false;
    const float f = 1.f / a;
    const LocalCollisionVec3 s = origin - v0;
    const float u = f * localCollisionDot(s, p);
    if (u < 0.f || u > 1.f) return false;
    const LocalCollisionVec3 q = localCollisionCross(s, e1);
    const float v = f * localCollisionDot(direction, q);
    if (v < 0.f || (u + v) > 1.f) return false;
    const float t = f * localCollisionDot(e2, q);
    if (t > 0.f && t < distance) { distance = t; return true; }
    return false;
}

/// The reference's ADT grid, SIZE_OF_GRIDS, and the <map>_<tileY>_<tileX> naming
/// StaticMapTree::getTileFileName writes (MapTree.cpp:76-84). A pack tile is one
/// ADT tile, so a pack tile and a vmap tile cover the same ground.
///
/// Deliberately, this file offers no world-coordinate-to-tile-index function.
/// The reference has two of them with different conventions (Map::LoadMapTile's
/// grid coordinates and the vmap tile name's) and neither could be checked here
/// against real data, so the runtime never derives a tile from a position: every
/// tile carries its own world bounding box in the manifest and the ray test asks
/// the boxes. A transposed convention would then be a missing tile, not a wrong
/// answer - and there is nothing in this sandbox that could have caught one.
inline constexpr float kLocalCollisionTileSize = 533.33333f;

/// One tile of baked, world-space collision: the vertices, the triangles and the
/// tree over them.
struct LocalCollisionTile {
    uint32_t mapId = 0;
    uint32_t tileX = 0, tileY = 0;
    std::vector<LocalCollisionVec3> vertices;
    std::vector<LocalCollisionTriangle> triangles;
    LocalCollisionBih tree;

    void buildTree() {
        tree.build(uint32_t(triangles.size()), [&](uint32_t i) {
            LocalCollisionBox box;
            for (auto index : {triangles[i].a, triangles[i].b, triangles[i].c}) {
                LocalCollisionBox one; one.lo = one.hi = vertices[index];
                box.merge(one);
            }
            return box;
        });
    }

    /// GroupModel::IntersectRay (WorldModel.cpp:442-452) over this tile, with
    /// WorldModel::IntersectRay's M2 arm (:545-552) applied per triangle because
    /// this pack has no model objects to carry the flag.
    bool intersectRay(const LocalCollisionVec3& origin, const LocalCollisionVec3& direction,
                      float& distance, bool stopAtFirstHit, bool ignoreM2) const {
        if (triangles.empty()) return false;
        bool hit = false;
        tree.intersectRay(origin, direction, [&](uint32_t entry, float& maxDist) {
            if (entry >= triangles.size()) return false;
            const auto& tri = triangles[entry];
            if (ignoreM2 && (tri.flags & kLocalCollisionFromM2)) return false;
            if (localCollisionIntersectTriangle(tri, vertices, origin, direction, maxDist)) hit = true;
            return hit;
        }, distance, stopAtFirstHit);
        return hit;
    }
};

/// Where a tile lives inside a pack, and its own hash.
struct LocalCollisionTileEntry {
    uint32_t mapId = 0, tileX = 0, tileY = 0;
    uint32_t triangles = 0, hash = 0;
    std::string file;
};

// --- the pack's wire format -------------------------------------------------
//
//  <directory>/collision.manifest   text, one "map tileY tileX triangles hash
//                                   file" line per tile after a header line
//  <directory>/<map>_<tileY>_<tileX>.wcol
//      "WCOL0001"  8 bytes
//      uint32 mapId, tileX, tileY
//      uint32 vertexCount, triangleCount, treeWords, objectCount
//      float  boundsLo[3], boundsHi[3]
//      float  vertices[3 * vertexCount]
//      uint32 triangles[4 * triangleCount]   (a, b, c, flags)
//      uint32 tree[treeWords]
//      uint32 objects[objectCount]
//      uint32 fnv1a of every byte above
//
// Little-endian throughout, which is what both the PS4 and the player's PC are.
inline constexpr char kLocalCollisionMagic[9] = "WCOL0001";
inline constexpr uint32_t kLocalCollisionMaxVertices = 4000000;
inline constexpr uint32_t kLocalCollisionMaxTriangles = 4000000;

inline uint32_t localCollisionHash(uint32_t seed, const void* data, size_t bytes) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) seed = (seed ^ p[i]) * 16777619U;
    return seed;
}

inline void localCollisionPut(std::vector<uint8_t>& out, uint32_t value) {
    for (unsigned b = 0; b < 4; ++b) out.push_back(uint8_t(value >> (b * 8)));
}
inline void localCollisionPutFloat(std::vector<uint8_t>& out, float value) {
    uint32_t bits; std::memcpy(&bits, &value, 4); localCollisionPut(out, bits);
}

/// Serialise one tile. Used by the extractor tool and by the suite that authors
/// synthetic geometry; the console only ever reads.
inline std::vector<uint8_t> localCollisionEncodeTile(const LocalCollisionTile& tile) {
    std::vector<uint8_t> out;
    out.insert(out.end(), kLocalCollisionMagic, kLocalCollisionMagic + 8);
    localCollisionPut(out, tile.mapId);
    localCollisionPut(out, tile.tileX);
    localCollisionPut(out, tile.tileY);
    localCollisionPut(out, uint32_t(tile.vertices.size()));
    localCollisionPut(out, uint32_t(tile.triangles.size()));
    localCollisionPut(out, uint32_t(tile.tree.tree().size()));
    localCollisionPut(out, uint32_t(tile.tree.objects().size()));
    for (unsigned i = 0; i < 3; ++i) localCollisionPutFloat(out, tile.tree.bounds().lo[i]);
    for (unsigned i = 0; i < 3; ++i) localCollisionPutFloat(out, tile.tree.bounds().hi[i]);
    for (const auto& v : tile.vertices) { localCollisionPutFloat(out, v.x); localCollisionPutFloat(out, v.y); localCollisionPutFloat(out, v.z); }
    for (const auto& t : tile.triangles) { localCollisionPut(out, t.a); localCollisionPut(out, t.b); localCollisionPut(out, t.c); localCollisionPut(out, t.flags); }
    for (auto word : tile.tree.tree()) localCollisionPut(out, word);
    for (auto word : tile.tree.objects()) localCollisionPut(out, word);
    localCollisionPut(out, localCollisionHash(2166136261U, out.data(), out.size()));
    return out;
}

/// Read one tile back, refusing anything malformed rather than trusting it: this
/// file arrives from the player's own machine and every bound is checked.
inline bool localCollisionDecodeTile(const std::vector<uint8_t>& bytes, LocalCollisionTile& tile, std::string& error) {
    const auto fail = [&](const char* why) { error = why; return false; };
    if (bytes.size() < 8 + 4 * 7 + 4 * 6 + 4) return fail("Collision tile is truncated");
    if (std::memcmp(bytes.data(), kLocalCollisionMagic, 8) != 0) return fail("Collision tile has the wrong magic");
    size_t at = 8;
    const auto u32 = [&]() { uint32_t v = 0; std::memcpy(&v, bytes.data() + at, 4); at += 4; return v; };
    const auto f32 = [&]() { const uint32_t v = u32(); float r; std::memcpy(&r, &v, 4); return r; };
    tile.mapId = u32(); tile.tileX = u32(); tile.tileY = u32();
    const uint32_t vertexCount = u32(), triangleCount = u32(), treeWords = u32(), objectCount = u32();
    if (vertexCount > kLocalCollisionMaxVertices || triangleCount > kLocalCollisionMaxTriangles ||
        treeWords > (1u << 28) || objectCount != triangleCount)
        return fail("Collision tile declares implausible counts");
    LocalCollisionBox bounds;
    for (unsigned i = 0; i < 3; ++i) bounds.lo[i] = f32();
    for (unsigned i = 0; i < 3; ++i) bounds.hi[i] = f32();
    const size_t body = size_t(vertexCount) * 12 + size_t(triangleCount) * 16 + size_t(treeWords) * 4 + size_t(objectCount) * 4;
    if (bytes.size() != at + body + 4) return fail("Collision tile size does not match its header");
    const uint32_t expected = localCollisionHash(2166136261U, bytes.data(), bytes.size() - 4);
    uint32_t stored = 0; std::memcpy(&stored, bytes.data() + bytes.size() - 4, 4);
    if (stored != expected) return fail("Collision tile hash mismatch");
    tile.vertices.resize(vertexCount);
    for (auto& v : tile.vertices) { v.x = f32(); v.y = f32(); v.z = f32(); }
    tile.triangles.resize(triangleCount);
    for (auto& t : tile.triangles) {
        t.a = u32(); t.b = u32(); t.c = u32(); t.flags = u32();
        if (t.a >= vertexCount || t.b >= vertexCount || t.c >= vertexCount)
            return fail("Collision tile triangle indexes a missing vertex");
    }
    std::vector<uint32_t> tree(treeWords), objects(objectCount);
    for (auto& word : tree) word = u32();
    for (auto& word : objects) word = u32();
    for (unsigned i = 0; i < 3; ++i)
        if (!std::isfinite(bounds.lo[i]) || !std::isfinite(bounds.hi[i])) return fail("Collision tile bounds are not finite");
    if (!tile.tree.adopt(bounds, std::move(tree), std::move(objects), triangleCount))
        return fail("Collision tile tree is inconsistent");
    return true;
}

} // namespace wowee::game
