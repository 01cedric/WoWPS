#pragma once

#include <string>

namespace wowee::core {

// Absolute path to the directory holding the running executable.
// Empty if it cannot be determined.
std::string getExecutableDir();

// Root directory for user config (login.cfg, settings.cfg, last_character.cfg,
// characters/). Two modes:
//   - Portable: if a "portable.txt" marker file, or an existing "config" folder,
//     sits next to the executable, config lives in <exe_dir>/config. This keeps
//     the whole client self-contained in one folder (USB sticks, clean uninstall,
//     easy backup of server profiles).
//   - Per-user (default): %APPDATA%\wowee on Windows, ~/.wowps elsewhere.
std::string getConfigRoot();

// One-time seeding of portable config. On the first launch after the user drops
// a "portable.txt" marker next to the executable (before any config folder
// exists), copies the existing per-user config tree into <exe_dir>/config so
// saved server profiles, settings, and characters carry over. No-op afterwards,
// and a no-op when not in portable mode. Call once at startup before config is
// read.
void migratePortableConfigIfNeeded();

// Enters the directory named by WOWEE_RESOURCE_ROOT, which holds assets/ and
// Data/ in the layout a desktop install has.
//
// Android changes the working directory: a process there
// starts in a directory holding neither, and shaders, interface art and the
// expansion profiles are all opened through relative paths. It is not enough to
// do this once at startup, because SDL and the Vulkan driver leave the working
// directory at /system/bin, so this is called again once they are up.
//
// On PS4 this NEVER changes the working directory, even when the environment
// variable is set: chdir is unsupported there. Instead it verifies that the
// package's WotLK metadata and essential SPIR-V shaders are readable through
// absolute paths. Returns false on an unreadable/incomplete package and logs
// the individual files. Relative asset opens must use the resolver below.
// Elsewhere a no-op when WOWEE_RESOURCE_ROOT is unset.
bool enterResourceRoot();

// Resolves a relative asset path literal (e.g. "assets/shaders/x.spv" or
// "assets/krayonload.png") to one that actually opens, regardless of
// whether the platform has a usable notion of "current working directory".
//
// A no-op (returns path unchanged, including already-absolute paths)
// everywhere except PS4. PS4 has no process working directory to chdir
// into - unlike Android, which enterResourceRoot() above handles - and its
// relative-path open() fails outright with ENOSYS rather than falling back
// to anything. There, anchors relative paths to the read-only package root
// ("/app0", or an absolute WOWEE_RESOURCE_ROOT override).
std::string resolveRelativeAssetPath(const std::string& path);

}  // namespace wowee::core
