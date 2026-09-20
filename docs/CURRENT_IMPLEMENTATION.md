# Current implementation — WoWPS 2.00

The public feature matrix is in [README.md](../README.md), consolidated changes are in [CHANGELOG.md](../CHANGELOG.md), and outstanding work is in [TODO.md](../TODO.md).

## Compatibility

| Field | Value |
|---|---|
| Public release | 2.00 |
| BUILD_VERSION / package APP_VER | 02.00 |
| Title / title ID | WoWPS / WOWE00001 |
| Client assets | WotLK 3.3.5a build 12340, user supplied |
| Save writer / accepted readers | 32 / 1–31 plus current format |
| LAN gameplay protocol | 87 |
| Platform | Native OpenOrbis PS4, Vulkan 1.0 over GNM/VideoOut |

The save and network format numbers are compatibility identifiers, not release versions. Do not alter them when changing public release branding. All LAN peers need matching builds and content fingerprints.

## Scope

Connected play delegates the world rules to the external server. Standalone play owns its own bounded world, persistence and LAN authority; it is not a complete embedded AzerothCore server.

Supported combat imports retain source metadata and reject unsupported complete profiles. Exact admitted counts depend on the supplied data and do not establish complete classes. Source inventories under `implementation_inventory/` are reference inputs, not a new console acceptance report.

Ghost/corpse state is persisted by the local authority. Geometry recovery is local/LAN only; connected servers retain authority. Line-of-sight without the optional user-extracted collision pack defaults to visible.

The renderer implements directional atlas shadows, world-space shafts, height fog, bloom and depth-aware filtering. The volume path uses RGBA16F attachments; it is not a narrow-format memory-saving variant. Local point lights remain unshadowed. Current GPU-query reporting deliberately does not claim calibrated PS4 timings.

## Validation boundaries

[BUILD_VALIDATION.md](BUILD_VALIDATION.md) records checks performed for this release. Hardware installation, complete FrameXML presentation, sustained FPS and full gameplay/LAN acceptance remain distinct from source, compiler and package checks.
