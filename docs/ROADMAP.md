# WoWPS roadmap

The release target is complete, stable WotLK gameplay on PS4 with a usable original interface and predictable frame times. [TODO.md](../TODO.md) is the active scope list.

## Implementation priorities

1. Stabilize rendering, streaming, memory ownership and frame pacing; retain useful diagnostics without adding unsafe synchronization shortcuts.
2. Complete combat, spells, talents, procs and auras, followed by pet/form/stance behavior and progression.
3. Complete quests, world events, instances, professions, economy and social/PvP systems, preserving host authority and transaction rollback.
4. Finish interface, controller, race/class presentation and travel integration.
5. Perform comprehensive offline, save, LAN, external-server and PS4 acceptance against the finished implementation.

Focused regression/build checks accompany implementation. Final full-game acceptance comes last and must include real hardware, actual client data, failures/recovery and long sessions. No stage is complete solely because an importer accepts data or code compiles.
