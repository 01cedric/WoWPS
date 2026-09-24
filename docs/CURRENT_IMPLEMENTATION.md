# Current implementation — WoWPS 2.10

| Field | Value |
|---|---|
| Release / BUILD_VERSION | 2.10 / 02.10 |
| Save writer / accepted versions | 45 / 1–45 |
| LAN protocol | 109 |
| Title ID | WOWE00001 |
| Client data | User-supplied WotLK 3.3.5a build 12340 |

## Connected realms

The client implements authentication, realm and character selection, world
transport and connected gameplay paths. Update 2.10 repairs TCP nonblocking
setup, framing, partial writes and Wrath SRP byte handling. See
[connection setup](CONNECTING.md) and [verification scope](BUILD_VALIDATION.md).
Automated protocol tests do not replace an actual PS4/live-server session.

## Local world and creature scripts

- The generated creature script family contains 4,655 entries, 2,243 spell IDs,
  600 timed action lists, 220 summoned entries and 350 escort paths. Supported
  events, targets, actions, phases and links execute within the bounded local
  runtime; this is not all original SmartAI/C++ content.
- Combat includes supported creature melee/ranged/magic effects, mana,
  buffs/heals, areas/cones/chains, control/diminishing returns, auras, procs,
  shields/absorbs, threat, summons and ground effects. Complete player classes,
  pets, items and every spell combination remain unfinished.
- Movement data includes 68,088 moving spawns, 5,722 patrol paths and 120,048
  nodes. Wander/patrol/escort movement, pauses, events, chasing and evade are
  implemented with local simulation limits.
- Gossip data includes 3,044 owners, 3,820 menus, 3,251 options and 3,973 texts.
  Supported conditions, action menus, box prices, quest lists and script
  callbacks feed the conversation window and original GossipFrame.
- Text emotes reach supported creature scripts. Further gossip conditions,
  option types, points of interest and LAN emote presentation remain open.

## Objects, events and persistence

The reviewed companion contributes 861 of 944 captured object placements:
606 presentation objects, 79 chairs/benches, 67 chests and 109 gathering nodes,
plus 14 pools, eight holiday schedules and one interval schedule. The remaining
83 placements are blocked with explicit reasons. See
[object semantics](ORIGINAL_OBJECT_SEMANTICS.md) and [world events](WORLD_EVENTS.md).

Authored vehicles, escorts, phasing, script state and timer sequences are
supported within their documented profiles. Save45 preserves the supplied
save/migration contract; LAN109 carries the expanded host-authoritative state.
The adapted quest catalog has 950 admission gates: 947 compile and three
seasonal/recurring prerequisites remain blocked. Admission is not proof of
complete objectives, rewards or encounter scripts.

## Remaining scope

Original quests, dungeon/raid encounters, full classes/pets/professions,
guilds, raid rules and standalone PvP are not complete. Rendering correctness,
collision, controller coverage, sustained FPS and long-session stability need
actual PS4/client-data acceptance. [TODO.md](../TODO.md) remains authoritative
for open work; no limitations are closed merely by changing the release number.
