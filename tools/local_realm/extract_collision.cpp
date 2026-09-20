// extract_collision - build this realm's line-of-sight collision pack from the
// player's own WoW 3.3.5a archives.
//
// P05 / the reference. This is the offline half of the VMAP equivalent the shared-combat
// source audit (the source audit section 4.3) named as the
// only structural gap left in P05's criterion. It reads the same MPQ archives the
// client reads, walks every map's ADT tiles, bakes every WMO and M2 collision
// triangle into world space and writes one `.wcol` tile plus a manifest, in the
// format include/game/local_collision.hpp defines. The console then loads that
// pack and answers line-of-sight from it; with no pack installed every test
// answers "visible", exactly as the implementation behaved.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33.
//   vmap4_extractor/vmapexport.cpp:205-233   the map / ADT walk.
//   vmap4_extractor/wdtfile.cpp:129          World\Maps\<m>\<m>_<x>_<y>.adt.
//   vmap4_extractor/wmo.cpp:396-416          the MOPY collision-triangle filter.
//   vmap4_extractor/wmo.cpp:560-573          fixCoords on the MODF position and
//                                            the 32 * 533.33333 world-spawn case.
//   vmap4_extractor/model.cpp:47-66, :133-135 the M2 bounding (collision) mesh
//                                            and fixCoordSystem.
//   vmap4_extractor/model.cpp:155-160        fixCoords on the MDDF position and
//                                            the Scale / 1024.
//   Collision/Maps/TileAssembler.cpp:46-51   ModelPosition::transform - scale,
//                                            then rotate, then translate.
//   Collision/Maps/TileAssembler.h:46-49     the Euler ZYX rotation the instance
//                                            transform is built from.
//
// **Never run in this repository's sandbox.** No client MPQ and no extracted
// WMO/M2/ADT asset existed there, so not one line below was executed against
// real map data. The suite that ships with it
// (tools/tests/run_local_collision_extract_tests.sh) declares MPQ_DIR and
// is therefore reported as skipped, in the same shape as the three FrameXML
// suites, until a player with the archives runs it.
//
// Usage:
//   extract_collision --mpq <WoW/Data directory> --out <collision directory>
//                     [--map <id>]... [--verbose]
//
// With no --map the whole of Map.dbc is walked.

#include "game/local_line_of_sight.hpp"
#include "pipeline/adt_loader.hpp"
#include "pipeline/dbc_loader.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/mpq_asset_source.hpp"
#include "pipeline/wmo_loader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using wowee::game::LocalCollisionTile;
using wowee::game::LocalCollisionTriangle;
using wowee::game::LocalCollisionVec3;
using wowee::game::kLocalCollisionFromM2;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMapHalf = 533.33333f * 32.f;

/// A 3x3 row-major rotation.
struct Mat3 {
    float m[3][3]{{1,0,0},{0,1,0},{0,0,1}};
    LocalCollisionVec3 operator*(const LocalCollisionVec3& v) const {
        return {m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z,
                m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z,
                m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z};
    }
    static Mat3 multiply(const Mat3& a, const Mat3& b) {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = a.m[i][0]*b.m[0][j] + a.m[i][1]*b.m[1][j] + a.m[i][2]*b.m[2][j];
        return r;
    }
};

/// G3D::Matrix3::fromEulerAnglesZYX(yaw, pitch, roll) = Rz(yaw) Ry(pitch) Rx(roll),
/// which is what ModelPosition::init builds from (rot.y, rot.x, rot.z) in radians
/// (TileAssembler.h:46-49). G3D is a third-party dependency of the reference and
/// is not vendored in the pinned checkout, so this is the documented composition
/// rather than a line citation.
Mat3 eulerZYX(float yaw, float pitch, float roll) {
    Mat3 z, y, x;
    float c = std::cos(yaw), s = std::sin(yaw);
    z.m[0][0]=c; z.m[0][1]=-s; z.m[1][0]=s; z.m[1][1]=c;
    c = std::cos(pitch); s = std::sin(pitch);
    y.m[0][0]=c; y.m[0][2]=s; y.m[2][0]=-s; y.m[2][2]=c;
    c = std::cos(roll); s = std::sin(roll);
    x.m[1][1]=c; x.m[1][2]=-s; x.m[2][1]=s; x.m[2][2]=c;
    return Mat3::multiply(z, Mat3::multiply(y, x));
}

/// wmo.h:68 - fixCoords(v) = (v.z, v.x, v.y).
LocalCollisionVec3 fixCoords(float x, float y, float z) { return {z, x, y}; }
/// model.cpp:133-135 - fixCoordSystem(v) = (v.x, v.z, -v.y).
LocalCollisionVec3 fixCoordSystem(float x, float y, float z) { return {x, z, -y}; }

std::string lower(std::string s) {
    for (auto& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}
std::string normalize(std::string path) {
    for (auto& ch : path) if (ch == '/') ch = '\\';
    return lower(std::move(path));
}

/// One tile under construction: vertices are deduplicated only per source model,
/// which is what keeps the pass single-threaded and streaming.
struct TileBuilder {
    LocalCollisionTile tile;
    void addTriangle(const LocalCollisionVec3& a, const LocalCollisionVec3& b,
                     const LocalCollisionVec3& c, uint32_t flags) {
        const uint32_t base = uint32_t(tile.vertices.size());
        tile.vertices.push_back(a); tile.vertices.push_back(b); tile.vertices.push_back(c);
        tile.triangles.push_back(LocalCollisionTriangle{base, base + 1, base + 2, flags});
    }
};

// --- the WMO group's own chunks --------------------------------------------
// The renderer's WMOLoader keeps only the MOPY flag byte, not the material id
// byte the reference's "collision only material 255" arm reads (wmo.cpp:405), so
// the group file's MOPY / MOVI / MOVT are walked here directly.
struct WmoGroupGeometry {
    std::vector<LocalCollisionVec3> vertices;
    std::vector<uint16_t> indices;   // 3 per triangle, already filtered
};

bool readGroupGeometry(const std::vector<uint8_t>& data, WmoGroupGeometry& out) {
    // MOGP's payload is itself a chunk stream, after 0x44 bytes of header.
    size_t at = 0;
    const auto u32 = [&](size_t off) {
        uint32_t v = 0; std::memcpy(&v, data.data() + off, 4); return v;
    };
    const uint8_t* mopy = nullptr; size_t mopySize = 0;
    const uint8_t* movi = nullptr; size_t moviSize = 0;
    const uint8_t* movt = nullptr; size_t movtSize = 0;
    while (at + 8 <= data.size()) {
        char fourcc[5] = {};
        // Chunk ids are stored reversed on disk.
        for (int i = 0; i < 4; ++i) fourcc[i] = char(data[at + 3 - size_t(i)]);
        const uint32_t size = u32(at + 4);
        at += 8;
        if (size > data.size() - at) return false;
        if (!std::strcmp(fourcc, "MOGP")) { at += 0x44; continue; } // descend into the group
        if (!std::strcmp(fourcc, "MOPY")) { mopy = data.data() + at; mopySize = size; }
        else if (!std::strcmp(fourcc, "MOVI")) { movi = data.data() + at; moviSize = size; }
        else if (!std::strcmp(fourcc, "MOVT")) { movt = data.data() + at; movtSize = size; }
        at += size;
    }
    if (!mopy || !movi || !movt) return false;
    const size_t triangles = mopySize / 2;
    if (moviSize / 6 < triangles || movtSize % 12) return false;
    const size_t vertexCount = movtSize / 12;
    out.vertices.resize(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        float v[3];
        std::memcpy(v, movt + i * 12, 12);
        // MOVT is written through unchanged by the reference (wmo.cpp:368); the
        // instance transform is what puts it into world space.
        out.vertices[i] = {v[0], v[1], v[2]};
    }
    // wmo.cpp:400-416, verbatim.
    constexpr uint8_t kDetail = 0x04, kCollision = 0x08, kRender = 0x20;
    for (size_t i = 0; i < triangles; ++i) {
        const uint8_t flags = mopy[2 * i];
        const uint8_t material = mopy[2 * i + 1];
        const bool renderFace = (flags & kRender) && !(flags & kDetail);
        const bool collisionOnly = material == 0xFF;
        if (!((flags & kCollision) || renderFace || collisionOnly)) continue;
        for (int j = 0; j < 3; ++j) {
            uint16_t index = 0;
            std::memcpy(&index, movi + (i * 3 + size_t(j)) * 2, 2);
            if (index >= vertexCount) return false;
            out.indices.push_back(index);
        }
    }
    return true;
}

struct Options {
    std::string mpqDir, outDir;
    std::set<uint32_t> maps;
    bool verbose = false;
};

} // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (arg == "--mpq") options.mpqDir = next();
        else if (arg == "--out") options.outDir = next();
        else if (arg == "--map") options.maps.insert(uint32_t(std::stoul(next())));
        else if (arg == "--verbose") options.verbose = true;
        else { std::cerr << "Unknown argument " << arg << "\n"; return 2; }
    }
    if (options.mpqDir.empty() || options.outDir.empty()) {
        std::cerr << "Usage: extract_collision --mpq <WoW Data directory> --out <collision directory>"
                     " [--map <id>]... [--verbose]\n";
        return 2;
    }

    std::string error;
    auto archives = wowee::pipeline::MpqAssetSource::open(options.mpqDir, &error);
    if (!archives) {
        std::cerr << "Cannot open the archives in " << options.mpqDir << ": " << error << "\n";
        return 1;
    }
    std::cout << "opened " << archives->archiveCount() << " archives, expansion "
              << archives->expansion() << ", locale " << archives->locale() << "\n";

    // Map.dbc: column 0 is the id, column 1 the directory name.
    const auto mapBytes = archives->readFile("dbfilesclient\\map.dbc");
    wowee::pipeline::DBCFile mapDbc;
    if (mapBytes.empty() || !mapDbc.load(mapBytes)) { std::cerr << "Cannot read Map.dbc\n"; return 1; }
    std::map<uint32_t, std::string> mapNames;
    for (uint32_t row = 0; row < mapDbc.getRecordCount(); ++row) {
        const uint32_t id = mapDbc.getUInt32(row, 0);
        const std::string name = mapDbc.getString(row, 1);
        if (!name.empty() && (options.maps.empty() || options.maps.count(id))) mapNames[id] = name;
    }
    std::cout << "walking " << mapNames.size() << " maps\n";

    std::error_code ec;
    fs::create_directories(options.outDir, ec);

    // A model's collision mesh is read once and reused by every placement.
    std::map<std::string, WmoGroupGeometry> wmoCache;   // root path -> merged groups
    std::map<std::string, WmoGroupGeometry> m2Cache;

    const auto wmoGeometry = [&](const std::string& path) -> const WmoGroupGeometry* {
        const auto key = normalize(path);
        const auto it = wmoCache.find(key);
        if (it != wmoCache.end()) return it->second.indices.empty() ? nullptr : &it->second;
        WmoGroupGeometry merged;
        const auto rootBytes = archives->readFile(key);
        if (!rootBytes.empty()) {
            const auto root = wowee::pipeline::WMOLoader::load(rootBytes);
            const auto stem = key.size() > 4 ? key.substr(0, key.size() - 4) : key;
            for (uint32_t group = 0; group < root.nGroups && group < 4096; ++group) {
                char suffix[16];
                std::snprintf(suffix, sizeof(suffix), "_%03u.wmo", group);
                const auto groupBytes = archives->readFile(stem + suffix);
                if (groupBytes.empty()) continue;
                WmoGroupGeometry one;
                if (!readGroupGeometry(groupBytes, one)) continue;
                const uint32_t base = uint32_t(merged.vertices.size());
                merged.vertices.insert(merged.vertices.end(), one.vertices.begin(), one.vertices.end());
                for (auto index : one.indices) merged.indices.push_back(uint16_t(base + index));
            }
        }
        auto& stored = wmoCache[key] = std::move(merged);
        return stored.indices.empty() ? nullptr : &stored;
    };

    const auto m2Geometry = [&](const std::string& path) -> const WmoGroupGeometry* {
        auto key = normalize(wowee::pipeline::modelPathToM2(path));
        const auto it = m2Cache.find(key);
        if (it != m2Cache.end()) return it->second.indices.empty() ? nullptr : &it->second;
        WmoGroupGeometry geometry;
        const auto bytes = archives->readFile(key);
        if (!bytes.empty()) {
            const auto model = wowee::pipeline::M2Loader::load(bytes);
            // model.cpp:47-66: the *bounding* mesh, which the loader keeps as
            // collisionVertices/collisionIndices, with fixCoordSystem applied.
            geometry.vertices.reserve(model.collisionVertices.size());
            for (const auto& v : model.collisionVertices) geometry.vertices.push_back(fixCoordSystem(v.x, v.y, v.z));
            for (auto index : model.collisionIndices)
                if (index < geometry.vertices.size()) geometry.indices.push_back(index);
            if (geometry.indices.size() % 3) geometry.indices.resize(geometry.indices.size() / 3 * 3);
        }
        auto& stored = m2Cache[key] = std::move(geometry);
        return stored.indices.empty() ? nullptr : &stored;
    };

    struct ManifestLine {
        uint32_t mapId, tileX, tileY, triangles, hash;
        float lo[3], hi[3];
        std::string file;
    };
    std::vector<ManifestLine> manifest;
    uint64_t totalTriangles = 0;

    for (const auto& [mapId, mapName] : mapNames) {
        unsigned tilesWritten = 0;
        for (uint32_t x = 0; x < 64; ++x) {
            for (uint32_t y = 0; y < 64; ++y) {
                char adtPath[256];
                std::snprintf(adtPath, sizeof(adtPath), "world\\maps\\%s\\%s_%u_%u.adt",
                              lower(mapName).c_str(), lower(mapName).c_str(), x, y);
                if (!archives->hasFile(adtPath)) continue;
                const auto adtBytes = archives->readFile(adtPath);
                if (adtBytes.empty()) continue;
                const auto terrain = wowee::pipeline::ADTLoader::load(adtBytes);
                if (!terrain.isLoaded()) continue;

                TileBuilder builder;
                builder.tile.mapId = mapId;
                builder.tile.tileX = x;
                builder.tile.tileY = y;

                for (const auto& placement : terrain.wmoPlacements) {
                    if (placement.nameId >= terrain.wmoNames.size()) continue;
                    const auto* geometry = wmoGeometry(terrain.wmoNames[placement.nameId]);
                    if (!geometry) continue;
                    // wmo.cpp:560-573: the 32 * 533.33333 world-spawn substitution
                    // and fixCoords on the placement position.
                    float px = placement.position[0], pz = placement.position[2];
                    if (px == 0.f && pz == 0.f) { px = kMapHalf; pz = kMapHalf; }
                    const LocalCollisionVec3 origin = fixCoords(px, placement.position[1], pz);
                    const Mat3 rotation = eulerZYX(kPi * placement.rotation[1] / 180.f,
                                                   kPi * placement.rotation[0] / 180.f,
                                                   kPi * placement.rotation[2] / 180.f);
                    for (size_t i = 0; i + 2 < geometry->indices.size(); i += 3) {
                        LocalCollisionVec3 v[3];
                        for (int j = 0; j < 3; ++j)
                            v[j] = rotation * geometry->vertices[geometry->indices[i + size_t(j)]] + origin;
                        builder.addTriangle(v[0], v[1], v[2], 0);
                    }
                }
                for (const auto& placement : terrain.doodadPlacements) {
                    if (placement.nameId >= terrain.doodadNames.size()) continue;
                    const auto* geometry = m2Geometry(terrain.doodadNames[placement.nameId]);
                    if (!geometry) continue;
                    // model.cpp:155-160: Scale / 1024 and fixCoords on the position.
                    const float scale = float(placement.scale) / 1024.f;
                    const LocalCollisionVec3 origin =
                        fixCoords(placement.position[0], placement.position[1], placement.position[2]);
                    const Mat3 rotation = eulerZYX(kPi * placement.rotation[1] / 180.f,
                                                   kPi * placement.rotation[0] / 180.f,
                                                   kPi * placement.rotation[2] / 180.f);
                    for (size_t i = 0; i + 2 < geometry->indices.size(); i += 3) {
                        LocalCollisionVec3 v[3];
                        for (int j = 0; j < 3; ++j)
                            v[j] = rotation * (geometry->vertices[geometry->indices[i + size_t(j)]] * scale) + origin;
                        builder.addTriangle(v[0], v[1], v[2], kLocalCollisionFromM2);
                    }
                }
                if (builder.tile.triangles.empty()) continue;
                builder.tile.buildTree();
                const auto bytes = wowee::game::localCollisionEncodeTile(builder.tile);
                const auto file = wowee::game::LocalCollisionData::localCollisionTileFileName(mapId, y, x);
                std::ofstream out(fs::path(options.outDir) / file, std::ios::binary);
                out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
                if (!out) { std::cerr << "Cannot write " << file << "\n"; return 1; }
                ManifestLine line{mapId, x, y, uint32_t(builder.tile.triangles.size()),
                                  wowee::game::localCollisionHash(2166136261U, bytes.data(), bytes.size()),
                                  {}, {}, file};
                for (unsigned i = 0; i < 3; ++i) {
                    line.lo[i] = builder.tile.tree.bounds().lo[i];
                    line.hi[i] = builder.tile.tree.bounds().hi[i];
                }
                manifest.push_back(std::move(line));
                totalTriangles += builder.tile.triangles.size();
                ++tilesWritten;
                if (options.verbose)
                    std::cout << "  map " << mapId << " tile " << x << "," << y << ": "
                              << builder.tile.triangles.size() << " triangles\n";
            }
        }
        std::cout << "map " << mapId << " (" << mapName << "): " << tilesWritten << " tiles\n";
    }

    std::sort(manifest.begin(), manifest.end(), [](const ManifestLine& a, const ManifestLine& b) {
        return std::tie(a.mapId, a.tileY, a.tileX) < std::tie(b.mapId, b.tileY, b.tileX);
    });
    std::vector<wowee::game::LocalCollisionData::Entry> entries;
    for (const auto& line : manifest) {
        wowee::game::LocalCollisionData::Entry e;
        e.mapId = line.mapId; e.tileX = line.tileX; e.tileY = line.tileY;
        e.triangles = line.triangles; e.hash = line.hash; e.file = line.file;
        for (unsigned i = 0; i < 3; ++i) { e.lo[i] = line.lo[i]; e.hi[i] = line.hi[i]; }
        entries.push_back(std::move(e));
    }
    std::ofstream out(fs::path(options.outDir) / "collision.manifest");
    out << "WCOLMANIFEST 1\n";
    out << "fingerprint " << wowee::game::LocalCollisionData::computeFingerprint(entries) << "\n";
    out.precision(9);
    for (const auto& line : manifest)
        out << "tile " << line.mapId << " " << line.tileY << " " << line.tileX << " "
            << line.triangles << " " << line.hash << " "
            << line.lo[0] << " " << line.lo[1] << " " << line.lo[2] << " "
            << line.hi[0] << " " << line.hi[1] << " " << line.hi[2] << " " << line.file << "\n";
    if (!out) { std::cerr << "Cannot write the collision manifest\n"; return 1; }
    std::cout << "wrote " << manifest.size() << " tiles, " << totalTriangles
              << " triangles, fingerprint " << wowee::game::LocalCollisionData::computeFingerprint(entries)
              << " into " << options.outDir << "\n";
    std::cout << "Install it as assets/local_realm/collision/ on every peer.\n";
    return 0;
}
