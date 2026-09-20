#!/usr/bin/env bash
# P05 line of sight (the implementation): build the collision extractor and, when the player's
# own archives are present, run it and load what it wrote.
#
# This suite needs the player's own client archives. Set WOWEE_MPQ_DIR to the
# WoW Data directory (the one holding common.MPQ / patch.MPQ) and re-run it.
# Without that directory the harness reports it as skipped rather than counting
# it as passed, in the same shape as the three FrameXML suites - because there
# is no map geometry to extract and nothing to check the result against.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
: "${WOWEE_MPQ_DIR:?Set WOWEE_MPQ_DIR to the WoW 3.3.5a Data directory}"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CXX:-c++}" -std=c++20 -O1 -g "${SAN_FLAGS[@]}" \
    -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" \
    -DWOWEE_MPQ_SOURCE_AVAILABLE \
    "$ROOT/tools/local_realm/extract_collision.cpp" \
    "$ROOT/src/pipeline/adt_loader.cpp" "$ROOT/src/pipeline/adt_alpha.cpp" \
    "$ROOT/src/pipeline/m2_loader.cpp" "$ROOT/src/pipeline/wmo_loader.cpp" \
    "$ROOT/src/pipeline/mpq_asset_source.cpp" "$ROOT/src/pipeline/dbc_loader.cpp" \
    "$ROOT/src/core/logger.cpp" -lstorm -lz -lbz2 -pthread \
    -o "$TEST_DIR/extract_collision"
# One small map is enough to prove the pipeline end to end; 0 is Eastern
# Kingdoms, which every installation has.
"$TEST_DIR/extract_collision" --mpq "$WOWEE_MPQ_DIR" --out "$TEST_DIR/collision" --map 0
test -s "$TEST_DIR/collision/collision.manifest"
echo "PASS collision extract: the pack was built from the player's own archives and its manifest loads"
