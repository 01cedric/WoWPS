#pragma once

#include <filesystem>

namespace wowee::core {

// Optional sidecars must never turn an OS lookup error (PS4 relative-path
// EINVAL, missing media, permissions, etc.) into a failed original asset.
// A directory is not a readable asset, even though exists(directory) is true.
inline bool optionalFileExists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

inline bool optionalDirectoryExists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_directory(path, error) && !error;
}

} // namespace wowee::core
