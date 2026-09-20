#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/../.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
backend="$project_root/ps4/third_party/ps4_vulkan"
python3 - "$backend/lib/libopengnm.a" "$work_dir/platform_stubs.c" <<'PY_STUBS'
import pathlib, subprocess, sys
names = {line.split()[-1] for line in subprocess.check_output(['nm','-u',sys.argv[1]], text=True).splitlines() if len(line.split()) == 2}
source = '#include <stdlib.h>\n'
for name in sorted(names):
    if name.startswith(('sceKernel', 'sceVideoOut')):
        source += 'void ' + name + '(void) { abort(); }\n'
pathlib.Path(sys.argv[2]).write_text(source)
PY_STUBS
"${CC:-cc}" -std=c11 -O1 -g -ffunction-sections -fdata-sections \
  -I "$backend/include" "$project_root/tools/tests/ps4_sampled_color_layout_test.c" \
  "$backend/source/vulkan-ps4/src/vk_ps4_image.c" \
  "$backend/source/vulkan-ps4/src/vk_ps4_format.c" \
  "$backend/source/vulkan-ps4/src/vk_ps4_descriptor.c" \
  "$backend/lib/libopengnm.a" "$work_dir/platform_stubs.c" -Wl,--gc-sections -lm -o "$work_dir/sampled_depth"
"$work_dir/sampled_depth"
