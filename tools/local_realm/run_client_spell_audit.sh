#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT
"${CXX:-c++}" -std=c++20 -O1 -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" \
  "$ROOT/tools/local_realm/audit_client_spells.cpp" "$ROOT/src/pipeline/dbc_loader.cpp" \
  "$ROOT/src/core/logger.cpp" -pthread -o "$BUILD_DIR/audit_client_spells"
"$BUILD_DIR/audit_client_spells" "$@"
