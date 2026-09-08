#pragma once

/// The names a WMO's group file might be stored under.
///
/// A WMO root names its groups by index: Stormwind.wmo has Stormwind_000.wmo
/// beside it, and so on. Which extension they actually carry is not something
/// the root says, so every loader tries more than one.
///
/// Five places did that and three of them tried different numbers of names.
/// Three walked the root's own extension, then ".wmo", then ".WMO"; one
/// stopped after two; one tried ".wmo" alone. On a filesystem that cares about
/// case - which is every Linux install, and the one the extracted archives are
/// most often unpacked onto - a building whose groups are spelled ".WMO" then
/// loaded its interior when the terrain streamed it in and did not when a
/// transport or a spawned doodad asked for the same file. Nothing reports it:
/// the root loads, the groups do not, and the building is an empty shell.
///
/// The order matters and is the root's own spelling first: that is the name
/// the archive actually used in the common case, so the other two are only
/// reached when it is wrong.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace wowee::pipeline {

/// Every name to try for one group of `rootPath`, in order.
///
/// The root's extension is replaced rather than appended to, but only when it
/// really is a WMO extension: a path with no extension keeps all of its name.
/// One of the five callers cut four characters off whatever it was given,
/// which turns a root stored without an extension into a truncated name that
/// matches nothing.
inline std::vector<std::string> wmoGroupCandidates(const std::string& rootPath,
                                                   uint32_t groupIndex) {
    std::string basePath = rootPath;
    std::string extension;
    if (basePath.size() > 4) {
        std::string tail = basePath.substr(basePath.size() - 4);
        std::string lowered = tail;
        for (char& c : lowered) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lowered == ".wmo") {
            extension = tail;
            basePath.resize(basePath.size() - 4);
        }
    }

    char suffix[16];
    std::vector<std::string> names;
    const auto add = [&](const std::string& ext) {
        std::snprintf(suffix, sizeof(suffix), "_%03u%s", groupIndex, ext.c_str());
        std::string candidate = basePath + suffix;
        for (const std::string& seen : names) {
            if (seen == candidate) return;
        }
        names.push_back(std::move(candidate));
    };
    if (!extension.empty()) add(extension);
    add(".wmo");
    add(".WMO");
    return names;
}

}  // namespace wowee::pipeline
