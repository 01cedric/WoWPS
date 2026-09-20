#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if [ "$#" -ne 1 ]; then
    echo "Usage: run_local_druid_progression_tests.sh DBC_DIRECTORY" >&2
    exit 2
fi
TEST_DIR="${DRUID_PROGRESSION_TEST_DIR:-$(mktemp -d)}"
mkdir -p "$TEST_DIR"
if [ -z "${DRUID_PROGRESSION_TEST_DIR:-}" ]; then trap 'rm -rf "$TEST_DIR"' EXIT; fi
"${CXX:-c++}" -std=c++20 -O1 -g1 -I"$ROOT/include" -I"$ROOT/extern" -I"$ROOT/extern/glm" \
    "$ROOT/tools/tests/local_druid_progression_test.cpp" "$ROOT/src/pipeline/dbc_loader.cpp" \
    "$ROOT/src/core/logger.cpp" -pthread -o "$TEST_DIR/local_druid_progression_test"
"$TEST_DIR/local_druid_progression_test" "$1"
