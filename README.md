<p align="center">
  <img src="ps4/sce_sys/icon0.png" alt="WoWPS" width="160">
</p>

<h1 align="center">WoWPS 2.11</h1>
<p align="center"><em>World of Warcraft, running natively on PlayStation 4.</em></p>
<p align="center">
  <img alt="Release" src="https://img.shields.io/badge/release-2.11-blue">
  <img alt="Platform" src="https://img.shields.io/badge/platform-PS4%20homebrew-003791">
  <img alt="Client" src="https://img.shields.io/badge/client-WotLK%203.3.5a%20build%2012340-c8a04a">
  <img alt="Renderer" src="https://img.shields.io/badge/renderer-Vulkan%201.0%20over%20GNM-a41e22">
</p>

> [!IMPORTANT]
> Supply your own legally obtained **WotLK 3.3.5a, build 12340** client data.
> Original Blizzard MPQ/DBC files are not included. WoWPS is not affiliated with or endorsed by Blizzard Entertainment.

## About

WoWPS is a native C++ client built for a homebrew-enabled PS4 with the OpenOrbis toolchain. It is not a stream of the PC game and does not run the Windows client in an emulator. Its Vulkan 1.0 implementation uses GNM and VideoOut on the console.

The world, characters, sounds and original FrameXML interface are read from your client archives. **Connected play** uses an external compatible AzerothCore, TrinityCore or MaNGOS server. **Standalone play** runs a separate, bounded local world simulation, either alone or with other consoles on the LAN. Local features must not be confused with the much wider server-side content available on an external realm.

## Update 2.11 — PS4 connection hotfix

Update 2.11 replaces the TCP mode switch rejected by the reported PS4 runtime
with the PlayStation `SO_NBIO` socket option. It applies to authentication and
world connections and retains the 2.10 SRP, packet-framing and encryption fixes.
The connection tests now include denied `fcntl`/`ioctl` calls. The local-world,
renderer, streaming, movement and persistence features from 2.10 are retained.

See [CHANGELOG.md](CHANGELOG.md) for the consolidated release notes,
[current implementation](docs/CURRENT_IMPLEMENTATION.md) for scope, and
[TODO.md](TODO.md) for remaining work. [Build validation](docs/BUILD_VALIDATION.md)
separates automated checks from PS4 acceptance. A successful build is not a
guarantee of complete gameplay, visual correctness or a particular frame rate.

## Feature status

**✅** Working within the stated scope. **⚠️** Partially implemented, limited or still affected by bugs. **❌** No working standalone implementation / unsupported release target.

### Engine, graphics and interface

| Status | Feature | Scope and limitations |
|:---:|---|---|
| ✅ | Native PS4 application | OpenOrbis build; Vulkan 1.0 over GNM/VideoOut; installable homebrew PKG. |
| ✅ | Terrain, buildings, objects and animated characters | ADT terrain, WMO structures and M2 models are rendered from your archives; this does not certify every asset. |
| ✅ | Regional and time-of-day lighting | Client light tables, zone palette, weather and day/night direction feed the world and sky. Local realms follow the console clock; LAN guests follow the host. |
| ⚠️ | Ground shadows | Near/far directional atlas with terrain, WMO, M2 and character casters. Working shadows were confirmed in tested scenes; every surface and location is not certified. |
| ⚠️ | Volumetric sun/moon shafts | World-space, depth- and shadow-clipped scattering, with independent intensity and depth-aware reconstruction/denoising. Residual banding/noise and scene-dependent appearance need console checks. |
| ⚠️ | Volumetric fog, bloom and water | Height-dependent fog, bright-surface bloom, water absorption/refraction/Fresnel and optional planar reflections. Not a full HDR renderer or hardware ray tracing. |
| ✅ | World-resolution selection | 720p and 1080p profiles with a separate 1080p output/UI path where supported. |
| ⚠️ | Original FrameXML interface | Scripts and artwork load from MPQs. Options categories, action bars, quests, talents and pet actions are connected, but not every panel is console-validated. |
| ⚠️ | Controller, keyboard, layout and portraits | Contextual input, modal guards, scrolling, forward anchors, resolution updates and portrait lifecycle fixes. Class-specific layout and full panel coverage remain open. |
| ⚠️ | Race introductions | Camera/narration, scene readiness, lookahead and loading prewarm are implemented; race-by-race smoothness and visual fidelity remain unverified. |
| ⚠️ | Streaming and memory | Resumable uploads, bounded CPU direct-memory pools, cached placement/visibility and safe deferred cleanup. Long travel and repeated character changes can still expose faults. |
| ⚠️ | Performance and stability | Rendering and submission costs have been reduced in specific paths. Sustained 30+ FPS, crash-free long sessions and all-zone visual correctness are **not** established. |
| ❌ | Expansions after WotLK | Not a supported release target. PS5 compatibility and upstream Vanilla/TBC paths are not verified PS4 features. |

### Standalone world, combat and progression

| Status | Feature | Scope and limitations |
|:---:|---|---|
| ✅ | Local world and basic quest lifecycle | Explore, fight, loot, level; accept, track, complete, turn in and abandon supported catalog quests. |
| ⚠️ | All original quests and progression | The 950-entry adapted catalog has 947 compiled source-backed admission gates and 3 explicitly blocked seasonal/recurring prerequisites. Objectives, rewards and scripts still do not reproduce all original content. |
| ⚠️ | Combat and classes | Source-backed melee/ranged and selected spell rules, threat, regeneration, forms, combo points, cooldowns and talents. Complete classes, channels, coefficients and all secondary effects remain unfinished. |
| ⚠️ | Auras and procs | Supported stacks, shields, periodic effects, rank/exclusive rules, charges, caster identity and bounded proc dispatch. General player control, item/enchant producers and complete profiles remain incomplete. |
| ⚠️ | Death Knight systems | Runic Power and base-rune timers/costs exist. Full disease, Death-rune, weapon and talent behavior remains incomplete. |
| ⚠️ | Pets and guardians | Owned actors, stats, threat, rewards, commands/lifecycle and Imp Firebolt with manual/autocast behavior are present. Complete Hunter/Warlock pets, stables and pet auras are unfinished. |
| ⚠️ | Death, ghost runback and corpse reclaim | Automatic ghost release, faction/zone-aware graveyards, retained corpse and instance, and Square reclaim within 10 yards. End-to-end console/LAN and dungeon cases still need acceptance. |
| ⚠️ | Ground recovery | Local/LAN and connected-realm recovery revalidate a previously stable solid floor. Local authority persists the correction directly; connected realms use ordinary movement updates only (no GM command). Life/ghost state is preserved, while mesh, lift and water edge cases remain acceptance items. |
| ✅ | Inventory and equipment | A local 24-slot backpack and 19 worn slots; moves, splits, merges, swaps and checked equipment changes. Additional bags, durability and full item-instance rules are not complete. |
| ⚠️ | Talents and training | Saved ranks, points, tiers/prerequisites and supported normal talent routes; unavailable effects are not granted merely to open a tree. |
| ⚠️ | Scripted local content | Bounded dialogue, spawn, despawn, move and combat actions plus authored simulation-time world events are transactional and replicated within their documented profiles. The reviewed companion installs 861 placements: decorations, chairs, chests and gathering nodes, with pools and calendar/interval events. Creature gossip, patrols, combat scripts and supported spell effects are implemented within the documented profiles; unsupported rows and C++ encounter scripts remain blocked. |
| ⚠️ | Saves | Atomic realm persistence with rollback on supported transactions; Save45 adds pooled-object dormancy plus schedule-ID/object-row migration on top of calendar events, deferred kill, ghost/corpse, pet, object and escort state. Full crash recovery and long-session acceptance remain necessary. |
| ⚠️ | Travel | Taxi discovery/flights, ships, zeppelins, mounts and instance entrances are implemented within the local ruleset; full travel and transport acceptance remains open. |
| ❌ | Complete dungeon/raid encounters | Entering an instance does not provide original boss scripts or full raid mechanics. |

### Economy, social systems and LAN

| Status | Feature | Scope and limitations |
|:---:|---|---|
| ⚠️ | Merchants, repair, trainers and professions | Local services and supported learning/crafting/gathering rules exist; complete recipes, skill rules and item properties are unfinished. |
| ⚠️ | Auction house | Browse, post, bid, buy out, expire/cancel eligible listings and save escrow/delivery. Simulated traders operate independently of walking playerbots. |
| ⚠️ | Simulated market pricing | New simulated listings use a random 4–10× base multiplier before rarity/drop/mount premiums and seller variation. Existing listings and player prices are retained. Deposits, cuts and retail invoices are absent. |
| ⚠️ | Mailboxes and mail | Authored local mailbox coordinates; letters, attachments, money, COD, returns and offline recipients. Square opens nearby mail where contextual focus allows; innkeepers are not mailbox substitutes. |
| ⚠️ | Personal bank and trade | Saved 28-slot bank and revision-checked two-player item/money trades with atomic saving. Bank bags and full binding/enchantment rules remain incomplete. |
| ⚠️ | Parties | Five-player same-faction human LAN parties, invitations, leadership, roster, shared supported rewards, chat and ready checks. Raid groups and retail loot rules are incomplete. |
| ⚠️ | Host / Join LAN | Host-authoritative custom UDP realm; owner-private progress and replicated world, combat, pet and corpse state. Matching versions/content and further two-console testing are required. |
| ❌ | Guilds and guild banks | No working local guild authority/storage. |
| ❌ | Standalone PvP, battlegrounds and arenas | No complete player-versus-player ruleset, match objectives or rating system. |
| ⚠️ | Reputation and durability | Source-backed reputation gates and persistent item-instance foundations exist; complete faction, durability and repair rules remain unfinished. |

The auction board is bounded at 256 listings, with up to 224 shared by simulated sellers and walking bots. The remainder is reserved for human listings. These are local simulation limits, not claims about the original retail economy.

### Connected realms

The client contains login, realm/character selection, movement, combat, quest, inventory, vendor, trainer, mail, auction, chat, party and travel paths for compatible servers. Their presence is not proof of complete end-to-end PS4 compatibility with every server implementation. Server scripts remain server-owned; standalone limitations do not describe what an external server implements. Battlegrounds and arenas remain unverified on the console.

## Install and upgrade

1. **Back up your saves first:** `/data/wow_ps/saves/local_realm/`. Keep the complete directory, including identity and backup files; retain your configuration/action-bar files as well.
2. Install the **WoWPS 2.11** PKG on a compatible homebrew-enabled PS4. The title remains **WoWPS**, title ID **WOWE00001**.
3. Copy your original WotLK client `Data` directory to `/data/wow_ps/Data/`, retaining locale subdirectories and MPQ layout. Launch WoWPS.

For solo play choose **Single Player**. For LAN, one console chooses **Host LAN**, the others **Join LAN**. Use **2.11** with matching content on all consoles. External realms use the connected-client/server setup.

**Compatibility:** this release uses `APP_VER=02.11`, Save45 (reads
Save1–45), and LAN109. All LAN peers need this release and matching content.
These save and network identifiers are independent of the display version.
Back up saves before upgrading; do not open migrated saves with an older build.
If a higher-numbered development package is installed, the 02.11
release number is numerically lower. Back up saves and configuration before
attempting installation, and do not delete them to resolve a version conflict.

For an external AzerothCore realm, use **External Realm**, the server's reachable IP or
DNS name, and its **auth port** (normally 3724). The realm-list world endpoint
must also be reachable from the PS4. See [connection setup and troubleshooting](docs/CONNECTING.md).

**Optional collision data:** the local line-of-sight rule supports collision data extracted from your own MPQs. No extracted collision pack is supplied. Without one, that visibility query defaults to visible and cannot prevent casting through walls. A host and its guests must use matching collision content.

Runtime logs are written below `/data/wow_ps/wowps/logs/`. Keep `boot`, `wowps` and `vulkan_icd` logs together when reporting a fault.

## Controller

With the original UI active, **L1/R1** select action buttons and **L2/R2** select the micro-menu/bag icons. **Square** activates the selection or contextual world action. Open panels, text entry and carried items/spells have their own focus rules.

| Input | Default action |
|---|---|
| Left stick / right stick | Move / camera; right stick moves the pointer in cursor mode. |
| **R3** | Toggle cursor/camera mode. |
| **Cross** | Jump/swim up in world camera mode; click/confirm or pick up/place when UI focus owns the input. |
| **Square** | Selected action, talk, attack or loot. As a ghost, reclaim the nearby matching corpse before other actions. |
| **Triangle** | In local play, acquire the nearest living NPC when no target is selected; clear the existing target otherwise. |
| **Circle** | Cancel a carried reference first; otherwise interact/back/close as appropriate. |
| **D-pad** | Navigate menus; zoom/turn in the world where the UI does not own focus. |
| **L3** | Autorun. |
| **Options** | Game menu / close. |
| **Touchpad** | World map; send when a text field owns focus. |

To move a bag item or spell, pick it up with Cross, move to the destination and place it with Cross. L1/R1 can move a carried action onto the action bar. Circle cancels the reference without destroying the item. Item deletion uses a separate confirmation.

For Imp Firebolt, use cursor mode to choose the target and pet action. Circle on the Firebolt button toggles autocast, which starts disabled. Pet Attack supplies a target; the selected behavior still applies.

In the on-screen keyboard, D-pad selects, Cross types, Square deletes, R2 finishes, Circle cancels, R1 changes the character page and Triangle changes case. Full controller acceptance across every original panel remains open.

## Lighting and graphics

The controller-accessible **Video → WoWPS → Lighting** page provides volumetric quality, sun/moon rays, ray intensity, fog and bloom controls. Settings are saved independently. Quality **Off** disables ray/fog rendering; **Low/High** choose the supported quality paths. Bloom has its own control. Inspection modes are session-only; restart returns to normal rendering.

For the supported PS4 volumetric path, use **Multisampling Off**, **FSR 3 Off** and enabled shadows; **Everything** includes buildings, trees and characters. The 720p world profile is the lower-cost starting point. A 60/unlimited frame cap is a ceiling, not achieved performance.

Sun/moon shafts use geometric shadowing, not screen-space radial blur. Height fog integrates along the visible ray. Bloom and world effects run before the UI/minimap overlay. Point lights remain unshadowed, the display pipeline retains legacy color assumptions, and full physically calibrated HDR atmospheric rendering is not implemented.

Local and locally hosted day/night lighting follows PS4 Date and Time changes during gameplay. Cooldowns, transports and network timers retain their own timing; LAN guests follow the host and connected realms follow their server.

## Build

See **[docs/BUILD_PS4.md](docs/BUILD_PS4.md)**. On Linux, with the OpenOrbis toolchain installed:

```bash
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain
./tools/ps4/build_ps4_pkg.sh --jobs 3
```

`BUILD_VERSION` is the shared source of client/package identity. Update 2.11 contains `02.11`. The source includes the required vendored PS4 rendering components and checked shader binaries, but not original client data or the external toolchain.

## Credits and licenses

**WoWee — Kelsi Davis and contributors** provides the technical foundation: the C++ client, Vulkan renderer, MPQ/DBC/M2/WMO pipeline, FrameXML host and protocol implementations. WoWPS adapts that work to the console.

**OpenOrbis PS4 Toolchain**, **OpenGNM**, **opengnm-psbc** and **vulkan-ps4** provide the console tools and rendering foundations. **AzerothCore**, **TrinityCore**, **MaNGOS** and **wowdev.wiki** are credited for their server/protocol and format work; imported local data retains its notices and pinned provenance.

Vendored dependencies include StormLib, Dear ImGui, Lua 5.1, glm, vk-bootstrap, Vulkan Memory Allocator, miniaudio, nlohmann/json, stb, Catch2 and SDL2. Their respective license/author notices remain with their sources.

See [LICENSE](LICENSE) for the project's exact terms, including the non-commercial-game-use restriction. Third-party code and local data retain their own terms: in particular [the local-data notice](assets/local_realm/NOTICE.txt) and [the PS4 backend notice](ps4/third_party/ps4_vulkan/NOTICE.md). No permission to redistribute Blizzard client data is implied.
