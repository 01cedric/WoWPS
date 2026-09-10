#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CXX:-c++}" -std=c++20 -O1 -g -ffunction-sections -fdata-sections "${SAN_FLAGS[@]}" \
    -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" \
    "$ROOT/tools/tests/local_quest_rewards_test.cpp" "$ROOT/src/game/local_gameplay.cpp" \
    "$ROOT/src/game/local_services.cpp" "$ROOT/src/game/local_travel.cpp" \
    "$ROOT/src/game/local_bots.cpp" "$ROOT/src/game/local_world_catalog.cpp" \
    "$ROOT/src/pipeline/dbc_loader.cpp" "$ROOT/src/core/logger.cpp" \
    -Wl,--gc-sections -pthread -o "$TEST_DIR/local_quest_rewards_test"
python3 - "$ROOT/assets/local_realm/catalog" "$TEST_DIR/catalog" <<'PYFIXTURE'
from pathlib import Path
import struct,sys
source=Path(sys.argv[1]);target=Path(sys.argv[2]);target.mkdir()
for f in source.iterdir():
    if f.suffix in {'.pack','.idx'} or f.name=='manifest.json':(target/f.name).symlink_to(f)
b=(source/'quests.pack').read_bytes();n=struct.unpack_from('<I',b,8)[0]
(target/'quest-test-ids.txt').write_text(''.join(str(struct.unpack_from('<I',b,16+16*i)[0])+'\n' for i in range(n)))
PYFIXTURE
"$TEST_DIR/local_quest_rewards_test" "$TEST_DIR/catalog"
