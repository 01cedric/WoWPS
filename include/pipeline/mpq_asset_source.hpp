#pragma once
// MpqAssetSource - the client's files straight out of the user's MPQ archives.
//
// What the real client does: no extracted tree, no manifest. The archives are
// opened once in the client's priority order (base, expansion, locale, then
// the patch chain; later wins) and every read is a hash lookup followed by a
// sector read and decompression inside StormLib. StormLib is not thread-safe,
// so one mutex serialises all archive calls; the AssetManager's file cache in
// front of this keeps repeated reads off the archives.
//
// Paths are the AssetManager's normalised form (lower case, backslashes);
// StormLib hashes names case-insensitively and accepts either separator.
//
// Built with StormLib on the console (and in the host smoke test). Elsewhere
// open() answers null and the AssetManager keeps its extracted-tree path.
#include <cstdint>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace wowee {
namespace pipeline {

class MpqAssetSource {
public:
    static std::unique_ptr<MpqAssetSource> open(const std::string& archiveDir, std::string* error = nullptr);
    ~MpqAssetSource();
    MpqAssetSource(const MpqAssetSource&) = delete;
    MpqAssetSource& operator=(const MpqAssetSource&) = delete;

    [[nodiscard]] bool hasFile(const std::string& normalizedPath) const;
    [[nodiscard]] std::vector<uint8_t> readFile(const std::string& normalizedPath) const;
    // Checks StormLib's uncompressed file size before allocating/reading it.
    [[nodiscard]] std::vector<uint8_t> readFileBounded(const std::string& normalizedPath, size_t maxBytes) const;
    /// Every file whose normalised path starts with the prefix, from the
    /// archives' internal listfiles; sorted, de-duplicated.
    [[nodiscard]] std::vector<std::string> listFiles(const std::string& normalizedPrefix) const;

    [[nodiscard]] const std::string& expansion() const { return expansion_; }
    [[nodiscard]] const std::string& locale() const { return locale_; }
    [[nodiscard]] size_t archiveCount() const { return archives_.size(); }
    [[nodiscard]] const std::vector<std::string>& archivePaths() const { return paths_; }

private:
    MpqAssetSource() = default;
    mutable std::mutex mutex_;
    std::vector<void*> archives_;      // StormLib HANDLEs, lowest priority first
    mutable bool listfilesLoaded_ = false;
    std::vector<std::string> paths_;
    std::string expansion_;
    std::string locale_;
};

} // namespace pipeline
} // namespace wowee
