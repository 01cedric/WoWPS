#!/usr/bin/env python3
"""Run self-contained release checks; results do not certify PS4 hardware behavior."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

SUITES = {
    'lighting_shader_pairs': 'tools/ps4/verify_shaders.py',
    'package_staging': 'tools/tests/package_staging_test.py',
    'm2_cutout_shader': 'tools/tests/run_m2_cutout_shader_tests.py',
    'blp_dxt5_alpha': 'tools/tests/run_blp_dxt5_alpha_tests.sh',
    'ground_recovery': 'tools/tests/run_ground_recovery_tests.py',
    'local_graveyard_sites': 'tools/tests/run_local_graveyard_sites_tests.sh',
    'local_wall_clock': 'tools/tests/run_local_wall_clock_tests.sh',
    'local_death_framexml': 'tools/tests/run_local_death_framexml_tests.sh',
    'local_ghost_presentation': 'tools/tests/local_ghost_presentation_test.py',
    'settings_access': 'tools/tests/run_settings_access_tests.py',
}

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('output', type=Path)
    ap.add_argument('suites', nargs='*', help='Optional suite names; default is all release checks.')
    args = ap.parse_args()
    unknown = set(args.suites) - SUITES.keys()
    if unknown:
        ap.error('Unknown suites: ' + ', '.join(sorted(unknown)))
    root = Path(__file__).resolve().parents[2]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env.setdefault('CXX', shutil.which('clang++') or shutil.which('c++') or 'c++')
    env.setdefault('CC', shutil.which('clang') or shutil.which('cc') or 'cc')
    env.setdefault('SANITIZE', '1')
    env.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
    env.setdefault('UBSAN_OPTIONS', 'halt_on_error=1')
    results = []
    for name in args.suites or SUITES:
        script = root / SUITES[name]
        command = [sys.executable if script.suffix == '.py' else 'bash', str(script)]
        logfile = out / (name + '.log')
        try:
            with logfile.open('w') as log:
                process = subprocess.run(command, cwd=root, env=env,
                                         stdout=log, stderr=subprocess.STDOUT, timeout=240)
            code = process.returncode
        except (OSError, subprocess.TimeoutExpired) as error:
            with logfile.open('a') as log:
                log.write('\nHARNESS ERROR: ' + str(error) + '\n')
            code = 98
        text = logfile.read_text(errors='replace')
        sanitizer_error = 'ERROR: AddressSanitizer' in text or 'runtime error:' in text
        status = 'skipped' if code == 77 else ('passed' if code == 0 and not sanitizer_error else 'failed')
        item = {'suite': name, 'status': status, 'exit': code,
                'pass_groups': sum(line.startswith(('PASS ', 'PASS:')) for line in text.splitlines()),
                'sanitizer_error': sanitizer_error, 'log': logfile.name}
        results.append(item)
        (out / 'release-checks.json').write_text(json.dumps({
            'release': '2.00', 'scope': 'self-contained host checks, not hardware acceptance',
            'results': results}, indent=2) + '\n')
        print(json.dumps(item), flush=True)
    return 1 if any(x['status'] == 'failed' for x in results) else 0

if __name__ == '__main__':
    raise SystemExit(main())
