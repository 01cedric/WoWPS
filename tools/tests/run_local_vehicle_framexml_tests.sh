#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
g++ -std=c++20 -O1 -I"$root/include" "$root/tools/tests/local_vehicle_framexml_test.cpp" -o "$build/test"
"$build/test"
