from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
realm = (root / "src/game/local_realm.cpp").read_text()
game = (root / "src/game/local_gameplay.cpp").read_text()
header = (root / "include/game/local_gameplay.hpp").read_text()
lan = (root / "include/game/lan_discovery.hpp").read_text()

save = int(re.search(r"SaveVersion\s*=\s*(\d+)", realm).group(1))
proto = int(re.search(r"GameplayVersion\s*=\s*(\d+)", lan).group(1))
assert save >= 36, save
assert proto >= 91, proto

required = [
    "uint32_t phaseMask = 1;",
    "std::vector<LocalScriptState> scriptStates;",
    "kLocalMaxScriptStates = 64",
    "requiredPhaseMask = 0, excludedPhaseMask = 0",
    "localPhaseVisible",
]
for token in required:
    assert token in header, token

for token in [
    "if(version>=36)",
    "w.u32(p.phaseMask)",
    "p.phaseMask=1;p.scriptStates.clear()",
    "validLocalScriptStates(p.scriptStates)",
]:
    assert token in realm, token

for token in [
    'number(v,"requiredPhaseMask",0,UINT32_MAX)',
    'number(v,"excludedPhaseMask",0,UINT32_MAX)',
    "gameplay.npcVisibleTo",
    "npcVisibleTo(p, npc)",
]:
    assert token in game or token in realm, token

# Old Save35 players must deterministically enter the normal world with no
# script variables rather than inheriting uninitialised state.
assert "p.phaseMask=1;p.scriptStates.clear();" in realm
# Owner progress budget must include every possible script-state row.
assert "kLocalMaxScriptStates * 8" in realm

print(f"PASS 5.1 phase/script contract: Save{save}/LAN{proto}, 64 script vars, per-player NPC deck filtering, Save1-35 migration")
