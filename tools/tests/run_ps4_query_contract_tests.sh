#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/../.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
"${CC:-cc}" -std=c11 -O1 -g -fsanitize=address,undefined \
  -ffunction-sections -fdata-sections \
  -I "$project_root/ps4/third_party/ps4_vulkan/include" \
  "$project_root/tools/tests/ps4_query_contract_test.c" \
  "$project_root/ps4/third_party/ps4_vulkan/source/vulkan-ps4/src/vk_ps4_query.c" \
  "$project_root/ps4/third_party/ps4_vulkan/source/vulkan-ps4/src/vk_ps4_instance.c" \
  "$project_root/ps4/third_party/ps4_vulkan/source/vulkan-ps4/src/vk_ps4_vulkan11.c" \
  -Wl,--gc-sections -o "$work_dir/query_contract"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$work_dir/query_contract"
"${CXX:-c++}" -std=c++17 -O1 -g -fsanitize=address,undefined \
  -I "$project_root/include" \
  "$project_root/tools/tests/gpu_timestamp_validation_test.cpp" \
  -o "$work_dir/timestamp_validation"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$work_dir/timestamp_validation"
