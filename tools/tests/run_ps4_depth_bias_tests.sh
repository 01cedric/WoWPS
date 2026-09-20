#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/../.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
ar p "$project_root/ps4/third_party/ps4_vulkan/lib/libopengnm.a" drawcommandbuffer.o > "$work_dir/drawcommandbuffer.o"
ar p "$project_root/ps4/third_party/ps4_vulkan/lib/libopengnm.a" depthrendertarget.o > "$work_dir/depthrendertarget.o"
ar p "$project_root/ps4/third_party/ps4_vulkan/lib/libopengnm.a" dataformat.o > "$work_dir/dataformat.o"
ar p "$project_root/ps4/third_party/ps4_vulkan/lib/libopengnm.a" tilemodes.o > "$work_dir/tilemodes.o"
python3 - "$work_dir/drawcommandbuffer.o" "$work_dir/depthrendertarget.o" "$work_dir/dataformat.o" "$work_dir/tilemodes.o" "$work_dir/unused_gnm_stubs.c" <<'PY'
import pathlib, subprocess, sys
source = '#include <stdlib.h>\n#include <stdio.h>\n#include <stdint.h>\nuintptr_t __stack_chk_guard = 0x257;\n'
objects = sys.argv[1:5]
defined = {line.split()[-1] for line in subprocess.check_output(['nm', '--defined-only', *objects], text=True).splitlines() if len(line.split()) >= 3}
undefined = {line.split()[-1] for line in subprocess.check_output(['nm', '-u', *objects], text=True).splitlines() if len(line.split()) == 2}
for name in sorted(undefined - defined):
    if name.startswith(('sceGnm', 'sceGpa')):
        source += 'void ' + name + '(void) { fputs("unexpected '+name+'\\n", stderr); abort(); }\n'
pathlib.Path(sys.argv[5]).write_text(source)
PY
command_source="${BACKEND_COMMAND_SOURCE:-$project_root/ps4/third_party/ps4_vulkan/source/vulkan-ps4/src/vk_ps4_command.c}"
"${CC:-cc}" -DBACKEND_COMMAND_SOURCE=\""$command_source"\" -std=c11 -O1 -g -ffunction-sections -fdata-sections \
  -I "$project_root/ps4/third_party/ps4_vulkan/include" \
  "$project_root/tools/tests/ps4_depth_bias_test.c" \
  "$work_dir/drawcommandbuffer.o" "$work_dir/depthrendertarget.o" "$work_dir/dataformat.o" "$work_dir/tilemodes.o" "$work_dir/unused_gnm_stubs.c" \
  -Wl,--gc-sections -lm -o "$work_dir/depth_bias"
"$work_dir/depth_bias"
sha256sum "$project_root/ps4/third_party/ps4_vulkan/lib/libopengnm.a"
