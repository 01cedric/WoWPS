#!/usr/bin/env python3
"""Compare archived and current character SPVs using production GFX7 descriptor ABI."""
from pathlib import Path
import zipfile, subprocess, tempfile, sys
root=Path(__file__).resolve().parents[2]
flags=[]
for family in ['compress_in_place','compress_xof','hash_many']:
    variants=['sse2','sse41','avx2','avx512'] if family=='hash_many' else ['sse2','sse41','avx512']
    for variant in variants:
        flags.append('-Wl,--defsym=blake3_'+family+'_'+variant+'=blake3_'+family+'_portable')
with tempfile.TemporaryDirectory() as temp, zipfile.ZipFile(sys.argv[1]) as z:
    temp=Path(temp)
    subprocess.run(['clang++-18','-std=c++17','-O2','-I'+str(root/'ps4/third_party/ps4_vulkan/include'),str(root/'tools/tests/psbc_character_skin_test.cpp'),str(root/'tools/tests/psbc_host_compat.cpp'),str(root/'ps4/third_party/ps4_vulkan/lib/libpsbc.orbis.a'),str(root/'ps4/third_party/ps4_vulkan/lib/libopengnm.a'),*flags,'-lpthread','-ldl','-lm','-lz','-o',str(temp/'psbc')],check=True)
    for kind,label in [('character','main'),('character_shadow','shadow')]:
        rel='assets/shaders/'+kind+'.vert.spv'
        old=temp/(kind+'-old.spv')
        old.write_bytes(z.read(next(n for n in z.namelist() if n.endswith('/'+rel))))
        for p in [old,root/rel]:
            subprocess.run([str(temp/'psbc'),str(p),label],check=True)
