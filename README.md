<p align="center">
  <img src="ps4/sce_sys/icon0.png" alt="WoWPS" width="160">
</p>

<h1 align="center">WoWPS</h1>
<p align="center"><em>World of Warcraft, running natively on a jailbroken PlayStation 4.</em></p>

<p align="center">
  <img alt="Version" src="https://img.shields.io/badge/version-1.61-c8a04a">
  <img alt="Platform" src="https://img.shields.io/badge/platform-PS4%20(jailbroken)-003791">
  <img alt="Client" src="https://img.shields.io/badge/client-WotLK%203.3.5a%20build%2012340-c8a04a">
  <img alt="Renderer" src="https://img.shields.io/badge/renderer-Vulkan%201.0%20over%20GNM-a41e22">
  <img alt="License" src="https://img.shields.io/badge/license-MIT%20(non--commercial)-2ea043">
</p>

---

> [!IMPORTANT]
> **WoWPS is a development and research project. Original client MPQ archives are not included.**
> You must supply your own legally obtained game data and comply with the laws in your jurisdiction.
> WoWPS is not affiliated with or endorsed by Blizzard Entertainment.

---

## What this is

WoWPS is a **native** World of Warcraft client for the PS4 — not a streaming
client, and not an emulator running the PC build. It is C++ compiled for the
console's own hardware, with a Vulkan 1.0 ICD written on top of the PS4's native
GNM/VideoOut driver.

It reads **your** WotLK 3.3.5a (build 12340) MPQ archives: terrain, buildings,
creatures, character models, textures, sounds, and the original Blizzard
FrameXML interface. Loading the original interface does **not** mean that every
underlying game system or every screen is already complete.

There are two ways to play, and they are different things:

**Connected play.** The client includes protocol support for compatible
AzerothCore, TrinityCore and MaNGOS servers. In this mode, the external server
provides the world rules and server-side content. The console port's complete
end-to-end compatibility with each server is not established by the local-realm
tests.

**Standalone play.** WoWPS includes its own local world simulation for
singleplayer and host/join LAN play. It combines your client data with a bundled,
attributed local-realm catalog. This is a **bounded ruleset written for this
project**, not a complete embedded AzerothCore or TrinityCore server. Importing
world records does not execute the original server's quest or encounter scripts.

---

## What works, and what does not

**Status: version 1.61 / PS4 package version 01.61 — 8 September 2026.**

| Status | Meaning |
|:---:|---|
| ✅ | Working within the specific scope described. This does not mean complete original-game parity. |
| ⚠️ | Partly working, but functionality, presentation or reliability is still incomplete. |
| ❌ | Not implemented or not functional in the stated mode. |

The tables distinguish the local simulation from connected play. A window, a
loaded map or a successful build does not by itself prove the corresponding game
system works. **Untested compatibility is not automatically marked ❌.** Where
there is only implementation or host-test evidence, that limitation is stated.

### Engine and platform

| Status | Feature | Scope / remaining limits |
|:---:|---|---|
| ✅ | Native PS4 application and installable `.pkg` | OpenOrbis build; the 01.61 archive includes cross-build and package-verification logs. |
| ✅ | Vulkan 1.0 ICD over GNM/VideoOut | Native console rendering and presentation. |
| ✅ | Terrain, WMO buildings, M2 objects and animated characters | Core world rendering is available. This is not a guarantee that every asset or location is free of errors. |
| ✅ | Zone and time-of-day lighting | Uses `Light.dbc` and its colour bands. |
| ✅ | Terrain, building, object and character shadows | Shadow rendering is present; 1.61 adds same-model M2 caster instancing with an individual-draw fallback. |
| ✅ | 1080p / 720p render-resolution selection | Changing render resolution is separate from correct automatic UI scaling. |
| ✅ | Original FrameXML loading from MPQs | The original interface is loaded; the PS4 build disables the native fallback. See the UI limitations below. |
| ⚠️ | Sun rays, water reflections, lens flare and portal effects | Rendering paths exist. Complete visual parity for every effect is not verified; reflections are off by default on PS4. |
| ⚠️ | World streaming and memory management | Bounded caches, model retirement, portrait allocation changes and frame-fenced GPU cleanup are implemented. Out-of-memory failures still occur in the supplied 1.61 console run. |
| ⚠️ | Performance | Pipelined submission, shadow batching/instancing and UI/streaming optimizations are present. Frame-time spikes and variable FPS remain; there is no guaranteed frame rate. |
| ⚠️ | Overall stability | The game reaches gameplay, but extended travel, character/portrait changes and shutdown are not reliably crash-free. |
| ❌ | Expansions after WotLK in this PS4 release | Not supported as a completed console gameplay target. |

**Other configurations:** PS5 compatibility has not been established. Upstream
Vanilla 1.12 and TBC 2.4.3 support must not be read as a verified PS4 feature; the
release target documented here is WotLK 3.3.5a, build 12340.

### Original interface, menus and controller

| Status | Feature | Scope / remaining limits |
|:---:|---|---|
| ✅ | Original menu, loading-screen and in-game assets | Read from the supplied client archives rather than replaced with unrelated artwork. |
| ✅ | Controller input, cursor mode and on-screen keyboard | Movement, camera, action layers and text input are implemented. |
| ⚠️ | Complete controller navigation of the original UI | Navigation and activation work, including the character microbutton and bag/spell interactions; not every original panel has been validated on PS4. |
| ⚠️ | Original 1:1 GUI | FrameXML runs, but layout, layering and class-specific behaviour remain incomplete. |
| ⚠️ | 24-slot backpack | 1.61 corrects slot rows, artwork margins and money/footer placement. The subsequent report still shows the backpack behind the lower action bar/UI. |
| ⚠️ | Automatic UI layout at different resolutions | Resolution selection and a TV safe-area setting exist. Excessive insets and incorrect automatic placement remain reported. |
| ⚠️ | Quest watch / Objectives | Tracking and progress hooks are implemented and host-tested against the original scripts; console layout is not certified pixel-perfect. |
| ⚠️ | Character and target portraits | Authored camera handling and lower-memory loading are implemented; portrait preparation can still fail when memory is exhausted. |
| ⚠️ | Death Knight resource UI | Runic Power events and six base-rune timers exist. A further Death Knight UI issue was reported after the 1.61 delivery. |
| ⚠️ | Race intros, camera and audio presentation | Intro paths, loading and narration are present; complete race-by-race fidelity and smooth playback remain unverified. |

### Connected play — AzerothCore / TrinityCore / MaNGOS

The existing client documents the following network-side capabilities. They are
**not** a claim that all of them were tested end to end on a PS4 running 1.61,
and they do **not** make the same features available in standalone mode.

| Client capability | 1.61 console verification |
|---|---|
| Login, realm list, character list, creation and deletion | Client paths exist; complete compatibility with each server implementation remains unverified. |
| Movement, combat, casting, looting, quests and chat | Client paths exist; server-side content and console correctness must be checked separately. |
| Inventory, equipment, vendors, trainers, mail and auction house | Client paths exist. Local auction delivery is not the connected server's mail system. |
| Groups, other players, transports and taxi flights | Client paths exist; full console multiplayer coverage is not established. |
| Battlegrounds and arenas | Not verified on the console. This is distinct from the missing standalone systems listed below. |

### Standalone play — world, quests, combat and characters

| Status | Feature | Scope / remaining limits |
|:---:|---|---|
| ✅ | Local world without an external server | Explore, fight NPCs, loot, gain experience and level within the local ruleset. |
| ✅ | Local character data and saves | Characters and progress are stored with the realm. Save format 9 is used in 1.61. |
| ✅ | Basic quest lifecycle | Accept, track, complete, turn in and abandon supported catalog quests. |
| ⚠️ | All original quests | The supported objective rules do not reproduce every quest chain, condition or scripted sequence. |
| ✅ | Basic NPC combat and supported spells | Melee attacks plus the implemented direct-damage, healing and periodic-damage spell subset. |
| ✅ | Cast times, interruption and cooldowns | Available for supported spells, with authority-owned resource checks. |
| ✅ | Death and revival | Present in the local gameplay ruleset. |
| ⚠️ | Complete classes and combat | Many original spell effects and mechanics remain unsupported, including channeling, stance/shapeshift requirements, totem/reagent requirements and area/scripted targeting. Unsupported abilities are rejected rather than treated as working. |
| ⚠️ | Death Knight gameplay | Runic Power, imported rune costs, six independent base-rune cooldowns and persistence are implemented. Full weapon/disease effects, talents and Death-rune conversion are unfinished. |
| ✅ | Inventory, equipment and item stats | Local 24-slot inventory and all 19 worn slots; this does not include a full bank or durability system. |
| ✅ | Local progress persistence | Equipment, supported quest/spell/profession progress, learned mounts and rune cooldowns are included in the saved state. Each console retains its local action-bar setup. |
| ⚠️ | Complete original character progression | Limited spell/recipe books and missing talent, reputation and advanced class systems prevent complete retail progression. |
| ❌ | Talent trees and talent-point spending | No complete working standalone talent system. |
| ❌ | Reputation progression and reputation-gated services | No working standalone reputation system; unsupported gated vendor offers are excluded. |
| ❌ | Pets and stables | No working standalone pet acquisition, pet progression or stable service. |

### Standalone play — travel, instances and scripted content

| Status | Feature | Scope / remaining limits |
|:---:|---|---|
| ✅ | Flight masters and taxi-route data | Discovery and travel use the supplied `TaxiNodes` / `TaxiPath` data and the local travel system. |
| ⚠️ | Ships and zeppelins | Configured hulls, authored routes, boarding, continent transfers and static crews are implemented. This is not every original transport, scripted passenger or vehicle; repeated travel still needs stability fixes and testing. |
| ✅ | Innkeepers and return-home travel | Bind a home location and return on a cooldown. |
| ⚠️ | Dungeon and raid access | Supported entrance triggers, instance maps and catalog population are available. Entering an instance is not a working scripted dungeon or raid. |
| ✅ | Acherus floor-transfer pads | The paired lower/upper-floor transport logic is implemented; effect appearance depends on available models. |
| ⚠️ | Scripted world events | Specific Acherus transfer logic exists. There is no general implementation of all original quest/world events. |
| ❌ | Complete dungeon and raid boss encounter scripts | Loading boss models or NPC records does not implement encounter phases, mechanics or scripted progression. |
| ❌ | Full server script execution in standalone mode | The local catalog does not execute the AzerothCore/TrinityCore encounter and quest-script libraries. |
| ⚠️ | Ground mounts | Supported, source-mapped mount items can be learned, saved, summoned, replicated to LAN peers and dismounted. Full riding skill/trainer requirements are missing; host tests are not a complete console travel test. |
| ❌ | Flying mounts, vehicles and unsupported scripted mount effects | Not functional in the local ruleset; unsupported mount purchases are blocked. |
| ❌ | Complete riding profession and trainer progression | Not implemented as an original-game-equivalent system. |

### Standalone play — professions, merchants and auction house

| Status | Feature | Scope / remaining limits |
|:---:|---|---|
| ⚠️ | Professions | Learning, rank training, supported recipes, reagent consumption, item creation and skill progress exist. Recipe coverage and profession mechanics remain bounded; profession unlearning is not implemented. |
| ⚠️ | Class and profession trainers | Teach the supported local spell/skill/recipe subset, not every original ability or specialist service. |
| ✅ | Basic merchant buying and selling | Per-NPC catalog stock, copper prices, purchase bundles, quantity checks and bag-capacity checks. |
| ⚠️ | Original MerchantFrame | Retail panel initialization and callbacks are host-tested; complete console UI/controller behaviour remains to be validated. |
| ⚠️ | Limited merchant stock | The host enforces shared stock and restocking. Depletion survives NPC unloading within the session, but resets on realm restart. |
| ⚠️ | LAN merchant state | Host checks the selected NPC, map, instance, range, life state and purchase. Remaining-stock counters are not yet synchronized into guest menus. |
| ❌ | Merchant buyback | No complete working local buyback system. |
| ❌ | Equipment durability, wear and real repairs | Durability is not tracked. The repair interaction is a no-op and does not charge gold; it must not be described as working repairs. |
| ❌ | Extended-currency, reputation-gated and script-conditioned vendor offers | Excluded until their conditions can be enforced. |
| ⚠️ | Playerbots | Optional wandering/fighting/gathering actors exist; they are not complete player-equivalent characters or a full economy simulation. |
| ✅ | Auction supply without walking playerbots | Virtual auction traders run independently, including when the Playerbots option is off. |
| ⚠️ | Local auction house | Browse, list, bid, buy out, expire and cancel unbid listings; includes multi-stack posting, refunds and saved pending deliveries. The exchange is bounded and not a complete retail economy. |
| ⚠️ | Original Blizzard_AuctionUI | Loaded before opening; production callbacks are host-tested. Full PS4 visual/controller and two-console transaction validation remains outstanding. |
| ✅ | Saved auction escrow and pending delivery | Full bags and offline owners retain pending items/proceeds. This is direct saved delivery, **not** a mailbox implementation. |
| ✅ | Configurable 4–10× auction base-price multiplier | In 1.61 the host selects the base multiplier; rarity/drop metadata and limited seller competition modify prices. |
| ❌ | Automatic random 4–10× base multiplier with no menu control | Requested after 1.61, but not implemented in this release. Seller price competition is not the requested random base-multiplier system. |
| ❌ | Full retail auction deposits, sale cuts and mailbox settlement | Not part of the local exchange. Cancellation after bids have been placed is also unavailable. |

The auction board is capped at **96 listings**, with at most **64 virtual-seller
listings**. Zero-vendor-value materials use an explicit local pricing rule. TCG
mount pricing uses the local **100,000-gold** cap; this is not presented as an
original retail price. Details and test limits are in the
[1.61 auction report](validation/auction-0161.md) and
[vendor catalog](tools/local_realm/VENDOR_CATALOG.md).

### Standalone play — LAN, social systems and PvP

| Status | Feature | Scope / remaining limits |
|:---:|---|---|
| ⚠️ | Host / Join LAN | Custom host-authoritative UDP realm with player, travel and gameplay-state replication. It is not feature-complete multiplayer; matching builds and further two-console tests are required. |
| ✅ | Versioned LAN messages and local save codecs | 1.61 uses LAN protocol 14 and save format 9; migration and malformed-input tests are included in the validation reports. |
| ❌ | Player-to-player trade | No working direct standalone trade system. Merchant trading and auctions are separate features. |
| ❌ | Mailboxes and player mail | No working standalone send/receive mailbox service. Auction escrow does not provide it. |
| ❌ | Banks | No working standalone bank service. |
| ❌ | Guilds | No working standalone guild system. |
| ❌ | Parties and raid groups | Sharing a LAN world or an instance is not a complete party/raid-group system. |
| ❌ | Original PvP and duels | No working standalone player-versus-player ruleset. |
| ❌ | Battlegrounds | No working standalone battleground matches, objectives or rewards. |
| ❌ | Arenas | No working standalone arena matches, teams or rating system. |

### Important remaining issues after the 1.61 delivery

The subsequent console feedback still reports backpack/action-bar overlap,
incorrect UI insets/resolution adaptation and a Death Knight UI problem. The
supplied 1.61 session also reaches an `std::bad_alloc` out-of-memory failure and a
fatal exception during shutdown. These remain open; they are not described as
fixed merely because earlier host tests passed.

The requested automatic random auction multiplier, every quest/event, all boss
scripts, complete professions/classes/social systems, full PvP, pixel-perfect
original UI and perfect stability **are not completed features of 1.61**.

### What was added or revised in 1.61

| Area | Delivered implementation |
|---|---|
| CPU memory and resource ownership | Lower-memory portrait loading, lazy animation-spline storage, periodic unused-character/attachment retirement, earlier release of uploaded CPU terrain chunks, and frame-fenced GPU destruction. |
| Shadows | Same-model M2 shadow instancing with fallback; scene coverage and visible-distance settings retained. |
| Original UI hooks | Backpack artwork/row/footer fixes, real character-microbutton handlers and improved controller focus bounds. |
| Merchant and auction services | Source-backed merchant offers, shared host stock checks, original service panels, bot-independent auction supply and durable transactions. |
| Local character resources | Supported ground mounts, Runic Power events, six base-rune timers and their saved/networked state. |
| Compatibility | Realm save format 9 and LAN protocol 14. |

The archive includes cross-compilation, package-integrity and host regression
results. Some tests use sanitizers and unmodified external retail Lua scripts
with controlled host fixtures. **Those tests do not execute the PS4 GPU, certify
pixel alignment or prove crash-free gameplay.** See
[validation and console checks](validation/README.md) and the
[character/travel memory review](validation/crash-memory-review.md).

---

## Controller

The pad provides movement, camera, action-bar and interface input. **L1, L2, R1 and R2 are modifier layers** — hold
one and the face buttons mean something else, which is how twelve action slots
fit onto four buttons.

### In the world

|Input|Action|
|---|---|
|Left stick|Move|
|Right stick|Look around, or move the cursor — **R3** toggles|
|**✕**|Left click|
|**○**|Right click / interact|
|**□**|Action slot 1; local play also uses the unmodified button for contextual talk / attack / loot|
|**△**|Target the nearest NPC or enemy|
|**D-pad ↑ / ↓**|Zoom in / out|
|**D-pad ← / →**|Turn left / right|
|**L3**|Autorun|
|**R3**|Toggle cursor / camera look|
|**OPTIONS**|Game menu / close|
|**Touchpad**|Chat|

These are the default mappings. Open panels and text entry take priority over
world actions; not every original panel is fully validated on the console.

### Action slots

|Hold|✕|○|△|□|
|---|---|---|---|---|
|**R2**|Slot 4|Slot 3|Slot 2|Slot 1|
|**L2**|Slot 8|Slot 7|Slot 6|Slot 5|
|**R1**|Slot 12|Slot 11|Slot 10|Slot 9|

### Hold L1 — movement and utility

|Input|Action|
|---|---|
|**✕**|Jump / swim up|
|**□**|Sit / stand|
|**△**|Reset camera|
|**○**|Screenshot|
|**D-pad ↑ / ↓**|Walk forward / back|
|**D-pad ← / →**|Strafe left / right|

### In menus and windows

|Input|Action|
|---|---|
|**D-pad**|Move the selection|
|**✕**|Confirm — and **pick up / put down** an item or spell|
|**□**|Use / cast / click|
|**○**|Back / close|
|**L1 / R1**|Scroll the panel, or step onto the action bar|

**Moving things around.** In the backpack or the spellbook, press **✕** on an
item or a spell to lift it onto the cursor. Walk the **D-pad** to where it
should go — another bag slot, or an action bar slot (**L1/R1** steps onto the
bar) — and press **✕** again to put it down. If something is already in that
slot the two swap, and the displaced one rides the cursor. To throw an item
away, carry it out of the bags entirely and press **✕**: the game's own English
confirmation dialog asks first, and *No* keeps it.

### While typing

|Input|Action|
|---|---|
|**△**|On-screen keyboard|
|**□**|Backspace|
|**Touchpad**|Send|
|**D-pad ↑ / ↓**|Chat history|
|**D-pad ← / →**|Move the caret|
|**R1**|Next field|
|**OPTIONS**|Close the box|

---

## Graphics settings

All of these are in the in-game options. The console defaults are chosen for
frame rate; turn them up if your console has the headroom.

|Setting|Where|Values|Default|
|---|---|---|---|
|**Shadows**|Graphics → Shadows|Off · Terrain only · Everything · Everything, sharp · sharpest|Everything|
|**Sun rays**|Graphics → Sky|On / off|On|
|**Water reflections**|Graphics → Surfaces|On / off|Off on PS4|
|**Render resolution**|Display|1920×1080 (High Definition) · 1280×720 (Normal HD)|1080p|
|**TV safe area**|Interface|0–10 % inset from every screen edge|4 % on PS4|

The shadow **scope** takes effect immediately. The two sharpest levels rebuild
the shadow map itself and need a restart. `WOWEE_SKIP_SHADOWS=1` remains an
emergency kill switch.

**Known limitation:** the TV safe-area control does not yet resolve every UI
anchor or scaling problem. A 720p/1080p render switch is not a guarantee that the
original interface immediately fits the new resolution correctly.

---

## How to build

To play a released build, install its `.pkg`; rebuilding is optional.

For development, use Linux, WSL, or the toolchain's supported macOS host layout.
The build requires the OpenOrbis PS4 Toolchain, LLVM/Clang and LLD (LLVM 18 is the
reference version), CMake ≥ 3.20, Ninja, Python ≥ 3.10 and Bash. The OpenOrbis
`create-fself`, `create-gp4` and `PkgTool.Core` tools must be executable.

From the repository root:

```bash
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain
./tools/ps4/build_ps4_pkg.sh --jobs 8
```

The matching `libopengnm.a` and `libpsbc.orbis.a` must remain in
`ps4/third_party/ps4_vulkan/lib/`; these are required PS4 link inputs, not obsolete
build files. The wrapper checks prerequisites, configures and compiles the client,
creates the eboot, assembles `sce_sys`, and prints the final package path.

Useful flags are `--clean`, `--jobs N`, `--build-dir DIR`, `--config CONFIG`,
`--title-id ABCD12345` and `--help`. The 1.61 wrapper does **not** provide a
`--verify` flag.

```bash
./tools/ps4/build_ps4_pkg.sh --help
```

The standard package output directory is `build-ps4/pkg/`. This branch's CMake
configuration is **PS4-only**; a normal desktop CMake build is not a supported
client target. Standalone host regression runners are under `tools/tests/`;
see [validation](validation/README.md) for the commands and external fixtures.

For the documented OpenSSL host-library compatibility path, the packaging script
accepts `WOWEE_PS4_LIBSSL11_DIR`. Package metadata can be set through
`WOWEE_PS4_TITLE`, `WOWEE_PS4_VERSION`, `WOWEE_PS4_TITLE_ID` and
`WOWEE_PS4_CONTENT_ID`. The 1.61 release uses title **WoWPS**, package version
**01.61**, and title ID **WOWE00001**.

---

## How to install

**1 — Back up existing saves.** Before replacing a build, copy the following
folder somewhere outside the application:

```text
/data/wow_ps/saves/local_realm/
```

Version 1.61 writes **save format 9** and reads formats 1–8. It also retains a
`realm.wprs.bak` recovery copy, but that does not replace a separate pre-upgrade
backup. Older clients cannot read the upgraded format; restore a compatible
backup when downgrading.

**2 — Install the package.** Install the released `.pkg` on your PS4 using your
usual homebrew package installer.

**3 — Install your own game data.** Copy the `Data` folder from your own legally
obtained **WotLK 3.3.5a build 12340** client to:

```text
/data/wow_ps/Data/
```

Keep the MPQ archive names and the original locale-subdirectory layout. This is
the path used by the 1.61 source; preserve its spelling and case. Do not upload
your MPQs, extracted client assets, local configuration or saves to the repository.

**4 — Run it.** Launch **WoWPS** from the PS4 menu.

| Mode | Setup |
|---|---|
| **Standalone** | Choose **Single Player**. The Playerbots option adds simulated world actors. Auction traders operate independently, even with Playerbots off. |
| **LAN** | One console chooses **Host LAN**; another chooses **Join LAN** and selects the hosted realm. Install **1.61 on every peer**: this release uses LAN gameplay protocol **14**. |
| **Connected server** | Configure a compatible server address in the realm connection flow. This uses an external server, not the embedded local ruleset. |

Runtime logs are written under:

```text
/data/wow_ps/wowps/logs/
```

When reporting a problem, include the package version, whether the realm was
singleplayer/host/guest/connected, the original logs and a screenshot or video
for a visual defect. The status above reflects the 1.61 implementation and the
available console feedback, not a full hardware certification.

---

## Mentions and credits

WoWPS would not exist without other people's work.

[**WoWee**](https://github.com/Kelsidavis/WoWee) — Kelsi Davis and contributors.
The major technical foundation of this project: the C++ client, the custom
Vulkan renderer, the MPQ/DBC/M2/WMO pipeline, the FrameXML host and the network
protocol implementations are all theirs. WoWPS is a fork that strips the desktop
platforms and adds the console.

[**OpenOrbis PS4 Toolchain**](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) — the LLVM toolchain, PS4 headers, libraries and packaging tools that make a native homebrew build possible at all.

[**AzerothCore**](https://github.com/azerothcore/azerothcore-wotlk), [**TrinityCore**](https://github.com/TrinityCore/TrinityCore) and [**MaNGOS**](https://github.com/mangos) — the server emulators this client talks to, and the reference for how the 3.3.5a protocol and world data behave.

[**wowdev.wiki**](https://wowdev.wiki/) — the community's file-format documentation for MPQ, DBC, ADT, WDT, M2, WMO and BLP.

### Vendored libraries

|Library|Version|Purpose|
|---|---|---|
|[StormLib](https://github.com/ladislav-zezula/StormLib)|vendored|Reading MPQ archives|
|[Dear ImGui](https://github.com/ocornut/imgui)|1.92.6|The client's own windows|
|[Lua](https://www.lua.org/)|5.1.5|FrameXML — 5.1 on purpose, for addon API compatibility|
|[glm](https://github.com/g-truc/glm)|vendored|Maths|
|[vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap)|1.3.302|Vulkan initialisation|
|[Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)|3.4.0|GPU allocation|
|[miniaudio](https://miniaud.io/)|0.11.24|Sound|
|[nlohmann/json](https://github.com/nlohmann/json)|3.11.3|Config and catalog parsing|
|[stb_image / stb_image_write](https://github.com/nothings/stb)|2.30 / 1.16|Image loading and screenshots|
|[Catch2](https://github.com/catchorg/Catch2)|v3 amalgamated|Test suite|
|[SDL2](https://www.libsdl.org/)|2.30.x|Windowing and input abstraction|

The PS4 Vulkan ICD, its GNM/VideoOut wrapper and its SPIR-V-to-GCN shader
compiler (`vulkan-ps4`, `opengnm`, `opengnm-psbc`) are vendored under
`ps4/third_party/ps4_vulkan`; the [renderer notice](ps4/third_party/ps4_vulkan/NOTICE.md)
records component provenance, licenses and source references.

The bundled local-realm catalog has separate attribution and licensing notices
under `assets/local_realm/`. Source-backed vendor and auction imports are
documented under `tools/local_realm/`.

---

## License

**MIT, with one additional restriction: no commercial game use.**

You may use, copy, modify, merge, publish, distribute, sublicense and sell
copies of this software. You may **not** use it, in whole or in part, as the
basis for or a component of any commercial video game or commercial game
product without written permission from the copyright holders.

MIT is the license this project inherits from WoWee and stays compatible with.
The added restriction is there because this is a preservation and research
project: it should stay freely available to the people who want to run it, study
it and improve it, and it should not quietly become somebody's product.

See [LICENSE](LICENSE) for the exact text. Vendored libraries keep their own
licenses; the local world-data subset carries its own attribution notices. The
original music that ships with upstream WoWee is not covered by that license and
is not included here.

**No game assets are distributed with this software, ever.** Blizzard's client
data is Blizzard's. Bring your own.
