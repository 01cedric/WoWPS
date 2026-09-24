# Shared authored objects — The local runtime

The local realm now distinguishes personal scripted objects from shared doors,
chests and resource nodes. Every shared spawn ID owns one durable state across
all players who can see that spawn. Two phase-specific copies need different IDs.
Objects remain open-world only; this does not import original server scripts.

## Content and state

`gameObjects` retains its existing placement, radius, phase, quest and scalar
state gates, with at most 256 definitions. New fields:

| Field | Contract |
|---|---|
| `kind` | `script` (default), `door`, `chest`, or `resource` |
| `respawnMs` | 0–86,400,000 ms; default 5,000 for doors, 60,000 for chests/resources, 0 for scripts |
| `loot` | At most 16 distinct `itemId` / positive `count` rows; count at most 65,535; installed item metadata required |
| `money` | 0–1,000,000,000 copper; recipient's total may not exceed 1,000,000,000 |
| `requiredSkillId`, `requiredSkill` | Resource-only profession ID and current skill requirement (1–450); both supplied or both zero |
| `toolItemId` | Resource-only installed item that must be present in the backpack; not consumed |

Doors and personal scripts cannot award fixed loot/money. A chest or resource
must award something. Shared kinds can optionally execute `objectUse` script
rows; personal scripts still require at least one matching available row.
Resources perform instant gathering with fixed authored rewards; retail gathering
channels, loot probability tables and skill-up formulas are not inferred.

A shared row records spawn ID, nonzero revision, status and remaining active
simulation time. Status is ready/closed, open, or depleted. Opening a door starts
its autoclose timer; explicit close cancels it. Collection depletes a chest or
resource until its timer expires. Zero respawn time means no automatic reset.
Streaming, disconnects and save/reload preserve the row and remaining time;
offline time does not advance timers.

## Transactions and concurrency

Use requires existing life/travel/combat, 3D radius, quest/state/phase gates and
an unobstructed ray when collision coverage is installed. Resource skill/tool
gates are checked on the authority. Missing collision coverage remains permissive.

Every shared request includes the revision the player saw. A door request also
names its desired open/closed state. Stale revisions, depleted objects and door
no-ops are refused before changes. A successful transition or timer reset advances
the revision, so a delayed pre-respawn request cannot consume a new generation.

All fixed loot, copper, script mutations and resulting collect/script quest
completion are staged together. An inventory overflow, money overflow or failed
script leaves both the player and object unchanged. The realm saves player and
world state atomically before acknowledging use. Failed persistence restores both.
Two players cannot claim the same chest generation. Object rewards belong to
the successful collector, without creature group-money distribution.

## Save and LAN

Save41 appends the bounded realm-wide object ledger after pets and also extends
escort progress with guide health. Older saves start shared objects ready; current
saves validate structural and installed-content constraints before adopting the
object section. Character-slot browsing can parse the structural state without
loading world content.

LAN99 publishes complete, context-bound object frames in up to four pages of
64 rows. Each row is 13 bytes; map, instance, player position revision and exact
phase accompany each page. Guests commit only a complete valid visible set.
They refuse shared actions while that context has no accepted snapshot. Ordinary
reliable command replay/session checks remain in force.

## Presentation and limits

Contextual selection skips depleted and unknown objects. Closed/ready objects
use the existing model spawner. An open door's model is removed, and a depleted
chest/resource is hidden; the normal despawn path cancels pending loads and retires
its render/collision instance. Resetting or closing restores it. This gives a
stateful blocker/presence path, not original model-specific opening animations.
Static baked collision packs are not regenerated when a door changes state.

Full retail object templates/scripts, probabilistic/shared loot windows,
lockpicking, instance doors, original door end-pose animation, dynamic authority
geometry and console appearance remain distinct work. No synthetic fixture is
installed as original playable world content. Validation evidence is recorded
separately from host regression results.
