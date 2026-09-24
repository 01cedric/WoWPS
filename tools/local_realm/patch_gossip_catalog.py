#!/usr/bin/env python3
"""Add the gossip menus to a compiled local world catalog (2.40).

The reference opens a creature's gossip through Player::PrepareGossipMenu /
SendPreparedGossip: the creature_template.gossip_menu_id menu, its
gossip_menu rows (the npc_text shown, the first whose conditions hold; menus
without one show the default greeting), its gossip_menu_option rows (icon,
text, option type, the npcflag the creature must carry, the action menu, the
confirmation box with its price) and the SmartAI rows a selection runs. The
shipped catalog carried none of it, so every creature said the same
placeholder greeting and offered nothing but quests and services. This tool
writes three files beside the catalog's packs:

  gossip_owners.pack  fixed 12-byte rows sorted by creature entry:
                      entry u32, gossip_menu_id u32, npcflag u32 - the spawned
                      templates with a menu or the GOSSIP flag.
  gossip.pack         a WPCAT01 keyed pack (menu id -> record) of every menu a
                      spawned creature, an action menu chain or an installed
                      SmartAI row (SEND_GOSSIP_MENU / SET_GOSSIP_MENU) can reach:
                        u16 textCount, per text: u32 npc_text id, then its
                          conditions (u8 count; u8 type, u8 elseGroup,
                          u8 negative, u32 value1..3);
                        u16 optionCount, per option: u16 option id, u8 icon,
                          u8 type, u32 npcflag, u32 action menu, u32 box money,
                          u8 box coded, u16-prefixed text, u16-prefixed box
                          text, then its conditions.
  gossip_texts.pack   a WPCAT01 keyed pack (npc_text id -> record) of the
                      texts those menus and rows show: u8 variant count, per
                      variant f32 probability, u8 language, u16-prefixed male
                      text, u16-prefixed female text, six u16 emote fields.

Conditions (the `conditions` table, source types 14 gossip menu and 15 gossip
option) are carried when every condition of the row is one the authority
evaluates (CONDITION_LIST below, on the player); a text or an option with a
condition outside that list, or one on the creature (ConditionTarget 1), is
dropped and counted in the report - hidden rather than shown blindly. Options
that need a coded answer (BoxCoded) and option types the realm has no window
for are dropped the same way. The manifest fingerprint is refreshed.
"""
from __future__ import annotations
import argparse, json, struct, tarfile, tempfile
from collections import Counter, defaultdict
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from import_azerothcore import sql_rows  # noqa: E402
from patch_reputation_catalog import refresh_manifest, MAGIC  # noqa: E402

OWNERS_MAGIC = b'WPGOW01\0'
# ConditionTypes the authority evaluates on the player (ConditionMgr.h).
CONDITION_LIST = {1, 2, 3, 5, 6, 7, 8, 9, 12, 14, 15, 16, 20, 25, 27, 28, 29, 47}
# GossipOptionType values the local dialogue can act on: gossip, quest giver,
# vendor, taxi, trainer, innkeeper, banker, auctioneer, stable, unlearn talents.
OPTION_TYPES = {1, 2, 3, 4, 5, 8, 9, 13, 14, 16}
SOURCE_GOSSIP_MENU, SOURCE_GOSSIP_OPTION = 14, 15
AC_SEND_GOSSIP_MENU, AC_SET_GOSSIP_MENU, EV_GOSSIP_SELECT = 98, 240, 62
MAX_TEXT = 4096


def load_tables(world: Path, scripts: Path, gossip: Path, vendor: Path):
    tables = {}
    with tempfile.TemporaryDirectory() as directory:
        for archive, names in ((world, ('creature_template', 'creature')), (scripts, ('smart_scripts',)),
                               (gossip, ('gossip_menu', 'gossip_menu_option', 'npc_text')), (vendor, ('conditions',))):
            with tarfile.open(archive) as tar:
                for table in names:
                    tar.extract(table + '.sql', directory, filter='data')
                    tables[table] = list(sql_rows(Path(directory) / (table + '.sql'), table))
    return tables


def compile_conditions(rows):
    """(source type, group, entry) -> list of (type, elseGroup, negative, v1, v2, v3) or None when unsupported."""
    out = {}
    for r in rows:
        if r['sourcetypeorreferenceid'] not in (SOURCE_GOSSIP_MENU, SOURCE_GOSSIP_OPTION):
            continue
        key = (r['sourcetypeorreferenceid'], r['sourcegroup'], r['sourceentry'])
        entry = out.setdefault(key, [])
        if entry is None:
            continue
        if r['conditiontypeorreference'] not in CONDITION_LIST or r['conditiontarget'] != 0:
            out[key] = None
            continue
        entry.append((r['conditiontypeorreference'], r['elsegroup'] & 0xff, 1 if r['negativecondition'] else 0,
                      r['conditionvalue1'] & 0xffffffff, r['conditionvalue2'] & 0xffffffff, r['conditionvalue3'] & 0xffffffff))
    return out


def encode_conditions(conditions):
    out = struct.pack('<B', len(conditions))
    for c in conditions:
        out += struct.pack('<BBBIII', *c)
    return out


def text_bytes(s):
    b = (s or '').encode('utf-8')[:MAX_TEXT]
    return struct.pack('<H', len(b)) + b


def patch(catalog: Path, tables, spawned_entries):
    conditions = compile_conditions(tables['conditions'])
    menus_texts = defaultdict(list)
    for r in tables['gossip_menu']:
        menus_texts[r['menuid']].append(r['textid'])
    menus_options = defaultdict(list)
    for r in tables['gossip_menu_option']:
        menus_options[r['menuid']].append(r)
    texts = {r['id']: r for r in tables['npc_text']}
    templates = {r['entry']: r for r in tables['creature_template']}
    counts = Counter()
    # The menus to reach: spawned templates, the SmartAI rows, then the action menus.
    owners = []
    reach = set()
    for entry in sorted(spawned_entries):
        t = templates.get(entry)
        if not t:
            continue
        menu = t['gossip_menu_id']
        if menu or t['npcflag'] & 1:
            owners.append((entry, menu, t['npcflag'] & 0xffffffff))
        if menu:
            reach.add(menu)
    script_texts = set()
    for r in tables['smart_scripts']:
        if r['source_type'] not in (0, 9):
            continue
        if r['action_type'] == AC_SEND_GOSSIP_MENU:
            if r['action_param1']:
                reach.add(r['action_param1'])
            if r['action_param2']:
                script_texts.add(r['action_param2'])
        elif r['action_type'] == AC_SET_GOSSIP_MENU and r['action_param1']:
            reach.add(r['action_param1'])
        if r['event_type'] == EV_GOSSIP_SELECT and r['event_param1']:
            reach.add(r['event_param1'])
    reach.add(0)  # the default per-npcflag options
    pending = list(reach)
    while pending:
        menu = pending.pop()
        for o in menus_options.get(menu, ()):
            if o['actionmenuid'] and o['actionmenuid'] not in reach:
                reach.add(o['actionmenuid'])
                pending.append(o['actionmenuid'])
    records, used_texts = {}, set(script_texts)
    for menu in sorted(reach):
        text_rows, option_rows = [], []
        for text_id in sorted(menus_texts.get(menu, ())):
            if text_id not in texts:
                counts['textMissing'] += 1
                continue
            cond = conditions.get((SOURCE_GOSSIP_MENU, menu, text_id), [])
            if cond is None:
                counts['textConditionDropped'] += 1
                continue
            text_rows.append(struct.pack('<I', text_id) + encode_conditions(cond))
            used_texts.add(text_id)
            counts['menuTexts'] += 1
        for o in sorted(menus_options.get(menu, ()), key=lambda r: r['optionid']):
            if o['optiontype'] not in OPTION_TYPES or o['boxcoded'] or o['optionid'] > 0xffff or o['optionicon'] > 255:
                counts['optionTypeDropped'] += 1
                continue
            cond = conditions.get((SOURCE_GOSSIP_OPTION, menu, o['optionid']), [])
            if cond is None:
                counts['optionConditionDropped'] += 1
                continue
            option_rows.append(struct.pack('<HBBIIIB', o['optionid'], o['optionicon'], o['optiontype'], o['optionnpcflag'] & 0xffffffff,
                                           o['actionmenuid'], o['boxmoney'] & 0xffffffff, 0) +
                               text_bytes(o['optiontext']) + text_bytes(o['boxtext']) + encode_conditions(cond))
            counts['options'] += 1
        if not text_rows and not option_rows and menu:
            counts['menusEmpty'] += 1
            continue
        blob = struct.pack('<H', len(text_rows)) + b''.join(text_rows) + struct.pack('<H', len(option_rows)) + b''.join(option_rows)
        records[menu] = blob
        counts['menus'] += 1
    text_records = {}
    for text_id in sorted(used_texts):
        t = texts.get(text_id)
        if not t:
            counts['scriptTextMissing'] += 1
            continue
        variants = []
        for k in range(8):
            male, female = t.get('text%d_0' % k) or '', t.get('text%d_1' % k) or ''
            probability = float(t.get('probability%d' % k) or 0)
            if not male and not female:
                continue
            if probability <= 0 and variants:
                continue
            emotes = [int(t.get('em%d_%d' % (k, j)) or 0) & 0xffff for j in range(6)]
            variants.append(struct.pack('<fB', max(probability, 0.0), int(t.get('lang%d' % k) or 0) & 0xff) +
                            text_bytes(male) + text_bytes(female) + struct.pack('<6H', *emotes))
        if not variants:
            counts['textsEmpty'] += 1
            continue
        text_records[text_id] = struct.pack('<B', len(variants)) + b''.join(variants)
        counts['texts'] += 1
        counts['textVariants'] += len(variants)
    write_keyed(catalog / 'gossip.pack', records)
    write_keyed(catalog / 'gossip_texts.pack', text_records)
    with (catalog / 'gossip_owners.pack').open('wb') as f:
        f.write(struct.pack('<8sII', OWNERS_MAGIC, len(owners), 12))
        for entry, menu, flags in owners:
            f.write(struct.pack('<III', entry, menu, flags))
    fingerprint = refresh_manifest(catalog)
    return {'owners': len(owners), 'ownersWithMenu': sum(1 for o in owners if o[1]), 'ownersWithGossipFlag': sum(1 for o in owners if o[2] & 1),
            'menusReached': len(reach), **counts, 'scriptTexts': len(script_texts), 'fingerprint': fingerprint}


def write_keyed(path: Path, records):
    rows = [(int(k), v) for k, v in sorted(records.items())]
    offset = 16 + 16 * len(rows)
    with path.open('wb') as f:
        f.write(struct.pack('<8sII', MAGIC, len(rows), 16))
        for key, blob in rows:
            f.write(struct.pack('<IQI', key, offset, len(blob)))
            offset += len(blob)
        for _, blob in rows:
            f.write(blob)


def keyed_pack_index(path: Path):
    """The keys of a WPCAT01 keyed pack with their (offset, length)."""
    data = path.read_bytes()
    magic, count, width = struct.unpack_from('<8sII', data, 0)
    if magic != MAGIC or width != 16:
        raise ValueError(f'{path}: unsupported pack')
    return data, {struct.unpack_from('<I', data, 16 + 16 * i)[0]: struct.unpack_from('<QI', data, 20 + 16 * i) for i in range(count)}


def catalog_gossip(catalog: Path):
    """What the catalog's gossip packs carry: menu ids, text ids, (menu, option) pairs."""
    menus, texts, options = set(), set(), set()
    if not (catalog / 'gossip.pack').exists():
        return menus, texts, options
    data, index = keyed_pack_index(catalog / 'gossip.pack')
    for menu, (offset, length) in index.items():
        menus.add(menu)
        at = offset
        text_count, = struct.unpack_from('<H', data, at); at += 2
        for _ in range(text_count):
            at += 4
            conditions, = struct.unpack_from('<B', data, at); at += 1 + 15 * conditions
        option_count, = struct.unpack_from('<H', data, at); at += 2
        for _ in range(option_count):
            option_id, = struct.unpack_from('<H', data, at); at += 2 + 1 + 1 + 4 + 4 + 4 + 1
            for _ in range(2):
                n, = struct.unpack_from('<H', data, at); at += 2 + n
            conditions, = struct.unpack_from('<B', data, at); at += 1 + 15 * conditions
            options.add((menu, option_id))
        if at != offset + length:
            raise ValueError(f'gossip.pack: menu {menu} record size')
    _, text_index = keyed_pack_index(catalog / 'gossip_texts.pack')
    texts.update(text_index)
    return menus, texts, options


def spawned_entries_of(catalog: Path):
    """The creature entries the catalog's spawns.pack carries (28-byte rows: id, entry, ...)."""
    data = (catalog / 'spawns.pack').read_bytes()
    return {struct.unpack_from('<I', data, i + 4)[0] for i in range(0, len(data) - len(data) % 28, 28)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--world-archive', type=Path, default=HERE / 'world_source_sql.tar.gz')
    parser.add_argument('--script-archive', type=Path, default=HERE / 'world_script_source_sql.tar.gz')
    parser.add_argument('--gossip-archive', type=Path, default=HERE / 'gossip_source_sql.tar.gz')
    parser.add_argument('--vendor-archive', type=Path, default=HERE / 'vendor_source_sql.tar.gz')
    parser.add_argument('--catalog', type=Path, default=HERE.parent.parent / 'assets/local_realm/catalog')
    parser.add_argument('--output-report', type=Path, default=HERE.parent.parent / 'docs/GOSSIP_CATALOG_REPORT.json')
    args = parser.parse_args()
    tables = load_tables(args.world_archive, args.script_archive, args.gossip_archive, args.vendor_archive)
    result = patch(args.catalog, tables, spawned_entries_of(args.catalog))
    args.output_report.write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
