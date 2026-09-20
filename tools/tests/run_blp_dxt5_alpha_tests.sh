#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
"${CXX:-clang++-18}" -std=c++20 -O2 -ffunction-sections -fdata-sections -I"$root/include" "$root/tools/tests/blp_dxt5_alpha_test.cpp" "$root/src/pipeline/blp_loader.cpp" -Wl,--gc-sections -o "$tmpdir/test"
"$tmpdir/test"
