#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
header = (root / 'include/game/local_gameplay.hpp').read_text()
game = (root / 'src/game/local_gameplay.cpp').read_text()
realm = (root / 'src/game/local_realm.cpp').read_text()
lan = (root / 'include/game/lan_discovery.hpp').read_text()

save = int(re.search(r'SaveVersion\s*=\s*(\d+)', realm).group(1))
proto = int(re.search(r'GameplayVersion\s*=\s*(\d+)', lan).group(1))
assert save >= 36, save
assert proto >= 91, proto

for token in [
    'enum class LocalScriptTriggerKind',
    'QuestAccept = 0, QuestComplete = 1, QuestReward = 2, NpcTalk = 3, NpcKill = 4',
    'enum class LocalScriptValueOp',
    'std::vector<LocalScriptTrigger> scriptTriggers;',
    'localScriptTriggerMatches',
    'localApplyScriptTrigger',
    'const uint32_t nextPhaseMask = (player.phaseMask | addPhaseMask) & ~removePhaseMask;',
    'localApplyScriptMutation',
]:
    assert token in header, token

for token in [
    'array(j,"scriptTriggers",4096)',
    'kind=="questAccept"',
    'kind=="questComplete"',
    'kind=="questReward"',
    'kind=="npcTalk"',
    'kind=="npcKill"',
    'authoredScriptIds.size()>kLocalMaxScriptStates',
    'LocalScriptTriggerKind::QuestAccept,cmd.id',
    'LocalScriptTriggerKind::QuestComplete,progress.id',
    'LocalScriptTriggerKind::QuestReward,cmd.id',
    'LocalScriptTriggerKind::NpcTalk,n->entry',
    'LocalScriptTriggerKind::NpcKill,it->npcEntry',
]:
    assert token in game, token

# Completion must be edge-triggered; repeated inventory/objective refreshes may
# not replay additive script rows.
assert 'previous!=LocalQuestStatus::Complete && progress.status==LocalQuestStatus::Complete' in game
# Multi-row event transitions are staged and committed only after every row succeeds.
assert 'LocalRealmPlayer staged;staged.phaseMask=p.phaseMask;staged.scriptStates=p.scriptStates;' in game
assert 'p.phaseMask=staged.phaseMask;p.scriptStates=std::move(staged.scriptStates);' in game

print(f'PASS 5.2 script-trigger contract: Save{save}/LAN{proto}, quest accept/complete/reward + NPC talk/kill, atomic rows + edge-triggered completion')
