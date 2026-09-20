#!/usr/bin/env python3
"""Check the actual packaged shader helper against a separate numeric oracle."""
from pathlib import Path
import os, re, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
functions=[]
for name in ['terrain','m2','character','wmo']:
    source=(root/'assets/shaders'/f'{name}.frag.glsl').read_text()
    start=source.index('float sampleShadowPCF(')
    helper=source[start:source.index('\n}',start)+2]
    assert helper.count('texture(smap,')==4, name
    functions.append(helper)
assert len(set(functions))==1, 'All four production helpers must be identical'
# GLSL scalar literals are float; GLM needs explicit float suffixes. This is
# syntax conversion only: the exact production arithmetic is compiled below.
helper=re.sub(r'(?<!\w)(\d+\.\d+)(?!\w)',r'\1f',functions[0])
helper=helper.replace('coords.xy','vec2(coords)')
with tempfile.TemporaryDirectory(prefix='wowps_pcf__') as temp:
    temp=Path(temp)
    (temp/'production_pcf.inc').write_text(helper)
    binary=temp/'pcf'
    subprocess.run([os.getenv('CXX','g++'),'-std=c++17','-O1','-g',
        '-fsanitize=address,undefined','-I'+str(root/'extern/glm'),'-I'+str(temp),
        str(root/'tools/tests/shadow_pcf_test.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
