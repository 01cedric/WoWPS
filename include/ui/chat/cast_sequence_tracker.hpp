#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace wowee {
namespace ui {

/**
 * /castsequence persistent state - shared across all macros using the same spell list.
 *
 * Extracted from chat_panel_commands.cpp static global (Phase 1.5 of chat_panel_ref.md).
 * Keyed by the normalized (lowercase, comma-joined) spell sequence string.
 */
class CastSequenceTracker {
public:
    struct State {
        size_t   index          = 0;
        float    lastPressSec   = 0.0f;
        uint64_t lastTargetGuid = 0;
        bool     lastInCombat   = false;
    };

    /** Get (or create) the state for a given sequence key. */
    State& get(const std::string& seqKey) { return states_[seqKey]; }

    /** Reset all tracked sequences. */
    void clear() { states_.clear(); }

private:
    std::unordered_map<std::string, State> states_;
};

} // namespace ui
} // namespace wowee
