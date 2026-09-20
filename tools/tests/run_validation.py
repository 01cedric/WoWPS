#!/usr/bin/env python3
"""Build real-authority regression suites using a dependency-verified object cache.

External DBC input is required and is never copied into the deliverable. Each
suite runs against its original inputs; notably the implementation world entry is unfiltered.
"""
import argparse
import concurrent.futures
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SHARED = [
    'src/game/local_gameplay.cpp', 'src/game/local_melee.cpp',
    'src/game/shapeshift_forms.cpp', 'src/game/local_services.cpp',
    'src/game/local_travel.cpp', 'src/game/local_bots.cpp',
    'src/game/local_world_catalog.cpp', 'src/pipeline/dbc_loader.cpp',
    'src/core/logger.cpp',
]

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dbc', type=Path, required=True)
    parser.add_argument('--cache-dir', type=Path, required=True)
    parser.add_argument('--suite', action='append', default=[])
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--no-sanitize', action='store_true')
    parser.add_argument('--compile-only', action='store_true')
    args = parser.parse_args()
    cache = args.cache_dir.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    lock = (cache / 'validation.lock').open('w')
    fcntl.flock(lock, fcntl.LOCK_EX)
    compiler = os.environ.get('CXX', 'c++')
    flags = ['-std=c++20', '-O1', '-g', '-ffunction-sections', '-fdata-sections']
    if not args.no_sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    flags += ['-I' + str(ROOT / p) for p in ('include', 'extern', 'extern/glm', 'tools/tests')]
    compiler_version = subprocess.check_output([compiler, '--version'], text=True)
    results = {'compiler': compiler_version, 'root': str(ROOT), 'objects': [], 'suites': []}

    def compile_one(relative):
        source = ROOT / relative
        stem = relative.replace('/', '_').removesuffix('.cpp')
        obj = cache / (stem + '.o')
        dep = cache / (stem + '.d')
        receipt = cache / (stem + '.json')
        log = cache / (stem + '.compile.log')
        cmd = [compiler, *flags, '-MMD', '-MF', str(dep), '-c', str(source), '-o', str(obj)]
        signature = hashlib.sha256((compiler_version + shlex.join(cmd)).encode()).hexdigest()
        valid = False
        if receipt.is_file() and obj.is_file() and obj.stat().st_size > 0:
            previous = json.loads(receipt.read_text())
            valid = (previous.get('signature') == signature and previous.get('object_sha256') == sha(obj)
                     and all(Path(p).is_file() and sha(p) == h for p, h in previous.get('dependencies', {}).items()))
        if not valid:
            process = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            log.write_text(shlex.join(cmd) + '\n' + process.stdout)
            if process.returncode:
                raise RuntimeError(f'Compile failed: {source}\n{log.read_text()[-12000:]}')
            # Compiler depfiles escape spaces; shlex handles those after joining continuations.
            dependencies = shlex.split(dep.read_text().replace('\\\n', ' ').split(':', 1)[1])
            previous = {'signature': signature, 'object_sha256': sha(obj),
                        'dependencies': {p: sha(p) for p in dependencies}}
            receipt.write_text(json.dumps(previous, indent=2) + '\n')
        print(('CACHED ' if valid else 'BUILT  ') + relative, flush=True)
        return obj, previous

    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            for obj, receipt in pool.map(compile_one, SHARED):
                results['objects'].append({'path': str(obj), **receipt})
        if args.compile_only:
            return 0
        suites = args.suite or ['local_test_characters_test']
        for suite in suites:
            if '/' in suite or not (ROOT / 'tools/tests' / (suite + '.cpp')).is_file():
                raise ValueError(f'Unknown suite: {suite}')
            test_obj, test_receipt = compile_one('tools/tests/' + suite + '.cpp')
            binary = cache / suite
            link_log = cache / (suite + '.link.log')
            command = [compiler, *flags, str(test_obj), *[item['path'] for item in results['objects']],
                       '-Wl,--gc-sections', '-pthread', '-o', str(binary)]
            linked = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            link_log.write_text(shlex.join(command) + '\n' + linked.stdout)
            if linked.returncode:
                raise RuntimeError(f'Link failed: {suite}\n{link_log.read_text()[-12000:]}')
            command = [str(binary), str(args.dbc.resolve())]
            if suite == 'local_test_characters_test':
                command.append(str(ROOT / 'assets/local_realm/world.json'))
            env = dict(os.environ, ASAN_OPTIONS=os.environ.get('ASAN_OPTIONS', 'detect_leaks=0'),
                       UBSAN_OPTIONS=os.environ.get('UBSAN_OPTIONS', 'halt_on_error=1'))
            run_log = cache / (suite + '.run.log')
            run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, text=True)
            run_log.write_text(shlex.join(command) + '\n' + run.stdout)
            entry = {'suite': suite, 'exit_code': run.returncode, 'command': command,
                     'binary_sha256': sha(binary), 'log_sha256': sha(run_log),
                     'source_sha256': sha(ROOT / 'tools/tests' / (suite + '.cpp')),
                     'object': test_receipt, 'log': str(run_log)}
            results['suites'].append(entry)
            print(('PASS ' if run.returncode == 0 else 'FAIL ') + suite + ': ' + str(run_log), flush=True)
            if run.returncode:
                print(run_log.read_text()[-16000:], file=sys.stderr)
                return run.returncode if run.returncode > 0 else 1
        return 0
    finally:
        (cache / 'validation_manifest.json').write_text(json.dumps(results, indent=2) + '\n')

if __name__ == '__main__':
    sys.exit(main())
