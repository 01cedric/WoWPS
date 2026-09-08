#pragma once

// One clock for anything that has to agree about "now".
//
// WoW's GetTime() is seconds since the client started, as a float, and the
// interface treats it as a shared reference: a cooldown records
// GetTime() + duration and something else compares against it later. Two
// callers each keeping their own start point would disagree by however long
// apart they first ran, which for a cooldown means a sweep that is wrong by
// seconds and never settles.

namespace wowee::core {

/// Seconds since the first call, monotonic. Not wall-clock time: it never goes
/// backwards, which is what anything measuring a duration needs.
double appTimeSeconds();

/// Harness only: add `seconds` to the clock without real time passing, so a
/// synchronous --tick loop can drive GetTime-based animations - FadingFrame
/// (RaidWarning, boss emote), cooldown sweeps, cast bars - which read the wall
/// clock rather than a per-frame elapsed and so stand still when ticks run
/// faster than real time. The live client never calls this, so its clock is
/// exactly the steady_clock as before.
void advanceTestClock(double seconds);

} // namespace wowee::core
