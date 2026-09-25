# WoWPS Update 2.12 — validation

Validated on 2026-09-25. PS4 Release compilation/linking and PKG creation completed
with Clang 18.1.3 and the supplied OpenOrbis SDK. These results do not certify
on-console runtime behavior or establish an FPS target.

## Release identity

- Public version: 2.12; BUILD_VERSION, packaged APP_VER and VERSION: 02.12.
- Title: WoWPS; title ID: WOWE00001.
- Content ID: IV0000-WOWE00001_00-WOWEEPS4CLIENT00.
- Save45 and LAN109 are unchanged. Existing content and shader binaries are retained.

## Completed checks

- All 12 self-contained release suites passed, including the 25-group TCP/SRP/world
  socket suite and 30 lighting source/binary manifest pairs.
- Six new regression groups execute production code or its actual predicate with
  controlled dependencies: missed/coalesced VideoOut events, exact flip serials,
  scanout ownership and timeout; WMO instance allocation rollback; restoration of
  server-confirmed quests without a pending Accept click; world-map allocation
  failures and pointer ownership; pending WMO upload/finalization failures; and
  rejection of synthetic World/Cosmic IDs during automatic continent selection.
- Five additional existing checks passed: WMO setup retry, WMO material retry,
  WMO upload retirement, deferred-present reset, and memory recovery.
- Updated two older WMO test fixtures to match current production material fields,
  draw bounds and upload-completion helper. WMO setup exercised 14 allocation
  failures; material construction exercised 13 CPU and 6 GPU allocation failures.
- AddressSanitizer/UndefinedBehaviorSanitizer reported no errors in checks using
  them. LeakSanitizer is disabled because the execution environment uses ptrace;
  the new fault-injection fixtures use explicit ownership checks, not sanitizers.
- OpenOrbis PKG validation passed 28 hash, signature and structure checks.
- 147 extracted runtime files match staged input bytes by SHA-256. The generated
  keystone is separate. The package's extracted SFO independently confirms 02.12.

## Runtime changes and limits

The quest message was a reconciliation branch, not itself a server rejection.
The fix restores quests introduced by authoritative fields and keeps duplicate
acceptance blocked. Character creation now waits for the character list; this is
not proof that every server's character enumeration is compatible.

WMO preparation counts queued uploads as well as running workers, caps retained
predecoded textures at 8 MiB per job, and retains work across allocation failures.
The source fixes concrete ownership/retry bugs; the excerpts alone cannot identify
every consumer exhausting the PS4 CPU heap. Repeated renderer allocation failures
queue normal session logout after bounded, paced retries. Unrelated unrecoverable
allocation failures may still terminate the application.

The world-map automatic redirect now rejects overview sentinels rather than
reloading a fictional physical map named World. Texture preparation retries retain
completed tiles; map command recording no longer allocates texture-slot storage.

VideoOut status (matching currentBuffer and flipArg) proves which submitted flips
completed, independent of event delivery. A genuine surface failure still requires
application exit; the client no longer repeatedly waits on that lost surface. This
is not automatic recovery from a GPU hang or a real VideoOut failure.

## Console retest

1. Log into the same AzerothCore realm and verify existing/new character selection.
2. Accept quest 7 or revisit its giver, then reopen the quest log and tracker.
3. Visit the area that failed while spawning game objects; test nearby doors and
   transports and remain in the area long enough to exercise memory pressure.
4. Open and navigate the world map repeatedly, including World/continent views.
5. Turn the camera, change zones and return to login. Send fresh wowps, boot and
   vulkan_icd logs if an allocation failure, surface loss or prolonged stall remains.

Host fixtures do not emulate the PS4 display engine, allocator or a live realm.
No console hardware run was performed for this build.
