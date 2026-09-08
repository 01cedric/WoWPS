#include "core/coordinates.hpp"
#include "pipeline/wowee_collision.hpp"
#include "pipeline/wowee_binary_io.hpp"
#include "pipeline/adt_loader.hpp"
#include "core/logger.hpp"
#include "core/file_probe.hpp"
#include <fstream>
#include <filesystem>
#include <cstring>
#include <cmath>

namespace wowee {
namespace pipeline {

static constexpr uint32_t WOC_MAGIC = 0x31434F57; // "WOC1"

size_t WoweeCollision::walkableCount() const {
    size_t n = 0;
    for (const auto& t : triangles)
        if (t.flags & 0x01) n++;
    return n;
}

size_t WoweeCollision::steepCount() const {
    size_t n = 0;
    for (const auto& t : triangles)
        if (t.flags & 0x04) n++;
    return n;
}

WoweeCollision WoweeCollisionBuilder::fromTerrain(const ADTTerrain& terrain,
                                                    float steepAngle) {
    WoweeCollision col;
    col.tileX = terrain.coord.x;
    col.tileY = terrain.coord.y;

    float steepCos = std::cos(steepAngle * core::coords::PI / 180.0f);

    float tileSize = core::coords::TILE_SIZE;
    float chunkSize = tileSize / 16.0f;
    float vertSpacing = chunkSize / 8.0f;

    for (int ci = 0; ci < 256; ci++) {
        const auto& chunk = terrain.chunks[ci];
        if (!chunk.hasHeightMap()) continue;

        int cx = ci % 16;
        int cy = ci / 16;
        float chunkBaseX = (32.0f - terrain.coord.y) * tileSize - cy * chunkSize;
        float chunkBaseY = (32.0f - terrain.coord.x) * tileSize - cx * chunkSize;

        bool isHoleChunk = (chunk.holes != 0);

        // Build outer vertex grid (9x9)
        for (int row = 0; row < 8; row++) {
            for (int col2 = 0; col2 < 8; col2++) {
                // Check hole mask (4x4 grid, each bit covers 2x2 sub-quads)
                if (isHoleChunk) {
                    int hx = col2 / 2, hy = row / 2;
                    if (chunk.holes & (1 << (hy * 4 + hx))) continue;
                }

                int i00 = row * 17 + col2;
                int i10 = row * 17 + col2 + 1;
                int i01 = (row + 1) * 17 + col2;
                int i11 = (row + 1) * 17 + col2 + 1;

                auto vtx = [&](int idx) -> glm::vec3 {
                    int r = idx / 17, c = idx % 17;
                    float x = chunkBaseX - r * vertSpacing;
                    float y = chunkBaseY - c * vertSpacing;
                    float z = chunk.position[2] + chunk.heightMap.heights[idx];
                    return glm::vec3(x, y, z);
                };

                glm::vec3 v00 = vtx(i00), v10 = vtx(i10);
                glm::vec3 v01 = vtx(i01), v11 = vtx(i11);

                auto classifyTri = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
                    // Skip degenerate triangles - would produce NaN normals
                    // and crash collision intersection tests downstream.
                    glm::vec3 cross = glm::cross(b - a, c - a);
                    float crossLen = glm::length(cross);
                    if (crossLen < 1e-8f) return;

                    WoweeCollision::Triangle tri;
                    tri.v0 = a; tri.v1 = b; tri.v2 = c;

                    glm::vec3 normal = cross / crossLen;
                    float nz = std::abs(normal.z);

                    tri.flags = 0;
                    if (nz >= steepCos)
                        tri.flags |= 0x01; // walkable
                    else
                        tri.flags |= 0x04; // steep

                    col.bounds.expand(a);
                    col.bounds.expand(b);
                    col.bounds.expand(c);
                    col.triangles.push_back(tri);
                };

                classifyTri(v00, v10, v01);
                classifyTri(v10, v11, v01);
            }
        }

        // Mark water triangles
        if (terrain.waterData[ci].hasWater()) {
            float waterH = terrain.waterData[ci].layers[0].maxHeight;
            for (size_t ti = col.triangles.size() - 128; ti < col.triangles.size(); ti++) {
                auto& tri = col.triangles[ti];
                float avgZ = (tri.v0.z + tri.v1.z + tri.v2.z) / 3.0f;
                if (avgZ < waterH) tri.flags |= 0x02;
            }
        }
    }

    LOG_INFO("Collision mesh: ", col.triangles.size(), " triangles (",
             col.walkableCount(), " walkable, ", col.steepCount(), " steep)");
    return col;
}

void WoweeCollisionBuilder::addMesh(WoweeCollision& collision,
                                     const std::vector<glm::vec3>& vertices,
                                     const std::vector<uint32_t>& indices,
                                     const glm::mat4& transform,
                                     uint8_t extraFlags,
                                     float steepAngle) {
    if (vertices.empty() || indices.size() < 3) return;
    const float steepCos = std::cos(glm::radians(steepAngle));

    collision.triangles.reserve(collision.triangles.size() + indices.size() / 3);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;

        WoweeCollision::Triangle tri;
        tri.v0 = glm::vec3(transform * glm::vec4(vertices[i0], 1.0f));
        tri.v1 = glm::vec3(transform * glm::vec4(vertices[i1], 1.0f));
        tri.v2 = glm::vec3(transform * glm::vec4(vertices[i2], 1.0f));

        glm::vec3 n = glm::cross(tri.v1 - tri.v0, tri.v2 - tri.v0);
        float len = glm::length(n);
        tri.flags = extraFlags;
        if (len > 1e-6f) {
            float nz = n.z / len;
            if (nz >= steepCos) tri.flags |= 0x01;       // walkable
            else if (nz < 0.0f) tri.flags |= 0x04;       // steep / facing-down
        }

        collision.bounds.expand(tri.v0);
        collision.bounds.expand(tri.v1);
        collision.bounds.expand(tri.v2);
        collision.triangles.push_back(tri);
    }
}

bool WoweeCollisionBuilder::save(const WoweeCollision& collision, const std::string& path) {
    namespace fs = std::filesystem;
    ensureParentDirectory(path);

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    f.write(reinterpret_cast<const char*>(&WOC_MAGIC), 4);
    // Sanitize bounds before writing - produces a clean WOC even when an
    // in-memory collision has been polluted by addMesh on bad input.
    auto sanVec = [](glm::vec3 v) {
        if (!std::isfinite(v.x)) v.x = 0.0f;
        if (!std::isfinite(v.y)) v.y = 0.0f;
        if (!std::isfinite(v.z)) v.z = 0.0f;
        return v;
    };
    // Cap triangle count at the load-side limit (2M). Save previously
    // wrote raw size() so a >2M-tri collision (theoretical, addMesh on
    // huge geometry) would be silently rejected on round-trip.
    uint32_t triCount = static_cast<uint32_t>(
        std::min<size_t>(collision.triangles.size(), 2'000'000));
    f.write(reinterpret_cast<const char*>(&triCount), 4);
    // Sanitize tile coords too - out-of-range would be clamped on load
    // anyway but writing a clean file means no warning on every reload.
    uint32_t tileX = collision.tileX > 63 ? 32 : collision.tileX;
    uint32_t tileY = collision.tileY > 63 ? 32 : collision.tileY;
    f.write(reinterpret_cast<const char*>(&tileX), 4);
    f.write(reinterpret_cast<const char*>(&tileY), 4);
    glm::vec3 bmin = sanVec(collision.bounds.min);
    glm::vec3 bmax = sanVec(collision.bounds.max);
    f.write(reinterpret_cast<const char*>(&bmin), 12);
    f.write(reinterpret_cast<const char*>(&bmax), 12);

    for (uint32_t ti = 0; ti < triCount; ti++) {
        const auto& tri = collision.triangles[ti];
        glm::vec3 v0 = sanVec(tri.v0), v1 = sanVec(tri.v1), v2 = sanVec(tri.v2);
        f.write(reinterpret_cast<const char*>(&v0), 12);
        f.write(reinterpret_cast<const char*>(&v1), 12);
        f.write(reinterpret_cast<const char*>(&v2), 12);
        f.write(reinterpret_cast<const char*>(&tri.flags), 1);
    }

    LOG_INFO("WOC saved: ", path, " (", triCount, " triangles)");
    return true;
}

WoweeCollision WoweeCollisionBuilder::load(const std::string& path) {
    WoweeCollision col;
    std::ifstream f(path, std::ios::binary);
    if (!f) return col;

    uint32_t magic;
    f.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != WOC_MAGIC) return col;

    uint32_t triCount;
    f.read(reinterpret_cast<char*>(&triCount), 4);
    f.read(reinterpret_cast<char*>(&col.tileX), 4);
    f.read(reinterpret_cast<char*>(&col.tileY), 4);
    f.read(reinterpret_cast<char*>(&col.bounds.min), 12);
    f.read(reinterpret_cast<char*>(&col.bounds.max), 12);
    // A whole-tile collision mesh tops out at ~256*128 = 32K terrain triangles
    // plus building overlays. >2M is corrupted and would OOM us.
    if (triCount > 2'000'000) {
        LOG_ERROR("WOC triCount rejected (", triCount, "): ", path);
        return WoweeCollision{};
    }
    // Tile coords are valid 0..63 in WoW; cap higher values that suggest
    // a corrupted file rather than letting them propagate.
    if (col.tileX > 63 || col.tileY > 63) {
        LOG_WARNING("WOC tile coord out of range (", col.tileX, ",", col.tileY,
                    ") - clamping");
        if (col.tileX > 63) col.tileX = 32;
        if (col.tileY > 63) col.tileY = 32;
    }

    col.triangles.reserve(triCount);
    auto fixVec = [](glm::vec3& v) {
        if (!std::isfinite(v.x)) v.x = 0.0f;
        if (!std::isfinite(v.y)) v.y = 0.0f;
        if (!std::isfinite(v.z)) v.z = 0.0f;
    };
    for (uint32_t i = 0; i < triCount; i++) {
        WoweeCollision::Triangle tri;
        f.read(reinterpret_cast<char*>(&tri.v0), 12);
        f.read(reinterpret_cast<char*>(&tri.v1), 12);
        f.read(reinterpret_cast<char*>(&tri.v2), 12);
        f.read(reinterpret_cast<char*>(&tri.flags), 1);
        // Skip degenerate / non-finite triangles. They'd produce NaNs in
        // ray-triangle intersection (used for movement collision), making
        // the player phase through walls or fall through the floor.
        fixVec(tri.v0); fixVec(tri.v1); fixVec(tri.v2);
        glm::vec3 e1 = tri.v1 - tri.v0;
        glm::vec3 e2 = tri.v2 - tri.v0;
        if (glm::length(glm::cross(e1, e2)) < 1e-8f) continue;
        col.triangles.push_back(tri);
    }
    // Sanitize stored bounds too - they're used as a coarse cull box.
    fixVec(col.bounds.min);
    fixVec(col.bounds.max);

    LOG_INFO("WOC loaded: ", path, " (", col.triangles.size(), "/",
             triCount, " triangles, ", triCount - col.triangles.size(),
             " degenerate skipped)");
    return col;
}

bool WoweeCollisionBuilder::exists(const std::string& basePath) {
    return core::optionalFileExists(basePath + ".woc");
}

} // namespace pipeline
} // namespace wowee
