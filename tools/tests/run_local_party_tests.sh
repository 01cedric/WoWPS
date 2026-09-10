#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-c++}" -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I"$ROOT/include" "$ROOT/tools/tests/local_party_test.cpp" -o "$TEST_DIR/party_test"
ASAN_OPTIONS=detect_leaks=0 "$TEST_DIR/party_test"
