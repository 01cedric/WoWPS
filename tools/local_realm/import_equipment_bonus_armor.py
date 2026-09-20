#!/usr/bin/env python3
"""Retain source bonus armor separately from form/talent-scaled base armor."""
import argparse
import hashlib
import json
import math
from pathlib import Path
from import_azerothcore import REPOSITORY, sql_rows
PINNED_COMMIT = "9c416aaacb5537636abb13c80f55a88947838e33"

def generate(source, output, report):
    records = []
    for row in sql_rows(source, 'item_template'):
        bonus = row['armordamagemodifier']
        if not bonus:
            continue
        if not math.isfinite(bonus) or abs(bonus) > 1000000:
            raise ValueError(f"Invalid bonus armor for {row['entry']}")
        records.append((row['entry'], float(bonus)))
    records.sort()
    if len({entry for entry, _ in records}) != len(records):
        raise ValueError('Duplicate item')
    output.write_text('// Generated from AzerothCore '+PINNED_COMMIT+'; see assets/local_realm/NOTICE.txt.\n' +
                      ''.join('{'+str(entry)+','+format(bonus,'.9f').rstrip('0').rstrip('.')+'.0f},\n' if bonus==int(bonus) else '{'+str(entry)+','+format(bonus,'.9g')+'f},\n' for entry, bonus in records))
    report.write_text(json.dumps({'repository': REPOSITORY, 'sqlCommit': PINNED_COMMIT,
        'records': len(records), 'sourceSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'outputSha256': hashlib.sha256(output.read_bytes()).hexdigest(),
        'scope': 'All item classes: subtract truncated signed ArmorDamageModifier from nonzero armor base; add positive float modifier as TOTAL_VALUE; default source script hook enabled.'}, indent=2)+'\n')
    print(f'PASS equipment bonus armor: {len(records)} source records')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('source', 'output', 'report'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    generate(args.source, args.output, args.report)
