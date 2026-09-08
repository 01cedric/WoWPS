#include "core/application.hpp"
#include "core/config_paths.hpp"
#include "core/logger.hpp"
#include <exception>
#include <csignal>
#include <cstdlib>
#include "core/env.hpp"
#include <cctype>
#include <filesystem>
#include <string>
#include <SDL2/SDL.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef WOWEE_PS4
#include "platform/ps4/ps4_platform.hpp"
#endif

// backtrace(3) and friends live in libSystem on macOS and in glibc on Linux, so
// the useful crash report - faulting address, symbolized frames, a copy in the
// crash log - works the same on both. It used to be guarded on __linux__ alone,
// which left a macOS crash with no backtrace and no log at all. Only the X11
// mouse ungrab below is genuinely Linux-specific.
#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#include <libgen.h>
#include <cstdio>
#include <cstring>
// Bionic ships execinfo.h but declares backtrace() only from API 33, and this
// build targets lower. The crash log keeps everything but its stack section on
// Android, where logcat carries a native trace anyway.
#if !defined(__ANDROID__)
#define WOWEE_HAS_BACKTRACE 1
#include <execinfo.h>
#endif
#endif

// Android defines __linux__ and has no X11, so the mouse-ungrab path is gated
// on both. The other Linux branches in this file - /proc/self/exe, the
// backtrace - are correct there and are left alone.
#if defined(__linux__) && !defined(__ANDROID__)
#include <X11/Xlib.h>

// Keep a persistent X11 connection for emergency mouse release in signal handlers.
// XOpenDisplay inside a signal handler is unreliable, so we open it once at startup.
static Display* g_emergencyDisplay = nullptr;

static void releaseMouseGrab() {
    if (g_emergencyDisplay) {
        XUngrabPointer(g_emergencyDisplay, CurrentTime);
        XUngrabKeyboard(g_emergencyDisplay, CurrentTime);
        XFlush(g_emergencyDisplay);
    }
}
#else
static void releaseMouseGrab() {}
#endif

#ifdef WOWEE_HAS_BACKTRACE
static void crashHandlerSigaction(int sig, siginfo_t* info, void* /*ucontext*/) {
    releaseMouseGrab();
    void* frames[64];
    int n = backtrace(frames, 64);
    const char* sigName = (sig == SIGSEGV) ? "SIGSEGV" :
                          (sig == SIGABRT) ? "SIGABRT" :
                          (sig == SIGFPE)  ? "SIGFPE"  : "UNKNOWN";
    void* faultAddr = info ? info->si_addr : nullptr;
    fprintf(stderr, "\n=== CRASH: signal %s (%d) faultAddr=%p ===\n",
            sigName, sig, faultAddr);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    FILE* f = fopen("/tmp/wowps_debug.log", "a");
    if (f) {
        fprintf(f, "\n=== CRASH: signal %s (%d) faultAddr=%p ===\n",
                sigName, sig, faultAddr);
        fflush(f);
        backtrace_symbols_fd(frames, n, fileno(f));
        fclose(f);
    }
    // Re-raise with default handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigaction(sig, &sa, nullptr);
    raise(sig);
}
#else
static void crashHandler(int sig) {
#ifdef WOWEE_PS4
    wowee::platform::ps4::reportCrashSignal(sig);
#endif
    releaseMouseGrab();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
#endif

static wowee::core::LogLevel readLogLevelFromEnv() {
    const char* raw = std::getenv("WOWEE_LOG_LEVEL");
    if (!raw || !*raw) return wowee::core::LogLevel::WARNING;
    std::string level(raw);
    for (char& c : level) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (level == "debug") return wowee::core::LogLevel::DEBUG;
    if (level == "info") return wowee::core::LogLevel::INFO;
    if (level == "warn" || level == "warning") return wowee::core::LogLevel::WARNING;
    if (level == "error") return wowee::core::kLogLevelError;
    if (level == "fatal") return wowee::core::LogLevel::FATAL;
    return wowee::core::LogLevel::WARNING;
}

#ifdef __APPLE__
static void selectMacUserDataPath() {
    if (std::getenv("WOW_DATA_PATH")) return;

    const char* home = std::getenv("HOME");
    if (!home || !*home) return;

    namespace fs = std::filesystem;
    const fs::path dataRoot = fs::path(home) / "Library/Application Support/Wowee/Data";
    std::error_code ec;
    bool hasManifest = fs::is_regular_file(dataRoot / "manifest.json", ec);

    const fs::path expansions = dataRoot / "expansions";
    if (!hasManifest && fs::is_directory(expansions, ec)) {
        for (fs::directory_iterator it(expansions, ec), end; it != end && !ec; it.increment(ec)) {
            if (fs::is_regular_file(it->path() / "manifest.json", ec)) {
                hasManifest = true;
                break;
            }
        }
    }

    if (hasManifest) {
        wowee::core::setEnvVar("WOW_DATA_PATH", dataRoot.c_str(), /*overwrite=*/false);
    }
}
#endif

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifdef WOWEE_PS4
    // Lives outside the try block: error dialogs in the catch clauses still
    // need the user/system services that this guard tears down.
    struct Ps4SystemGuard {
        ~Ps4SystemGuard() { wowee::platform::ps4::shutdownSystem(); }
    } ps4SystemGuard;
#endif
    try {
#ifdef WOWEE_PS4
    // Install the small signal-safe reporter before platform/module bring-up.
    // No logger locks or C++ allocation are touched inside the handler.
    // Through the kernel's own sigaction ABI, on an alternate stack: the
    // toolchain's signal() leaves a stack overflow with no marker at all.
    // Falls back to the plain handlers if the kernel refuses.
    if (!wowee::platform::ps4::installCrashReporter()) {
        std::signal(SIGSEGV, crashHandler);
        std::signal(SIGABRT, crashHandler);
        std::signal(SIGFPE, crashHandler);
        std::signal(SIGILL, crashHandler);
        std::signal(SIGBUS, crashHandler);
    }
    // The console starts the process with an empty environment and a working
    // directory that holds nothing of ours, so the platform layer goes first:
    // it loads the system modules, sets WOW_DATA_PATH, HOME and the WOWEE_*
    // tuning knobs and creates the writable tree - all of which
    // readLogLevelFromEnv, the logger and Application::initialize read below.
    // The guard runs shutdownSystem on every way out of main, the exception
    // paths included.
    //
    // The target defines __FreeBSD__, not __linux__, so the X11 mouse-ungrab,
    // /proc/self/exe chdir and desktop backtrace blocks below are skipped.
    // Keep the kernel siginfo/alternate-stack handlers installed above.
    if (!wowee::platform::ps4::initSystem()) {
        wowee::platform::ps4::showStartupError("A required PS4 system module failed to initialize.");
        return 1;
    }
#endif
#if defined(__ANDROID__) || defined(WOWEE_PS4)
    // Android anchors its working directory. PS4 verifies actual absolute
    // package reads instead: chdir("/app0") is unsupported even for a healthy
    // install and must not prevent the client from starting.
    if (!wowee::core::enterResourceRoot()) {
#ifdef WOWEE_PS4
        wowee::platform::ps4::showStartupError("Required package files are missing or unreadable. Check wowps.log for the exact paths.");
#endif
        return 1;
    }
#endif
#ifndef _WIN32
    // Writing to a socket the server has already closed raises SIGPIPE, whose
    // default action is to terminate - the client would vanish mid-frame with
    // no log line and no crash report, because SIGPIPE is not one of the
    // signals handled below. Ignoring it makes send() answer EPIPE instead,
    // which the send paths already handle: they log the failure and stop
    // writing, and the recv side sees the closed connection and disconnects.
    //
    // Done process-wide rather than per-socket: the flag that suppresses this
    // at the call site is spelled differently on each platform (MSG_NOSIGNAL
    // on Linux, the SO_NOSIGPIPE socket option on macOS/BSD), and nothing here
    // wants SIGPIPE for anything.
    std::signal(SIGPIPE, SIG_IGN);
#endif
#if defined(__linux__) && !defined(__ANDROID__)
    g_emergencyDisplay = XOpenDisplay(nullptr);
#endif
#ifdef WOWEE_HAS_BACKTRACE
    // Use sigaction for SIGSEGV/SIGABRT/SIGFPE to get si_addr (faulting address)
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = crashHandlerSigaction;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
        sigaction(SIGFPE,  &sa, nullptr);
    }
    std::signal(SIGTERM, [](int) { std::_Exit(1); });
    std::signal(SIGINT,  [](int) { std::_Exit(1); });
#elif defined(WOWEE_PS4)
    // Reinstalling plain handlers here discards SA_SIGINFO and SA_ONSTACK;
    // B10 consequently recorded only SIGABRT and lost the failing thread.
    // Fatal signals already have the kernel reporter (or its early fallback).
    std::signal(SIGTERM, crashHandler);
    std::signal(SIGINT,  crashHandler);
#elif defined(__ANDROID__)
    // Android already has a crash reporter, and it is a far better one than
    // this: debuggerd writes a symbolised tombstone and the abort message to
    // logcat. Ours writes a backtrace to stderr, which on Android goes nowhere,
    // and installing it costs the tombstone. So only the two exit signals here.
    std::signal(SIGTERM, crashHandler);
    std::signal(SIGINT,  crashHandler);
#else
    std::signal(SIGSEGV, crashHandler);
    std::signal(SIGABRT, crashHandler);
    std::signal(SIGFPE,  crashHandler);
    std::signal(SIGTERM, crashHandler);
    std::signal(SIGINT,  crashHandler);
#endif
    // Change working directory so relative asset paths resolve from any launch
    // location. A signed macOS bundle keeps data in Contents/Resources because
    // Contents/MacOS may contain code only.
#ifdef __APPLE__
    {
        uint32_t bufSize = 0;
        _NSGetExecutablePath(nullptr, &bufSize);
        std::string exePath(bufSize, '\0');
        _NSGetExecutablePath(exePath.data(), &bufSize);
        const std::filesystem::path executableDir =
            std::filesystem::path(exePath.c_str()).parent_path();
        const std::filesystem::path resourceDir =
            executableDir.parent_path() / "Resources";
        const std::filesystem::path runtimeDir =
            std::filesystem::is_directory(resourceDir / "assets")
                ? resourceDir
                : executableDir;
        if (chdir(runtimeDir.c_str()) != 0) {}
    }
    selectMacUserDataPath();
#elif defined(__linux__)
    {
        char buf[4096];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) { buf[len] = '\0'; if (chdir(dirname(buf)) != 0) {} }
    }
#endif

        wowee::core::Logger::getInstance().setLogLevel(readLogLevelFromEnv());
        LOG_INFO("=== wow_ps Native Client ===");
        LOG_INFO("Starting application...");
#ifdef WOWEE_PS4
        wowee::platform::ps4::reportBootStage("application: constructing");
#endif

        // Seed portable config from the per-user location on first portable launch.
        wowee::core::migratePortableConfigIfNeeded();

        wowee::core::Application app;

#ifdef WOWEE_PS4
        wowee::platform::ps4::reportBootStage("application: initializing");
#endif
        if (!app.initialize()) {
            LOG_FATAL("Failed to initialize application");
#ifdef WOWEE_PS4
            wowee::platform::ps4::showStartupError("Client initialization failed. Check boot.log and wowps.log.");
#endif
            return 1;
        }

#ifdef WOWEE_PS4
        wowee::platform::ps4::reportBootStage("application: main loop");
#endif
        app.run();
        app.shutdown();

        LOG_INFO("Application exited successfully");
#if defined(__linux__) && !defined(__ANDROID__)
        if (g_emergencyDisplay) { XCloseDisplay(g_emergencyDisplay); g_emergencyDisplay = nullptr; }
#endif
        return 0;
    }
    catch (const std::exception& e) {
        releaseMouseGrab();
        LOG_FATAL("Unhandled exception: ", e.what());
#ifdef WOWEE_PS4
        wowee::platform::ps4::showStartupError(e.what());
#endif
        return 1;
    }
    catch (...) {
        releaseMouseGrab();
        LOG_FATAL("Unknown exception occurred");
#ifdef WOWEE_PS4
        wowee::platform::ps4::showStartupError("An unknown exception interrupted startup or rendering.");
#endif
        return 1;
    }
}
