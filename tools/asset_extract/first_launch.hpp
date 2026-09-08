#pragma once
// First-launch (on-console) extraction on top of Extractor.
//
// The desktop flow is extract_assets.sh: the user points it at a client's
// Data directory and it writes Data/expansions/<expansion>/ next to the
// client. On a console there is no shell, so the client does the same thing
// itself the first time it finds MPQs and no extracted tree (or a tree older
// than the MPQs). This file is the portable half of that: it decides whether
// an extraction is due, keeps the client's bundled expansion metadata beside
// the extracted files, and drives Extractor::run with a progress sink. It
// has no console API in it so ps4/tests/extract_smoke can run it on Linux;
// src/platform/ps4/extract_ps4.cpp supplies the paths and the progress
// dialog.
//
// Layout produced under outputRoot (the client's WOW_DATA_PATH):
//
//   <outputRoot>/Data/<*.MPQ>                     the user's client archives (input)
//   <outputRoot>/expansions/<expansion>/manifest.json
//   <outputRoot>/expansions/<expansion>/<lowercase wow path>   extracted files
//   <outputRoot>/expansions/<expansion>/expansion.json, opcodes.json, ...
//                                                 copied from the bundled Data tree
//   <outputRoot>/expansions/<expansion>/wowee_extract_state.txt
//                                                 size/mtime of the archives the tree
//                                                 came from (the staleness check)
//
// which is exactly what extract_assets.sh produces with --expansion-subdir.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wowee {
namespace tools {

struct FirstLaunchPlan {
    std::string mpqDir;          // where the archives are (<root>/Data)
    std::string outputRoot;      // the tree the client reads (<root>)
    std::string expansion;       // detected id ("wotlk", ...) or "" if unknown
    std::string locale;          // detected locale directory or ""
    std::string outputDir;       // <root>/expansions/<expansion>
    std::string manifestPath;    // <outputDir>/manifest.json
    std::string listFile;        // external listfile found next to the archives, or ""
    std::vector<std::string> archives;  // every *.mpq under mpqDir (and its locale dirs)
    bool hasArchives = false;    // mpqDir exists and holds at least one archive
    bool needed = false;         // extraction is due
    std::string reason;          // why (needed or not), for the log
};

/// Inspect mpqDir and outputRoot and decide whether an extraction is due:
/// archives present and the expansion recognised, and the manifest either
/// missing or made from different archives (the size/mtime record written
/// after the last run; without a record, an archive newer than the manifest
/// counts). Never throws.
FirstLaunchPlan planFirstLaunch(const std::string& outputRoot, const std::string& mpqDir);

/// Copy the client's bundled Data tree (Data/expansions/<id>/expansion.json,
/// opcodes.json, update_fields.json, dbc_layouts.json, Data/opcodes/*.json)
/// into outputRoot so the expansion registry finds the profile next to the
/// extracted files. Only the bundled files are written; a manifest.json or
/// extracted asset already there is never touched. Returns the number of
/// files copied, or -1 when bundledDataDir is not a directory. Never throws.
int syncBundledData(const std::string& bundledDataDir, const std::string& outputRoot,
                    std::string* error = nullptr);

/// stage: short label; fraction: 0..1 over the whole run.
using FirstLaunchProgress = std::function<void(const std::string& stage, float fraction)>;

/// A non-fatal failure worth recording (an archive that would not open, a
/// reduced archive count) - see Extractor::Options::diagnostic. Defaults to
/// nothing so existing callers (the desktop tool, ps4/tests/extract_smoke)
/// are unaffected; src/platform/ps4/extract_ps4.cpp is the one caller that
/// needs it, to put these lines somewhere the console user's wowee.log
/// actually shows them.
using FirstLaunchDiagnostic = std::function<void(const std::string& message)>;

/// Run the extraction the plan describes (Extractor::run with
/// --expansion-subdir semantics). threads <= 0 leaves the Extractor default.
/// Returns false when the plan has nothing to extract or the run failed.
/// Never throws.
bool runFirstLaunch(const FirstLaunchPlan& plan, int threads, const FirstLaunchProgress& report,
                    const FirstLaunchDiagnostic& diagnostic = nullptr);

} // namespace tools
} // namespace wowee
