#include "path_mapper.hpp"
#include <algorithm>
#include <cctype>

namespace wowee {
namespace tools {

std::string PathMapper::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::string PathMapper::toForwardSlash(const std::string& str) {
    std::string result = str;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

std::string PathMapper::mapPath(const std::string& wowPath) {
    // Lowercase entire output path - WoW archives contain mixed-case variants
    // of the same path which create duplicate directories on case-sensitive filesystems.
    return toLower(toForwardSlash(wowPath));
}

} // namespace tools
} // namespace wowee
