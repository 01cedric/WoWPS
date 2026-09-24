# Local door presentation

`EntitySpawner::setLocalDoorPresentation(guid, open, revision)` is the display
boundary for an authority-owned local door. Updates use a monotone revision and
are retained even when the object's asynchronous model load has not finished.
When the M2 instance appears, the spawner selects the canonical `OPEN` (148) or
`CLOSE` (146) sequence and freezes it at its last authored sample. Repeating the
same state is harmless; older revisions and a conflicting value at the same
revision are rejected. Revision ordering uses the unsigned 32-bit serial-number
half range: a nonzero forward delta below `0x80000000` is newer. This preserves
ordering across wrap from `UINT32_MAX` to 1 while rejecting an exactly half-range
ambiguous update.

The caller must set a stable realm/map/instance token with
`setGameObjectPresentationContext`. Changing the token clears every buffered
pose, while repeating it preserves poses across distance streaming. Full entity
spawner reset and shutdown also clear the cache.

There is intentionally no invented generic hinge transform. M2 models without
the canonical transition remain visible in a frozen bind pose. WMO instances do
not expose per-instance M2 animation sequences, so a WMO-backed door also stays
visible and retains its authority state without a visual deformation. This
keeps visual geometry and collision together until authored WMO state data is
available. Resource depletion remains on the existing despawn path and does not
use this cache.

Run the focused policy test with:

```sh
tools/tests/run_game_object_door_presentation_tests.sh
```

This presentation policy consumes already-authoritative local object state. It
does not import original GameObject placements, Lock/loot rules, WMO state groups
or collision variants, and it is not a PS4 visual acceptance result.
