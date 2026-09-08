#pragma once

// Cross-platform subprocess helpers for spawning ffplay (audio playback).
// Linux: fork/exec/kill/waitpid.  Windows: CreateProcess/TerminateProcess.

#include <string>
#include <vector>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>

  using ProcessHandle = HANDLE;
  inline const ProcessHandle INVALID_PROCESS = INVALID_HANDLE_VALUE;

#else
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <unistd.h>
  #include <csignal>

  using ProcessHandle = pid_t;
  inline constexpr ProcessHandle INVALID_PROCESS = -1;

#endif

#include <filesystem>
#include <system_error>

#ifdef WOWEE_PS4
  #include "platform/ps4/ps4_platform.hpp"
#endif

namespace wowee {
namespace platform {

// Return a platform-appropriate temp file path for the given filename.
//
// Never throws. std::filesystem::temp_directory_path() raises a
// filesystem_error when neither $TMPDIR nor /tmp exists, and the PS4 sandbox
// has no /tmp at all: on the first console launch that got audio running,
// this call sat in a member initializer of ActivitySoundManager and took the
// whole client down right after "AudioEngine initialized" (B3, boot.log:
// "filesystem error: in temp_directory_path: path "/tmp" is not a
// directory"). The console gets its scratch file in the writable tree the
// platform layer owns; elsewhere a failed lookup falls back to the working
// directory instead of an exception.
inline std::string getTempFilePath(const std::string& filename) {
#ifdef WOWEE_PS4
    return (std::filesystem::path(ps4::writableRoot()) / "tmp" / filename).string();
#else
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec || dir.empty()) {
        ec.clear();
        dir = std::filesystem::current_path(ec);
        if (ec || dir.empty()) dir = ".";
    }
    return (dir / filename).string();
#endif
}

// Kill a subprocess (and its children on Linux).
inline void killProcess(ProcessHandle& handle) {
    if (handle == INVALID_PROCESS) return;

#ifdef _WIN32
    TerminateProcess(handle, 0);
    WaitForSingleObject(handle, 2000);
    CloseHandle(handle);
#else
    kill(-handle, SIGTERM);  // kill process group
    kill(handle, SIGTERM);
    int status = 0;
    // Non-blocking wait with SIGKILL fallback after ~200ms
    for (int i = 0; i < 20; ++i) {
        pid_t ret = waitpid(handle, &status, WNOHANG);
        if (ret != 0) break;  // exited or error
        usleep(10000);         // 10ms
    }
    // If still alive, force kill
    if (waitpid(handle, &status, WNOHANG) == 0) {
        kill(-handle, SIGKILL);
        kill(handle, SIGKILL);
        waitpid(handle, &status, 0);
    }
#endif

    handle = INVALID_PROCESS;
}

// Check if a process has exited. If so, clean up and set handle to INVALID_PROCESS.
// Returns true if the process is still running.
inline bool isProcessRunning(ProcessHandle& handle) {
    if (handle == INVALID_PROCESS) return false;

#ifdef _WIN32
    DWORD result = WaitForSingleObject(handle, 0);
    if (result == WAIT_OBJECT_0) {
        // Process has exited
        CloseHandle(handle);
        handle = INVALID_PROCESS;
        return false;
    }
    return true;
#else
    int status = 0;
    pid_t result = waitpid(handle, &status, WNOHANG);
    if (result == handle) {
        handle = INVALID_PROCESS;
        return false;
    }
    return true;
#endif
}

} // namespace platform
} // namespace wowee
