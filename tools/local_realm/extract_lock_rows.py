#!/usr/bin/env python3
"""Extract the Lock.dbc rows referenced by the captured original GameObjects.

The reviewed-content compiler must know which profession skill, skill value or
key a chest/resource lock demands. Those facts are columns of the WotLK 3.3.5a
client's own Lock.dbc (ID, Type[8], Index[8], Skill[8], Action[8]). This tool
reads the user's DBC once and writes a small pinned companion with the file
hash, so compilation stays reproducible without shipping client data.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent
LOCK_TEMPLATE_TYPES = {0, 1, 2, 3, 6, 10, 25}  # door, button, questgiver, chest, trap, goober, fishinghole


def read_lock_dbc(data: bytes):
    magic, count, fields, record_size, _ = struct.unpack_from("<4s4I", data)
    if magic != b"WDBC" or fields != 33 or record_size != 132:
        raise ValueError("Lock.dbc is not the WotLK 3.3.5a 33-field layout")
    rows = {}
    for index in range(count):
        values = struct.unpack_from("<33I", data, 20 + index * record_size)
        rows[values[0]] = {"id": values[0], "type": list(values[1:9]), "index": list(values[9:17]),
                           "skill": list(values[17:25]), "action": list(values[25:33])}
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("lock_dbc", type=Path)
    parser.add_argument("--source", type=Path, default=PROJECT / "assets/local_realm/original_script_sources.json.gz")
    parser.add_argument("--output", type=Path, default=HERE / "lock_rows.json")
    args = parser.parse_args()
    data = args.lock_dbc.read_bytes()
    rows = read_lock_dbc(data)
    source = json.loads(gzip.decompress(args.source.read_bytes()))
    wanted = sorted({t["data0"] for t in source["tables"]["gameobject_template"]
                     if t["type"] in LOCK_TEMPLATE_TYPES and t["data0"]})
    missing = [lock for lock in wanted if lock not in rows]
    out = {"schemaVersion": 1, "kind": "wotlk-lock-rows", "build": 12340,
           "lockDbcSha256": hashlib.sha256(data).hexdigest(),
           "rows": [rows[lock] for lock in wanted if lock in rows], "missing": missing}
    args.output.write_text(json.dumps(out, sort_keys=True, indent=1) + "\n")
    print(f"{len(out['rows'])} lock rows, {len(missing)} missing")


if __name__ == "__main__":
    main()
