#include "first_launch.hpp"
#include "extractor.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <system_error>

namespace wowee {
namespace tools {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isArchive(const fs::directory_entry& e) {
    std::error_code ec;
    if (!e.is_regular_file(ec)) return false;
    const std::string name = e.path().filename().string();
    if (name.rfind("._", 0) == 0) return false;  // macOS resource forks
    return lower(e.path().extension().string()) == ".mpq";
}

// Every archive Extractor may read: the ones in mpqDir and the ones one
// level down (the locale directories). Sorted for a stable log.
std::vector<std::string> listArchives(const std::string& mpqDir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(mpqDir, ec), end; it != end && !ec; it.increment(ec)) {
        if (isArchive(*it)) {
            out.push_back(it->path().string());
        } else if (it->is_directory(ec)) {
            std::error_code ec2;
            for (fs::directory_iterator sub(it->path(), ec2), subEnd; sub != subEnd && !ec2; sub.increment(ec2)) {
                if (isArchive(*sub)) out.push_back(sub->path().string());
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ---- the archive record --------------------------------------------------
//
// Written next to the manifest after a successful run: one line per archive,
// "<size>\t<mtime>\t<path relative to mpqDir>". A later plan compares the
// archives it finds against it, so a client copied over later (new sizes or
// times, added or removed archives) is picked up, while an archive whose copy
// gave it a time in the future (clock skew on the FTP side) does not look
// newer than the manifest on every launch.
constexpr const char* kStateFile = "wowee_extract_state.txt";
constexpr const char* kPendingFile = "wow_ps_extract_pending.txt";

struct ArchiveStamp {
    uint64_t size = 0;
    int64_t mtime = 0;   // file clock ticks as seconds; only compared with itself
    bool operator==(const ArchiveStamp& o) const { return size == o.size && mtime == o.mtime; }
};

using ArchiveStamps = std::map<std::string, ArchiveStamp>;  // relative path -> stamp

ArchiveStamps stampArchives(const std::string& mpqDir, const std::vector<std::string>& archives) {
    ArchiveStamps stamps;
    for (const auto& a : archives) {
        std::error_code ec;
        ArchiveStamp st;
        st.size = fs::file_size(a, ec);
        if (ec) continue;
        const auto t = fs::last_write_time(a, ec);
        if (ec) continue;
        st.mtime = std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
        // Both paths originate from this directory traversal. No realpath /
        // current-directory lookup is needed (or reliable under PS4 /app0).
        const fs::path rel = fs::path(a).lexically_normal().lexically_relative(fs::path(mpqDir).lexically_normal());
        stamps[rel.empty() ? a : rel.generic_string()] = st;
    }
    return stamps;
}

bool readStamps(const std::string& path, ArchiveStamps& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        ArchiveStamp st;
        std::string rel;
        if (!(in >> st.size >> st.mtime)) return false;
        std::getline(in, rel);
        // one tab after the numbers
        if (!rel.empty() && rel[0] == '\t') rel.erase(0, 1);
        if (rel.empty()) return false;
        out[rel] = st;
    }
    return true;
}

bool writeStamps(const std::string& path, const ArchiveStamps& stamps) {
    const std::string temporary = path + ".wow_ps.tmp";
    std::ofstream f(temporary, std::ios::trunc);
    if (!f) return false;
    f << "# archives this tree was extracted from: size, mtime, path (see first_launch.cpp)\n";
    for (const auto& [rel, st] : stamps) f << st.size << '\t' << st.mtime << '\t' << rel << '\n';
    f.close();
    if (!f) { std::remove(temporary.c_str()); return false; }
#ifdef _WIN32
    std::error_code ec;
    fs::copy_file(temporary, path, fs::copy_options::overwrite_existing, ec);
    std::remove(temporary.c_str());
    return !ec;
#else
    if (std::rename(temporary.c_str(), path.c_str()) == 0) return true;
    std::remove(temporary.c_str());
    return false;
#endif
}

// The first difference between what was recorded and what is there, or ""
// when the sets match.
std::string compareStamps(const ArchiveStamps& recorded, const ArchiveStamps& current) {
    for (const auto& [rel, st] : current) {
        auto it = recorded.find(rel);
        if (it == recorded.end()) return rel + " was added since the last extraction";
        if (!(it->second == st)) return rel + " changed since the last extraction";
    }
    for (const auto& [rel, st] : recorded) {
        if (!current.count(rel)) return rel + " was removed since the last extraction";
    }
    return "";
}

} // namespace

FirstLaunchPlan planFirstLaunch(const std::string& outputRoot, const std::string& mpqDir) {
    FirstLaunchPlan plan;
    plan.mpqDir = mpqDir;
    plan.outputRoot = outputRoot;

    std::error_code ec;
    if (!fs::is_directory(mpqDir, ec)) {
        plan.reason = "no archive directory at " + mpqDir;
        return plan;
    }
    plan.archives = listArchives(mpqDir);
    if (plan.archives.empty()) {
        plan.reason = "no .mpq files under " + mpqDir;
        return plan;
    }
    plan.hasArchives = true;

    try {
        plan.expansion = Extractor::detectExpansion(mpqDir);
        plan.locale = Extractor::detectLocale(mpqDir);
    } catch (const std::exception& e) {
        plan.reason = std::string("archive detection failed: ") + e.what();
        return plan;
    }
    if (plan.expansion.empty()) {
        plan.reason = "archives under " + mpqDir + " are not a recognised client (no lichking/expansion/dbc/terrain.mpq)";
        return plan;
    }

    plan.outputDir = (fs::path(outputRoot) / "expansions" / plan.expansion).string();
    plan.manifestPath = (fs::path(plan.outputDir) / "manifest.json").string();

    // The desktop tool looks next to its binary and in the archive directory;
    // here the archive directory and the data root are the places a user can
    // reach.
    for (const auto& candidate : {fs::path(mpqDir) / "listfile.txt", fs::path(outputRoot) / "listfile.txt"}) {
        if (fs::is_regular_file(candidate, ec)) {
            plan.listFile = candidate.string();
            break;
        }
    }

    // A failed/interrupted update can leave a usable partial manifest. It
    // must still resume on next boot, even when that manifest is newer than
    // every source archive or the old successful archive record still matches.
    if (fs::exists(fs::path(plan.outputDir) / kPendingFile, ec)) {
        plan.needed = true;
        plan.reason = "previous extraction did not finish";
        return plan;
    }

    if (!fs::is_regular_file(plan.manifestPath, ec)) {
        plan.needed = true;
        plan.reason = "no manifest at " + plan.manifestPath;
        return plan;
    }

    // Stale: the archives differ from the ones recorded after the last
    // successful run (a client copied over afterwards by FTP or USB).
    // Re-running merges into the existing manifest rather than starting
    // over. The manifest is the last thing Extractor::run writes, so an
    // interrupted run leaves none and is caught above.
    const std::string statePath = (fs::path(plan.outputDir) / kStateFile).string();
    ArchiveStamps recorded;
    if (fs::is_regular_file(statePath, ec) && readStamps(statePath, recorded)) {
        const std::string diff = compareStamps(recorded, stampArchives(mpqDir, plan.archives));
        plan.needed = !diff.empty();
        plan.reason = plan.needed ? diff : "archives match the record at " + statePath;
        return plan;
    }

    // No record (a manifest made by the desktop tool, or by an older build):
    // an archive newer than the manifest means stale.
    const auto manifestTime = fs::last_write_time(plan.manifestPath, ec);
    if (ec) {
        plan.needed = true;
        plan.reason = "cannot read the manifest's time (" + ec.message() + ")";
        return plan;
    }
    for (const auto& archive : plan.archives) {
        std::error_code aec;
        const auto t = fs::last_write_time(archive, aec);
        if (!aec && t > manifestTime) {
            plan.needed = true;
            plan.reason = fs::path(archive).filename().string() + " is newer than the manifest";
            return plan;
        }
    }
    plan.reason = "manifest at " + plan.manifestPath + " is up to date";
    return plan;
}

namespace {

bool sameFileContents(const fs::path& left, const fs::path& right) {
    std::ifstream a(left, std::ios::binary), b(right, std::ios::binary);
    if (!a || !b) return false;
    char aa[4096], bb[4096];
    do {
        a.read(aa, sizeof(aa));
        b.read(bb, sizeof(bb));
        if (a.gcount() != b.gcount() || std::memcmp(aa, bb, static_cast<size_t>(a.gcount())) != 0) return false;
    } while (a && b);
    return a.eof() && b.eof();
}

// Small metadata files are staged beside the destination. A failed write
// leaves the last working profile intact. Avoid copy_file's canonical-path
// and sendfile paths, which failed on the supplied console's /app0 mount.
bool copyMetadata(const fs::path& source, const fs::path& destination, std::string& error) {
    const std::string src = source.string(), dst = destination.string();
    const std::string temporary = dst + ".wow_ps.tmp";
    FILE* input = std::fopen(src.c_str(), "rb");
    if (!input) { error = src + ": open: " + std::strerror(errno); return false; }
    FILE* output = std::fopen(temporary.c_str(), "wb");
    if (!output) {
        error = temporary + ": open: " + std::strerror(errno);
        std::fclose(input);
        return false;
    }
    bool ok = true;
    char buffer[4096];
    for (;;) {
        const size_t size = std::fread(buffer, 1, sizeof(buffer), input);
        if (size && std::fwrite(buffer, 1, size, output) != size) { ok = false; break; }
        if (size != sizeof(buffer)) { ok = !std::ferror(input); break; }
    }
    if (std::fclose(input) != 0) ok = false;
    if (std::fflush(output) != 0) ok = false;
    if (std::fclose(output) != 0) ok = false;
    if (!ok) {
        error = dst + ": metadata read/write failed";
        std::remove(temporary.c_str());
        return false;
    }
#ifdef _WIN32
    // Windows rename does not replace an existing file. This is a desktop
    // tools fallback; console/POSIX replacement below is atomic.
    std::error_code ec;
    fs::copy_file(temporary, destination, fs::copy_options::overwrite_existing, ec);
    std::remove(temporary.c_str());
    if (ec) { error = dst + ": replace: " + ec.message(); return false; }
#else
    if (std::rename(temporary.c_str(), dst.c_str()) != 0) {
        error = dst + ": rename: " + std::strerror(errno);
        std::remove(temporary.c_str());
        return false;
    }
#endif
    return true;
}

} // namespace

int syncBundledData(const std::string& bundledDataDir, const std::string& outputRoot,
                    std::string* error) {
    if (error) error->clear();
    std::error_code ec;
    if (!fs::is_directory(bundledDataDir, ec)) {
        if (error) *error = "not a directory: " + bundledDataDir;
        return -1;
    }
    int copied = 0;
    std::string firstError;
    for (fs::recursive_directory_iterator it(bundledDataDir, ec), end; it != end && !ec; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        const fs::path rel = it->path().lexically_normal().lexically_relative(fs::path(bundledDataDir).lexically_normal());
        if (rel.empty() || rel == "." || rel.is_absolute() || *rel.begin() == "..") {
            if (firstError.empty()) firstError = "invalid bundled metadata path: " + it->path().string();
            continue;
        }
        // The bundled tree holds the client's own metadata only; anything
        // else (an extracted file, a manifest) is not ours to overwrite.
        const std::string name = lower(rel.filename().string());
        if (name == "manifest.json") continue;
        const fs::path dst = fs::path(outputRoot) / rel;

        // Compare content, not mtime: package timestamps and the console
        // clock need not agree, and same-size metadata updates must land.
        if (sameFileContents(it->path(), dst)) continue;

        fs::create_directories(dst.parent_path(), fec);
        if (fec) {
            if (firstError.empty()) firstError = dst.parent_path().string() + ": mkdir: " + fec.message();
            continue;
        }
        std::string copyError;
        if (!copyMetadata(it->path(), dst, copyError)) {
            if (firstError.empty()) firstError = copyError;
            continue;
        }
        ++copied;
    }
    if (ec && firstError.empty()) firstError = bundledDataDir + ": enumerate: " + ec.message();
    if (error) *error = firstError;
    return copied;
}

bool runFirstLaunch(const FirstLaunchPlan& plan, int threads, const FirstLaunchProgress& report,
                    const FirstLaunchDiagnostic& diagnostic) {
    if (!plan.hasArchives || plan.expansion.empty()) {
        std::cerr << "first-launch extraction: nothing to do (" << plan.reason << ")\n";
        return false;
    }

    Extractor::Options opts;
    opts.mpqDir = plan.mpqDir;
    opts.outputDir = plan.outputRoot;
    opts.expansion = plan.expansion;
    opts.locale = plan.locale;
    opts.expansionSubdir = true;  // <root>/expansions/<expansion>, as extract_assets.sh
    opts.threads = threads;
    opts.listFile = plan.listFile;
    opts.diagnostic = diagnostic;

    // Stage weights: the archive scan is quick, the file copy is the run.
    if (report) {
        opts.progress = [&report](const std::string& stage, uint64_t done, uint64_t total) {
            const float unit = total ? static_cast<float>(done) / static_cast<float>(total) : 0.0f;
            float f;
            if (stage == "Scanning archives")     f = 0.02f + 0.06f * unit;
            else if (stage == "Extracting files") f = 0.08f + 0.88f * unit;
            else if (stage == "Done")             f = 1.0f;
            else                                  f = 0.97f;  // manifest, verify, conversions
            report(stage, std::clamp(f, 0.0f, 1.0f));
        };
        report("Preparing", 0.0f);
    }

    try {
        std::error_code ec;
        fs::create_directories(plan.outputDir, ec);
        if (ec) {
            std::cerr << "first-launch extraction: cannot create " << plan.outputDir << ": " << ec.message() << "\n";
            return false;
        }
        const std::string pendingPath = (fs::path(plan.outputDir) / kPendingFile).string();
        {
            std::ofstream pending(pendingPath, std::ios::trunc);
            pending << "Extraction in progress; keep this file until the manifest and archive record are complete.\n";
            pending.close();
            if (!pending) {
                std::cerr << "first-launch extraction: cannot write pending marker " << pendingPath << "\n";
                return false;
            }
        }
        if (!Extractor::run(opts)) return false;
        // Record what this tree came from, for the next plan. Re-listed and
        // re-stamped now rather than taken from the plan, so an archive that
        // changed under the run is seen next time.
        const std::string statePath = (fs::path(plan.outputDir) / kStateFile).string();
        if (!writeStamps(statePath, stampArchives(plan.mpqDir, listArchives(plan.mpqDir)))) {
            std::cerr << "first-launch extraction: could not write " << statePath << "\n";
            return false;
        }
        if (std::remove(pendingPath.c_str()) != 0) {
            std::cerr << "first-launch extraction: could not clear pending marker " << pendingPath << "\n";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "first-launch extraction: exception: " << e.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "first-launch extraction: unknown exception\n";
        return false;
    }
}

} // namespace tools
} // namespace wowee
