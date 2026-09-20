#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
"${CXX:-clang++-18}" -std=c++20 -O2 -I"$root/include" "$root/tools/tests/local_graveyard_sites_test.cpp" -o "$tmpdir/test"
"$tmpdir/test"
