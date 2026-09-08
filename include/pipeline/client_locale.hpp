#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace wowee::pipeline {

inline std::string englishFirstLocaleDirectory(const std::string& root,
                                               const std::vector<std::string>& known) {
    namespace fs = std::filesystem;
    auto lower = [](std::string text) {
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    std::error_code error;
    if (!fs::is_directory(root, error)) return {};
    std::vector<std::string> available;
    for (fs::directory_iterator dirs(root, error), end; !error && dirs != end; dirs.increment(error)) {
        std::error_code entryError;
        if (!dirs->is_directory(entryError) || entryError) continue;
        bool archives = false;
        const std::string locale = lower(dirs->path().filename().string());
        for (fs::directory_iterator files(dirs->path(), entryError);
             !entryError && files != end; files.increment(entryError)) {
            if (files->is_regular_file(entryError) && !entryError &&
                (lower(files->path().filename().string()) == "locale-" + locale + ".mpq" ||
                 lower(files->path().filename().string()) == "base-" + locale + ".mpq")) {
                archives = true;
                break;
            }
        }
        // Empty or speech-only enUS folders must not hide a usable text pack.
        if (archives) available.push_back(dirs->path().filename().string());
    }
    std::sort(available.begin(), available.end());
    std::vector<std::string> preference{"enUS", "enGB"};
    preference.insert(preference.end(), known.begin(), known.end());
    for (const auto& wanted : preference)
        for (const auto& name : available)
            if (lower(name) == lower(wanted)) return name;
    return {};
}

} // namespace wowee::pipeline
