#!/usr/bin/env python3
"""Add the spawns' default movement to a compiled local world catalog (2.39).

The reference gives every spawned creature a default movement generator
(Creature::LoadFromDB: `creature.MovementType` with `wander_distance`, and the
`creature_addon.path_id` patrol whose points are the `waypoint_data` rows;
ObjectMgr::LoadCreatures turns a random type without a wander distance into an
idle one). The shipped catalog carries neither, so its creatures stood still
between fights. This tool writes two files beside the catalog's packs:

  motion.pack   fixed 16-byte rows sorted by spawn guid:
                guid u32, movementType u8 (1 random, 2 waypoint), reserved u8,
                currentWaypoint u16 (the node the path starts at), wander f32,
                pathId u32 - only the spawns that move.
  paths.pack    a WPCAT01 keyed pack (path id -> raw record) of the
                waypoint_data paths those spawns use and the ones the installed
                SmartAI rows name (WAYPOINT_START / WAYPOINT_DATA_RANDOM):
                24 bytes per node - x, y, z, orientation f32 (a NULL
                orientation is written as -1000), delay u32 (ms), moveType u8
                (0 walk, 1 run, 2 land, 3 takeoff), smoothTransition u8, the
                point id u16. Nodes keep the table order (point ascending); a
                path whose points are not 1..n (or 0..n-1) in order is dropped.

waypoint_data.action (waypoint_scripts) and velocity overrides are not
carried; the report lists how many rows carry them. The manifest fingerprint
is refreshed so peers with a different motion table do not pair.
"""
from __future__ import annotations
import argparse, json, struct, tarfile, tempfile
from collections import Counter
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from import_azerothcore import sql_rows  # noqa: E402
from patch_reputation_catalog import refresh_manifest, MAGIC  # noqa: E402

NO_ORIENTATION = -1000.0
MAX_NODES = 1024


def load_tables(world: Path, scripts: Path):
    tables = {}
    with tempfile.TemporaryDirectory() as directory:
        with tarfile.open(world) as tar:
            tar.extract('creature.sql', directory, filter='data')
            tables['creature'] = list(sql_rows(Path(directory) / 'creature.sql', 'creature'))
        with tarfile.open(scripts) as tar:
            for table in ('creature_addon', 'waypoint_data', 'smart_scripts'):
                tar.extract(table + '.sql', directory, filter='data')
                tables[table] = list(sql_rows(Path(directory) / (table + '.sql'), table))
    return tables


def compile_paths(rows):
    """waypoint_data grouped by path: id -> ordered node list, plus the dropped ids."""
    grouped = {}
    for r in rows:
        grouped.setdefault(r['id'], []).append(r)
    paths, dropped = {}, []
    for path_id, nodes in grouped.items():
        nodes.sort(key=lambda r: r['point'])
        points = [r['point'] for r in nodes]
        first = points[0] if points else 0
        if not nodes or len(nodes) > MAX_NODES or points != list(range(first, first + len(nodes))) or first not in (0, 1) or \
                any(r['move_type'] not in (0, 1, 2, 3) or r['delay'] < 0 or r['delay'] > 3600000 for r in nodes) or \
                any(not all(abs(float(r[c])) <= 1e5 for c in ('position_x', 'position_y')) or abs(float(r['position_z'])) > 2e4 for r in nodes):
            dropped.append(path_id)
            continue
        paths[path_id] = nodes
    return paths, dropped


def encode_path(nodes):
    out = bytearray()
    for r in nodes:
        orientation = r['orientation']
        o = NO_ORIENTATION if orientation is None else float(orientation)
        out += struct.pack('<ffffIBBH', float(r['position_x']), float(r['position_y']), float(r['position_z']), o,
                           int(r['delay']), int(r['move_type']), 1 if r['smoothtransition'] else 0, int(r['point']))
    return bytes(out)


def write_paths(path: Path, paths):
    records = [(int(k), encode_path(v)) for k, v in sorted(paths.items())]
    offset = 16 + 16 * len(records)
    with path.open('wb') as f:
        f.write(struct.pack('<8sII', MAGIC, len(records), 16))
        for key, blob in records:
            f.write(struct.pack('<IQI', key, offset, len(blob)))
            offset += len(blob)
        for _, blob in records:
            f.write(blob)


def write_motion(path: Path, rows):
    with path.open('wb') as f:
        f.write(struct.pack('<8sII', b'WPMOT01\0', len(rows), 16))
        for guid, kind, current, wander, path_id in rows:
            f.write(struct.pack('<IBBHfI', guid, kind, 0, current, wander, path_id))


def patch(catalog: Path, tables, spawned_guids):
    paths, dropped = compile_paths(tables['waypoint_data'])
    addon_paths = {r['guid']: r['path_id'] for r in tables['creature_addon'] if r['path_id']}
    # The paths SmartAI rows start (the generator admits only the ones carried here).
    scripted = set()
    for r in tables['smart_scripts']:
        if r['action_type'] == 232 and r['action_param1']:
            scripted.add(r['action_param1'])
        elif r['action_type'] == 233 and r['action_param1'] and r['action_param2'] and r['action_param2'] >= r['action_param1'] and \
                r['action_param2'] - r['action_param1'] < 64:
            scripted.update(range(r['action_param1'], r['action_param2'] + 1))
    motion, used, counts = [], set(), Counter()
    for c in tables['creature']:
        guid = c['guid']
        if guid not in spawned_guids:
            continue
        kind = c['movementtype']
        wander = float(c['wander_distance'] or 0)
        if kind == 1:
            # ObjectMgr::LoadCreatures: a random mover without a wander distance idles.
            if wander <= 0:
                counts['randomWithoutDistance'] += 1
                continue
            motion.append((guid, 1, 0, min(wander, 1000.0), 0))
            counts['random'] += 1
        elif kind == 2:
            path_id = addon_paths.get(guid, 0)
            if not path_id or path_id not in paths:
                counts['waypointWithoutPath'] += 1
                continue
            current = int(c['currentwaypoint'] or 0)
            if current >= len(paths[path_id]):
                current = 0
            motion.append((guid, 2, current, 0.0, path_id))
            used.add(path_id)
            counts['waypoint'] += 1
        else:
            counts['idle'] += 1
    motion.sort()
    carried = {p: paths[p] for p in used | (scripted & set(paths))}
    write_motion(catalog / 'motion.pack', motion)
    write_paths(catalog / 'paths.pack', carried)
    fingerprint = refresh_manifest(catalog)
    nodes = sum(len(v) for v in carried.values())
    return {'spawnsWithMotion': len(motion), **counts, 'pathsCarried': len(carried), 'pathsScripted': len(scripted & set(paths)),
            'pathsDropped': dropped, 'nodes': nodes,
            'nodesWithDelay': sum(1 for v in carried.values() for r in v if r['delay']),
            'nodesWithAction': sum(1 for v in carried.values() for r in v if r['action']),
            'nodesWithVelocity': sum(1 for v in carried.values() for r in v if r['velocity']),
            'moveTypes': dict(sorted(Counter(r['move_type'] for v in carried.values() for r in v).items())),
            'fingerprint': fingerprint}


def spawned_guids_of(catalog: Path):
    """The spawn guids the catalog's spawns.pack carries (28-byte rows)."""
    data = (catalog / 'spawns.pack').read_bytes()
    return {struct.unpack_from('<I', data, i)[0] for i in range(0, len(data) - len(data) % 28, 28)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--world-archive', type=Path, default=HERE / 'world_source_sql.tar.gz')
    parser.add_argument('--script-archive', type=Path, default=HERE / 'world_script_source_sql.tar.gz')
    parser.add_argument('--catalog', type=Path, default=HERE.parent.parent / 'assets/local_realm/catalog')
    parser.add_argument('--output-report', type=Path, default=HERE.parent.parent / 'docs/MOTION_CATALOG_REPORT.json')
    args = parser.parse_args()
    result = patch(args.catalog, load_tables(args.world_archive, args.script_archive), spawned_guids_of(args.catalog))
    args.output_report.write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps({k: (len(v) if isinstance(v, list) else v) for k, v in result.items()}))


if __name__ == '__main__':
    main()
