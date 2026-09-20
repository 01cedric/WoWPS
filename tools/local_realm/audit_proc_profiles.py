#!/usr/bin/env python3
"""Join P03's reviewed proc IDs to real DBCs, pinned SQL and production decoder output.

Reports decoder admission separately from talent progression. Does not simulate
combat and is not P35 gameplay or console acceptance.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
from audit_regeneration_talents import records

DBC_REV = 'cefe45546e001f7745022479912dd140861a3647'
REFERENCE_REV = '9c416aaacb5537636abb13c80f55a88947838e33'
FAMILIES = {
    'Earth Shield': [974, 32593, 32594, 49283, 49284],
    'Unbridled Wrath': [12322, 12999, 13000, 13001, 13002],
    'Focused Attacks': [51634, 51635, 51636],
    'Combat Potency': [35541, 35550, 35551, 35552, 35553],
    'Cruelty': [12320, 12852, 12853, 12855, 12856],
    'Primal Fury': [37116, 37117],
    'Bloodthirst': [23881],
    'Unending Fury': [56927, 56929, 56930, 56931, 56932],
    'Warrior Flurry': [12319, 12971, 12972, 12973, 12974],
    'Shaman Flurry': [16256, 16281, 16282, 16283, 16284],
    'Molten Armor': [30482, 43045, 43046],
    'Molten Shields': [11094, 13043],
    'Fire Ward': [543, 8457, 8458, 10223, 10225, 27128, 43010],
    'Frost Ward': [6143, 8461, 8462, 10177, 28609, 32796, 43012],
    'Ignite': [11119, 11120, 12846, 12847, 12848],
    'Arcane Concentration': [11213, 12574, 12575, 12576, 12577],
    'Omen of Clarity': [16864],
    'Ancestral Knowledge': [17485, 17486, 17487, 17488, 17489],
    'Thundering Strikes': [16255, 16302, 16303, 16304, 16305],
    'Improved Ghost Wolf': [16262, 16287],
    'Ghost Wolf': [2645],
    'Armored to the Teeth': [61216, 61221, 61222],
    'Blood Craze': [16487, 16489, 16492],
    'Dual Wield Specialization': [23584, 23585, 23586, 23587, 23588],
    'Precision': [29590, 29591, 29592],
    'Death Wish': [12292],
    'Improved Mark of the Wild': [17050, 17051],
    'Naturalist': [17069, 17070, 17071, 17072, 17073],
    'Natural Shapeshifter': [16833, 16834, 16835],
    'Arcane Blast': [30451, 42894, 42896, 42897],
    'Arcane Stability': [11237, 12463, 12464, 16769, 16770],
    'Ferocity': [16934, 16935, 16936, 16937, 16938],
    'Savage Fury': [16998, 16999],
    'Thick Hide': [16929, 16930, 16931],
    'Feral Swiftness': [17002, 24866],
    'Sharpened Claws': [16942, 16943, 16944],
    'Mental Dexterity': [51883, 51884, 51885],
    'Weapon Mastery': [29082, 29084, 29086],
    'Spirit Weapons': [16268],
    'Shamanistic Focus': [43338],
    'Dual Wield': [30798],
    'Shaman Dual Wield Specialization': [30816, 30818, 30819],
    'Stormstrike': [17364],
    'Improved Stormstrike': [51521, 51522],
    'Go for the Throat': [34950, 34954],
}
# These families have complete active/passive effects rather than a parent proc
# override. Arcane Blast's separate child proc row is mandatory below.
NO_PARENT_PROC_ROW = {
    'Cruelty', 'Bloodthirst', 'Unending Fury', 'Ancestral Knowledge',
    'Thundering Strikes', 'Improved Ghost Wolf', 'Ghost Wolf',
    'Armored to the Teeth', 'Dual Wield Specialization', 'Precision', 'Death Wish',
    'Improved Mark of the Wild', 'Naturalist', 'Natural Shapeshifter',
    'Arcane Blast', 'Arcane Stability',
    'Ferocity', 'Savage Fury', 'Thick Hide', 'Feral Swiftness', 'Sharpened Claws',
    'Mental Dexterity', 'Weapon Mastery', 'Spirit Weapons', 'Shamanistic Focus',
    'Dual Wield', 'Shaman Dual Wield Specialization',
    'Molten Shields', 'Fire Ward', 'Frost Ward',
}


def source_effects(row):
    return [{'effect': row[71 + i], 'aura': row[95 + i],
             'targetA': row[86 + i], 'targetB': row[89 + i],
             'basePointsRaw': row[80 + i], 'dieSides': row[74 + i],
             'periodMs': row[98 + i], 'miscValueRaw': row[110 + i],
             'classMask': row[122 + 3 * i:125 + 3 * i],
             'triggerSpell': row[116 + i]} for i in range(3) if row[71 + i]]



def run(audit_path, dbc, source, output):
    audit = json.loads(audit_path.read_text())
    if not audit.get('inputComplete'):
        raise ValueError('Complete production importer audit required')
    definitions = {r['spellId']: r for r in audit['retainedDefinitions']}
    spells, _ = records(dbc / 'Spell.dbc')
    sql = (source / 'spell_proc.sql').read_text()
    sql_rows = {int(values[0]): [float(v) if '.' in v else int(v) for v in values]
                for line in sql.splitlines()
                if (m := re.match(r'^\(([-0-9.,]+)\)[,;]?$', line))
                for values in [m[1].split(',')]}
    if (source / 'revision.txt').read_text().strip() != REFERENCE_REV:
        raise ValueError('Unexpected reference revision')
    for key, expected in {
        -16487: [-16487, 0, 0, 0, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0],
        36032: [36032, 0, 3, 4096, 32768, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0],
        17364: [17364, 8, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0],
        -51521: [-51521, 0, 11, 0, 16777216, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0],
        -11119: [-11119, 4, 3, 0, 0, 0, 0, 1, 2, 2, 0, 0, 0, 0, 0, 0],
        -30482: [-30482, 0, 0, 0, 0, 0, 0, 1, 0, 1027, 2, 0, 0, 0, 0, 0],
        -34950: [-34950, 0, 0, 0, 0, 0, 0, 1, 2, 2, 0, 0, 0, 0, 0, 0],
    }.items():
        if sql_rows.get(key) != expected:
            raise ValueError(f'Unexpected reviewed proc override for {key}')
    trees = {}
    for tab in sorted({d['talentTab'] for d in definitions.values() if d['talentId']}):
        candidates = sorted((d for d in definitions.values() if d['talentId'] and
                             d['talentTab'] == tab and d['decoderAccepted']),
                            key=lambda d: (d['talentRow'], d['talentId'], d['talentRank']))
        learned, path = {}, []
        while len(path) < 71:
            changed = False
            for d in candidates:
                if len(path) == 71:
                    break
                if learned.get(d['talentId'], 0) + 1 != d['talentRank'] or len(path) < d['talentRow'] * 5:
                    continue
                if any(req and learned.get(req, 0) < rank + 1 for req, rank in
                       zip(d['talentPrerequisites'], d['talentPrerequisiteRanks'])):
                    continue
                learned[d['talentId']] = d['talentRank']
                path.append(d['spellId'])
                changed = True
            if not changed:
                break
        trees[tab] = {'reachablePoints': len(path), 'learnedRanks': learned, 'spellRankLearningOrder': path}
    families = []
    for name, ids in FAMILIES.items():
        profiles = []
        for spell_id in ids:
            d, row = definitions[spell_id], spells[spell_id]
            root = definitions[974] if name == 'Earth Shield' else d
            tree = trees[root['talentTab']] if root['talentId'] else {'learnedRanks': {}, 'reachablePoints': 0}
            reachable = tree['learnedRanks'].get(root['talentId'], 0) >= root['talentRank'] if root['talentId'] else d['decoderAccepted']
            proc_row = sql_rows.get(spell_id) or sql_rows.get(-ids[0])
            if name == 'Primal Fury':
                proc_row = [sql_rows[-16958], sql_rows[-16952]]
            if not proc_row and name not in NO_PARENT_PROC_ROW:
                raise ValueError(f'Missing reference proc record for {spell_id}')
            children = ([12536] if name == 'Arcane Concentration' else
                        [12654] if name == 'Ignite' else
                        [16870] if name == 'Omen of Clarity' else
                        [67116] if name == 'Ghost Wolf' else
                        [36032] if name == 'Arcane Blast' else
                        [24867 if spell_id == 17002 else 24864] if name == 'Feral Swiftness' else
                        [18848, 36591] if name == 'Spirit Weapons' else
                        [674] if name == 'Dual Wield' else
                        [32175, 32176] if name == 'Stormstrike' else
                        [63375] if name == 'Improved Stormstrike' else
                        [34952 if spell_id == 34950 else 34953] if name == 'Go for the Throat' else
                        [row[116]] if name in ('Molten Armor', 'Blood Craze') or name.endswith(' Flurry') else
                        [row[116], row[117]] if name == 'Primal Fury' else
                        [23885, 23880] if name == 'Bloodthirst' else [])
            profiles.append({'spellId': spell_id, 'decoderAccepted': d['decoderAccepted'],
                             'firstRejection': d['firstRejection'], 'reachableWithCurrentDecoder': reachable,
                             'rootTalentId': root['talentId'], 'requiredTreePoints': root['talentRow'] * 5,
                             'reachableTreePoints': tree['reachablePoints'], 'referenceProcRow': proc_row,
                             'sourceEffects': source_effects(row),
                             'sourceProfileChildren': children,
                             'sourceChildEffects': {str(child): source_effects(spells[child]) for child in children},
                             'internalReviewedChildren': {str(child): definitions.get(child) for child in children},
                             'sourceDurationId': row[40],
                             'sourceCorrections': [{'field': 'DurationIndex', 'raw': 32, 'corrected': 21,
                                'reference': 'SpellInfoCorrections.cpp'}] if spell_id in (16834, 16835) else [],
                             'internalMoltenLeaf': definitions.get(row[116]) if name == 'Molten Armor' else None,
                             'spiritCritRatingPct': d.get('spiritCritRatingPct', 0),
                             'incomingCritReductionPct': d.get('incomingCritReductionPct', 0),
                             'wardProfile': d.get('wardProfile', 0),
                             'moltenShieldsChancePct': d.get('moltenShieldsChancePct', 0),
                             'moltenArmorScriptGate': {'sourceProcFlags': row[34],
                                 'meleeOrSpelllessChance': 100,
                                 'otherEventChanceByMoltenShieldsRank': [0,50,100]}
                                 if name == 'Molten Armor' else None,
                             'trainerLevel': row[39] if name in ('Molten Armor', 'Arcane Blast', 'Ghost Wolf') else None,
                             'internalFlurryAura': definitions.get(row[116]) if name.endswith(' Flurry') else None,
                             'referenceChildProcRow': sql_rows[36032] if name == 'Arcane Blast' else sql_rows.get(-12966 if name == 'Warrior Flurry' else -16257) if name.endswith(' Flurry') else None,
                             'triggeredAuraSpellId': d.get('triggeredAuraSpellId', 0),
                             'internalAura': definitions.get(d.get('triggeredAuraSpellId')) if name == 'Bloodthirst' else None,
                             'passiveMeleeCritPct': d['passiveMeleeCritPct'],
                             'passiveSpellCritPct': d.get('passiveSpellCritPct', 0),
                             'passiveTotalStatPct': d.get('passiveTotalStatPct', [0, 0, 0, 0, 0]),
                             'passiveArmorAttackPowerDivisor': d.get('passiveArmorAttackPowerDivisor', 0),
                             'passiveOffhandDamagePct': d.get('passiveOffhandDamagePct', 0),
                             'passiveWeaponHitPct': d.get('passiveWeaponHitPct', 0),
                             'passivePhysicalDamagePct': d.get('passivePhysicalDamagePct', 0),
                             'passiveEquipmentArmorPct': d.get('passiveEquipmentArmorPct', 0),
                             'passiveFeralCritPct': d.get('passiveFeralCritPct', 0),
                             'passiveFeralDodgePct': d.get('passiveFeralDodgePct', 0),
                             'passiveCatRunPct': d.get('passiveCatRunPct', 0),
                             'stormstrikeProfile': d.get('stormstrikeProfile', 0),
                             'stormstrikeManaChancePct': d.get('stormstrikeManaChancePct', 0),
                             'passiveIntellectAttackPowerPct': d.get('passiveIntellectAttackPowerPct', 0),
                             'passiveDualWieldHitPct': d.get('passiveDualWieldHitPct', 0),
                             'passiveCanParry': d.get('passiveCanParry', False),
                             'passiveCanDualWield': d.get('passiveCanDualWield', False),
                             'physicalDamageDonePct': d.get('physicalDamageDonePct', 0),
                             'damageTakenPct': d.get('damageTakenPct', 0),
                             'periodicHealMaxHealthPct': d.get('periodicHealMaxHealthPct', 0),
                             'arcaneBlastProfile': d.get('arcaneBlastProfile', 0),
                             'passivePushbackPct': d.get('passivePushbackPct', 0),
                             'pushbackSpellMask': d.get('pushbackSpellMask', [0, 0, 0]),
                             'castModifiers': d.get('castModifiers', []),
                             'clearcastingProfile': d.get('clearcastingProfile', 0),
                             'chargedCostPct': d.get('chargedCostPct', 0),
                             'chargedCostMask': d.get('chargedCostMask', [0, 0, 0]),
                             'requiredItemClass': d['requiredItemClass'], 'requiredItemSubclasses': d['requiredItemSubclasses'],
                             'proc': {key: value for key, value in d.items() if key.startswith('proc')},
                             'gameplayVerified': False})
        families.append({'family': name, 'decodedRanks': sum(d['decoderAccepted'] for d in profiles),
                         'reachableRanks': sum(d['decoderAccepted'] and d['reachableWithCurrentDecoder'] for d in profiles),
                         'profiles': profiles})
    result = {'schemaVersion': 1, 'scope': 'P03 source, decoder and progression evidence; P35 pending',
              'dbcCommit': DBC_REV, 'referenceCommit': REFERENCE_REV,
              'referenceRepository': 'https://github.com/azerothcore/azerothcore-wotlk',
              'sourceHashes': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(source.iterdir()) if p.is_file()},
              'dbcHashes': {name: hashlib.sha256((dbc / name).read_bytes()).hexdigest() for name in ['Spell.dbc', 'Talent.dbc', 'TalentTab.dbc']},
              'acceptedAuditedSpellIds': len({d['spellId'] for d in audit['importAudit'] if d['firstResult'] == 'Supported decoder; imported'}),
              'acceptedTalentRanks': sum(bool(d['talentId']) and d['decoderAccepted'] for d in definitions.values()),
              'npcOnlySpellIds': sorted(d['spellId'] for d in definitions.values() if d.get('npcOnly', False) and d['decoderAccepted']),
              'families': families,
              'remainingProfiles': [
                  {'family': 'Retribution Aura', 'blocker': 'Party area aura effects65 plus secondary auras79/193; cannot strip to damage shield'},
                  {'family': 'Shadowguard', 'blocker': 'Priest racial rank chain absent supplied DBC; only NPC32861/38379 family0 aura43 found'},
                  # Go for the Throat left this list in the implementation: talent1818 now decodes as a
                  # RestorePetPower profile and is reported under families above.
              ],
              'limitations': ['Reachability uses the existing monotone tier/rank/prerequisite rules at level80 with71 talent points.',
                              'Earth Shield trainer ranks require root talent; decoder acceptance cannot bypass that gate.',
                              'Internal proc children are excluded from class/talent admission counts and cannot be learned or cast independently; exact retained helper counts are reported with the release.',
                              'Feral Swiftness, Spirit Weapons and Dual Wield source children may be folded into reviewed parent capabilities rather than retained as independently castable spells.',
                              'Savage Fury effect-three support does not admit missing Mangle, Maul or Swipe abilities.',
                              'Pet-power proc recipients need an owned pet at runtime; decoder admission is not pet combat behaviour.',
                              'No runtime timing, balance, LAN, save, controller, graphics or PS4 acceptance is established.',
                              'Critical/family/PPM infrastructure does not implement all source proc families or custom scripts.']}
    storm = next(f for f in families if f['family'] == 'Improved Stormstrike')
    if storm['decodedRanks'] != 2 or storm['reachableRanks'] != 2:
        result['remainingProfiles'].append({'family': 'Improved Stormstrike',
            'blocker': 'Complete current source admission and reachable parent route required'})
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'acceptedAuditedSpellIds': result['acceptedAuditedSpellIds'],
                      'acceptedTalentRanks': result['acceptedTalentRanks'],
                      'families': [{k: r[k] for k in ['family', 'decodedRanks', 'reachableRanks']} for r in families]}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['audit', 'dbc', 'source', 'output']:
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    run(args.audit, args.dbc, args.source, args.output)
