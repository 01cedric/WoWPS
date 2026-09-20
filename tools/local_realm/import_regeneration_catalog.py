#!/usr/bin/env python3
"""Compile level 1-80 regeneration coefficients and direct equipped regen stats."""
import argparse, hashlib, json, math, struct
from pathlib import Path
from import_azerothcore import sql_rows, PINNED_COMMIT, REPOSITORY
DBC_COMMIT = 'cefe45546e001f7745022479912dd140861a3647'

def generate(dbcdir, item_sql, output, report):
    tables = []
    paths = [dbcdir / (name + '.dbc') for name in ('gtOCTRegenHP', 'gtRegenHPPerSpt', 'gtRegenMPPerSpt')]
    for path in paths:
        b = path.read_bytes()
        magic, rows, fields, size, strings = struct.unpack_from('<4s4I', b)
        if (magic, rows, fields, size) != (b'WDBC', 1100, 1, 4) or len(b) != 20 + rows * size + strings:
            raise ValueError('Unexpected game table: ' + str(path))
        values = list(struct.unpack_from('<1100f', b, 20))
        if any(not math.isfinite(v) or v < 0 or v > 100 for v in values):
            raise ValueError('Invalid regeneration coefficient')
        tables.append(values)
    lines = []
    for clazz in (1,2,3,4,5,6,7,8,9,11):
        for level in range(1,81):
            vals = [t[(clazz-1)*100+level-1] for t in tables]
            lines.append('{' + str(clazz) + ',' + str(level) + ',' + ','.join(format(v, '.12e') + 'f' for v in vals) + '},')
    items = []
    for row in sql_rows(item_sql, 'item_template'):
        if row['class'] not in (2,4):
            continue
        mp5 = sum(row['stat_value'+str(i)] for i in range(1,11) if row['stat_type'+str(i)] == 43)
        hp5 = sum(row['stat_value'+str(i)] for i in range(1,11) if row['stat_type'+str(i)] == 46)
        if abs(mp5) > 1000000 or abs(hp5) > 1000000:
            raise ValueError('Invalid item regeneration')
        if mp5 or hp5:
            items.append((row['entry'], mp5, hp5))
    output.mkdir(parents=True, exist_ok=True)
    notice = '// Generated source metadata; see docs/implementation_inventory/regeneration_source.json and assets/local_realm/NOTICE.txt.\n'
    (output/'local_regeneration_ratios_generated.inc').write_text(notice + '\n'.join(lines) + '\n')
    (output/'local_regeneration_items_generated.inc').write_text(notice + ''.join('{' + ','.join(map(str,r)) + '},\n' for r in sorted(items)))
    report.write_text(json.dumps({'repository':REPOSITORY,'sqlCommit':PINNED_COMMIT,
        'dbcRepository':'https://github.com/Elwynsiaa/data','dbcCommit':DBC_COMMIT,
        'classLevelRows':len(lines),'directItemRegenRows':len(items),
        'sourceHashes':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in paths+[item_sql]},
        'scope':'Base spirit regeneration and direct item stats; conditional passives/item spells remain incomplete; functional acceptance is P35.'}, indent=2) + '\n')
    print('Regeneration rows:', len(lines), 'direct item regen rows:', len(items))

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    for key in ('dbcdir','item_sql','output','report'):p.add_argument(key,type=Path)
    a=p.parse_args();generate(a.dbcdir,a.item_sql,a.output,a.report)
