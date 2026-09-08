#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace wowee {
namespace pipeline {

/**
 * LooseFileReader - Thread-safe filesystem file reader
 *
 * Each read opens its own file descriptor, so no mutex is needed.
 * This replaces the serialized MPQ read path.
 */
class LooseFileReader {
public:
    LooseFileReader() = default;

    /**
     * Read entire file into memory
     * @param filesystemPath Full path to file on disk
     * @return File contents (empty if not found or error)
     */
    static std::vector<uint8_t> readFile(const std::string& filesystemPath);
    // Checks the opened file's size before allocating its result buffer.
    static std::vector<uint8_t> readFileBounded(const std::string& filesystemPath, size_t maxBytes);

    /**
     * Check if a file exists on disk
     */
    static bool fileExists(const std::string& filesystemPath);

};

} // namespace pipeline
} // namespace wowee
