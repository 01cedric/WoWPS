#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/../.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
backend="$project_root/ps4/third_party/ps4_vulkan"
"${CC:-cc}" -std=c11 -O1 -g -ffunction-sections -fdata-sections \
  -I "$backend/include" -c "$backend/source/vulkan-ps4/src/vk_ps4_instance.c" -o "$work_dir/instance.o"
"${CXX:-c++}" -std=c++17 -O1 -g -ffunction-sections -fdata-sections \
  -I "$backend/include" -I "$project_root/include" -I "$project_root/extern" \
  "$project_root/tools/tests/ps4_attachment_memory_test.cpp" "$work_dir/instance.o" \
  -Wl,--gc-sections -pthread -o "$work_dir/selection"
"$work_dir/selection"
