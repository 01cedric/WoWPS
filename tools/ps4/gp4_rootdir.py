#!/usr/bin/env python3
"""Rewrite the <rootdir> block of a GP4 project from the real directory tree.

OpenOrbis' create-gp4 writes a fixed sample layout (sce_sys, sce_module,
assets/{audio,fonts,...}) instead of deriving the directories from --files,
and PkgTool's PFS builder throws "Sequence contains no elements" for any file
whose directory is missing there. This regenerates the block from <stage-dir>.

Usage: gp4_rootdir.py <project.gp4> <stage-dir>
"""
import os
import re
import sys

gp4, stage = sys.argv[1], sys.argv[2]

def tree(path, depth):
    out = []
    for name in sorted(os.listdir(path)):
        full = os.path.join(path, name)
        if not os.path.isdir(full):
            continue
        indent = "\t" * depth
        sub = tree(full, depth + 1)
        if sub:
            out.append(f'{indent}<dir targ_name="{name}">')
            out.extend(sub)
            out.append(f'{indent}</dir>')
        else:
            out.append(f'{indent}<dir targ_name="{name}" />')
    return out

block = "\t<rootdir>\n" + "\n".join(tree(stage, 2)) + "\n\t</rootdir>"
text = open(gp4).read()
new, n = re.subn(r"\t<rootdir>.*?</rootdir>", block, text, flags=re.S)
if n != 1:
    sys.exit("gp4_rootdir.py: no <rootdir> block found in " + gp4)
open(gp4, "w").write(new)
print(f"gp4_rootdir.py: rebuilt <rootdir> with {sum(1 for l in block.splitlines() if '<dir' in l)} directories")
