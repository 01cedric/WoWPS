#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <cstdint>
#include <functional>

namespace wowee {
namespace tools {

/**
 * Extraction pipeline: MPQ archives → loose files + manifest
 */
class Extractor {
public:
    struct Options {
        std::string mpqDir;       // Path to WoW Data directory
        std::string outputDir;    // Output directory for extracted assets
        std::string expansion;    // "classic", "turtle", "tbc", "wotlk", or "" for auto-detect
        std::string locale;       // "enUS", "deDE", etc., or "" for auto-detect
        bool expansionSubdir = false; // Write under outputDir/expansions/<expansion>
        int threads = 0;          // 0 = auto-detect
        bool verify = false;      // CRC32 verify after extraction
        bool verbose = false;     // Verbose logging
        bool generateDbcCsv = false; // Convert selected DBFilesClient/*.dbc to CSV for committing
        bool skipDbcExtraction = false; // Extract visual assets only (recommended when CSV DBCs are in repo)
        bool onlyUsedDbcs = false; // Extract only the DBC files wowee uses (implies DBFilesClient/*.dbc filter)
        std::string dbcCsvOutputDir; // When set, write CSVs into this directory instead of outputDir/expansions/<exp>/db
        std::string referenceManifest; // If set, only extract files NOT in this manifest (delta extraction)
        std::string listFile;         // External listfile for MPQ enumeration (resolves unnamed hash entries)
        // Open-format emission: post-extract pass that writes wowee
        // open-format side-files (e.g. foo.blp → foo.png) without
        // touching the original. Lets wowee's runtime/editor consume
        // the open formats while keeping the proprietary copies that
        // private servers (AzerothCore/TrinityCore) read from.
        bool emitPng = false;          // BLP → PNG side-files
        bool emitJsonDbc = false;      // DBC → JSON side-files
        bool emitWom = false;          // M2 (+skin) → WOM side-files
        bool emitWob = false;          // WMO (+groups) → WOB side-files
        bool emitTerrain = false;      // ADT → WHM + WOT + WOC side-files

        // Progress sink for in-process callers (the PS4 first-launch
        // extraction drives a system progress dialog from it). stage is a
        // short label ("Scanning archives", "Extracting files", ...);
        // done/total count the current stage's units, total 0 meaning
        // indeterminate. Called from the extraction worker threads as well
        // as the calling thread, never concurrently (serialised inside
        // run). The command-line tool leaves it empty and prints instead.
        std::function<void(const std::string& stage, uint64_t done, uint64_t total)> progress;

        // Text sink for failures that are not fatal to the whole run - a
        // single archive that would not open, an "opened N/M archives"
        // shortfall - and would otherwise be visible only on a terminal
        // std::cerr the PS4 has none of. The command-line tool leaves it
        // empty and relies on its own stderr output instead; the PS4
        // first-launch path (src/platform/ps4/extract_ps4.cpp) routes it
        // into wowee.log, which the console-only failure this was written
        // for ("extraction failed after 0 s", no further detail) had none of.
        std::function<void(const std::string& message)> diagnostic;
    };

    struct Stats {
        std::atomic<uint64_t> filesExtracted{0};
        std::atomic<uint64_t> bytesExtracted{0};
        std::atomic<uint64_t> filesSkipped{0};
        std::atomic<uint64_t> filesFailed{0};
    };

    /**
     * Auto-detect expansion from files in mpqDir.
     * @return "classic", "turtle", "tbc", "wotlk", or "" if unknown
     */
    static std::string detectExpansion(const std::string& mpqDir);

    /**
     * Auto-detect locale by scanning for locale subdirectories.
     * @return locale string like "enUS", or "" if none found
     */
    static std::string detectLocale(const std::string& mpqDir);
    /**
     * The archives the client loads for this expansion and locale, in load
     * order (base, expansion, locale, then the patch chain; later wins).
     */
    static std::vector<std::string> discoverArchives(const std::string& mpqDir,
                                                     const std::string& expansion,
                                                     const std::string& locale);

    /**
     * Run the extraction pipeline
     * @return true on success
     */
    static bool run(const Options& opts);

private:
    static bool enumerateFiles(const Options& opts,
                               std::vector<std::string>& outFiles);
};

} // namespace tools
} // namespace wowee
