#include "addons/framexml_recovery.hpp"
// system_ps4.cpp - PS4 (OpenOrbis) process and system services for WoWPS.
//
// The system_ps4.cpp half of include/platform/ps4/ps4_platform.hpp: system
// module loading, user/system/network service bring-up, the on-console
// directory layout, the process environment the rest of the client reads,
// host-name resolution through sceNetResolver, and the per-frame system
// event pump.
//
// Main-thread only, except resolveIPv4, which may be called from any thread
// once initSystem has returned (it creates and destroys its own resolver).

#include "platform/ps4/ps4_platform.hpp"
#include "platform/ps4/signal_context.hpp"
#include "ps4_kstat.h"
#include "ps4_ksignal.h"
#include <pthread.h>
#include "core/env.hpp"
#include "core/logger.hpp"

#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <orbis/CommonDialog.h>
#include <orbis/MsgDialog.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cxxabi.h>
#include <exception>
#include <string>
#include <utility>
#include <typeinfo>

// orbis/SystemService.h is deliberately not included. It declares
// sceSystemServiceReceiveEvent() with no parameters, which is not the
// function's signature, and a second declaration with the real one would be a
// conflicting extern "C" declaration. The symbols this file needs are declared
// here with the prototypes the libraries export (checked against the stub
// libSceSystemService.so / libSceUserService.so / libSceNetCtl.so shipped with
// the toolchain; sceNetCtlGetState is exported but missing from the OpenOrbis
// headers, and sceUserServiceTerminate is repeated here only so this block is
// self-contained - orbis/UserService.h declares it with the same signature).
extern "C" {

// OpenOrbis link.x supplies these relocated symbols. Use them directly in
// the signal handler: module lookup, dladdr and unwinding may allocate/lock.
extern char __text_start[];
extern char __eh_frame_start[];

// SceSystemServiceEvent: an event type followed by 8 KiB of payload. The
// client ignores the payload; only the size matters so the library's write
// lands inside our buffer.
struct OrbisSystemServiceEvent {
    int32_t eventType;
    char data[8192];
};

int32_t sceSystemServiceHideSplashScreen(void);
int32_t sceSystemServiceReceiveEvent(OrbisSystemServiceEvent* event);
int32_t sceUserServiceTerminate(void);
int32_t sceNetCtlGetState(int32_t* state);

}  // extern "C"

namespace wowee {
namespace platform {
namespace ps4 {

namespace {

// ---- constants -------------------------------------------------------------

constexpr const char* kDataRoot = "/data/wow_ps";
constexpr const char* kPreferredWritableRoot = "/data/wow_ps/wowps";
constexpr const char* kLegacyWritableRoot = "/data/wow_ps/wowee";
const char* kWritableRoot = kPreferredWritableRoot;
constexpr const char* kAppRoot = "/app0";

// Memory the resolver may use for its queries. Sockets do not draw on it
// (they live in the kernel), so a small pool is plenty.
constexpr int32_t kNetPoolBytes = 32 * 1024;

// sceSystemServiceReceiveEvent answers this when the queue is empty.
constexpr int32_t kSystemServiceErrorNoEvent = static_cast<int32_t>(0x80A10003);

// Upper bound on events drained per pumpSystemEvents call, so a burst cannot
// hold the frame.
constexpr int kMaxEventsPerPump = 16;

// ---- state -----------------------------------------------------------------

bool g_initialised = false;
bool g_systemServiceLoaded = false;
bool g_userServiceReady = false;
bool g_netInitialised = false;
bool g_netCtlInitialised = false;
bool g_messageDialogLoaded = false;
bool g_splashHidden = false;
// Never closed during the process lifetime: the signal handler can run on
// another thread even while normal shutdown is in progress.
volatile std::sig_atomic_t g_bootFd = -1;
bool g_crashReporterAltStack = false;
unsigned g_crashReporterSignalMask = 0;
void reportCrashReporterState();
// The resolver's memory pool; < 0 when networking is unavailable. Atomic
// because resolveIPv4 may run on a network thread.
std::atomic<int32_t> g_netPool{-1};

// ---- helpers ---------------------------------------------------------------

// printf goes to the kernel log, which is what a klog viewer shows. Used for
// the part of initSystem that runs before the logger has a directory to write
// to; after that the core logger (which mirrors to stdout) does the job.
void klog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::fputs("[wowps/ps4] ", stdout);
    std::vfprintf(stdout, fmt, args);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    va_end(args);
}

std::string hex(int32_t rc) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08x", static_cast<uint32_t>(rc));
    return buf;
}

// mkdir that treats "already there" as success. Only one level: callers pass
// parents before children.
bool makeDirectory(const std::string& path) {
    if (::mkdir(path.c_str(), 0777) == 0) return true;
    if (errno == EEXIST) {
        Ps4KernelStat st{};
        if (ps4KernelStat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return true;
    }
    klog("mkdir(%s) failed: errno %d (%s)", path.c_str(), errno, std::strerror(errno));
    return false;
}

// The system modules the client uses. Loading one that the loader already
// resolved from the ELF's import table is a no-op, so this costs nothing when
// redundant and saves a crash on the first call when it is not.
//
// Only libSceSystemService and libSceUserService are required: without them
// the process cannot pump its event queue or find the user, and Sony's shell
// treats such an app as hung. Everything else degrades: no network (auth
// screen reports it), no pad, no audio, no on-screen keyboard.
struct ModuleSpec {
    const char* name;
    uint32_t id;
    bool internal;   // sceSysmoduleLoadModuleInternal vs sceSysmoduleLoadModule
    bool required;
};

const ModuleSpec kModules[] = {
    {"libSceSystemService", ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE, true,  true},
    {"libSceUserService",   ORBIS_SYSMODULE_INTERNAL_USER_SERVICE,   true,  true},
    {"libSceNet",           ORBIS_SYSMODULE_INTERNAL_NET,            true,  false},
    {"libSceNetCtl",        ORBIS_SYSMODULE_INTERNAL_NETCTL,         true,  false},
    {"libScePad",           ORBIS_SYSMODULE_INTERNAL_PAD,            true,  false},
    {"libSceAudioOut",      ORBIS_SYSMODULE_INTERNAL_AUDIOOUT,       true,  false},
    // libSceImeDialog and libSceMsgDialog sit on libSceCommonDialog; load the
    // base first so the two dialogs find it.
    {"libSceCommonDialog",  ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG,  true,  false},
    {"libSceImeDialog",     ORBIS_SYSMODULE_IME_DIALOG,              false, false},
    {"libSceMsgDialog",     ORBIS_SYSMODULE_MESSAGE_DIALOG,          false, false},
};

bool loadModules() {
    bool ok = true;
    for (const ModuleSpec& m : kModules) {
        int32_t rc;
        if (m.internal) {
            rc = static_cast<int32_t>(
                sceSysmoduleLoadModuleInternal(static_cast<OrbisSysModuleInternal>(m.id)));
        } else {
            rc = sceSysmoduleLoadModule(static_cast<OrbisSysModule>(m.id));
        }
        if (rc != 0) {
            klog("%s: sceSysmoduleLoadModule%s failed: %s%s", m.name,
                 m.internal ? "Internal" : "", hex(rc).c_str(),
                 m.required ? " (required)" : "");
            if (m.required) ok = false;
            continue;
        }
        if (m.id == ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE) g_systemServiceLoaded = true;
        if (m.id == ORBIS_SYSMODULE_MESSAGE_DIALOG) g_messageDialogLoaded = true;
    }
    return ok;
}

// The writable tree. Created parent-first; the platform's writable root for
// homebrew is /data, so /data/wow_ps is created too in case the user has not
// copied their client in yet (the client then reports the missing data
// rather than the missing directory).
//
//   /data/wow_ps           game data the user provides (WOW_DATA_PATH)
//   /data/wow_ps/wowps     everything the client writes (HOME)
//     logs/                wowps.log (see core/logger.cpp)
//     cache/               derived data (warden/texture caches)
//     saves/               saved variables, last-world info
//     config/              settings (WOWEE_CONFIG_ROOT)
//     tmp/                 scratch files ($TMPDIR; the sandbox has no /tmp)
// B25: startup must not use filesystem::symlink_status or a recursive
// iterator. OpenOrbis libc's lstat resolves to an ENOSYS fstatat stub in the
// shipped ELF. Use the already verified kernel stat ABI and native rename.
// Never merge, overwrite or delete either tree during process bring-up.
enum class RuntimeMove { Absent, Moved, BothPresent, Failed };
RuntimeMove moveRuntimeDirectory(const std::string& oldPath, const std::string& newPath) {
    Ps4KernelStat info{};
    if (ps4KernelStat(oldPath.c_str(), &info) != 0) {
        if (errno == ENOENT) return RuntimeMove::Absent;
        return RuntimeMove::Failed;
    }
    if (!S_ISDIR(info.st_mode)) { errno = ENOTDIR; return RuntimeMove::Failed; }
    if (ps4KernelStat(newPath.c_str(), &info) == 0) return RuntimeMove::BothPresent;
    if (errno != ENOENT) return RuntimeMove::Failed;
    if (std::rename(oldPath.c_str(), newPath.c_str()) != 0) return RuntimeMove::Failed;
    return RuntimeMove::Moved;
}

bool createDirectories() {
    if (!makeDirectory(kDataRoot)) return false;
    reportBootStage("runtime: checking legacy directory (kernel stat, no lstat)");
    const RuntimeMove moved = moveRuntimeDirectory(kLegacyWritableRoot, kPreferredWritableRoot);
    if (moved == RuntimeMove::Failed || moved == RuntimeMove::BothPresent) {
        // Both trees may contain settings. Keep using the legacy tree in this
        // recovery build instead of silently choosing a different save/config.
        Ps4KernelStat oldInfo{};
        if (ps4KernelStat(kLegacyWritableRoot, &oldInfo) == 0 && S_ISDIR(oldInfo.st_mode)) {
            kWritableRoot = kLegacyWritableRoot;
            reportBootStage("runtime: preserving existing trees; using legacy directory for compatibility");
        } else reportBootStage("runtime: legacy directory unavailable; using WoWPS directory");
    } else reportBootStage(moved == RuntimeMove::Moved ? "runtime: legacy directory renamed to wowps" : "runtime: using wowps directory");
    const std::string root = kWritableRoot;
    bool ok = makeDirectory(root);
    for (const char* sub : {"logs", "cache", "saves", "config", "tmp"})
        ok = makeDirectory(root + "/" + sub) && ok;
    // These are optional caches/old desktop settings, not local realm saves.
    // On conflicts retain both versions for later explicit reconciliation.
    for (const auto& paths : {std::pair<std::string,std::string>{root+"/.wowee", root+"/.wowps"},
                             {root+"/.local/share/wowee", root+"/.local/share/wowps"}}) {
        const RuntimeMove result = moveRuntimeDirectory(paths.first, paths.second);
        if (result == RuntimeMove::Failed || result == RuntimeMove::BothPresent)
            reportBootStage("runtime: optional legacy subdirectory retained (no merge)");
    }
    return ok;
}

// The process starts with an empty environment on the console, and the client
// reads its paths and tuning from getenv. Every value here is set with
// overwrite=false: nothing sets them earlier today, but a loader that wants to
// stage the port differently can, and its value then wins over these.
struct EnvDefault {
    const char* name;
    std::string value;
    const char* why;
};

/// Load KEY=VALUE lines from a plain text file into the environment before
/// the hardcoded defaults below are applied, so a console can be tuned
/// (WOWEE_PS4_PIGLET_PATCH, WOWEE_PS4_PGL_*, WOWEE_LOG_LEVEL, cache sizes,
/// ...) by editing a file over FTP instead of rebuilding. Comments ('#' at
/// the start of a trimmed line) and blank lines are skipped; malformed lines
/// are skipped with a klog line rather than aborting the rest of the file.
/// Values already in the environment (a future loader that execve's us with
/// its own env) win, matching the overwrite=false convention below.
void loadEnvFile(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return;  // no such file is the common case: nothing to log
    char line[512];
    int lineNo = 0;
    int applied = 0;
    while (std::fgets(line, sizeof(line), f)) {
        ++lineNo;
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        size_t start = s.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        s = s.substr(start);
        if (s.empty() || s[0] == '#') continue;
        const size_t eq = s.find('=');
        if (eq == std::string::npos || eq == 0) {
            klog("%s:%d: expected KEY=VALUE, skipping", path.c_str(), lineNo);
            continue;
        }
        std::string key = s.substr(0, eq);
        std::string value = s.substr(eq + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        // New spelling for user-maintained config; existing tuning keys still work.
        if (key.rfind("WOWPS_",0)==0) key="WOWEE_"+key.substr(6);
        const std::string legacy="/data/wow_ps/wowee";
        if (value.rfind(legacy,0)==0 && (value.size()==legacy.size() || value[legacy.size()]=='/'))
            value=std::string(kWritableRoot)+value.substr(legacy.size());
        if (key=="WOWEE_LOG_FILE" && value=="wowee.log") value="wowps.log";
        core::setEnvVar(key.c_str(), value.c_str(), /*overwrite=*/false);
        ++applied;
    }
    std::fclose(f);
    klog("environment: %d setting(s) read from %s", applied, path.c_str());
}

void setEnvironment() {
    const std::string writable = kWritableRoot;
    loadEnvFile(writable + "/config/env.txt");
    const EnvDefault defaults[] = {
        // ---- paths ---------------------------------------------------------
        {"WOW_DATA_PATH", kDataRoot,
         "Application::initialize/ExpansionRegistry: the extracted client tree "
         "(manifest.json at the root or under expansions/*/); the fallback is "
         "./Data, and the working directory here holds nothing of ours"},
        {"TMPDIR", writable + "/tmp",
         "std::filesystem::temp_directory_path (libc++ tries $TMPDIR, $TMP, "
         "$TEMP, $TEMPDIR, then /tmp and throws filesystem_error when none is "
         "a directory) and platform::getTempFilePath: the console sandbox has "
         "no /tmp, which ended the first B3 launch with audio right after "
         "AudioEngine initialized"},
        {"HOME", writable,
         "config_paths perUserConfigDir ($HOME/.wowps), the warden cache "
         "($HOME/.local/share/wowps), social/world-loader state; there is no "
         "HOME on the console otherwise"},
        {"WOWEE_CONFIG_ROOT", writable + "/config",
         "core::getConfigRoot: settings and saved variables, and the logger's "
         "fallback directory, under the writable tree"},
        {"WOWEE_RESOURCE_ROOT", kAppRoot,
         "absolute package file resolution; PS4 does not change working directory"},

        // ---- logging -------------------------------------------------------
        {"WOWEE_LOG_LEVEL", "info",
         "main.cpp defaults to WARNING; on a console the klog and the log file "
         "are the only diagnostics, so keep the startup INFO lines"},
        {"WOWEE_LOG_STDOUT", "0",
         "retain file/boot diagnostics without a second synchronous kernel write per line"},
        {"WOWEE_LOG_FLUSH_MS", "250",
         "batch routine file diagnostics; errors and fatal records still flush immediately"},

        // ---- threads: 6 usable cores, the main thread and the audio/network
        //      pumps already take three. The desktop defaults derive from
        //      hardware_concurrency (terrain (hc-1)/2, M2 about half the rest).
        {"WOWEE_TERRAIN_WORKERS", "2",
         "terrain_manager computeTerrainWorkerCount: chunk loading workers"},
        {"WOWEE_M2_ANIM_THREADS", "1",
         "m2_renderer: bone animation worker threads (default hc/2)"},
        {"WOWEE_CHAR_ANIM_THREADS", "1",
         "character_renderer: same partitioning for character animation"},
        {"WOWEE_WMO_CULL_THREADS", "1",
         "wmo_renderer: culling workers (culling itself stays off, see "
         "WOWEE_WMO_CULL)"},

        // ---- memory: the desktop budgets are 4-8 GB per texture cache and
        //      up to 12 GB of file cache, sized from sysinfo. On the console
        //      malloc draws on the flexible-memory pool, about 448 MiB by
        //      default; the 4 GB reported by sceKernelGetDirectMemorySize is
        //      the GPU/direct pool the Vulkan allocator uses, not the CPU
        //      heap. The B3 values (512 MB each, five of them) exceeded the
        //      whole heap; these leave room for the world itself.
        {"WOWEE_CHARACTER_TEX_CACHE_MB", "96",  "character_renderer texture cache budget"},
        {"WOWEE_M2_TEX_CACHE_MB",        "128", "m2_renderer texture cache budget"},
        {"WOWEE_TERRAIN_TEX_CACHE_MB",   "128", "terrain_renderer texture cache budget"},
        {"WOWEE_WMO_TEX_CACHE_MB",       "96",  "wmo_renderer texture cache budget"},
        // No fixed file-cache size any more, deliberately.
        //
        // It was pinned at 32 MB because the budget used to be derived from
        // sysinfo(), which does not mean anything on this console. It is now
        // derived from queryAvailableCpuMemory - the console's own answer - and
        // regraded a few times a second, so the reason is gone.
        //
        // Leaving it set was actively harmful: WOWEE_FILE_CACHE_MB is treated
        // as an instruction rather than a recommendation, so it *disabled* that
        // regrading. A session would sit at 32 MB of cache while free memory
        // fell to 3 MiB, and the log said "measured headroom will not resize
        // it" right up to the std::bad_alloc.

        // ---- optional GPU work, off until proven on the real Vulkan/GNM ICD -
        // The shadow kill switch is no longer forced on.
        //
        // It was set because renderShadowPass had not been verified against
        // this ICD's depth images. The pass has since been rewritten: it always
        // begins, clears and transitions its map, and the quality level only
        // decides what is drawn into it - so level 0 is an empty shadow map
        // every reader can still sample, which is the case that used to lose
        // the device. There is now a Shadows row in the video options, and with
        // this forced on it could not do anything at all: the pass returned on
        // its first line whatever the player chose.
        //
        // Still available as an environment override for anyone who needs to
        // rule the pass out while diagnosing something else.
        //
        // The M2 particle kill switch is no longer forced on either, and none
        // of the three things it named still holds.
        //
        // "descriptors are allocated on worker threads" was true of one branch
        // of renderM2Particles - the fallback when a texture group had no
        // pre-allocated set. That branch is gone; a group without one is
        // skipped. It was also never reachable here: it runs while recording
        // the M2 secondary, and PS4 does not enable parallel recording at all
        // (ps4_vulkan implements no render-pass inheritance for secondaries,
        // see Renderer::initialize), so everything the particle path does has
        // always been on the main thread. The stable sets it uses are made in
        // loadModel, on the main thread, beside the ribbon sets that have been
        // shipping enabled the whole time.
        //
        // "until vkQueueSubmit ... is serialized": B39 did that.
        // vk_ps4_QueueSubmit is now a wrapper that holds vk_ps4_queue_lock
        // across the whole submission, and QueueWaitIdle and DeviceWaitIdle
        // hold it across their drains, so the GNM submission sequence, the
        // completion counter and the slot ring have one writer at a time.
        //
        // "pipeline-cache access from multiple threads": the particle path
        // touches no pipeline cache. Its two pipelines are built once, in
        // M2Renderer::initialize and recreatePipelines, both on the main
        // thread; nothing per frame reaches vkCreateGraphicsPipelines.
        //
        // The ICD's descriptor pool is guarded now as well (vk_ps4_pipeline.c),
        // so the remaining claim in that sentence is no longer true of the one
        // object the particle path allocates from.
        //
        // Still available as an override: WOWEE_M2_NO_PARTICLES=1 drops every
        // particle draw, which is what tells a particle artifact apart from a
        // skinned-geometry one.
    };

    std::string summary;
    for (const EnvDefault& d : defaults) {
        core::setEnvVar(d.name, d.value.c_str(), /*overwrite=*/false);
        if (!summary.empty()) summary += ' ';
        summary += d.name;
        summary += '=';
        summary += std::getenv(d.name) ? std::getenv(d.name) : "";
    }
    klog("environment: %s", summary.c_str());
    // The PS4 client uses the original 3.3.5a FrameXML interface exclusively.
    // The session marker remains diagnostic, but never silently selects the
    // incomplete native replacement after a previous failure.
    const char* experimentalUi = std::getenv("WOWPS_EXPERIMENTAL_FRAMEXML");
    if (!experimentalUi) experimentalUi = std::getenv("WOWEE_EXPERIMENTAL_FRAMEXML");
    const bool explicitRetry = experimentalUi && std::strcmp(experimentalUi, "1") == 0;
    const char* configuredRoot = std::getenv("WOWEE_CONFIG_ROOT");
    const auto marker = addons::frameXmlRecoveryPath(configuredRoot ? configuredRoot : writable + "/config");
    // Consume explicit recovery once at boot. A failure in this process still
    // blocks automatic retries when selecting a different character.
    if (explicitRetry)
        addons::writeFrameXmlRecovery(marker, "clean", "explicit original UI retry requested");
    const auto recovery = addons::readFrameXmlRecovery(marker);
    if (!recovery.allowsAttempt())
        reportBootStage("UI policy: previous FrameXML failure recorded; mandatory retry");
    addons::writeFrameXmlRecovery(marker, "clean", "mandatory FrameXML retry");
    core::setEnvVar("WOWEE_LOAD_FRAMEXML", "1", true);
    core::setEnvVar("WOWPS_REQUIRE_FRAMEXML", "1", true);
    reportBootStage("UI policy: original FrameXML mandatory; native fallback disabled");

}

bool initUserService() {
    OrbisUserServiceInitializeParams param{};
    param.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    int32_t rc = sceUserServiceInitialize(&param);
    if (rc != 0) {
        LOG_WARNING("sceUserServiceInitialize failed: ", hex(rc));
        return false;
    }
    int32_t userId = -1;
    rc = sceUserServiceGetInitialUser(&userId);
    if (rc != 0) {
        LOG_WARNING("sceUserServiceGetInitialUser failed: ", hex(rc));
    } else {
        char name[64] = {};
        if (sceUserServiceGetUserName(userId, name, sizeof(name) - 1) == 0) {
            LOG_INFO("PS4 user ", userId, " (", name, ")");
        } else {
            LOG_INFO("PS4 user ", userId);
        }
    }
    return true;
}

void initNetwork() {
    int32_t rc = sceNetInit();
    if (rc < 0) {
        LOG_WARNING("sceNetInit failed: ", hex(rc), " - host names will not resolve");
        return;
    }
    g_netInitialised = true;

    const int32_t pool = sceNetPoolCreate("wowps", kNetPoolBytes, 0);
    if (pool < 0) {
        LOG_WARNING("sceNetPoolCreate failed: ", hex(pool), " - host names will not resolve");
    } else {
        g_netPool.store(pool, std::memory_order_release);
    }

    rc = sceNetCtlInit();
    if (rc < 0) {
        LOG_WARNING("sceNetCtlInit failed: ", hex(rc));
        return;
    }
    g_netCtlInitialised = true;

    // Purely informative: the state and address, so a log from a console with
    // no cable says so in its first lines.
    int32_t state = -1;
    if (sceNetCtlGetState(&state) == 0) {
        static const char* const kStates[] = {"disconnected", "connecting", "obtaining IP", "IP obtained"};
        const char* text = (state >= 0 && state < 4) ? kStates[state] : "unknown";
        OrbisNetCtlInfo info{};
        if (state == 3 && sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info) == 0) {
            LOG_INFO("Network: ", text, "; address hidden");
        } else {
            LOG_INFO("Network: ", text);
        }
    }
}

}  // namespace

// ---- public API --------------------------------------------------------------

/// core::LogLevel from WOWEE_LOG_LEVEL, mirroring main.cpp's
/// readLogLevelFromEnv (private to that file, so duplicated here rather than
/// exposed just for this). Applied right after setEnvironment() below so
/// that initSystem()'s own LOG_INFO calls - the data/writable/app roots, the
/// PS4 user and network state - are not silently dropped: main.cpp only
/// calls Logger::setLogLevel after initSystem() has already returned, and a
/// Release build's default level is WARNING until then.
core::LogLevel logLevelFromEnv() {
    const char* raw = std::getenv("WOWEE_LOG_LEVEL");
    if (!raw || !*raw) return core::LogLevel::WARNING;
    std::string level(raw);
    for (char& c : level) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (level == "debug") return core::LogLevel::DEBUG;
    if (level == "info") return core::LogLevel::INFO;
    if (level == "warn" || level == "warning") return core::LogLevel::WARNING;
    if (level == "error") return core::kLogLevelError;
    if (level == "fatal") return core::LogLevel::FATAL;
    return core::LogLevel::WARNING;
}

bool initSystem() {
    if (g_initialised) return true;
    g_initialised = true;

    klog("initSystem");

    // Directories and environment before the first LOG_* call: the logger
    // opens its file on first use, in writableRoot()/logs, and reads
    // WOWEE_LOG_* from the environment as it does so.
    // Bootstrap log must exist before any migration or optional modules.
    // Keep this fd in the signal reporter if normal log opening fails.
    if (::mkdir(kDataRoot, 0777) == 0 || errno == EEXIST) {
        std::rename("/data/wow_ps/boot_startup.log", "/data/wow_ps/boot_startup_previous.log");
        g_bootFd = ::open("/data/wow_ps/boot_startup.log", O_CREAT | O_WRONLY | O_TRUNC | O_APPEND, 0666);
    }
    reportBootStage("B25: early startup diagnostics before runtime migration");
    reportCrashReporterState();
    const bool dirsOk = createDirectories();
    if (!dirsOk) {
        reportBootStage("runtime: required directory creation failed; see boot_startup.log");
        return false;
    }
    // Start diagnostics before calling into any of the optional system
    // modules. A crash in bring-up must still identify the last stage.
    const std::string bootPath = std::string(kWritableRoot) + "/logs/boot.log";
    const std::string previousPath = std::string(kWritableRoot) + "/logs/boot_previous.log";
    std::rename(bootPath.c_str(), previousPath.c_str());
    const int normalBootFd = ::open(bootPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_APPEND, 0666);
    if (normalBootFd >= 0) g_bootFd = normalBootFd; // Bootstrap fd stays open for signal safety.
    reportBootStage("platform: begin");
    // The first install preceded creation of boot.log. Persist its result
    // now, including the fallback case, before any module initialization.
    reportCrashReporterState();
    setEnvironment();
    // A Release build's Logger defaults to WARNING (see logger.hpp); without
    // this, every LOG_INFO below - and main.cpp's own "=== Wowee Native
    // Client ===" banner, which logs before its own setLogLevel call in a
    // release build - would be silently dropped from the log file the
    // console ships back for diagnosis. main.cpp still calls setLogLevel
    // again after initSystem() returns, in case something set
    // WOWEE_LOG_LEVEL later than this.
    core::Logger::getInstance().setLogLevel(logLevelFromEnv());

    reportBootStage("platform: loading system modules");
    const bool modulesOk = loadModules();

    if (!dirsOk) {
        klog("warning: the writable tree under %s could not be created; the log and settings will not persist", kWritableRoot);
    }
    if (!modulesOk) {
        klog("error: a required system module failed to load");
        reportBootStage("platform: required module failed");
        return false;
    }

    LOG_INFO("PS4 platform: data ", dataRoot(), ", writable ", writableRoot(), ", app ", appRoot());
    // The toolchain's struct stat is not the kernel's (ps4/compat/ps4_kstat.h);
    // say so in the log whenever it shows, so the next mismatch is found in
    // a log rather than in a week of archive-open failures.
    {
        const std::string probe = std::string(kAppRoot) + "/eboot.bin";
        struct stat toolchainStat{};
        struct Ps4KernelStat kernelStat{};
        long long seekEnd = -1;
        if (std::FILE* f = std::fopen(probe.c_str(), "rb")) {
            if (std::fseek(f, 0, SEEK_END) == 0) seekEnd = static_cast<long long>(std::ftell(f));
            std::fclose(f);
        }
        if (::stat(probe.c_str(), &toolchainStat) == 0 && ps4KernelStat(probe.c_str(), &kernelStat) == 0) {
            LOG_INFO("stat layout check on ", probe, ": toolchain st_size=", static_cast<long long>(toolchainStat.st_size),
                     " kernel-layout st_size=", static_cast<long long>(kernelStat.st_size), " seek-to-end=", seekEnd,
                     (seekEnd == static_cast<long long>(kernelStat.st_size)) ? " (kernel layout agrees with seek)" : " (MISMATCH: neither layout matches seek)");
        }
    }

    // Keep the package splash until a frame or a system dialog is ready.
    reportBootStage("platform: initializing user service");
    g_userServiceReady = initUserService();
    reportBootStage("platform: initializing network");
    initNetwork();
    reportBootStage("platform: ready");
    return true;
}

void hideSplashScreen() {
    if (!g_systemServiceLoaded || g_splashHidden) return;
    const int32_t rc = sceSystemServiceHideSplashScreen();
    if (rc < 0) LOG_WARNING("sceSystemServiceHideSplashScreen failed: ", hex(rc));
    else {
        g_splashHidden = true;
        reportBootStage("display: splash dismissed, frame or dialog ready");
    }
}

static bool frameTrace = false;
void setFrameTraceEnabled(bool enabled) { frameTrace = enabled; }
bool frameTraceEnabled() { return frameTrace; }

void reportBootStage(const char* stage) {
    if (!stage) return;
    // Use the kernel's elapsed microseconds directly: the supplied libc++
    // forwards Linux CLOCK_MONOTONIC=1 to native clock_gettime without an ID
    // translation, which produced +0ms throughout an 18-second B1 session.
    static const uint64_t started = sceKernelGetProcessTime();
    const uint64_t now = sceKernelGetProcessTime();
    const uint64_t milliseconds = now >= started ? (now - started) / 1000 : 0;
    char line[512];
    const int n = std::snprintf(line, sizeof(line), "[wow_ps boot +%lldms] %s\n",
                               static_cast<long long>(milliseconds), stage);
    if (n <= 0) return;
    const size_t length = static_cast<size_t>(n) < sizeof(line)
        ? static_cast<size_t>(n) : sizeof(line) - 1;
    if (g_bootFd >= 0) {
        // A single append means another logging thread cannot splice its
        // output into the middle of this checkpoint.
        const ssize_t ignored = ::write(g_bootFd, line, length);
        (void)ignored;
    }
    std::fwrite(line, 1, length, stdout);
    std::fflush(stdout);
}

namespace {

// Threads that asked to be named in a crash marker. Written only by
// registerCrashReportingThread (under a spin on the count), read by the
// signal handler; the array never shrinks, so a reader can only see a
// complete entry or none.
struct NamedThread {
    pthread_t id;
    std::atomic<const char*> name{nullptr};
    void* altStack;
};
constexpr unsigned kMaxNamedThreads = 16;
NamedThread g_namedThreads[kMaxNamedThreads];
std::atomic<unsigned> g_namedThreadCount{0};
static_assert(std::atomic<unsigned>::is_always_lock_free,
              "Crash reporting must not acquire atomic-library locks");
static_assert(std::atomic<const char*>::is_always_lock_free,
              "Crash thread names must be read without library locks");
std::atomic_flag g_namedThreadRegistration = ATOMIC_FLAG_INIT;
std::atomic_flag g_terminateReporting = ATOMIC_FLAG_INIT;
constexpr size_t kAltStackBytes = 64 * 1024;

// Async-signal-safe string builder: no locale, no allocation, no stdio.
struct CrashLine {
    char buf[512];
    size_t len = 0;
    void put(const char* s) {
        while (s && *s && len + 1 < sizeof(buf)) buf[len++] = *s++;
    }
    void hex(uintptr_t v) {
        char tmp[2 + sizeof(uintptr_t) * 2 + 1];
        size_t n = 0;
        tmp[n++] = '0';
        tmp[n++] = 'x';
        bool started = false;
        for (int shift = static_cast<int>(sizeof(uintptr_t) * 8) - 4; shift >= 0; shift -= 4) {
            const unsigned nibble = static_cast<unsigned>((v >> shift) & 0xFu);
            if (!started && nibble == 0 && shift != 0) continue;
            started = true;
            tmp[n++] = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
        }
        tmp[n] = '\0';
        put(tmp);
    }
    void dec(long v) {
        char tmp[24];
        size_t n = 0;
        if (v < 0) { put("-"); v = -v; }
        do { tmp[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v > 0 && n < sizeof(tmp) - 1);
        while (n > 0 && len + 1 < sizeof(buf)) buf[len++] = tmp[--n];
    }
};

const char* signalName(int signal) {
    switch (signal) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGFPE: return "SIGFPE";
        case SIGILL: return "SIGILL";
        case SIGBUS: return "SIGBUS";
        default: return "fatal signal";
    }
}

void writeCrashLine(const CrashLine& line) {
    if (g_bootFd >= 0) {
        const ssize_t ignored = ::write(g_bootFd, line.buf, line.len);
        (void)ignored;
    }
    const ssize_t ignored2 = ::write(STDOUT_FILENO, line.buf, line.len);
    (void)ignored2;
}

void appendCrashThread(CrashLine& line) {
    const pthread_t self = pthread_self();
    const unsigned count = g_namedThreadCount.load(std::memory_order_acquire);
    const char* name = nullptr;
    for (unsigned i = 0; i < count && i < kMaxNamedThreads; ++i) {
        if (pthread_equal(g_namedThreads[i].id, self)) {
            name = g_namedThreads[i].name.load(std::memory_order_acquire);
            break;
        }
    }
    line.put(" thread=");
    line.put(name ? name : "unregistered");
    if (!name) {
        line.put(" id=");
        line.hex(reinterpret_cast<uintptr_t>(self));
    }
}

[[noreturn]] void crashTerminate() noexcept {
    // C++ termination is distinct from an explicit abort/assert. The ABI
    // accessor is declared by this toolchain's cxxabi.h and exported by its
    // libc++abi.a; it observes the current exception without allocating or
    // rethrowing it. Keep mangled type names to avoid allocating a demangle.
    if (g_terminateReporting.test_and_set(std::memory_order_relaxed)) std::_Exit(128 + SIGABRT);
    CrashLine line;
    line.put("[wow_ps terminate]");
    appendCrashThread(line);
    line.put(" active_exception_type=");
    const auto* type = __cxxabiv1::__cxa_current_exception_type();
    line.put(type ? type->name() : "none");
    line.put("\n");
    writeCrashLine(line);
    std::abort(); // Preserve fatal termination and the kernel crash report.
}

void crashSigaction(int signal, siginfo_t* info, void* context) {
    CrashLine line;
    line.put("[wow_ps crash] ");
    line.put(signalName(signal));
    if (info) {
        line.put(" code=");
        line.dec(info->si_code);
        line.put(" addr=");
        line.hex(reinterpret_cast<uintptr_t>(info->si_addr));
    }
    InterruptedRegisters registers;
    const bool haveRegisters = decodeKernelSignalContext(
        context, kKernelSignalContextPrefixBytes, registers);
    if (haveRegisters) {
        line.put(" rip="); line.hex(registers.rip);
        line.put(" rsp="); line.hex(registers.rsp);
        line.put(" rbp="); line.hex(registers.rbp);
        const uintptr_t textBase = reinterpret_cast<uintptr_t>(__text_start);
        const uintptr_t textEnd = reinterpret_cast<uintptr_t>(__eh_frame_start);
        line.put(" eboot_base="); line.hex(textBase);
        if (registers.rip >= textBase && registers.rip < textEnd) {
            // __text_start has ELF VMA 0 in the project's OpenOrbis link.x;
            // this offset can be passed to addr2line with the matching ELF.
            line.put(" eboot_pc="); line.hex(registers.rip - textBase);
        }
    } else {
        line.put(" context=unavailable");
    }
    // This remains the reporter's alternate stack, distinct from rsp above.
    int marker = 0;
    line.put(" handler_sp~");
    line.hex(reinterpret_cast<uintptr_t>(&marker));
    appendCrashThread(line);
    line.put("\n");
    writeCrashLine(line);
    if (haveRegisters) {
        CrashLine detail;
        detail.put("[wow_ps registers] trap="); detail.dec(registers.trap);
        detail.put(" err="); detail.hex(registers.error);
        detail.put(" fault="); detail.hex(registers.faultAddress);
        detail.put(" rax="); detail.hex(registers.rax);
        detail.put(" rbx="); detail.hex(registers.rbx);
        detail.put(" rdi="); detail.hex(registers.rdi);
        detail.put(" rsi="); detail.hex(registers.rsi);
        detail.put(" rdx="); detail.hex(registers.rdx);
        detail.put(" rcx="); detail.hex(registers.rcx);
        detail.put(" r8="); detail.hex(registers.r8);
        detail.put(" r9="); detail.hex(registers.r9);
        detail.put(" r10="); detail.hex(registers.r10);
        detail.put(" r11="); detail.hex(registers.r11);
        detail.put(" r12="); detail.hex(registers.r12);
        detail.put(" r13="); detail.hex(registers.r13);
        detail.put(" r14="); detail.hex(registers.r14);
        detail.put(" r15="); detail.hex(registers.r15);
        detail.put("\n");
        writeCrashLine(detail);
    }
    // SA_RESETHAND restored the default disposition: returning re-executes
    // the faulting instruction and the system's own crash report follows.
    if (signal == SIGABRT) {
        ::raise(SIGABRT);
    }
}

bool installAltStackForCurrentThread(const char* name) {
    // Registration can run concurrently in the audio/terrain workers. The
    // signal handler only reads published entries and never takes this lock.
    while (g_namedThreadRegistration.test_and_set(std::memory_order_acquire)) {}
    struct RegistrationGuard {
        ~RegistrationGuard() { g_namedThreadRegistration.clear(std::memory_order_release); }
    } registrationGuard;
    const pthread_t self = pthread_self();
    unsigned count = g_namedThreadCount.load(std::memory_order_acquire);
    for (unsigned i = 0; i < count && i < kMaxNamedThreads; ++i) {
        // A new worker may reuse a departed worker's pthread ID. Its signal
        // stack does not survive thread exit, so reapply the retained buffer.
        if (pthread_equal(g_namedThreads[i].id, self)) {
            if (ps4KernelSigaltstack(g_namedThreads[i].altStack, kAltStackBytes) != 0) return false;
            g_namedThreads[i].name.store(name ? name : "unnamed", std::memory_order_release);
            return true;
        }
    }
    if (count >= kMaxNamedThreads) return false;
    void* stack = std::malloc(kAltStackBytes);
    if (!stack) return false;
    const int rc = ps4KernelSigaltstack(stack, kAltStackBytes);
    if (rc != 0) {
        std::free(stack);
        return false;
    }
    g_namedThreads[count].id = self;
    g_namedThreads[count].altStack = stack;
    g_namedThreads[count].name.store(name ? name : "unnamed", std::memory_order_relaxed);
    g_namedThreadCount.store(count + 1, std::memory_order_release);
    return true;
}

void reportCrashReporterState() {
    const bool ok = g_crashReporterAltStack && g_crashReporterSignalMask == 0x1fu;
    reportBootStage("01.90: action cursor, combat targeting, mailbox sites and cinematic streaming, save19 and LAN34");
    reportBootStage("memory: realloc shrink guard active; bounded copy, retain on allocation failure");
    reportBootStage("memory: heap arena growth reserve capped at 2 MiB; 16 KiB page fallback; native allocator bins retained");
    reportBootStage(ok ? "platform: crash reporter installed (alt stack, siginfo); terminate diagnostic active"
                       : "platform: kernel crash reporter incomplete; plain-signal fallback requested");
    char line[144];
    std::snprintf(line, sizeof(line), "platform: crash reporter altStack=%u signalMask=0x%x; kernel default thread stack=%zu KiB",
                  g_crashReporterAltStack ? 1u : 0u, g_crashReporterSignalMask, defaultThreadStackBytes() / 1024u);
    reportBootStage(line);
}

} // namespace

void reportCrashSignal(int signal) {
    CrashLine line;
    line.put("[wow_ps crash] ");
    line.put(signalName(signal));
    line.put("\n");
    writeCrashLine(line);
}

bool installCrashReporter() {
    const bool altStack = installAltStackForCurrentThread("main");
    std::set_terminate(&crashTerminate);
    g_crashReporterAltStack = altStack;
    g_crashReporterSignalMask = 0;
    bool ok = altStack;
    const int signals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};
    for (unsigned i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        if (ps4KernelSigaction(signals[i], &crashSigaction, PS4_SA_RESETHAND | PS4_SA_NODEFER) != 0) {
            ok = false;
        } else g_crashReporterSignalMask |= 1u << i;
    }
    reportCrashReporterState();
    return ok;
}

void registerCrashReportingThread(const char* name) {
    if (!installAltStackForCurrentThread(name)) {
        CrashLine line;
        line.put("[wow_ps boot] crash thread registration failed: ");
        line.put(name ? name : "unnamed");
        line.put("\n");
        writeCrashLine(line);
    }
}

void showStartupError(const char* message) {
    reportBootStage(message ? message : "startup failed");
    if (!g_messageDialogLoaded) return;
    sceCommonDialogInitialize();
    if (sceMsgDialogInitialize() < 0) return;
    // Keep this independent of Vulkan: initialization may have failed before
    // a device existed, or a shader compile may have failed. OK dismisses the
    // error immediately; the timeout also handles an unavailable controller.
    const std::string text = std::string("WoWPS encountered an error.\n") +
        (message ? message : "Initialization failed.") +
        "\nLogs: /data/wow_ps/wowps/logs/\nReturning to the system in 20 seconds.";
    OrbisMsgDialogUserMessageParam userMessage{};
    userMessage.buttonType = ORBIS_MSG_DIALOG_BUTTON_TYPE_OK;
    userMessage.msg = text.c_str();
    OrbisMsgDialogParam param{};
    param.baseParam.size = sizeof(OrbisCommonDialogBaseParam);
    param.baseParam.magic = static_cast<uint32_t>(
        ORBIS_COMMON_DIALOG_MAGIC_NUMBER + reinterpret_cast<uint64_t>(&param.baseParam));
    param.size = sizeof(param);
    param.mode = ORBIS_MSG_DIALOG_MODE_USER_MSG;
    param.userMsgParam = &userMessage;
    if (sceMsgDialogOpen(&param) == 0) {
        hideSplashScreen();
        for (int i = 0; i < 200; ++i) {
            pumpSystemEvents();
            if (sceMsgDialogUpdateStatus() == ORBIS_COMMON_DIALOG_STATUS_FINISHED) break;
            ::usleep(100000);
        }
        sceMsgDialogClose();
        for (int i = 0; i < 30; ++i) {
            pumpSystemEvents();
            if (sceMsgDialogUpdateStatus() == ORBIS_COMMON_DIALOG_STATUS_FINISHED) break;
            ::usleep(100000);
        }
    }
    sceMsgDialogTerminate();
}

void shutdownSystem() {
    if (!g_initialised) return;
    g_initialised = false;

    LOG_INFO("PS4 platform: shutting down");
    reportBootStage("platform: shutdown");

    if (g_netCtlInitialised) {
        reportBootStage("shutdown: netctl terminate");
        sceNetCtlTerm();
        g_netCtlInitialised = false;
    }
    const int32_t pool = g_netPool.exchange(-1, std::memory_order_acq_rel);
    if (pool >= 0) {
        reportBootStage("shutdown: net pool destroy");
        sceNetPoolDestroy(pool);
    }
    if (g_netInitialised) {
        reportBootStage("shutdown: net terminate");
        sceNetTerm();
        g_netInitialised = false;
    }
    if (g_userServiceReady) {
        reportBootStage("shutdown: user service terminate");
        sceUserServiceTerminate();
        g_userServiceReady = false;
    }
    // The system modules stay loaded: the process is about to exit, and the
    // piglet sample notes the system service needs no unload.
    reportBootStage("shutdown: platform complete");
    klog("shutdownSystem done");
}

std::string dataRoot() {
    const char* configured = std::getenv("WOW_DATA_PATH");
    return configured && *configured ? configured : kDataRoot;
}
std::string writableRoot() { return kWritableRoot; }
std::string appRoot() { return kAppRoot; }

uint32_t resolveIPv4(const std::string& host) {
    if (host.empty()) return 0;

    // A dotted quad needs no resolver, and the auth screen's default realm
    // entries are frequently one.
    in_addr literal{};
    if (::inet_pton(AF_INET, host.c_str(), &literal) == 1) {
        return static_cast<uint32_t>(literal.s_addr);
    }

    const int32_t pool = g_netPool.load(std::memory_order_acquire);
    if (pool < 0) {
        LOG_ERROR("Cannot resolve ", host, ": networking is not initialised");
        return 0;
    }

    const OrbisNetId rid = sceNetResolverCreate("wowps", pool, 0);
    if (rid < 0) {
        LOG_ERROR("sceNetResolverCreate failed: ", hex(rid));
        return 0;
    }
    OrbisNetInAddr addr{};
    // timeout 0 / retry 0: the library's defaults (a few seconds, a few
    // retries), the same values the homebrew network tools pass.
    const int32_t rc = sceNetResolverStartNtoa(rid, host.c_str(), &addr, 0, 0, 0);
    sceNetResolverDestroy(rid);
    if (rc < 0) {
        LOG_ERROR("sceNetResolverStartNtoa(", host, ") failed: ", hex(rc));
        return 0;
    }
    // s_addr is already in network byte order, as the contract asks.
    return addr.s_addr;
}

void pumpSystemEvents() {
    if (!g_systemServiceLoaded) return;
    // 8 KiB; static rather than on the frame's stack. Main thread only.
    static OrbisSystemServiceEvent event;
    for (int i = 0; i < kMaxEventsPerPump; ++i) {
        const int32_t rc = sceSystemServiceReceiveEvent(&event);
        // kSystemServiceErrorNoEvent is the normal end of the drain; any other
        // error is not worth a log line per frame either.
        if (rc == kSystemServiceErrorNoEvent || rc < 0) break;
        // Events (suspend/resume, PS button, gesture, ...) are drained and
        // ignored: draining is what keeps the shell happy, and nothing in the
        // client acts on them yet.
    }
}

}  // namespace ps4
}  // namespace platform
}  // namespace wowee

// ---------------------------------------------------------------------------
// The OpenOrbis libc++abi has no __cxa_thread_atexit_impl, which clang emits
// for thread_local objects with non-trivial destructors (WoWee uses a few
// thread_local std::string / std::vector). Registering nothing means those
// destructors never run at thread exit; the objects are leaked, which is
// harmless for a process that lives until the console kills it.
extern "C" int __cxa_thread_atexit_impl(void (*)(void*), void*, void*) {
    return 0;
}
