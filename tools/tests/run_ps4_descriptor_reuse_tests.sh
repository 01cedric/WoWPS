#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/../.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
"${CC:-cc}" -std=c11 -O1 -g -fsanitize=address,undefined \
  -ffunction-sections -fdata-sections \
  -I "$project_root/ps4/third_party/ps4_vulkan/include" \
  "$project_root/tools/tests/ps4_descriptor_reuse_test.c" \
  "$project_root/ps4/third_party/ps4_vulkan/source/vulkan-ps4/src/vk_ps4_descriptor.c" \
  -Wl,--gc-sections -o "$work_dir/descriptor_reuse"
# LeakSanitizer cannot operate under the workspace's tracing; memory/bounds and
# undefined-behavior instrumentation remain enabled.
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$work_dir/descriptor_reuse"
