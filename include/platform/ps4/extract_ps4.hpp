#pragma once
// On-console asset extraction for the PS4 port (src/platform/ps4/extract_ps4.cpp).
//
// The client reads the tree the desktop extract_assets.sh produces, not
// MPQs. On the console the user puts their client's Data directory at
// dataRoot()/Data and the client builds that tree itself on first launch,
// with the same extractor code (tools/asset_extract) linked in and the
// system message dialog's progress bar for feedback. The result lands under
// dataRoot()/expansions/<expansion>/, where WOW_DATA_PATH (= dataRoot())
// makes Application::initialize look. See docs/ps4.md, "Game data on the
// console". Main-thread only.

#include <functional>
#include <string>

namespace wowee {
namespace platform {
namespace ps4 {

/// Archives are present under dataRoot()/Data and the extracted tree the
/// client expects (dataRoot()/expansions/<expansion>/manifest.json) is
/// missing or older than the archives. Logs the decision.
bool needsExtraction();

/// Run the extraction in the calling thread. report(stage, progress 0..1)
/// is called from the extractor's worker threads (serialised). Also copies
/// the package's bundled expansion metadata (/app0/Data) beside the output.
/// Returns false when there was nothing to extract or the run failed; the
/// tree that exists afterwards is whatever the run managed.
bool runExtraction(std::function<void(const std::string& stage, float progress)> report);

/// The startup step: if needsExtraction(), run it on a worker thread with
/// the system progress dialog on screen and the system event queue pumped,
/// then return. Fail-safe: any failure is logged and the caller continues
/// with what is on disk. Returns true only when an extraction ran and
/// finished successfully. onProgress is called on the MAIN thread every
/// poll; the application can keep presenting its own loading screen even if
/// the system dialog is unavailable. Call after initSystem and UI setup.
/// Mirror the package's expansion metadata into the data root and log the
/// archive set the client will stream from. No extraction (B4 test 7).
void prepareAssetMetadata();

bool extractAtStartup(std::function<void(const std::string& stage, float progress)> onProgress = {});

} // namespace ps4
} // namespace platform
} // namespace wowee
