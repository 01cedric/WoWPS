#!/usr/bin/env python3
"""Compile the pinned world dump's pet tables into bounded generated headers.

Three tables, read by column NAME and never by position:

  pet_levelstats        - Guardian::InitStatsForLevel's per-level source
                          (Pet.cpp:1041-1200 via ObjectMgr::GetPetLevelInfo).
                          The reference's own query (ObjectMgr.cpp:4212) names
                          its columns in a DIFFERENT order from the schema -
                          `hp, mana, str, agi, sta, inte, spi, armor, …` against
                          the table's `hp, mana, armor, str, …` - so a positional
                          read of the dump would put armour where strength is.
  creature_template     - the `cinfo` half InitStatsForLevel reads: BaseAttackTime,
  + _model + model_info   dmgschool, type, family, unit_class, the first model by
                          idx and its CombatReach/BoundingRadius scaled by
                          DisplayScale, exactly as Creature::SetObjectScale does
                          (Creature.cpp:3528-3550, ObjectDefines.h:44).
  pet_name_generation   - ObjectMgr::GeneratePetName (ObjectMgr.cpp:8132-8148):
                          one random word from half 0 plus one from half 1.

Scope is the source's own: every creature_entry pet_levelstats names. That is
the world data's statement of "this creature has pet stats", and it is not a
judgement of this build's. All three files are byte-identical between the SQL
pin and 9c416aaacb5537636abb13c80f55a88947838e33.
"""
import argparse, hashlib, json, re
from pathlib import Path
from import_azerothcore import sql_rows, clean, PINNED_COMMIT, REPOSITORY

HEADER = ('// Generated from AzerothCore ' + PINNED_COMMIT +
          '; see assets/local_realm/NOTICE.txt.\n')
# ObjectDefines.h:44 - the reach a creature gets when creature_model_info has no
# row for its display id, or its CombatReach column is not positive.
DEFAULT_WORLD_OBJECT_SIZE = 0.388999998569489
MAX_LEVEL = 80


def emit(path, lines):
    path.write_text(HEADER + '\n'.join(lines) + '\n')


def cfloat(value):
    text = format(float(value), '.9g')
    return text + ('f' if any(c in text for c in '.eE') else '.0f')


def cstring(text):
    out = []
    for ch in text:
        if ch in '"\\':
            out.append('\\' + ch)
        elif ' ' <= ch <= '~':
            out.append(ch)
        else:                      # never emit a raw non-ASCII byte into a header
            out.append('\\x%02x' % ord(ch.encode('utf-8')[0:1]))
    return '"' + ''.join(out) + '"'


def levelstats(path):
    """Read pet_levelstats by column name and return {entry: {level: row}}."""
    rows = list(sql_rows(path, 'pet_levelstats'))
    out = {}
    for r in rows:
        level = r['level']
        if not 1 <= level <= MAX_LEVEL:
            raise ValueError('pet_levelstats level out of range: %r' % (r,))
        values = [r['hp'], r['mana'], r['armor'],
                  r['str'], r['agi'], r['sta'], r['inte'], r['spi'],
                  r['min_dmg'], r['max_dmg']]
        if any(not isinstance(v, int) or not 0 <= v <= 10 ** 9 for v in values):
            raise ValueError('pet_levelstats value out of range: %r' % (r,))
        if values[8] > values[9]:
            raise ValueError('pet_levelstats min_dmg > max_dmg: %r' % (r,))
        if out.setdefault(r['creature_entry'], {}).setdefault(level, values) is not values:
            raise ValueError('duplicate pet_levelstats row: %r' % (r,))
    for entry, byLevel in out.items():
        if sorted(byLevel) != list(range(1, MAX_LEVEL + 1)):
            raise ValueError('pet_levelstats %d is not contiguous 1..%d' % (entry, MAX_LEVEL))
        # ObjectMgr::LoadPetLevelInfo exits the process when level 1 is absent
        # or its health is zero; a placeholder of 1 is legal and expected for a
        # pet whose summon spell has a higher SpellLevel.
        if not byLevel[1][0]:
            raise ValueError('pet_levelstats %d has no usable level 1 row' % entry)
    return out


def generate(sql, out, report):
    stats = levelstats(sql / 'pet_levelstats.sql')
    entries = sorted(stats)
    templates = {r['entry']: r for r in sql_rows(sql / 'creature_template.sql', 'creature_template')}
    models = {}
    for r in sorted(sql_rows(sql / 'creature_template_model.sql', 'creature_template_model'),
                    key=lambda r: r['idx']):
        if r['creaturedisplayid']:
            models.setdefault(r['creatureid'], r)
    info = {r['displayid']: r for r in sql_rows(sql / 'creature_model_info.sql', 'creature_model_info')}

    templateLines = []
    for entry in entries:
        t = templates.get(entry)
        if t is None:
            raise ValueError('pet_levelstats names creature %d with no template' % entry)
        m = models.get(entry)
        if m is None:
            raise ValueError('pet creature %d has no creature_template_model row' % entry)
        scale = m['displayscale']
        if not 0 < scale <= 100:
            raise ValueError('pet creature %d has an unusable DisplayScale %r' % (entry, scale))
        row = info.get(m['creaturedisplayid'])
        reach = (row['combatreach'] if row and row['combatreach'] > 0 else DEFAULT_WORLD_OBJECT_SIZE) * scale
        radius = (row['boundingradius'] if row and row['boundingradius'] > 0 else 0.0) * scale
        if not 0 < reach <= 1000 or not 0 <= radius <= 1000:
            raise ValueError('pet creature %d has an unusable reach/radius' % entry)
        # Guardian::InitStatsForLevel takes cinfo->BaseAttackTime only when it is
        # at least 1000; below that the reference substitutes BASE_ATTACK_TIME.
        attack = t['baseattacktime'] if t['baseattacktime'] >= 1000 else 2000
        if not 1000 <= attack <= 10000:
            raise ValueError('pet creature %d has an unusable BaseAttackTime %r' % (entry, attack))
        for key, limit in (('dmgschool', 6), ('type', 15), ('family', 255), ('unit_class', 8)):
            if not 0 <= t[key] <= limit:
                raise ValueError('pet creature %d has an unusable %s %r' % (entry, key, t[key]))
        templateLines.append('{%d,%d,%d,%d,%d,%d,%d,%s,%s,%s},' % (
            entry, m['creaturedisplayid'], attack, t['dmgschool'], t['type'], t['family'],
            t['unit_class'], cfloat(round(reach, 4)), cfloat(round(radius, 4)),
            cstring(clean(t['name']) or ('Creature %d' % entry))))
    emit(out / 'local_pet_templates_generated.inc', templateLines)

    levelLines = []
    for entry in entries:
        for level in range(1, MAX_LEVEL + 1):
            levelLines.append('{%d,%d,{%s}},' % (entry, level,
                              ','.join(str(v) for v in stats[entry][level])))
    emit(out / 'local_pet_levels_generated.inc', levelLines)

    names = {}
    for r in sql_rows(sql / 'pet_name_generation.sql', 'pet_name_generation'):
        if r['half'] not in (0, 1):
            raise ValueError('pet_name_generation half out of range: %r' % (r,))
        word = clean(r['word'])
        if not word or len(word) > 24:
            raise ValueError('pet_name_generation word unusable: %r' % (r,))
        names.setdefault((r['entry'], r['half']), []).append(word)
    nameLines = []
    for (entry, half) in sorted(names):
        for word in names[(entry, half)]:
            nameLines.append('{%d,%d,%s},' % (entry, half, cstring(word)))
    # GeneratePetName falls back to CreatureFamily.Name and then to the template
    # name when either half is empty, so a one-sided entry would silently make
    # every pet of it share one name. Refuse it here instead.
    for (entry, half) in sorted(names):
        if (entry, 1 - half) not in names:
            raise ValueError('pet_name_generation entry %d has only half %d' % (entry, half))
    emit(out / 'local_pet_names_generated.inc', nameLines)

    paths = [sql / f for f in ('pet_levelstats.sql', 'pet_name_generation.sql',
                               'creature_template.sql', 'creature_template_model.sql',
                               'creature_model_info.sql')]
    summary = {
        'repository': REPOSITORY, 'sqlCommit': PINNED_COMMIT,
        'petTemplates': len(templateLines), 'petLevelRows': len(levelLines),
        'petNameWords': len(nameLines),
        'petNameGroups': len(names),
        'entries': entries,
        'sourceHashes': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
        'scope': 'Source pet metadata, not gameplay acceptance',
    }
    report.write_text(json.dumps(summary, indent=2) + '\n')
    print('Pet templates:', len(templateLines), 'level rows:', len(levelLines),
          'name words:', len(nameLines), 'in', len(names), 'halves')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    for name in ('sql', 'output', 'report'):
        p.add_argument(name, type=Path)
    a = p.parse_args()
    generate(a.sql, a.output, a.report)
