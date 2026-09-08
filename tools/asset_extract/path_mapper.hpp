#pragma once

#include <string>

namespace wowee {
namespace tools {

/**
 * Maps WoW virtual paths to organized filesystem categories.
 *
 * Input:  WoW virtual path (e.g., "Creature\\Bear\\BearSkin.blp")
 * Output: Category-based relative path (e.g., "creature/bear/BearSkin.blp")
 */
class PathMapper {
public:
    /**
     * Map a WoW virtual path to a organized filesystem path.
     * @param wowPath Original WoW virtual path (backslash-separated)
     * @return Organized relative path (forward-slash separated, fully lowercased)
     */
    static std::string mapPath(const std::string& wowPath);

private:
    static std::string toLower(const std::string& str);
    static std::string toForwardSlash(const std::string& str);
};

} // namespace tools
} // namespace wowee
