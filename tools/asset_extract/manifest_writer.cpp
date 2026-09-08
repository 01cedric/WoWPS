#include "manifest_writer.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdio>
#ifdef _WIN32
#include <filesystem>
#endif
#include <zlib.h>

namespace wowee {
namespace tools {

uint32_t ManifestWriter::computeCRC32(const uint8_t* data, size_t size) {
    return static_cast<uint32_t>(::crc32(::crc32(0L, Z_NULL, 0), data, static_cast<uInt>(size)));
}

bool ManifestWriter::write(const std::string& outputPath,
                           const std::string& basePath,
                           const std::string& expansion,
                           const std::vector<FileEntry>& entries) {
    // Write JSON manually to avoid pulling nlohmann/json into the tool
    // (though it would also work fine). This keeps the tool dependency-light.
    const std::string temporary = outputPath + ".wow_ps.tmp";
    std::ofstream file(temporary, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n";
    file << "  \"version\": 1,\n";
    file << "  \"basePath\": \"" << basePath << "\",\n";
    // Readers written before this field ignore it, so no version bump.
    if (!expansion.empty())
        file << "  \"expansion\": \"" << expansion << "\",\n";
    file << "  \"fileCount\": " << entries.size() << ",\n";
    file << "  \"entries\": {\n";

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];

        // Escape backslashes in WoW path for JSON
        std::string escapedKey;
        for (char c : e.wowPath) {
            if (c == '\\') escapedKey += "\\\\";
            else if (c == '"') escapedKey += "\\\"";
            else escapedKey += c;
        }

        std::string escapedPath;
        for (char c : e.filesystemPath) {
            if (c == '\\') escapedPath += "\\\\";
            else if (c == '"') escapedPath += "\\\"";
            else escapedPath += c;
        }

        // CRC32 as hex
        std::ostringstream hexCrc;
        hexCrc << std::hex << std::setfill('0') << std::setw(8) << e.crc32;

        file << "    \"" << escapedKey << "\": {\"p\": \"" << escapedPath
             << "\", \"s\": " << e.size
             << ", \"h\": \"" << hexCrc.str() << "\"}";

        if (i + 1 < entries.size()) file << ",";
        file << "\n";
    }

    file << "  }\n";
    file << "}\n";

    file.close();
    if (!file) { std::remove(temporary.c_str()); return false; }
#ifdef _WIN32
    std::error_code ec;
    std::filesystem::copy_file(temporary, outputPath, std::filesystem::copy_options::overwrite_existing, ec);
    std::remove(temporary.c_str());
    return !ec;
#else
    if (std::rename(temporary.c_str(), outputPath.c_str()) == 0) return true;
    std::remove(temporary.c_str());
    return false;
#endif
}

} // namespace tools
} // namespace wowee
