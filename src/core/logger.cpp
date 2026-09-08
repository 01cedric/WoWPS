#include "core/logger.hpp"
#include <chrono>
#include <iomanip>
#include <ctime>
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#ifndef WOWEE_PS4
#include <ranges>
#endif
#include "core/local_time.hpp"
#include <cstdio>
#ifdef __ANDROID__
#include <android/log.h>
#endif
#ifdef WOWEE_PS4
#include "platform/ps4/ps4_platform.hpp"
#endif

namespace wowee {
namespace core {

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

namespace {

/// Where to write when the working directory will not take a log.
///
/// Deliberately not getConfigRoot(): the logger is linked into every tool and
/// most of the tests, and none of them wants config_paths and what it pulls in
/// behind it. WOWEE_CONFIG_ROOT is still honoured, so a harness that redirects
/// its config redirects its log with it.
std::filesystem::path perUserLogDir() {
    if (const char* root = std::getenv("WOWEE_CONFIG_ROOT"); root && *root) {
        return std::filesystem::path(root) / "logs";
    }
#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) {
        return std::filesystem::path(local) / "WoWPS" / "logs";
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / "Library" / "Logs" / "WoWPS";
    }
#else
    if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state) {
        return std::filesystem::path(state) / "wowps" / "logs";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "state" / "wowps" / "logs";
    }
#endif
    // The throwing overload is deliberately avoided: a host without $TMPDIR
    // and /tmp (the PS4 sandbox, some containers) must not turn "where do I
    // write the log" into an unhandled filesystem_error.
    std::error_code ec;
    const std::filesystem::path tmp = std::filesystem::temp_directory_path(ec);
    if (ec || tmp.empty()) return std::filesystem::path("wowps-logs");
    return tmp / "wowps-logs";
}

}  // namespace

void Logger::ensureFile() {
    if (fileReady) return;
    fileReady = true;
    if (const char* logStdout = std::getenv("WOWEE_LOG_STDOUT")) {
        if (logStdout[0] == '0') {
            echoToStdout_ = false;
        }
    }
    if (const char* flushMs = std::getenv("WOWEE_LOG_FLUSH_MS")) {
        char* end = nullptr;
        unsigned long parsed = std::strtoul(flushMs, &end, 10);
        if (end != flushMs && parsed <= 10000ul) {
            flushIntervalMs_ = static_cast<uint32_t>(parsed);
        }
    }
    if (const char* dedupe = std::getenv("WOWEE_LOG_DEDUPE")) {
        dedupeEnabled_ = !(dedupe[0] == '0' || dedupe[0] == 'f' || dedupe[0] == 'F' ||
                           dedupe[0] == 'n' || dedupe[0] == 'N');
    }
    if (const char* dedupeMs = std::getenv("WOWEE_LOG_DEDUPE_MS")) {
        char* end = nullptr;
        unsigned long parsed = std::strtoul(dedupeMs, &end, 10);
        if (end != dedupeMs && parsed <= 60000ul) {
            dedupeWindowMs_ = static_cast<uint32_t>(parsed);
        }
    }
    if (const char* level = std::getenv("WOWEE_LOG_LEVEL")) {
#ifdef WOWEE_PS4
        // The console toolchain's libc++ (11) has no <ranges>; same parse,
        // spelled with a lowered copy.
        std::string v(level);
        for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (v == "debug") setLogLevel(LogLevel::DEBUG);
        else if (v == "info") setLogLevel(LogLevel::INFO);
        else if (v == "warn" || v == "warning") setLogLevel(LogLevel::WARNING);
        else if (v == "error") setLogLevel(kLogLevelError);
        else if (v == "fatal") setLogLevel(LogLevel::FATAL);
#else
        auto toLower = [] (unsigned char c) { return std::tolower(c); };
        using namespace std::literals;

        auto v = std::string_view{level} | std::views::transform(toLower);
        if (std::ranges::equal(v, "debug"sv)) setLogLevel(LogLevel::DEBUG);
        else if (std::ranges::equal(v, "info"sv)) setLogLevel(LogLevel::INFO);
        else if (std::ranges::equal(v, "warn"sv) || std::ranges::equal(v, "warning"sv))
			setLogLevel(LogLevel::WARNING);
        else if (std::ranges::equal(v, "error"sv)) setLogLevel(kLogLevelError);
        else if (std::ranges::equal(v, "fatal"sv)) setLogLevel(LogLevel::FATAL);
#endif
    }
    std::error_code ec;
#ifdef WOWEE_PS4
    // The console's working directory is not ours to write in; the log lives
    // in the writable tree platform::ps4::initSystem created, next to the
    // caches and settings.
    const std::filesystem::path logDir =
        std::filesystem::path(platform::ps4::writableRoot()) / "logs";
    std::filesystem::create_directories(logDir, ec);
#else
    std::filesystem::create_directories("logs", ec);
#endif
    // WOWEE_LOG_FILE names the file, so a tool run beside the client does not
    // destroy the log the client wrote.
    //
    // This opens with trunc, and every process using this logger opened the
    // same path - so running framexml_run from the repository root wiped the
    // session log of the client that had just been played, which is the one
    // file anyone diagnosing a report needs. It was found the only way it
    // could be: by being asked to read a log and finding my own run in it.
    const char* logName = std::getenv("WOWEE_LOG_FILE");
    const std::string logFile = (logName && *logName) ? logName : "wowps.log";
#ifdef WOWEE_PS4
    const std::string logPath = (logDir / logFile).string();
#else
    const std::string logPath = std::string("logs/") + logFile;
#endif
    fileStream.open(logPath, std::ios::out | std::ios::trunc);

    // Beside the working directory when that is writable, which is how this is
    // run from a checkout and where every tool expects to find it.
    //
    // A bundled application has no such directory. macOS launches an .app with
    // the working directory set to "/", so create_directories("logs") fails on
    // a read-only root, the open fails with it, and the client runs with no log
    // at all. That is not a quiet degradation: the log is the only thing a bug
    // report has to go on, and the absence looks exactly like a client that
    // wrote nothing worth saying.
    if (!fileStream.is_open()) {
        const std::filesystem::path fallback = perUserLogDir();
        std::filesystem::create_directories(fallback, ec);
        const std::filesystem::path at = fallback / logFile;
        fileStream.open(at, std::ios::out | std::ios::trunc);
        if (fileStream.is_open()) {
            // Said on the console, because the file it names is the one thing
            // someone reading this needs and it is not where they will look.
            std::fprintf(stderr, "wowps: writing the log to %s\n", at.string().c_str());
        }
    }
    lastFlushTime_ = std::chrono::steady_clock::now();
}

void Logger::emitLineLocked(LogLevel level, const std::string& message) {
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm = core::localTime(time);
    // Format: [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] message
    std::ostringstream line;
    line << "["
         << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
         << "." << std::setfill('0') << std::setw(3) << ms.count()
         << "] [";

    switch (level) {
        case LogLevel::DEBUG:   line << "DEBUG"; break;
        case LogLevel::INFO:    line << "INFO "; break;
        case LogLevel::WARNING: line << "WARN "; break;
        case kLogLevelError:    line << "ERROR"; break;
        case LogLevel::FATAL:   line << "FATAL"; break;
    }

    line << "] " << message;

    if (echoToStdout_) {
#ifdef WOWEE_PS4
        // stdout is the kernel log on the console, read live through a klog
        // viewer, and a fully buffered cout would hand it 4 KiB at a time,
        // long after the line mattered. One write and a flush per line.
        const std::string text = line.str();
        std::fputs(text.c_str(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
#else
        std::cout << line.str() << '\n';
#endif
    }
#ifdef __ANDROID__
    // stdout goes nowhere on Android and the file has to be pulled off the
    // device to be read, so every line also goes to logcat, where `adb logcat
    // -s wowee` shows it live. The timestamp and level are logcat's own job,
    // so this passes the message rather than the formatted line.
    int priority = ANDROID_LOG_INFO;
    switch (level) {
        case LogLevel::DEBUG:   priority = ANDROID_LOG_DEBUG; break;
        case LogLevel::INFO:    priority = ANDROID_LOG_INFO; break;
        case LogLevel::WARNING: priority = ANDROID_LOG_WARN; break;
        case kLogLevelError:    priority = ANDROID_LOG_ERROR; break;
        case LogLevel::FATAL:   priority = ANDROID_LOG_FATAL; break;
    }
    __android_log_write(priority, "wowps", message.c_str());
#endif
    if (fileStream.is_open()) {
        fileStream << line.str() << '\n';
        // An error or worse goes to disk at once: the lines just before a crash
        // are the ones a report is opened for, and a crash runs no destructor.
        //
        // A warning does not, though it used to. This client puts its
        // diagnostics at warning deliberately, so warnings arrive in bursts -
        // a hundred and twelve lines while FrameXML loads, two hundred for one
        // takeover check - and a flush per line is a write syscall per line,
        // back to back, in the middle of a frame. They take the same interval
        // as everything else now, which turns a burst into one write; a warning
        // on its own still goes out immediately, because the interval has long
        // since elapsed by the time it arrives.
        bool shouldFlush = (level >= kLogLevelError);
        if (!shouldFlush) {
            auto nowSteady = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowSteady - lastFlushTime_).count();
            shouldFlush = (elapsedMs >= static_cast<long long>(flushIntervalMs_));
        }
        if (shouldFlush) {
            lastFlushTime_ = std::chrono::steady_clock::now();
            fileStream.flush();
            unflushed_ = false;
        } else {
            unflushed_ = true;
        }
    }
}

void Logger::flushIfStale() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!unflushed_ || !fileStream.is_open()) return;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlushTime_).count();
    if (elapsedMs < static_cast<long long>(flushIntervalMs_)) return;
    lastFlushTime_ = now;
    fileStream.flush();
    unflushed_ = false;
}

void Logger::flushSuppressedLocked() {
    if (suppressedCount_ == 0) return;
    emitLineLocked(lastLevel_, "Previous message repeated " + std::to_string(suppressedCount_) + " times");
    suppressedCount_ = 0;
}

void Logger::log(LogLevel level, const std::string& message) {
    if (!shouldLog(level)) {
        return;
    }

    // Capture timestamp before acquiring lock to minimize critical section
    auto nowSteady = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(mutex);
    ensureFile();
    if (dedupeEnabled_ && !lastMessage_.empty() &&
        level == lastLevel_ && message == lastMessage_) {
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowSteady - lastMessageTime_).count();
        if (elapsedMs >= 0 && elapsedMs <= static_cast<long long>(dedupeWindowMs_)) {
            ++suppressedCount_;
            lastMessageTime_ = nowSteady;
            return;
        }
    }

    flushSuppressedLocked();
    emitLineLocked(level, message);
    lastLevel_ = level;
    lastMessage_ = message;
    lastMessageTime_ = nowSteady;
}

void Logger::setLogLevel(LogLevel level) {
    minLevel_.store(static_cast<int>(level), std::memory_order_relaxed);
}

bool Logger::shouldLog(LogLevel level) const {
    return static_cast<int>(level) >= minLevel_.load(std::memory_order_relaxed);
}

} // namespace core
} // namespace wowee
