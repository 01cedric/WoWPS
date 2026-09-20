#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
"${CXX:-g++}" -std=c++17 -O2 -I"$root/include" -I"$root/extern/glm" "$root/tools/tests/m2_visibility_clusters_test.cpp" "$root/src/rendering/frustum.cpp" -o "$tmpdir/test"
"$tmpdir/test"
