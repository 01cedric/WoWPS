# Changelog

## 2.12 — quest and memory stability

- Restore server-confirmed quests to the local log without requiring a pending local Accept command; track an already accepted quest when reconciling the quest dialog.
- Keep asynchronous game-object results until their upload owner is secured. Bound running/prepared WMO jobs together and cap each job's decoded texture reserve at 8 MiB.
- Defer game-object allocation failures after releasing disposable caches. Retain queued requests and partial WMO uploads; clear borrowed decode-cache pointers on every exit.
- Roll back WMO instance/spatial-index creation on allocation failure and resume transport/doodad follow-ups without duplicating the render instance.
- Ignore synthetic World/Cosmic IDs during automatic continent selection, preventing unnecessary world-map data reloads. Keep map slot allocations outside command recording.
- Publish world-map texture pointers only after ownership is committed. Preserve completed tiles across allocation failures and retry after a cooldown.
- Pace renderer memory retries, release cached interface sources, and queue a normal session logout when repeated recovery fails.
- Reconcile asynchronous VideoOut completion from the exact flip identifier and current scanout buffer. Do not reclaim displayed images from acquire fences or device-idle alone; stop retrying an unrecoverable lost surface every frame.
- Keep online character creation on the selection screen until the server character list is ready. This does not bypass authentication or fabricate an empty character list.

Build, regression and package results are recorded in [build validation](docs/BUILD_VALIDATION.md).
Real PS4 memory-pressure and presentation behavior still require a console retest.


## 2.11 — PS4 connection hotfix

- **PS4 TCP setup:** use the PlayStation `SO_NBIO` socket option for authentication and world connections. This replaces the `fcntl` mode switch that returned `EACCES` on a reported console and stopped login before contacting the auth server.
- **Failure handling:** report the failing `setsockopt(SO_NBIO)` operation and its error code. Failed setup still closes the socket; connection deadlines and nonblocking send/receive remain enforced.
- **Regression coverage:** exercise the production PS4 mode-switch helper while `fcntl` and `ioctl` are denied. Verify a loopback connection, two-way traffic, an empty nonblocking receive and propagation of socket-option failures. This host fixture does not emulate the PS4 kernel.
- **Release:** package/client version `02.11`, GitHub-ready source and updated connection instructions. Save45, LAN109, runtime content and the 2.10 authentication/encryption fixes are retained.

PS4 installation and a live AzerothCore login through world entry still require
console testing. See [build validation](docs/BUILD_VALIDATION.md).

## 2.10 — changes since 2.00

Console feedback subsequently showed the nonblocking mode switch was still
rejected; the targeted correction is listed under 2.11 above.

- **Connected realms:** use the OpenOrbis nonblocking TCP path for authentication and world connections; set the BSD address length; check connection-status errors; retry interrupted and partial sends with a deadline; discard broken streams before further encrypted traffic.
- **Wrath authentication:** frame proof responses by the announced client build, retain fixed-width SRP keys/salts, and handle zero-prefixed shared secrets. Send world authentication and enable encryption under one lock so an immediate encrypted reply cannot race the background receiver. Fragmented packets, stored credential hashes, account rejection and server-proof verification have dedicated regression coverage with both OpenSSL and the bundled crypto implementation.
- **Rendering and streaming:** reduce recurring allocation, draw-submission and shadow setup work; retain useful scratch/cache data; defer upload retirement and improve memory ownership across world transitions. Sustained FPS remains a hardware measurement, not a release promise.
- **Movement and session stability:** tighten stairs, ground/water transitions, transport-local coordinates, boarding ownership, death/travel boundaries and session-reset cleanup.
- **Local combat and persistence:** extend source-backed creature melee/ranged/magic effects, mana, control, auras, procs, summons and ground effects, plus item-instance/reputation foundations and saved script state. Complete class and encounter fidelity remains unfinished.
- **World interactions:** expand reviewed objects, chests, gathering nodes, exclusive pools, chairs, holiday/interval events, authored vehicles, escorts, script actions and quest-chain rules. Unsupported content stays explicitly blocked.
- **Creature behavior and interface:** add default wander/patrol paths, timed action lists, gossip menus and conditions, text-emote script events, and the original GossipFrame bridge. The current generated family covers 4,655 creature entries and 2,243 spell IDs within its documented scope.
- **Release and compatibility:** consolidate documentation for Update 2.10; retain Save45 and LAN109 independently of the display version; preserve licences, attribution, provenance and tests. Back up saves and use matching LAN peers.

PS4 installation, connection to a user's live realm, original-data visual checks,
full LAN acceptance and long-session stability require console testing. See
[build validation](docs/BUILD_VALIDATION.md) and [remaining work](TODO.md).

## 2.00 — changes since 1.90

This release brings together the following changes to WoWPS. Descriptions refer to the implemented scope, not complete retail parity. Source/host validation is separate from final PS4 gameplay, visual and performance acceptance. See [README.md](README.md) for the feature matrix and [TODO.md](TODO.md) for remaining work.

### World rendering, lighting and materials

- Added world-space volumetric sunlight/moonlight using scene depth and directional shadow visibility. Rays stop at visible geometry and follow the actual light direction, including an offscreen source. They are not a radial-blur effect or a replacement sky texture.
- Added near/far shadow-atlas sampling to the volume path, with finite integration intervals, consistent cascade transitions and source-facing scattering. The camera's interior classification no longer disables every ray through a doorway; geometry/shadow visibility determines openings. Underwater and unsupported resource-layout cases remain guarded.
- Added height-dependent volumetric fog and independent bright-surface bloom. Their controls and intensities are saved separately; volumetric quality Off disables the ray/fog path. Fog-only and zero-scattering frames avoid unnecessary shaft work while retaining the analytical fog contribution.
- Added depth-aware reconstruction and full-image radiance denoising, including exact surface-clipped fallback rays where low-resolution guides cannot be used. Foreground/background guides are checked individually, surviving weights are normalized, and thin depth layers are not filled from incompatible neighbors. Filtering affects radiance rather than world textures or the UI.
- Replaced strongly correlated pixel jitter with stable hashed samples and added conservative within-shadow-texel coverage integration. Optimized reconstruction depth math, Gaussian weights and radiance filtering without removing the supported fallback path. The final attachment path uses RGBA16F; it does not claim a narrow-format memory saving.
- Tuned shaft directionality and regional colors, reduced excessive source exposure, and added bounded attenuation in elevated areas when the light is at the same level or above. These controls change scattered light rather than globally darkening terrain or replacing authored fog colors. Residual scene-dependent noise/banding remains a console test item.
- Unified sun/moon discs, directional light, shadow orientation, horizon fade and clouds around the resolved regional light. Celestial discs face the camera; authored sky rendering no longer paints over the procedural celestial pass. Corrected the moon phase update's time-unit use without claiming a date-based lunar calendar.
- Refined Tirisfal, Silverpine and Duskwood's cooler muted palette, bounded night brightness and Durotar/Orgrimmar evening warmth. Weather, clear-server weather, overcast intensity and water reflections share the resolved forecast. Actual camera water depth selects underwater lighting; gameplay swimming alone does not.
- Corrected WMO diffuse/specular/unfogged distinctions, unlit outdoor light handling, M2 leaf transmission and cutout fringe tint, and back-facing/degenerate highlights. Character unlit emission is no longer doubled. Existing authored interior color remains an approximation rather than a claim of full original shader parity.
- Improved water Fresnel, zone-lit fallback, view-ray absorption, refraction validity and foreground rejection; black captured pixels are no longer automatically considered missing. Simplified shoreline foam without adding a new scene pass. Planar reflections remain optional.
- Kept the minimap and original interface outside volumetric/bloom processing, and corrected reversed-edge smoothstep use in affected sky, flare, weather and particle shaders.
- Corrected single-sample M2 cutout rejection before derivative/mip coverage calculations, refreshed multisample alpha-to-coverage state per material, and fixed DXT5 transparency classification for equal opaque endpoints with a transparent selector. Rejected cutout fragments no longer write color/depth as opaque sheets.

### Shadows, PS4 backend and performance

- Added a focused near shadow region and retained configured distant coverage, with independent texel snapping, boundary blending, world-unit bias and tile-local filtering. The atlas improves near detail but adds storage and cascade work; distant subtexel caster omission is a deliberate shadow LOD tradeoff, not a lossless claim.
- Applied conservative finite-light-volume culling to terrain, buildings and characters so off-camera/upstream objects can still cast onto visible ground. Added actual WMO index-range bounds and camera-projected receiver-footprint rejection, including filter padding, wind, scale/shear and reflected-camera cases.
- Added material-aware WMO/M2 shadow shaders with authored alpha masks, UV channels/animation and foliage wind. Kept opaque merging/instancing and ordering where compatible. Per-cascade matrix/descriptor storage prevents later passes from overwriting earlier recorded state.
- Explicitly set depth-format polygon-offset state and converted Vulkan slope-bias units for the PS4 backend. Corrected render-pass release packet emission and shader-readable cache invalidation, and explicitly selected suitable GPU memory for shadow attachments.
- Added explicit fragment-depth output to the shadow compatibility path. Ground shadows and shafts were confirmed in tested scenes with this path; that does not certify every receiver or geometry type.
- Removed redundant descriptor copies, unused vertex-slot hashing, repeated index/register binds and equivalent adjacent barriers where their full coverage is already satisfied. Required GPU completion, image ownership and display synchronization remain in place.
- Avoided PS4 M2 GPU-culling work whose results the active renderer did not consume. Requested three scanout images with a two-image allocation fallback, while retaining two frame slots and completion safety.
- Cached exact UV animation results, dense-scene shadow snapshots, local-light candidates and camera visibility groups. Reused unchanged character placement and pose uploads across animation, shadow, color, attachments and reflections. Identity texture transforms avoid unnecessary instance work; changed inputs invalidate caches.
- Merged only compatible contiguous WMO/character draw ranges, preserving material state, geometry order and transparency. Reused current shadow/collision candidate indices and maintained ribbon/water-vegetation eligibility lists instead of rescanning every world object.
- Reduced local-light work outside its radius, compacted M2 placement/shadow records, and avoided repeated index repair, duplicate animation-clock work and unnecessary bone loads. Simulation, animation and view-distance reductions are not substituted for these exact-work optimizations.
- Added CPU phase diagnostics for update, streaming, shadow preparation, frame setup, acquisition, submission and presentation. Invalid PS4 timestamp/occlusion-query contracts are rejected instead of reporting invented GPU timings. Render-completion wait remains CPU wall time, not a calibrated GPU-pass measurement.
- These changes target measured/identified costs. **Sustained 30+ FPS with effects enabled, all-zone performance and freedom from visual artifacts remain unverified release goals.**

### Streaming, memory, intros and world presence

- Moved large city/model/terrain geometry and selected decoded audio storage into a bounded CPU write-back direct-memory pool, retaining ordinary allocation for small objects. Reduced temporary growth peaks and large embedded child-model records without increasing the pool's quota.
- Bounded optional texture prefetch under memory pressure, reclaimed no-longer-needed raw/preparation caches and added diagnostics for failed WMO/model/renderer substages and actual memory ownership.
- Retained interrupted upload ownership through allocation failures and retried safely after completion. Deferred M2/WMO retirement waits for upload and frame ownership rather than freeing live resources. Permanent allocation/submission failures are not silently declared recoverable.
- Reused exact terrain alpha masks with byte-checked collision handling and corrected partial-tile ownership/retirement so shared textures survive until all consumers and uploads are finished.
- Published usable terrain before all surrounding scenery completed, prioritized current/cinematic scene repair over distant speculative work, and deduplicated repair requests with retry backoff. Compact placement lookups avoid repeated full copies and quadratic merging.
- Released reloadable session cache capacity after borrowers and the old renderer are destroyed, before allocating the replacement renderer. Added low-allocation diagnostics around session reconstruction. Repeated-session crashes are not marked solved solely by these changes.
- Mailboxes and transports now verify that their renderer instances still exist; lost instances can be recreated, failed model uploads retry after a delay, and queue allocation failure no longer silently discards a valid request.
- Intro readiness tracks terrain and its actual building/object preparation. Completed-shot readiness latches; genuine load pauses hold a valid view and pause narration instead of needlessly restarting presentation. Failed readiness returns to the spawn without marking the intro seen.
- Added lookahead repair for already loaded but incomplete scenery along the upcoming camera corridor. Required scene work takes priority. Minimap parsing and postprocessing resource preparation move into world loading rather than the first live shot; that work is relocated, not claimed to disappear.

### Death, corpse runback, recovery and travel

- Death records the body's map, instance, position and orientation. The local authority releases the character as a ghost after three seconds, choosing an eligible zone/faction graveyard, with a nearest eligible same-map fallback when zone routing is unavailable.
- Added a separately rendered corpse, moving ghost appearance/view and corpse minimap state. **Square within 10 yards**, on the matching map/instance, reclaims the corpse with half health/mana. Reclaim takes priority over action bars, NPC interaction and combat; the old instant sanctuary-revival shortcut is removed.
- Dungeon runback retains the original instance binding. Returning through the entrance cannot create a different instance or claim an unrelated corpse. Ghost/corpse state persists and replicates through the local save/LAN authority.
- Relocation streaming waits for destination readiness instead of logging out merely because zero terrain tiles are briefly resident. Gravity pauses during the wait and the waiting camera does not overwrite the authoritative position; bounded failure handling remains.
- Added solid-floor recovery for local/LAN play. Stable terrain, WMO/city and model contacts are remembered and revalidated before correction. Teleports, map changes, travel ownership and ghost transitions invalidate stale checkpoints.
- A swept-floor check catches passing beneath a recently confirmed solid floor even when lower terrain exists. Ordinary ledges, stairs, roofs, underground rooms and water are distinguished conservatively. Recovery preserves life/ghost state and does not issue a GM resurrection or override an external server's movement authority.
- Corrected related combat/target lifecycle cleanup across death, respawn, leash reset, transport, flight and instance changes. Full dungeon, lift, water and long-distance travel acceptance still requires actual console/client geometry.

### Spell rules, casting and combat resolution

- Corrected Spell.dbc stack/proc field decoding and BaseLevel/SpellLevel handling across the supported import/runtime paths. Preserved source damage ranges, per-level values, maxima and cast-time snapshots instead of flattening periodic effects prematurely.
- Added stable fractional timing across casts, auras, cooldowns, runes and regeneration. Separated spell and shared category cooldowns, kept them active through travel/training changes, and aligned authority/UI cost, range, cooldown and usability calculations.
- Added source movement/damage interrupt behavior and bounded damage pushback, with supported passive talent reductions and distinct fully absorbed-hit handling. Cast sequence/delay, success and abort events synchronize without restarting presentation unnecessarily. Costs, cooldowns and effects commit after final preflight.
- Enforced real equipped class/subclass/inventory-type requirements, source facing arcs, creature-target masks, creature combat reach and range allowance consistently. Unsupported or missing required equipment metadata is not bypassed.
- Added source-aware armor mitigation, shared melee-class hit/critical/block handling, spell hit rating, conditional guaranteed criticals, and creature immunity/partial-resistance data. Frost Shock has its supported binary-resistance rule; that is not a universal player/creature resistance implementation.
- Added Chain Lightning and Chain Heal's bounded distinct-target selection and diminishing jumps, with stable tie ordering and a single resource commit. Added the supported Arcane Explosion radius/target-cap behavior and normal kill/quest credit. Unsupported chain and area shapes remain rejected.
- Added actual weapon speed/damage and main/off-hand swing rules, source-backed class/race/level attributes, attack power/rating, avoidance/block/critical/glancing/crushing outcomes and correct proc timing inputs. UI statistics and combat text share those results.
- Added threat from damage and effective healing, source modifiers, initial threat and target-switch thresholds. LoS/facing/target validity and combat membership affect casts and services consistently; loot ownership remains separate from threat.
- NPC life epochs prevent stale periodic/control effects from attaching to a respawned actor. Casts and threat are cleared or rejected on invalid targets, travel, death, lost equipment and applicable state changes. Viewer-specific threat snapshots and original threat APIs do not fabricate unavailable values.
- Added a local line-of-sight rule and extraction/runtime support for user-owned collision inputs, including LAN fingerprinting. **No collision pack ships with this release; without it the visibility query defaults to visible.**

### Auras, shields, procs and creature control

- Added supported fixed stat, shield and periodic stacking with bounded source limits, refresh behavior, owner counts/timers and cancellation. Save/load normalizes invalid legacy stacks and capacity. Periodic effects from different casters retain distinct attribution where the source rules allow it.
- Added Lightning Shield, Water Shield, Thorns and Mana Shield behavior, including ranked child effects, charges/chance/internal cooldown, passive mana regeneration and mana-funded partial absorption. Elemental shields use explicit exclusivity and preserve outstanding cooldowns across replacement.
- Added explicit rank-chain and spell-group stacking rules, including replacement by a different rank, stronger-effect exceptions, caster-scoped curses and supported armor/shield groups. Trainer/spellbook rank identity no longer relies on names or unrelated columns.
- Expanded bounded proc dispatch with real source/recipient, family, school, hit result, hand, phase and effect metadata; original-caster timing, chance/PPM modifiers, prepared costs, raw-source qualifiers and effect masks. Charges/cooldowns are reserved before callbacks and root/depth limits prevent uncontrolled recursion.
- Added successful-cast FINISH events, source-first prepared callbacks and persistent application identity so refresh/removal/vector reuse cannot debit the wrong aura. Unknown conditions and incomplete effect profiles remain rejected.
- Implemented supported Flurry, Clearcasting/Omen, Blood Craze, Molten Armor, Ward reflection/Molten Shields, Ignite, Stormstrike vulnerability/Improved Stormstrike and related charged/snapshotted effects. Each retains its own source filters and limits rather than granting generic proc behavior to every spell.
- Added owned-creature event attribution and the Go for the Throat focus recipient. Added Retribution Aura's owner emitter/derived recipient separation, party/range/lifecycle checks and source-order arbitration. Pet-owned proc auras and general raid/area/item/enchant producers remain incomplete.
- Added supported periodic critical and Resist/Immune/Deflect result metadata, plus reviewed incoming NPC spell execution and control impacts. Representation of an outcome is not proof that every possible producer exists.
- Added creature stuns, silence, applicable damage-break rules and bounded diminishing-return groups with reset/expiry behavior. Creature source immunity/resistance, effect-level stripping and supported dispel shape metadata are retained. General player control, immunity, dispels and full aura interactions remain open.

### Character progression, talents, forms and pets

- Expanded source-backed cast-time, mana-cost, cooldown, range, damage/healing, pushback, threat, critical, attack-speed and regeneration talents with family/effect matching. Only valid learned ranks, actual tier/prerequisite points and supported complete profiles contribute.
- Opened the supported normal Warrior Fury route to Bloodthirst/Flurry, Shaman Flurry prerequisites, and Druid/Mage Clearcasting routes without bypassing point rules. Added supported Bloodthirst healing/weapon behavior, Death Wish damage modifiers, Arcane Blast's stack/cost interaction and relevant passive prerequisites. Unsupported side effects remain explicit limits.
- Added source-backed forms/stances and Ghost Wolf, current form models/UI and hidden Druid mana. Corrected form boosts, attack power/stamina, critical/armor penetration, source entry-resource rules and supported Furor/Tactical Mastery interactions.
- Added target-life-bound combo generators/finishers, rear-position/energy checks, capped points and appropriate miss/resource handling for the supported profiles. Remaining form, finisher and class mechanics are not declared complete.
- Reworked level/class health and mana, gear/stat interaction, percentage base-mana costs, spirit/intellect regeneration, spending windows, timed health recovery, rage/runic decay and passive mana bonuses. Fractional credit cannot accumulate indefinitely at a full resource bar or resurrect a dead character.
- Action bars reconcile known rank successors after training, reset and login, preserve valid lower ranks and utility bindings, and reject invalid metadata/cycles. Local actions route to the local authority rather than a disconnected network path.
- Added real owned creature/guardian roster, separate threat/damage identity, owner reward forwarding and pet state replication/persistence. Pet data uses source summon/stat profiles rather than requiring summon-only actors to appear in the world-spawn table.
- Added the Imp's Firebolt ranks, manual casting, saved autocast setting, mana/regeneration, cast/projectile timing, target/action validation and local pet-bar routing. Pet commands and lifecycle are connected within the supported subset; full pets/stables/auras remain unfinished.
- Corrected world-entry validation so legitimate NPC-only imported spells do not invalidate the whole local catalog while player class checks remain. Added optional prepared level-80 test characters without overwriting existing characters; level is applied before derived pools and spellbooks.

### Original interface, controller, services and local clock

- Fixed action-bar pickup/replacement/cancellation semantics and original-UI focus routing. Enemy targeting retains spell selection/casting; panels, text entry and explicit menu selection take priority over nearby world actions.
- Canonicalized FrameXML frame types so original options category buttons participate in controller/pointer selection. Routed graphics/effects/lighting controls through the original hierarchy and kept diagnostic inspection session-only.
- Improved forward anchors, root-size/resize event ordering, retained safe-area preferences and the one-time default-inset migration. Improved quest, aura, target threat, combat result, statistics, rune, form, talent and pet state delivery to the original UI.
- Replaced guessed mailbox/innkeeper placement with authored local mailbox coordinates shared by rendering and authority. Removed innkeeper mail authorization as a substitute for a mailbox. Saved letters, attachments, escrow and recipients are retained; connected mailbox placement stays server-owned.
- Kept supported service/travel availability tied to real combat membership and valid local targets. Existing saved banking, trade, mail and auction transactions retain rollback and acknowledgement rules; this release does not claim retail deposits, cuts or complete item-instance handling.
- Local/hosted day/night lighting reads the PS4 local clock while running and reanchors after clock/timezone changes. Joined clients follow the host; external servers keep their own authority. Cooldowns, transports and network timeouts do not change with the lighting clock. Failed RTC reads keep the last interpolated clock and produce bounded diagnostics.

### Persistence, LAN, diagnostics and distribution

- Save format **32** retains current local state, including forms/hidden mana, supported auras and snapshots, owned actors/autocast and ghost/corpse state, while reading earlier formats **1–31**. Back up the complete realm directory before migration; older clients cannot read the new save format.
- LAN gameplay protocol **87** carries the supported owner-private progress and world/combat/target/control, pet, form, travel and corpse state. Snapshots, identities, lifetimes and content fingerprints are checked before applying them. Host and guests must run the same release with matching optional content.
- Added focused regression fixtures for import/runtime rules, transaction/lifecycle boundaries, UI input, rendering math, driver command behavior, upload ownership and recovery. Tests requiring original extracted client inputs remain separate from self-contained checks.
- Unified public release identity as **2.00** and package/client build identity as **02.00**. The title, title ID, data/save paths and actual save/network compatibility identifiers are retained.
- Consolidated the release documentation, feature status, build instructions and remaining-work list. Kept third-party licenses, attribution and imported-data provenance with the distribution. Original Blizzard MPQ/DBC files and the external toolchain are not included.

### Remaining limitations

Complete classes/procs/auras/pets, channels and all spell interactions, scripted quests and boss encounters, full professions/economy/item rules, reputation/durability, guilds, raid rules and standalone PvP are not finished. Original UI coverage, sustained console FPS, all-zone graphics, long-session stability and complete two-console/connected-server acceptance remain open. This release does not claim full WotLK parity.

## 1.90

Previous public release. Existing functionality forms the baseline for the changes listed above.
