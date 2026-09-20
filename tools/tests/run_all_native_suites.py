#!/usr/bin/env python3
"""Run every native host suite under ASan/UBSan and report one line per suite.

Usage: tools/tests/run_all_native_suites.py <output-dir> [suite ...]

Each `tools/tests/run_*_tests.sh` is one suite. Suites that need the player's
own client data - a DBC directory, extracted FrameXML or an MPQ - are skipped
unless that input is available; the report says so explicitly rather than
counting a skipped suite as passed.
"""
import concurrent.futures, json, os, pathlib, subprocess, sys, threading

root = pathlib.Path(__file__).resolve().parents[2]
out = pathlib.Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
selected = set(sys.argv[2:])

dbc = os.environ.get("WOWPS_DBC_DIR", "")
jobs = []
for script in sorted((root / "tools/tests").glob("run_*_tests.sh")):
    name = script.stem[len("run_"):-len("_tests")]
    if selected and name not in selected:
        continue
    text = script.read_text()
    # A suite needs the player's own client data when its runner consumes a
    # positional argument (a DBC directory) or names an extracted-asset variable.
    needs_dbc = '"$1"' in text or "${1:?" in text or "${1:-" in text or "$1}" in text
    # Some runners read the same directory from DBC_DIR rather than a positional
    # argument. Without this they are neither skipped nor given the directory,
    # so they abort on the unset variable and read as a failing suite.
    needs_dbc_env = "${DBC_DIR" in text
    needs_other = "FRAME_XML_DIR" in text or "MPQ" in text or "MPQ_DIR" in text
    jobs.append((name, script, "dbc" if needs_dbc else "dbc-env" if needs_dbc_env
                 else "client-data" if needs_other else ""))

python_suites = ["local_snapshot_reuse", "package_staging", "controller_p02_regression",
                 "intro_spawn_return", "entity_residency", "character_residency"]
for name in python_suites:
    script = root / "tools/tests" / (name + "_test.py")
    if script.exists() and (not selected or name in selected):
        jobs.append((name, script, ""))


lock = threading.Lock()
results_so_far = []


def _write_log(log, text):
    """Write a log line even if the output directory vanished under us."""
    for _ in range(2):
        try:
            log.write_text(text)
            return
        except (FileNotFoundError, NotADirectoryError):
            log.parent.mkdir(parents=True, exist_ok=True)


def _record(result):
    """Publish one suite's result immediately so a later crash cannot lose it."""
    with lock:
        results_so_far.append(result)
        try:
            (out / "native-suites.json").write_text(
                json.dumps(results_so_far, indent=2) + "\n")
        except OSError:
            pass
    print(json.dumps(result), flush=True)
    return result


def run(job):
    name, script, needs = job
    log = out / (name + ".log")
    # The output directory is recreated defensively: a suite that runs for
    # minutes can outlive a tmp-reaper that removed it, and losing the whole
    # run to a FileNotFoundError while writing a log is never the right answer.
    try:
        out.mkdir(parents=True, exist_ok=True)
    except OSError:
        pass
    if (needs in ("dbc", "dbc-env") and not dbc) or needs == "client-data":
        reason = ("suite requires a DBC directory (set WOWPS_DBC_DIR)"
                  if needs in ("dbc", "dbc-env")
                  else "suite requires extracted client assets (FrameXML/MPQ)")
        _write_log(log, "SKIPPED: " + reason + "\n")
        return _record({"suite": name, "exit": 0, "pass_groups": 0, "skipped": True,
                        "sanitizer_error": False, "skip_reason": reason})
    cmd = (["python3", str(script)] if script.suffix == ".py"
           else ["bash", str(script)] + ([dbc] if needs == "dbc" else []))
    env = {**os.environ, "SANITIZE": "1", "ASAN_OPTIONS": "detect_leaks=0"}
    if dbc: env["DBC_DIR"] = dbc
    try:
        with log.open("w") as f:
            p = subprocess.run(cmd, cwd=root, env=env, stdout=f, stderr=subprocess.STDOUT)
        rc = p.returncode
    except (FileNotFoundError, NotADirectoryError, PermissionError, OSError) as exc:
        # Never let an I/O problem in the harness abort the whole run: report it
        # as a failing suite so it shows up in the table instead of vanishing.
        _write_log(log, "HARNESS ERROR: %s: %s\n" % (type(exc).__name__, exc))
        return _record({"suite": name, "exit": 97, "pass_groups": 0, "skipped": False,
                        "sanitizer_error": False, "harness_error": str(exc)})
    try:
        text = log.read_text(errors="replace")
    except OSError:
        text = ""
    result = {
        "suite": name,
        "exit": rc,
        # Suites announce a group as either "PASS <what>" or "PASS: <what>";
        # counting only the first spelling silently dropped whole suites from
        # the total, which read as "this suite proved nothing".
        "pass_groups": sum(line.startswith(("PASS ", "PASS:")) for line in text.splitlines()),
        "skipped": False,
        "sanitizer_error": "ERROR: AddressSanitizer" in text or "runtime error:" in text,
    }
    return _record(result)


def run_guarded(job):
    """A crash inside one suite must not take the other suites with it."""
    try:
        return run(job)
    except BaseException as exc:  # noqa: BLE001 - harness must survive anything
        return _record({"suite": job[0], "exit": 98, "pass_groups": 0, "skipped": False,
                        "sanitizer_error": False, "harness_error": repr(exc)})


workers = max(1, min(4, (os.cpu_count() or 2)))
with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
    results = list(pool.map(run_guarded, jobs))
_write_log(out / "native-suites.json", json.dumps(results, indent=2) + "\n")
ran = [r for r in results if not r["skipped"]]
failed = [r for r in results if r["exit"] or r["sanitizer_error"]]
print(f"suites={len(results)} ran={len(ran)} skipped={len(results)-len(ran)} "
      f"pass_groups={sum(r['pass_groups'] for r in ran)} failed={len(failed)}")
for r in failed:
    print("FAILED:", r["suite"], "exit=", r["exit"], "sanitizer=", r["sanitizer_error"])
sys.exit(1 if failed else 0)
