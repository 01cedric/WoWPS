#!/usr/bin/env python3
"""Check packaged SPIR-V depth-write dominance, including opaque early returns."""
from pathlib import Path
import re
import subprocess
root = Path(__file__).resolve().parents[2]
for name in ('shadow', 'wmo_shadow', 'm2_shadow', 'character_shadow'):
    text = subprocess.check_output(['spirv-dis', str(root / 'assets/shaders' / (name + '.frag.spv'))], text=True)
    assert 'OpExecutionMode %main DepthReplacing' in text
    assert 'OpDecorate %gl_FragDepth BuiltIn FragDepth' in text
    assert 'OpDecorate %gl_FragCoord BuiltIn FragCoord' in text
    body = text.split('%main = OpFunction', 1)[1].split('OpFunctionEnd', 1)[0]
    blocks = {}
    block = None
    for line in body.splitlines():
        line = line.strip()
        label = re.match(r'(%\w+) = OpLabel', line)
        if label:
            block = label.group(1)
            blocks[block] = []
        elif block:
            blocks[block].append(line)
    queue = [(next(iter(blocks)), False)]
    seen = set()
    returns = kills = 0
    while queue:
        block, wrote = queue.pop()
        if (block, wrote) in seen:
            continue
        seen.add((block, wrote))
        for line in blocks[block]:
            if line.startswith('OpStore %gl_FragDepth '):
                wrote = True
            elif line == 'OpReturn':
                assert wrote, (name, 'surviving return without explicit depth', block)
                returns += 1
            elif line == 'OpKill':
                kills += 1
            elif line.startswith('OpBranchConditional '):
                for successor in line.split()[2:4]:
                    queue.append((successor, wrote))
            elif line.startswith('OpBranch '):
                queue.append((line.split()[1], wrote))
            elif line.startswith('OpSwitch '):
                for successor in line.split()[2::2]:
                    queue.append((successor, wrote))
    assert returns > 0 and kills > 0, (name, returns, kills)
    print(f'PASS {name}: every surviving SPIR-V return writes depth; {kills} reachable discard branches retained')
