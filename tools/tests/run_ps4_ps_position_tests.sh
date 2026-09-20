#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
ar p "$ROOT/ps4/third_party/ps4_vulkan/lib/libopengnm.a" drawcommandbuffer.o > "$TEMP/drawcommandbuffer.o"
ar p "$ROOT/ps4/third_party/ps4_vulkan/lib/libopengnm.a" driver_orbis.o > "$TEMP/driver_orbis.o"
python3 - "$TEMP/drawcommandbuffer.o" "$TEMP/stubs.c" "$TEMP/driver_orbis.o" <<'PY'
from pathlib import Path
import subprocess,sys
undefined={line.split()[-1] for line in subprocess.check_output(['nm','-u',sys.argv[1]],text=True).splitlines() if len(line.split())==2}
undefined |= {line.split()[-1] for line in subprocess.check_output(['nm','-u',sys.argv[3]],text=True).splitlines() if len(line.split())==2}
defined={line.split()[-1] for line in subprocess.check_output(['nm','--defined-only',sys.argv[3]],text=True).splitlines() if len(line.split())==3}
undefined -= defined
source='#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\nuintptr_t __stack_chk_guard=0x258;\nunsigned gnm_diagnostic_count=0;\n'
for name in sorted(undefined):
    if name=='sceGnmWriteMsg':
        source+='void sceGnmWriteMsg(int level,const char* msg) { (void)level; ++gnm_diagnostic_count; fprintf(stderr,"GNM: %s\\n",msg); }\n'
    elif name.startswith(('sceGnm','sceGpa','sceKernel')):
        source+='void '+name+'(void) { fputs("unexpected '+name+'\\n",stderr); abort(); }\n'
Path(sys.argv[2]).write_text(source)
PY
SOURCE="${BACKEND_COMMAND_SOURCE:-$ROOT/ps4/third_party/ps4_vulkan/source/vulkan-ps4/src/vk_ps4_command.c}"
"${CC:-cc}" -DBACKEND_COMMAND_SOURCE=\""$SOURCE"\" -std=c11 -O1 -g -ffunction-sections -fdata-sections \
    -I"$ROOT/ps4/third_party/ps4_vulkan/include" \
    "$ROOT/tools/tests/ps4_ps_position_test.c" "$TEMP/drawcommandbuffer.o" "$TEMP/driver_orbis.o" "$TEMP/stubs.c" \
    -Wl,--gc-sections -lm -o "$TEMP/cache-sync"
"$TEMP/cache-sync"
