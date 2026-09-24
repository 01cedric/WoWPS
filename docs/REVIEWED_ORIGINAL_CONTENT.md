# Reviewed original runtime content

`reviewed_original_content.json` is a generated, fail-closed production companion
for `world.json`. Inputs retain pinned provenance and licence notices. The bytes
participate in the LAN content fingerprint, so hosts and guests must match.

The reviewed set installs 861 of 944 captured placements: 606 presentation
objects, 79 chairs/benches, 67 chests and 109 gathering nodes. It includes 14
exclusive pools, eight holiday schedules and one interval schedule. See
[original object semantics](ORIGINAL_OBJECT_SEMANTICS.md) for the supported rules.

Decorations are visible and distance-streamed but not usable; they carry no
persistent interaction state. Event-gated content follows host-authoritative
phase masks. Holiday stage lengths come from the user's `Holidays.dbc` and
missing or invalid data fails closed.

Unsupported scripts, locks, rotations, object types and event semantics keep
the remaining 83 placements blocked. Decisions are recorded in
`REVIEWED_ORIGINAL_RUNTIME_REPORT.json`; captured SQL is not automatically
playable content.

Regenerate with `python3 tools/local_realm/compile_reviewed_original_content.py`.
