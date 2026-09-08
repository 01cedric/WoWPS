#!/usr/bin/env python3
"""Generate a deterministic creature-entry/display index from the pinned AC table.

Spell.dbc mounted aura 78 stores a creature entry, not a CreatureDisplayInfo ID.
The full compact model index avoids guessing based on localized creature names.
"""
from pathlib import Path
import argparse
from import_azerothcore import PINNED_COMMIT, sql_rows

def main():
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    choices={}
    for r in sql_rows(a.source,'creature_template_model'):
        entry,idx,display=int(r['creatureid']),int(r['idx']),int(r['creaturedisplayid'])
        if not entry or not display:continue
        if entry not in choices or idx<choices[entry][0]:choices[entry]=(idx,display)
    a.output.write_text('// Generated from AzerothCore '+PINNED_COMMIT+' creature_template_model. GPL-2.0.\n'+''.join('{'+str(k)+','+str(v[1])+'},\n' for k,v in sorted(choices.items())))
    print('Indexed',len(choices),'creature display choices')
if __name__=='__main__':main()
