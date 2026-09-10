#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-c++}" -std=c++20 -I"$ROOT/include" "$ROOT/tools/tests/local_travel_ui_changes_0185_test.cpp" -o "$TEST_DIR/ui_changes"
"$TEST_DIR/ui_changes"
