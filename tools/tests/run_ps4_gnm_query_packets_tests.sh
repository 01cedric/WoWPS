#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/../.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
ar p "$project_root/ps4/third_party/ps4_vulkan/lib/libopengnm.a" drawcommandbuffer.o > "$work_dir/drawcommandbuffer.o"
# Every external GNM call aborts: only self-contained, CPU-side packet encoding
# is permitted. libc and the stack guard cover the original compiled object.
python3 - "$work_dir/drawcommandbuffer.o" "$work_dir/unused_gnm_stubs.c" <<'PY'
import pathlib, subprocess, sys
symbols = subprocess.check_output(['nm', '-u', sys.argv[1]], text=True)
source = '#include <stdlib.h>\n#include <stdint.h>\nuintptr_t __stack_chk_guard = 0x251055;\n'
for line in symbols.splitlines():
    name = line.split()[-1]
    if name.startswith('sceGnm'):
        source += 'void ' + name + '(void) { abort(); }\n'
pathlib.Path(sys.argv[2]).write_text(source)
PY
"${CC:-cc}" -std=c11 -O1 -g \
  -I "$project_root/ps4/third_party/ps4_vulkan/include" \
  "$project_root/tools/tests/ps4_gnm_query_packets_test.c" \
  "$work_dir/drawcommandbuffer.o" "$work_dir/unused_gnm_stubs.c" \
  -o "$work_dir/query_packets"
"$work_dir/query_packets"
sha256sum "$project_root/ps4/third_party/ps4_vulkan/lib/libopengnm.a"
