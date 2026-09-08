#pragma once

/// Reading a boolean out of an environment variable.
///
/// Five files had their own copy of this and two of them disagreed. Three
/// tested only the first character and rejected '0', 'f', 'F', 'n' and 'N';
/// the other two lowercased the whole value and rejected "0", "false", "off"
/// and "no".
///
/// The two answer differently for "off", which is the obvious thing to type:
/// it begins with 'o', so the first-character form read it as *enabled*.
/// Turning a diagnostic off that way switched it on in three subsystems and
/// off in the other two, and every one of these flags is something someone
/// reaches for while chasing a bug - so the tool lies exactly when it is being
/// leaned on.
///
/// The decision is split from the lookup so it can be tested without touching
/// the environment, which is not portable to every platform this builds on.

#include <cstdlib>
#include <string>

namespace wowee::core {

/// Whether a value found in the environment means "on".
///
/// Empty means "not set" and gives the default. Anything that is not one of
/// the four ways of writing off is on, so a flag set to any nonsense still
/// enables - which is what someone typing `=1` or `=yes` or `=please` wants.
inline bool envValueEnables(const std::string& raw, bool defaultValue) {
    if (raw.empty()) return defaultValue;
    std::string v;
    v.reserve(raw.size());
    for (char c : raw) {
        v += static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    }
    return !(v == "0" || v == "false" || v == "off" || v == "no");
}

/// A whole number from the environment, held inside a range.
///
/// Two files had the same copy of this, next to the two-rule flag reader that
/// was the reason for this header. Every caller is a per-frame budget - how
/// many packets or update blocks to chew through - where a value outside the
/// range is worse than the default: zero stalls the client outright and a
/// huge one gives back the stall the budget exists to prevent.
///
/// Anything that is not a number at all takes the default rather than zero,
/// which is what strtol would otherwise hand back for a typo.
inline int envIntClamped(const char* key, int defaultValue, int minValue, int maxValue) {
    const char* raw = key ? std::getenv(key) : nullptr;
    if (!raw || !*raw) return defaultValue;
    char* end = nullptr;
    const long parsed = std::strtol(raw, &end, 10);
    if (end == raw) return defaultValue;
    if (parsed < minValue) return minValue;
    if (parsed > maxValue) return maxValue;
    return static_cast<int>(parsed);
}

/// Whether the named environment variable is set to something meaning "on".
inline bool envFlagEnabled(const char* key, bool defaultValue = false) {
    const char* raw = key ? std::getenv(key) : nullptr;
    return envValueEnables(raw ? raw : std::string(), defaultValue);
}

}  // namespace wowee::core
