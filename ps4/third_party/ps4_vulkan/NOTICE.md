# PS4 renderer: components and notices

This directory contains the Vulkan backend source and headers used by WoWPS. The backend submits work through OpenGNM and the PS4 GNM/VideoOut interfaces. PSBC compiles SPIR-V into GPU instructions.

## Components

| Files | Component | Notice |
|---|---|---|
| `source/vulkan-ps4/`, `include/vk_ps4*.h`, related compatibility headers | vulkan-ps4 | `licenses/vulkan-ps4-LICENSE`. |
| `include/vulkan/`, `include/vk_video/` | Khronos Vulkan headers | Copyright and license notices in each file. |
| `include/gnm*`, `include/pm4/` | OpenGNM interfaces | `licenses/opengnm-LICENSE` and source notices. |
| Required `lib/libopengnm.a` | OpenGNM native implementation | `licenses/opengnm-LICENSE`. |
| Required `lib/libpsbc.orbis.a` | OpenGNM PSBC, including Mesa-derived compiler components | `licenses/opengnm-psbc-LICENSE`; preserve applicable Mesa and Khronos per-file notices when building or redistributing the library. |

## Included static libraries

`libopengnm.a` and `libpsbc.orbis.a` are included in `lib/`. They are the matching PS4 libraries retained from the B38 client distribution; the metadata interface headers match the headers used in this tree. A complete OpenGNM/PSBC source checkout is not bundled. Preserve the licenses and compiler patches alongside the libraries.

Library SHA-256 values:

```text
e45e0235a8ae0eead0db4d1585da577d2e60ae5dc239aa3891cb492488c0225f  lib/libopengnm.a
f6bd820d34433f26195a2307436d7a7f03886be09b29cc7ff63bead46b1e185a  lib/libpsbc.orbis.a
```

Upstream sources referenced by this port:

- OpenGNM: https://github.com/PS4-OpenGNM/opengnm
- PSBC: https://github.com/PS4-OpenGNM/opengnm-psbc

The headers and metadata layout must match the compiled libraries. Do not substitute ordinary desktop builds or assume the latest upstream revision has the same ABI.

## Retained compiler patches

The `patches/` directory preserves the port's source changes for standalone vertex processing, descriptor layouts, push constants, fragment-input locations, and raster semantics. These affect the compiler/library interface used by the backend and should accompany a compatible source build:

```text
opengnm-psbc-standalone-vs-prolog.patch
opengnm-psbc-descriptor-layout.patch
opengnm-psbc-push-constants.patch
opengnm-psbc-fragment-input-locations.patch
opengnm-psbc-raster-semantics.patch
```

`glm-openorbis-round.patch` records the separate GLM compatibility change. Existing patched client headers and source files are already used by the normal CMake build; these patch files are not reapplied to the client at build time.

The root README describes the build entrypoint and required external dependencies. The original component license texts remain in `licenses/`.
