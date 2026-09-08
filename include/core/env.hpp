#pragma once

/**
 * env.hpp - setting and clearing environment variables on every platform.
 *
 * setenv and unsetenv are POSIX and do not exist on Windows, where the spelling
 * is _putenv_s. Three places had already written that #ifdef out by hand and a
 * fourth had not, which is the only reason anyone found out: a test that clears
 * two variables before the first case compiled everywhere except Windows and
 * failed CI there with "'::unsetenv' has not been declared".
 *
 * The hand-written branches also disagreed about what they were implementing.
 * POSIX setenv takes an overwrite flag and three call sites pass 0 - meaning
 * "only if it is not already set", which is how the client lets a variable the
 * player exported win over its own default. _putenv_s has no such flag and
 * always overwrites, so on Windows those defaults quietly took precedence over
 * the player's own setting. Doing the check here fixes that everywhere at once.
 *
 * Header-only on purpose: the tests link a chosen handful of sources each, and
 * a helper in a .cpp would mean picking that file into targets that want
 * nothing else from it.
 */

#include <cstdlib>

namespace wowee {
namespace core {

/// Set NAME to VALUE. With overwrite false, an existing value is left alone.
inline void setEnvVar(const char* name, const char* value, bool overwrite = true) {
    if (!name || !value) return;
    if (!overwrite && std::getenv(name) != nullptr) return;
#ifdef _WIN32
    _putenv_s(name, value);
#else
    ::setenv(name, value, 1);
#endif
}

/// Remove NAME from the environment.
inline void unsetEnvVar(const char* name) {
    if (!name) return;
#ifdef _WIN32
    // An empty value is how Windows deletes one; there is no _unputenv.
    _putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}

}  // namespace core
}  // namespace wowee
