# Local world events

World events are authored under `worldEvents`. The local runtime supports two clocks:
`simulation` and `holiday`. Both are host-authoritative and publish through the
same phase/LAN visibility path. Guests never advance either clock locally.

## Simulation schedules

A simulation schedule has a unique nonzero `id`, one map/instance scope,
`clock: "simulation"`, an initial delay, active duration and optional repeat
cooldown. Realm suspension/offline time does not advance it. Start/end action
batches are bounded and atomic; a failed boundary remains pending and is retried
without consuming a cycle. Event-owned phase masks must be nonzero where used,
may not overlap each other, and may not collide with ordinary script phase
transitions.

The authority advances gameplay in slices ending at schedule boundaries, so an
event crossed by a long frame still receives only its authored active interval.
Revisions and cycle counters skip zero on wrap. Durable start actions are restored
for active saved simulation events.

## Holiday schedules

A holiday schedule uses `clock: "holiday"`, `holidayId`, `holidayStage`, and an
`activePhaseMask`. It has no simulation delay/duration/repeat and no start/end
script actions. The local runtime loads the user's WotLK `Holidays.dbc`; fields 1..10
provide stage durations. Supported reviewed production mappings currently cover:
Winter Veil (141), Hallow's End (324), Midsummer (341), Brewfest (372),
Darkmoon Faire (374), Pilgrim's Bounty (404) and Day of the Dead (409).

The host reads local civil date/time from the PS4 clock. Recurring calendar rules
select the current year's base occurrence, then `holidayStage` advances by the
preceding DBC stage durations. This avoids pinning the WotLK data to a historical
2009/2010 timestamp while retaining its authored stage lengths. If the clock,
Holiday row or supported recurrence rule is unavailable/invalid, the schedule is
inactive (fail closed).

Holiday state is recomputed after load rather than restoring a stale wall-clock
answer. A transition increments revision and an activation increments cycle.
The resulting phase mask is reconciled onto players and distributed using the
existing LAN100 world-event snapshots.

## Save and LAN

Save42 introduced the 17-byte event-state row. Save44 keeps that layout and
migrates older saves by schedule ID so newly added/reordered schedules do not
misapply state. Simulation schedules retain compatible saved state; holiday
schedules are deliberately resolved again from the current calendar. Save44 also
retains the deferred kill/script facts introduced in Save43.

LAN100 sends complete world-event decks for the receiving player's current map,
instance and position revision. Mixed/incomplete decks never replace the accepted
view.

Run the focused runtime suite with:

```sh
tools/tests/run_local_world_event_tests.sh
```

## Production content boundary

The generated `reviewed_original_content.json` now contributes eight reviewed
holiday schedules and 332 event-gated decorative GameObjects, in addition to
eight static signs. This is not a claim that all 15 captured `game_event` rows
or their scripts/progress machines are implemented. Interactive, scripted,
pooled, loot/lock and unsupported event semantics remain blocked.
