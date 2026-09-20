#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
    "$ROOT/tools/tests/character_bone_upload_test.cpp" -o "$TEST_DIR/bone_upload"
"$TEST_DIR/bone_upload"
