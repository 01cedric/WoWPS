#include "pipeline/loose_file_reader.hpp"
#include "pipeline/asset_read_bounds.hpp"
#include "core/logger.hpp"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <limits>

namespace wowee {
namespace pipeline {

std::vector<uint8_t> LooseFileReader::readFile(const std::string& filesystemPath) {
    return readFileBounded(filesystemPath, std::numeric_limits<size_t>::max());
}

std::vector<uint8_t> LooseFileReader::readFileBounded(const std::string& filesystemPath, size_t maxBytes) {
    std::ifstream file(filesystemPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }

    const std::streamoff size = file.tellg();
    if (size <= 0) {
        return {};
    }

    const uint64_t bytes = static_cast<uint64_t>(size);
    const size_t limit = std::min(maxBytes, std::vector<uint8_t>().max_size());
    if (bytes > limit || bytes > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        logAssetReadLimit("LooseFileReader", filesystemPath, bytes, limit);
        return {};
    }

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(data.data()), size);

    if (!file.good()) {
        LOG_WARNING("Incomplete read of: ", filesystemPath);
        data.resize(static_cast<size_t>(file.gcount()));
    }

    return data;
}

bool LooseFileReader::fileExists(const std::string& filesystemPath) {
    std::error_code ec;
    return std::filesystem::exists(filesystemPath, ec);
}
} // namespace pipeline
} // namespace wowee
