# Authored vehicle projectiles

This extends the bounded [vehicle ability runtime](VEHICLE_ABILITIES.md) with
per-seat aim and authoritative ballistic projectiles. Direct damage and repair
remain available. Spell IDs supply existing metadata and combat-event identity;
they do not import the corresponding retail spell effects or encounter scripts.
The [original vehicle UI contracts](VEHICLE_FRAME_XML.md) retain the
native aiming guide/fallback described here.

## Authored profile

Vehicle kits retain their existing power, regeneration, six ability slots,
seat masks and shared cooldown rules. These additional fields use server-space
yards, seconds and radians:

| Field | Accepted values | Default |
|---|---|---|
| Kit `minPitch` | -1.4 to +1.4 radians | -1.4 |
| Kit `maxPitch` | `minPitch` to +1.4 radians | +1.4 |
| Kit `muzzleHeight` | 0–20 yards above the authored seat position | 1.5 |
| Ability `projectileSpeed` | 0 for a direct ability; 1–120 yards/second for a projectile | 0 |
| Ability `projectileGravity` | 0–30 yards/second squared | 0 |
| Ability `projectileRadius` | 0.1–5 yards, added to the NPC collision sphere | 0.5 |
| Ability `projectileLifetimeMs` | 100–10,000 ms for a projectile | 3,000 for a projectile; 0 otherwise |
| Ability `range` | Greater than 0, at most 60 yards for damage | 30 |

A projectile requires nonzero `damage` and cannot repair. A direct ability
cannot declare nonzero projectile gravity/lifetime or explicitly supply
`projectileRadius`. The existing unique slot/spell, metadata reference and
seat-mask validation still applies.

For example, the following row can be added to an existing valid kit with the
referenced spell metadata and seat already defined:

```json
{
  "slot": 1,
  "spellId": 9001,
  "seatMask": 1,
  "powerCost": 25,
  "cooldownMs": 1500,
  "damage": 90,
  "range": 60,
  "projectileSpeed": 30,
  "projectileGravity": 9.8,
  "projectileRadius": 0.5,
  "projectileLifetimeMs": 4000
}
```

`range` limits the distance traveled along the flight path, including ascent
and descent. It is neither horizontal range nor distance from the hull. The
host accumulates swept segment lengths and trims the final segment to the
remaining distance budget. Lifetime is an independent simulation-time limit;
the earlier range, lifetime, collision or cancellation condition ends the shot.

## Aim, launch and impact

Each of up to eight seats has its own hull-relative yaw and elevation. Yaw is
bounded to ±pi; elevation uses the kit's limits. The host accepts an aim command
only for the sender's current living hull and exact current seat, with matching
map/instance, a visible vehicle phase and an aimed weapon allowed in that seat.
Movement remains restricted to the controller seat.

Projectile abilities require target GUID zero and never track a selected enemy.
The launch position is the rotated authored seat offset plus `muzzleHeight` in
world Z. The heading is hull orientation plus seat yaw. Initial velocity is
`speed * (cos(pitch)*cos(heading), cos(pitch)*sin(heading), sin(pitch))`; gravity
acts downward. The source does not inherit hull movement velocity.

The authority advances shots in segments of at most 10 ms. It selects the first
intersection with a living, attackable NPC sphere in the same map/instance,
using NPC bounding radius plus the authored projectile radius. Equal contact
times use GUID order. Players, pets, vehicles and transport NPCs are excluded
from projectile targets. An authored area radius can damage other valid NPCs
around the first impact. There is no piercing, bounce or homing.

Before an NPC impact or an unobstructed segment advance, the existing collision
pack tests the center-line ray, including M2 geometry. The authored projectile
radius enlarges NPC collision only; it does not sweep a volume against world
geometry. Missing collision coverage or an unavailable tile cannot block the
shot. Successive projectile rays overlap their start by 0.01 yards so the open
ray endpoints cannot let a shot cross a wall exactly between simulation segments.
The overlap never extends the ray past the first NPC contact. A blocked ray ends
it without damage. A hit applies the authored physical or elemental school,
threat and owner/party reward paths once. Physical damage uses armor; the
bounded elemental profile does not model resistance.

At most **16 projectiles are active globally in one realm**, across all hulls
and players. A seventeenth launch is rejected before energy or cooldowns are
spent. Accepted launches spend resources immediately; a later miss or
cancellation does not refund them.

A shot is canceled when its owner disappears, dies, becomes a ghost, leaves
the source hull, enters other travel, changes map/instance or changes the exact
phase mask. A missing/dead source hull, a changed hull incarnation or loss of
the hull's phase visibility also cancels it. Successful exit and forced detach
remove the owner's shots. Switching seats within the same live hull preserves
already launched shots; they retain their original owner and trajectory.

## Controls

The following labels and controls belong to the native vehicle overlay. Open
original UI panels retain their input/navigation ownership.

| Action | Keyboard / mouse | PS4 source binding |
|---|---|---|
| Select and use an allowed ability | F1–F6 or its ability button | L1/R1 selects; R2+Square uses |
| Turn aim | Numpad 4/6 or the `Yaw` slider | Hold L2 with L1/R1 |
| Change elevation | Numpad 8/2 or the `Elevation` slider | Hold R2 with L1/R1 |
| Next available seat | V or `Next seat` | L2+Square |
| Exit | 4 or `Exit vehicle` | Plain Square |

Ballistic fire uses the confirmed aim without a target selection. Direct damage
still uses the selected enemy; repair uses the current hull. Camera sticks and
existing D-pad camera controls are retained. Modified Square does not fall
through to plain-Square exit, and the native bar does not take generic ImGui
gamepad navigation. Keyboard I/J/K/L retain their existing panel shortcuts.

Aim edits are coalesced into one local pending request. The native UI attempts
at most four submissions per second and waits for a matching published seat
aim before sending further accumulated edits. Idle frames send none. Published
angle comparison allows the network quantization tolerance. While an edit is
pending, the bar displays `Updating aim...` and blocks ballistic fire. After two
seconds without a matching published aim, it discards the pending local edit
and adopts the current snapshot; this does not withdraw a reliable command
already in transit. Owner, hull and seat changes reset the local edit buffer.

The trajectory guide uses confirmed aim, at most 32 segments, authored gravity,
lifetime and an analytic arc-distance range cutoff. The authority remains the
impact decision maker: this guide does not predict NPC or geometry impacts and
is not depth-occluded. Up to 16 published projectiles within 500 yards receive
small screen-space dots/tails. These are native aiming aids, not original spell
models, missile animations or impact effects.

## LAN and save boundaries

This checkpoint uses **LAN100**; every peer must use matching code and content.
Reliable aim commands carry the hull GUID, seat identity and finite yaw/pitch.
The NPC deck carries eight pairs of normalized 12-bit angles: three bytes per
seat, **24 bytes total**. Reserved invalid encodings and out-of-profile angles
are rejected. Guest seat offsets are reconstructed from matching immutable
spawn content. A viewer-specific combat flag also reports combat involving the
occupied hull.

Projectile snapshots carry one complete bounded list with a sequence number,
map, instance, player position revision and exact phase mask. Each projectile
record is 60 bytes. Stale sequences, wrong context, duplicate projectile IDs,
invalid motion values and spell/profile mismatches prevent list replacement.
Guests display the accepted state; they do not decide impacts.

The maximum encoded two-NPC datagram is **1,399 bytes**. The maximum projectile
datagram is **1,001 bytes**: 20-byte protocol header, 21-byte list header and
16 × 60-byte records. These sizes include the local protocol header, not the
UDP/IP headers, and fit the existing 1,400-byte application limit.

Save41 adds shared objects and escort health. Aim, active projectiles,
hull resources and live seats are transient. Loading restores the existing
character/exit-recovery state rather than an in-flight shot.

This is an authored local runtime, not original WotLK vehicle parity. Retail
spell conversion, model/bone muzzle and seat attachments, full vehicle physics,
verified original vehicle presentation, encounter coverage and console acceptance remain
separate work. This document describes code behavior and makes no test-result,
performance or hardware-validation claim.
