#!/usr/bin/env python3
"""Record the full original membership size of every captured GameObject pool.

The production capture is limited to one region, so a pool_gameobject pool may
be only partly present. The reviewed-content compiler needs the original total
member count (and mother-pool links) to keep the regional spawn density equal
to AzerothCore's max_limit/total. This reads the pinned base SQL dumps.
"""
from __future__ import annotations

import gzip
import hashlib
import io
import json
import sys
import tarfile
import tempfile
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent
sys.path.insert(0, str(HERE))
from import_azerothcore import sql_rows  # noqa: E402


def main():
    archive = HERE / "world_script_source_sql.tar.gz"
    source = json.loads(gzip.decompress((PROJECT / "assets/local_realm/original_script_sources.json.gz").read_bytes()))
    captured = {row["pool_entry"] for row in source["tables"]["pool_gameobject"]}
    with tempfile.TemporaryDirectory() as directory, tarfile.open(archive) as tar:
        for name in ("pool_gameobject.sql", "pool_pool.sql"):
            tar.extract(name, directory, filter="data")
        members = Counter(row["pool_entry"] for row in sql_rows(Path(directory) / "pool_gameobject.sql", "pool_gameobject"))
        mothers = {row["pool_id"]: row["mother_pool"] for row in sql_rows(Path(directory) / "pool_pool.sql", "pool_pool")}
    out = {"schemaVersion": 1, "kind": "original-pool-totals",
           "archiveSha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
           "pools": [{"entry": entry, "totalMembers": members[entry], "motherPool": mothers.get(entry, 0)} for entry in sorted(captured)]}
    (HERE / "pool_totals.json").write_text(json.dumps(out, sort_keys=True, indent=1) + "\n")
    print(json.dumps(out["pools"]))


if __name__ == "__main__":
    main()
