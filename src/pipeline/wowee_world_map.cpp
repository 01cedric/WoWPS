#include "pipeline/wowee_world_map.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'P', 'X'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".womx";

size_t bitmapBytesFor(uint32_t gridSize) {
    return (static_cast<size_t>(gridSize) * gridSize + 7) / 8;
}

} // namespace

bool WoweeWorldMap::hasTile(uint32_t x, uint32_t y) const {
    if (x >= gridSize || y >= gridSize) return false;
    size_t bit = static_cast<size_t>(y) * gridSize + x;
    size_t byte = bit / 8;
    if (byte >= tileBitmap.size()) return false;
    return (tileBitmap[byte] >> (bit & 7)) & 1;
}

void WoweeWorldMap::setTile(uint32_t x, uint32_t y, bool present) {
    if (x >= gridSize || y >= gridSize) return;
    size_t bit = static_cast<size_t>(y) * gridSize + x;
    size_t byte = bit / 8;
    if (tileBitmap.size() <= byte) tileBitmap.resize(byte + 1, 0);
    uint8_t mask = static_cast<uint8_t>(1u << (bit & 7));
    if (present) tileBitmap[byte] |= mask;
    else         tileBitmap[byte] &= static_cast<uint8_t>(~mask);
}

uint32_t WoweeWorldMap::countTiles() const {
    uint32_t n = 0;
    size_t totalBits = static_cast<size_t>(gridSize) * gridSize;
    for (size_t bit = 0; bit < totalBits; ++bit) {
        size_t byte = bit / 8;
        if (byte >= tileBitmap.size()) break;
        if ((tileBitmap[byte] >> (bit & 7)) & 1) n++;
    }
    return n;
}

const char* WoweeWorldMap::worldTypeName(uint8_t t) {
    switch (t) {
        case Continent:    return "continent";
        case Instance:     return "instance";
        case Battleground: return "battleground";
        case Arena:        return "arena";
        default:           return "unknown";
    }
}

bool WoweeWorldMapLoader::save(const WoweeWorldMap& m,
                               const std::string& basePath) {
    if (m.gridSize == 0 || m.gridSize > 128) return false;
    std::ofstream os(normalizePath(basePath, kExtension), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, m.name);
    writePOD(os, m.worldType);
    writePOD(os, m.gridSize);
    uint16_t pad = 0;
    writePOD(os, pad);
    writePOD(os, m.defaultLightId);
    writePOD(os, m.defaultWeatherId);
    uint32_t reserved = 0;
    writePOD(os, reserved);
    writePOD(os, reserved);
    writePOD(os, reserved);
    size_t expectedBytes = bitmapBytesFor(m.gridSize);
    uint32_t bitmapBytes = static_cast<uint32_t>(expectedBytes);
    writePOD(os, bitmapBytes);
    // Pad bitmap up to expectedBytes if caller under-sized it
    // (setTile may not have grown it to the full grid coverage).
    if (m.tileBitmap.size() >= expectedBytes) {
        os.write(reinterpret_cast<const char*>(m.tileBitmap.data()),
                 expectedBytes);
    } else {
        if (!m.tileBitmap.empty()) {
            os.write(reinterpret_cast<const char*>(m.tileBitmap.data()),
                     m.tileBitmap.size());
        }
        std::vector<uint8_t> tail(expectedBytes - m.tileBitmap.size(), 0);
        os.write(reinterpret_cast<const char*>(tail.data()), tail.size());
    }
    return os.good();
}

WoweeWorldMap WoweeWorldMapLoader::load(const std::string& basePath) {
    WoweeWorldMap out;
    std::ifstream is(normalizePath(basePath, kExtension), std::ios::binary);
    if (!is) return out;
    // Magic and version only. This format's header does not continue into a
    // name and an entry count - a world type and a grid size follow instead -
    // so readCatalogHeader would eat four bytes of the world type.
    if (!readMagicAndVersion(is, kMagic, kVersion)) return out;
    // readStr rather than a hand-rolled length-and-bytes: this is the one place
    // that spelled it out itself, and so the one place with no cap on the length.
    if (!readStr(is, out.name)) return out;
    if (!readPOD(is, out.worldType)) return out;
    if (!readPOD(is, out.gridSize)) return out;
    uint16_t pad = 0;
    if (!readPOD(is, pad)) return out;
    if (!readPOD(is, out.defaultLightId)) return out;
    if (!readPOD(is, out.defaultWeatherId)) return out;
    uint32_t reserved = 0;
    if (!readPOD(is, reserved)) return out;
    if (!readPOD(is, reserved)) return out;
    if (!readPOD(is, reserved)) return out;
    uint32_t bitmapBytes = 0;
    if (!readPOD(is, bitmapBytes)) return out;
    // Cap to a sane upper bound in case the file is corrupted -
    // a 128×128 grid is 2048 bytes, so anything > 4 KiB is a sign
    // of trouble.
    if (bitmapBytes > 4096) {
        out.gridSize = 0;
        return out;
    }
    out.tileBitmap.resize(bitmapBytes);
    if (bitmapBytes > 0) {
        is.read(reinterpret_cast<char*>(out.tileBitmap.data()), bitmapBytes);
        if (is.gcount() != static_cast<std::streamsize>(bitmapBytes)) {
            out.tileBitmap.clear();
            out.gridSize = 0;
            return out;
        }
    }
    return out;
}

bool WoweeWorldMapLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeWorldMap WoweeWorldMapLoader::makeContinent(const std::string& mapName) {
    WoweeWorldMap m;
    m.name = mapName;
    m.worldType = WoweeWorldMap::Continent;
    m.gridSize = 64;
    m.tileBitmap.assign(bitmapBytesFor(64), 0xFF);
    // Last byte may have spare bits past 64*64 - but 64*64 is
    // a multiple of 8 (4096), so this is exact and no masking
    // is needed.
    return m;
}

WoweeWorldMap WoweeWorldMapLoader::makeInstance(const std::string& mapName) {
    WoweeWorldMap m;
    m.name = mapName;
    m.worldType = WoweeWorldMap::Instance;
    m.gridSize = 4;
    m.tileBitmap.assign(bitmapBytesFor(4), 0);
    for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 0; x < 4; ++x)
            m.setTile(x, y, true);
    return m;
}

WoweeWorldMap WoweeWorldMapLoader::makeArena(const std::string& mapName) {
    WoweeWorldMap m;
    m.name = mapName;
    m.worldType = WoweeWorldMap::Arena;
    m.gridSize = 1;
    m.tileBitmap.assign(bitmapBytesFor(1), 0);
    m.setTile(0, 0, true);
    return m;
}

} // namespace pipeline
} // namespace wowee
