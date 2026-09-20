#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="${TEST_DIR:-$(mktemp -d)}"
mkdir -p "$TEST_DIR"
SAN_FLAGS=()
if [ "${SANITIZE:-1}" = 1 ]; then SAN_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CXX:-g++}" -std=c++20 -O1 -g -ffunction-sections -fdata-sections "${SAN_FLAGS[@]}" \
    -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" \
    "$ROOT/tools/tests/local_combat_view_test.cpp" \
    -pthread -Wl,--gc-sections -o "$TEST_DIR/local_combat_view_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}" "$TEST_DIR/local_combat_view_test"
# The real socket receive path has broad realm dependencies. Assert its existing
# mismatch guard remains before normal dispatch; do not claim a UDP handshake.
python3 - "$ROOT" <<'PY'
from pathlib import Path
import re, sys
s=(Path(sys.argv[1])/'src/game/local_realm.cpp').read_text()
start=s.index('if (wireVersion != Version) {')
end=s.index('if (state == LocalRealmState::Hosting) handleHost(',start)
guard=s[start:end]
assert 'state == LocalRealmState::Hosting && type == Message::Hello && token' in guard
assert 'reason.u8(6); send(Message::Reject, token, reason, address, wireVersion);' in guard
assert guard.rstrip().endswith('continue;\n            }')
assert 'LAN protocol version differs' in guard
# Name the rejected version from the header rather than a literal, so the
# message cannot drift away from the protocol the guard actually rejects.
version=int(re.search(r'GameplayVersion = (\d+)',
                      (Path(sys.argv[1])/'include/game/lan_discovery.hpp').read_text()).group(1))
print(f'PASS structural compatibility guard: old LAN{version-1} enters version-mismatch rejection '
      'before gameplay dispatch (no UDP handshake executed)')
PY
printf 'Test executable: %s/local_combat_view_test\n' "$TEST_DIR"
