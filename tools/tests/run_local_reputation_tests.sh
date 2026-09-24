#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-clang++}"
TMP="$(mktemp -t wowps-local-reputation.XXXXXX)"
trap 'rm -f "$TMP"' EXIT
"$CXX" -std=c++20 -O0 -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" \
  "$ROOT/tools/tests/local_reputation_test.cpp" -o "$TMP"
"$TMP"
python3 "$ROOT/tools/tests/local_reputation_contract_test.py"
