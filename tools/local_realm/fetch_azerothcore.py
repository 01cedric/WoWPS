#!/usr/bin/env python3
"""Download the exact open-source SQL input tables used by the realm importer."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import urllib.request
from import_azerothcore import PINNED_COMMIT, TABLES


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("output", type=Path)
    p.add_argument("--commit", default=PINNED_COMMIT)
    p.add_argument("--full-world", action="store_true", help="also download catalog instance and teleport tables")
    p.add_argument("--vendors", action="store_true", help="also download npc_vendor and purchase conditions for import_vendor_catalog.py")
    args = p.parse_args()
    if len(args.commit) != 40 or any(c not in "0123456789abcdef" for c in args.commit.lower()):
        p.error("--commit must be a complete 40-character Git commit hash")
    args.output.mkdir(parents=True, exist_ok=True)

    def download(table):
        url = f"https://raw.githubusercontent.com/azerothcore/azerothcore-wotlk/{args.commit}/data/sql/base/db_world/{table}.sql"
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
        if b"CREATE TABLE" not in data or b"INSERT INTO" not in data:
            raise ValueError(f"{table}: response is not an SQL base dump")
        path = args.output / (table + ".sql")
        temporary = path.with_suffix(".sql.tmp")
        temporary.write_bytes(data)
        temporary.replace(path)
        print(f"{table}: {len(data)} bytes", flush=True)
        return table, {"url": url, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}

    with ThreadPoolExecutor(max_workers=4) as executor:
        tables = TABLES + (["areatrigger_teleport", "instance_template"] if args.full_world else [])
        if args.vendors:
            tables += ["npc_vendor", "conditions"]
        manifest = dict(executor.map(download, tables))
    (args.output / "download_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
