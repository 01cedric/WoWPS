#!/usr/bin/env python3
"""Fetch the pinned, hash-verified original world script SQL inputs.

No SQL is executed. The manifest beside this script pins every downloaded byte.
The emitted tar.gz has deterministic filenames, timestamps and ownership.
"""
from __future__ import annotations
import argparse
import concurrent.futures
import gzip
import hashlib
import io
import json
from pathlib import Path
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parent
MANIFEST = ROOT / "world_script_source_manifest.json"


def verify(data, row, name):
    if len(data) != row["bytes"] or hashlib.sha256(data).hexdigest() != row["sha256"]:
        raise ValueError(f"{name}: source bytes differ from the pinned manifest")
    if b"CREATE TABLE" not in data:
        raise ValueError(f"{name}: not a complete SQL base table")


def write_archive(path, files):
    target = path.with_suffix(path.suffix + ".tmp")
    try:
        with target.open("wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w") as archive:
                for name, data in sorted(files.items()):
                    member = tarfile.TarInfo(name)
                    member.size, member.mode, member.mtime = len(data), 0o644, 0
                    archive.addfile(member, io.BytesIO(data))
        target.replace(path)
    finally:
        # A failed download/build must not leave a plausible-looking partial
        # archive for a later packaging run.
        target.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--output", type=Path, default=ROOT / "world_script_source_sql.tar.gz")
    parser.add_argument("--source-dir", type=Path, help="Use previously downloaded SQL instead of the network")
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    rows = {name: row for name, row in manifest["tables"].items() if row["archive"] == "world_script_source_sql.tar.gz"}

    def fetch(item):
        name, row = item
        if args.source_dir:
            data = (args.source_dir / name).read_bytes()
        else:
            with urllib.request.urlopen(row["url"], timeout=60) as response:
                data = response.read(64 * 1024 * 1024 + 1)
        verify(data, row, name)
        return name, data

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        files = dict(executor.map(fetch, sorted(rows.items())))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_archive(args.output, files)
    print(f"Verified {len(files)} original tables; wrote {args.output.name}")


if __name__ == "__main__":
    main()
