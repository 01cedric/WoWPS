#include "pipeline/wdt_loader.hpp"
#include "core/logger.hpp"
#include <cstring>

namespace wowee {
namespace pipeline {

namespace {

uint32_t readU32(const uint8_t* data, size_t offset) {
    uint32_t v;
    std::memcpy(&v, data + offset, 4);
    return v;
}

uint16_t readU16(const uint8_t* data, size_t offset) {
    uint16_t v;
    std::memcpy(&v, data + offset, 2);
    return v;
}

float readF32(const uint8_t* data, size_t offset) {
    float v;
    std::memcpy(&v, data + offset, 4);
    return v;
}

// Chunk magic constants (big-endian ASCII, same as ADTLoader)
constexpr uint32_t MVER = 0x4D564552; // "MVER"
constexpr uint32_t MPHD = 0x4D504844; // "MPHD"
constexpr uint32_t MWMO = 0x4D574D4F; // "MWMO"
constexpr uint32_t MODF = 0x4D4F4446; // "MODF"

} // anonymous namespace

WDTInfo parseWDT(const std::vector<uint8_t>& data) {
    WDTInfo info;

    if (data.size() < 8) {
        LOG_WARNING("WDT data too small (", data.size(), " bytes)");
        return info;
    }

    size_t offset = 0;

    while (offset + 8 <= data.size()) {
        uint32_t magic = readU32(data.data(), offset);
        uint32_t chunkSize = readU32(data.data(), offset + 4);

        if (offset + 8 + chunkSize > data.size()) {
            LOG_WARNING("WDT chunk extends beyond file at offset ", offset);
            break;
        }

        const uint8_t* chunkData = data.data() + offset + 8;

        if (magic == MVER) {
            if (chunkSize >= 4) {
                uint32_t version = readU32(chunkData, 0);
                LOG_DEBUG("WDT version: ", version);
            }
        } else if (magic == MPHD) {
            if (chunkSize >= 4) {
                info.mphdFlags = readU32(chunkData, 0);
                LOG_DEBUG("WDT MPHD flags: 0x", std::hex, info.mphdFlags, std::dec);
            }
        } else if (magic == MWMO) {
            // Null-terminated WMO path string(s)
            if (chunkSize > 0) {
                const char* str = reinterpret_cast<const char*>(chunkData);
                // Bound scan to chunkSize to avoid OOB read on truncated files
                // (strlen has no upper bound if the data lacks a null terminator).
                size_t len = strnlen(str, chunkSize);
                if (len > 0) {
                    info.rootWMOPath = std::string(str, len);
                    LOG_DEBUG("WDT root WMO: ", info.rootWMOPath);
                }
            }
        } else if (magic == MODF) {
            // MODF entry is 64 bytes (same layout as ADT MODF)
            if (chunkSize >= 64) {
                // nameId at offset 0 (unused for WDT - path comes from MWMO)
                // uniqueId at offset 4
                info.position[0] = readF32(chunkData, 8);
                info.position[1] = readF32(chunkData, 12);
                info.position[2] = readF32(chunkData, 16);
                info.rotation[0] = readF32(chunkData, 20);
                info.rotation[1] = readF32(chunkData, 24);
                info.rotation[2] = readF32(chunkData, 28);
                // extents at 32-55
                info.flags = readU16(chunkData, 56);
                info.doodadSet = readU16(chunkData, 58);
                LOG_DEBUG("WDT MODF placement: pos=(", info.position[0], ", ",
                         info.position[1], ", ", info.position[2], ") rot=(",
                         info.rotation[0], ", ", info.rotation[1], ", ",
                         info.rotation[2], ") doodadSet=", info.doodadSet);
            }
        }

        offset += 8 + chunkSize;
    }

    LOG_DEBUG("WDT parse result: mphdFlags=0x", std::hex, info.mphdFlags, std::dec,
             " isWMOOnly=", info.isWMOOnly(),
             " rootWMO='", info.rootWMOPath, "'");

    return info;
}

} // namespace pipeline
} // namespace wowee
