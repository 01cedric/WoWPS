#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/wowps_local_phase_state_test"
g++ -std=c++20 -I"$ROOT/include" -I"$ROOT/extern" "$ROOT/tools/tests/local_phase_state_test.cpp" -o "$OUT"
"$OUT"
