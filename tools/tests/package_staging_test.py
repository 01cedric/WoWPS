"""Exercise the exact runtime cleanup block from the production package script."""
from pathlib import Path
import subprocess
import sys
import tempfile

script = Path(__file__).resolve().parents[2] / 'tools/ps4/package.sh'
script_text = script.read_text()
block = script_text.split('python3 - "$REPO_DIR" "$OUT_DIR" <<\'PY\'\n', 1)[1].split('\nPY\n', 1)[0]
guard = script_text.split('python3 - "$REPO_DIR" "$BUILD_DIR" "$OUT_DIR" "$OO_PS4_TOOLCHAIN" <<\'PY\'\n', 1)[1].split('\nPY\n', 1)[0]
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'project'
    for name in ('assets', 'Data', 'addons'):
        (source / name).mkdir(parents=True)
        (source / name / 'owned.txt').write_text(name)
    def run(output, expected):
        r = subprocess.run([sys.executable, '-c', block, str(source), str(output)], capture_output=True, text=True)
        assert (r.returncode == 0) == expected, r.stderr
        for name in ('assets', 'Data', 'addons'):
            assert (source / name / 'owned.txt').read_text() == name
    run(source, False)
    run(source / 'assets', False)
    output = source / 'build' / 'pkg'
    for name in ('assets', 'Data', 'addons'):
        (output / name / name).mkdir(parents=True)
        (output / name / name / 'stale.txt').write_text('stale')
    run(output, True)
    assert all(not (output / n).exists() for n in ('assets', 'Data', 'addons'))
    run(output, True)
    (output / 'assets').symlink_to(source / 'assets', target_is_directory=True)
    run(output, False)
    build = source / 'build'
    sdk = Path(tmp) / 'sdk'
    sdk.mkdir()
    def check_guard(path, expected):
        result = subprocess.run([sys.executable, '-c', guard, str(source), str(build), str(path), str(sdk)],
                                capture_output=True, text=True)
        assert (result.returncode == 0) == expected, (path, result.stderr)
    for path in (Path('/'), Path.home(), source, source.parent, build, sdk, sdk / 'lib', source / 'src'):
        check_guard(path, False)
    check_guard(build / 'fresh-pkg', True)
    check_guard(output, False)  # Existing unrelated files, no package metadata.
    (output / 'sce_sys').mkdir()
    (output / 'sce_sys/param.sfo').write_bytes(b'fixture')
    check_guard(output, True)
    alias = build / 'alias'
    alias.symlink_to(output, target_is_directory=True)
    check_guard(alias, False)
print('PASS production staging cleanup: reused/nested output, output inside build tree, source overlap and symlink protection')
print('PASS package output guard: broad/source/toolchain roots, unrelated files, generated package and symlink')
