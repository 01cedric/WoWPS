#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
python3 - "$ROOT" "$TEST_DIR" <<'PY'
from pathlib import Path
import sys
root,out=map(Path,sys.argv[1:])
s=(root/'src/ui/unit_portrait.cpp').read_text();a=s.index('template <class Build>');b=s.index('\n}\n}\nUnitPortrait::UnitPortrait',a)+2
(out/'portrait_rebuild_source.inc').write_text(s[a:b])
s=(root/'src/rendering/character_preview.cpp').read_text();a=s.index('void CharacterPreview::readPortraitFraming(');b=s.index('\nvoid CharacterPreview::setPortraitFraming',a)
(out/'portrait_reader_source.inc').write_text(s[a:b])
s=(root/'src/game/game_handler_packets.cpp').read_text();a=s.index('dispatchTable_[Opcode::SMSG_CONVERT_RUNE]');b=s.index('    // uint32 runeMask',a)
(out/'rune_handlers_source.inc').write_text(s[a:b])
PY
SAN_FLAGS=()
if [ "${SANITIZE:-0}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CXX:-c++}" -std=c++20 -O1 -g "${SAN_FLAGS[@]}" -I"$ROOT/include" -I"$ROOT/extern/glm" -I"$TEST_DIR" \
 "$ROOT/tools/tests/visual_state_0179_test.cpp" "$ROOT/src/network/packet.cpp" -o "$TEST_DIR/visual_test"
"$TEST_DIR/visual_test"
