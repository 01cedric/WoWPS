#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/../.." && pwd)"
test_binary="$(mktemp /tmp/wowps-stream-timing-XXXXXX)"
trap 'rm -f "$test_binary"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
    -I "$project_root/include" \
    "$project_root/tools/tests/stream_load_timing_test.cpp" \
    -o "$test_binary"
"$test_binary"
