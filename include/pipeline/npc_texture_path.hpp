#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace wowee::pipeline {
// CreatureDisplayInfoExtra names a file, not a Texture-array slot. Stock
// bakes are in Creature/Baked; accept an explicit path and older layouts too.
template<class Exists>
std::string resolveNpcBakePath(std::string_view name, Exists&& exists) {
    if (name.empty() || name.size() > 1024 || name.find('\0') != std::string_view::npos) return {};
    std::string file(name);
    std::replace(file.begin(), file.end(), '/', '\\');
    while (!file.empty() && std::isspace(static_cast<unsigned char>(file.back()))) file.pop_back();
    while (!file.empty() && std::isspace(static_cast<unsigned char>(file.front()))) file.erase(file.begin());
    if (file.empty() || file.front() == '\\' || file.find(':') != std::string::npos ||
        file == ".." || file.find("..\\") != std::string::npos) return {};
    std::string lower = file;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (!lower.ends_with(".blp")) file += ".blp";
    if (file.find('\\') != std::string::npos) return exists(file) ? file : std::string{};
    for (const char* directory : {"Creature\\Baked\\", "Textures\\BakedNpcTextures\\"}) {
        std::string path = directory + file;
        if (exists(path)) return path;
    }
    return {};
}
} // namespace wowee::pipeline
