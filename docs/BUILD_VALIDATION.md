# WoWPS 2.00 — build and package validation

## Delivered identity

|Field|Value|
|-|-|
|Public release|2.00|
|BUILD\_VERSION / APP\_VER / VERSION|02.00|
|TITLE / TITLE\_ID|WoWPS / WOWE00001|
|CONTENT\_ID|IV0000-WOWE00001\_00-WOWEEPS4CLIENT00|
|Build type|PS4 Release, OpenOrbis target|
|Host compiler|clang / clang++ 17.0.0 with the supplied OpenOrbis SDK|
|Validation date|2026-09-20|

## Checks performed

* Initial clean configure/compile/link completed successfully. The final integration rebuild and final PKG creation also returned exit code 0. Compiler warnings remain; this is not a warning-free-build claim.
* **28/28 package hash, signature and structural checks passed** using the toolchain's package validator.
* **138 extracted runtime files**, including the executable, match the staged inputs by SHA-256. This includes **95 packaged shader binaries**. The package tool's generated keystone is accounted for separately.
* **30 lighting GLSL/SPIR-V manifest pairs passed** the source/binary hash and SPIR-V header guard. This is a pairing/integrity check, not a new hardware rendering test or recompilation of unchanged shader sources.
* The actual package SFO contains **APP\_VER 02.00**, **VERSION 02.00**, **TITLE WoWPS** and **TITLE\_ID WOWE00001**. The packager adds its normal PUBTOOLINFO/PUBTOOLVER fields; those are not expected to be byte-identical to the staging SFO.
* **10/10 self-contained host suites passed**, with no detected AddressSanitizer/UndefinedBehaviorSanitizer error in their output. Individual runners apply sanitizers where implemented; this is not a claim that every runner is instrumented.
* A lexical comparison of **1,342 production C/C++ source/header files** found no control-flow or numeric-token changes after accounting for the renamed identifiers. The three changed runtime literals are two renamed include paths and a release-labelled diagnostic. This is a source comparison, not full behavioral certification.
* **113 retained binary/data archive files** compared unchanged to their supplied inputs. Runtime world/game values are not rewritten to hide numbers resembling versions.
* Syntax checks passed for **119 Python files**, **157 shell files**, **49 strict JSON files** and the comment-enabled grass-biome JSON file. Public local documentation links were checked.
* Project license text and third-party author/license notices are retained. Renamed references in the local-data notice are updated without removing its attribution or terms.

## Host suites

|Suite|Result|
|-|:-:|
|Lighting shader pairing|PASS|
|Runtime package staging safety|PASS|
|M2 cutout shader source/coverage|PASS|
|BLP DXT5 alpha classification|PASS|
|Solid-floor recovery|PASS|
|Graveyard eligibility/sites|PASS|
|Local wall-clock synchronization|PASS|
|Death FrameXML bridge|PASS|
|Ghost presentation routing|PASS|
|Settings category access/input|PASS|

Reproduce these checks with `python3 tools/tests/run\_release\_checks.py build-test-results`. Other native suites and data-dependent regressions remain available in `tools/tests/`; the complete suite collection was not rerun for this release preparation.

## Not performed

No PS4 installation or hardware play session was performed here. Original MPQ/DBC visual/UI acceptance, full LAN/external-server testing, sustained FPS, long-session stability, complete save migration and every class/quest/encounter remain unverified by these checks. Release cleanup is not a new gameplay implementation or a claim of completed WotLK parity.

