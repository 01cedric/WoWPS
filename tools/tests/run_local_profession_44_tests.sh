#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP="$(mktemp -t wowps-profession44.XXXXXX)"
trap 'rm -f "$TMP"' EXIT
"${CXX:-clang++}" -std=c++20 -O0 -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" \
  "$ROOT/tools/tests/local_profession_44_test.cpp" "$ROOT/src/game/local_services.cpp" -o "$TMP"
"$TMP"
python3 "$ROOT/tools/tests/local_profession_44_contract_test.py"
