# WoWPS Update 2.11 — build and package validation

Validated on 2026-09-25. These results describe this release's source, host tests
and package; they do not certify a PS4 installation or a live server session.

## Release identity

| Field | Value |
|---|---|
| Public release / CMake project | 2.11 / 2.11.0 |
| BUILD_VERSION / packaged APP_VER / VERSION | 02.11 |
| TITLE / TITLE_ID | WoWPS / WOWE00001 |
| CONTENT_ID | IV0000-WOWE00001_00-WOWEEPS4CLIENT00 |
| Build | PS4 Release, supplied OpenOrbis SDK |
| Host compiler / build tools | Clang 18.1.3, CMake 3.28.3, Ninja 1.11.1 |
| Save / LAN format | Save45 / LAN109, unchanged |

## Build and package

- Fresh CMake configuration, all 778 build steps including the PS4 compile/link,
  and PKG creation completed successfully. Existing compiler warnings remain.
- The compiled authentication/world socket objects call `setsockopt` with
  `SOL_SOCKET=0xffff` and `SO_NBIO=0x1200`; neither imports `fcntl` or `ioctl`.
  This verifies the target build uses the correction, not console acceptance.
- The OpenOrbis package validator passed all **28 hash, signature and structural
  checks**. These are homebrew package checks, not retail signing/notarization.
- **148 extracted runtime files**, including the executable and **95 shader
  binaries**, match their staged inputs by SHA-256. The generated keystone is
  accounted for separately; the icon and SFO were extracted from package entries.
- The actual package SFO contains APP_VER and VERSION `02.11`, TITLE `WoWPS`,
  TITLE_ID `WOWE00001` and the content ID above.
- **30 lighting GLSL/SPIR-V manifest pairs** passed their source/binary integrity
  checks. Unchanged shader sources were not recompiled or hardware-rendered here.

## Host regression checks

The self-contained release runner passed **11 suites**. No AddressSanitizer or
UndefinedBehaviorSanitizer errors were detected in their output. Instrumentation
is enabled by the individual runners; not every check is sanitizer-instrumented.

| Suite | Coverage | Result |
|---|---|---|
| Network/authentication | PS4 socket-control fixture, production TCP/SRP and world socket | PASS |
| Lighting shader pairs | Committed GLSL/SPIR-V manifest | PASS |
| Package staging | Repeat staging and unsafe output-directory rejection | PASS |
| M2 cutout shader | Source and alpha-coverage guards | PASS |
| BLP DXT5 alpha | Alpha classification | PASS |
| Ground recovery | Solid-floor recovery rules | PASS |
| Graveyard sites | Eligibility and site selection | PASS |
| Local wall clock | Clock synchronization | PASS |
| Death FrameXML | Death interface bridge | PASS |
| Ghost presentation | Presentation routing | PASS |
| Settings access | Category access and input | PASS |

The network suite includes 25 reported passing groups. Two new groups compile
the production PS4 mode-switch helper and use a host socket-option adapter while
`fcntl` and `ioctl` explicitly return `EACCES`. They verify the `SO_NBIO` option
and integer payload, loopback connection, two-way traffic, nonblocking empty
receive, preserved descriptor flags and propagation of option/descriptor errors.
The adapter maps the PS4 option to a Linux socket for the test; it does not
execute a PlayStation syscall. This closes the earlier host-test coverage gap
without claiming the previously failing console has been retested.

The network suite also covers proof framing at every split for four client
builds; fragmented realm lists; short writes; EINTR/EAGAIN; status-query failures;
reconnects; terminal failure responses followed by FIN; and deadline/descriptor
guards. Eighteen synthetic authentication exchanges cover normal login, proof
splits, padded SRP values, a zero-prefixed shared secret, saved credential hashes,
account rejection and invalid server proofs, using both system OpenSSL and the
bundled PS4 crypto implementation.

Two additional world-transport exchanges test immediate encrypted replies and
fragmented headers with the background receiver both disabled and enabled. They
exercise the production socket/cipher transition using a fixture payload, not a
complete GameHandler login or character/world-entry session. Host tests cannot
prove console syscall behavior; the PS4 target was separately compiled/linked.

Reproduce the host checks from the source root:

```bash
CXX=clang++ CC=clang python3 tools/tests/run_release_checks.py build-test-results
```

See [BUILD_PS4.md](BUILD_PS4.md) for build/package commands and
[CONNECTING.md](CONNECTING.md) for endpoint settings and console acceptance.

## Source preparation

- Python and shell syntax checks passed for 148 Python files and 187 shell files.
- Public documentation's local file links resolve. Every delivered source file
  passes the repository's Git ignore check; no delivered file exceeds 100 MiB.
- Project/third-party licences, notices and credits are retained. All 240
  supplied runtime files under `assets/`, `Data/` and `addons/` remain unchanged.
- Obsolete development handovers and superseded validation notes were removed
  from the release copy. Public documentation is updated for Update 2.11.
  Toolchains, build products, personal state, test logs and Python caches are
  excluded from the source ZIP. Required renderer libraries remain included.

## Still requires console testing

No actual PS4 installation or live AzerothCore connection was performed here.
Verify login, realm selection, character selection, world entry, logout and
reconnection on your console/server. Original MPQ/DBC visuals, full LAN play,
sustained FPS, long sessions, save migration and every class/quest/encounter
remain outside these checks. Additional data-dependent tests were not all rerun.

Back up saves and configuration before installing. `02.11` upgrades the public
`02.10` package normally, but is numerically lower
than higher-numbered development packages; do not delete saves to work around
an installer version conflict. The display version does not downgrade save or
LAN protocol identifiers.

## Socket API references

- [WebKit PlayStation nonblocking helper](https://github.com/WebKit/WebKit/blob/main/Source/WTF/wtf/playstation/UniStdExtrasPlayStation.cpp)
  uses the `SO_NBIO` socket option.
- [shadPS4 network ABI definitions](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/network/net.h)
  identify `SO_NBIO=0x1200` and `SOL_SOCKET=0xffff`.
- The supplied OpenOrbis library exports the descriptor-based `setsockopt`
  function. This release keeps the existing kernel socket descriptor API.
