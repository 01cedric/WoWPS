#!/usr/bin/env python3
"""Verify packaged lighting shader pairs; --rebuild refreshes them with glslang."""
from pathlib import Path
import argparse, hashlib, json, shutil, struct, subprocess, sys

def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--rebuild', action='store_true', help='compile the listed GLSL sources with glslangValidator')
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[2]
    shaders = root / 'assets/shaders'
    manifest_path = shaders / 'lighting_manifest.json'
    manifest = json.loads(manifest_path.read_text())
    if args.rebuild:
        compiler = shutil.which('glslangValidator')
        if not compiler:
            raise RuntimeError('glslangValidator is required for --rebuild; unchanged builds use the checked precompiled shaders')
        for name, record in manifest['shaders'].items():
            src = shaders / (name + '.glsl')
            dst = shaders / (name + '.spv')
            temp = dst.with_suffix('.spv.tmp')
            try:
                subprocess.run([compiler, '-V', '--target-env', 'vulkan1.0', '-Os',
                                '-S', name.rsplit('.', 1)[1], str(src), '-o', str(temp)], check=True)
                validator = shutil.which('spirv-val')
                if validator:
                    subprocess.run([validator, '--target-env', 'vulkan1.0', str(temp)], check=True)
                temp.replace(dst)
            finally:
                temp.unlink(missing_ok=True)
            record.update(source_sha256=digest(src), spirv_sha256=digest(dst))
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    for name, record in manifest['shaders'].items():
        src, dst = shaders / (name + '.glsl'), shaders / (name + '.spv')
        if digest(src) != record['source_sha256'] or digest(dst) != record['spirv_sha256']:
            raise RuntimeError(f'{name}: GLSL/SPIR-V pair changed; run tools/ps4/verify_shaders.py --rebuild before packaging')
        data = dst.read_bytes()
        if len(data) < 20 or len(data) % 4 or struct.unpack_from('<I', data)[0] != 0x07230203:
            raise RuntimeError(f'{name}: invalid SPIR-V header')
    print(f"PASS {len(manifest['shaders'])} lighting source/binary pairs; package will not use stale lighting shaders")
    return 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        sys.exit(1)
