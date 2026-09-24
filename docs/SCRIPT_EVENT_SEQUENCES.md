# Script areas and timer sequences

This extends the local script runtime with area events and timer sequences.
It provides authored event building blocks, not imported WotLK quest scripts.

## Area edges

`world.json` accepts up to 32 `scriptAreas`. An area has a unique nonzero `id`,
`mapId`, `x/y/z`, spherical `radius` (0.1–500 yards), optional `hysteresis`
(0–10 yards, default 0.5), and optional `requiredPhaseMask`/`excludedPhaseMask`.

```json
{"id":800, "mapId":0, "x":20, "y":0, "z":0,
 "radius":2, "hysteresis":0.5, "requiredPhaseMask":1}
```

The authority samples player positions at the end of its tick. Unchanged membership uses a fixed scratch array without per-tick vector allocation. There is no guest
command for claiming an area event. Entry requires being inside the ordinary
radius; an existing membership remains until the player leaves radius plus
hysteresis. This suppresses repeated entry/exit at a boundary. It is sampled
position detection, not swept collision: crossing an entire small area between
samples can miss it.

`areaEnter` and `areaLeave` triggers name the area ID in `sourceId`. They use the
same state, phase, condition and timer operations as other script events. All
leaves run first, then entries, each in ascending area-ID order. All transitions
for one sampled position commit together or none do. Conditions see preceding
mutations from that sample. Geometry/phase eligibility is sampled before those
mutations; their effect on membership is evaluated on the next tick.

Dead/ghost players are outside every area. Phase loss, map travel and instance
changes generate the corresponding edges. A same-map instance change generates
leave then entry even when coordinates match. Save39 retains sorted area IDs and
their instance identity, so loading while still inside does not repeat an entry.
Older saves start with no memberships and receive their first sampled entry.

## Timer sequences

`scriptTimers` actions now accept `scheduleTimerId`, positive `scheduleDelayMs`
and/or `cancelTimerId`, in addition to existing state/phase changes. A timer-only
action is supported. Every referenced timer must exist; scheduling and cancelling
the same ID in one action is rejected.

```json
{"timerId":901, "scriptId":600, "value":2,
 "scheduleTimerId":902, "scheduleDelayMs":1000}
```

Expired IDs are processed in ascending order. An earlier action can cancel or
restart a later pending expiry: that old expiry then does not fire. A newly
scheduled timer runs on a later tick, even if its ID was already pending. Positive
self-rescheduling is allowed but executes at most once per tick, preventing a
same-frame infinite loop. Timers count simulated online time; offline wall-clock
time is not deducted.

An expiry consumes its old timer. Its state/phase/scheduling changes are atomic;
an invalid condition or failed mutation produces no partial change and does not
retry the consumed expiry automatically. Authors should make success/failure
branches explicit through conditions and state. Total active timers remain 16
per character, script states 64, and authored timer actions 1024.

Area entry/leave and timer actions can update script-backed quest objectives.
Already earned objective credit is monotonic. `questAbandon` can cancel
all timers in a multi-stage attempt using several trigger rows. Reset relevant
state on `questAccept` when a new attempt must start fresh. An accepted quest
while already inside an area does not invent a new area edge.

## Compatibility and example

Save39 reads versions 1–38; LAN94 carries the same owner area-membership fields.
All LAN peers must use matching builds/content. The synthetic example in
`tools/tests/fixtures/area_script_world.json` demonstrates area entry, timed
quest completion, a follow-up timer and cancellation on exit. It is test content
and is not installed into the shipped world.

Still open: complete original quest chains, escort AI, game-object use scripts,
encounter/raid scripts, original vehicle abilities/seat geometry and PS4 acceptance.
