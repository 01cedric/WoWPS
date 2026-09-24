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
assert save >= 37, save
assert proto >= 92, proto
for token in [
    'struct LocalScriptTimer {', 'kLocalMaxScriptTimers = 16',
    'std::vector<LocalScriptTimer> scriptTimers;',
    'uint32_t scheduleTimerId = 0, scheduleDelayMs = 0, cancelTimerId = 0;',
    'struct LocalScriptTimerAction {', 'localSetScriptTimer', 'localCancelScriptTimer',
    'localApplyScriptTimerAction', 'std::vector<LocalScriptTimerAction> scriptTimerActions;',
]: assert token in header, token
for token in [
    'array(j,"scriptTimers",1024)', 'scheduleTimerId', 'scheduleDelayMs', 'cancelTimerId',
    'Script trigger schedules missing timer', 'Script trigger cancels missing timer',
    'for(auto* p:players)if(p&&elapsedMs&&!p->scriptTimers.empty())',
    'candidate.scriptTimers.erase(pending)', 'localApplyScriptTimerAction(candidate,*it)',
]: assert token in game, token
for token in [
    'if(version>=37){', 'w.u8(uint8_t(p.scriptTimers.size()))',
    'timer.timerId=r.u32();timer.remainingMs=r.u32()', 'validLocalScriptTimers(p.scriptTimers)',
    'kLocalMaxScriptTimers * 8',
]: assert token in realm, token
print(f'PASS 5.3 script-timer contract: Save{save}/LAN{proto}, bounded persisted timers + content-defined expiry actions')
