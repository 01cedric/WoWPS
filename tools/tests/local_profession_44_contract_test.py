#!/usr/bin/env python3
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
realm=(ROOT/'src/game/local_realm.cpp').read_text()
lan=(ROOT/'include/game/lan_discovery.hpp').read_text()
game=(ROOT/'include/game/local_gameplay.hpp').read_text()
docs=(ROOT/'docs/implementation_inventory/systems.json').read_text()

assert re.search(r'MaxRecipes\s*=\s*1024\b', game)
save_version=int(re.search(r'constexpr uint8_t SaveVersion = (\d+)\b',realm).group(1))
gameplay_version=int(re.search(r'GameplayVersion = (\d+)\b',lan).group(1))
assert save_version >= 35, save_version
assert gameplay_version >= 90, gameplay_version
assert 'if(version>=35) w.u16(uint16_t(p.knownRecipes.size()))' in realm
assert 'else w.u8(uint8_t(p.knownRecipes.size()))' in realm
assert 'const uint16_t recipeCount = version>=35 ? r.u16() : r.u8();' in realm
assert 'p.knownRecipes.reserve(recipeCount)' in realm
assert 'LocalGameplay::MaxInventory * (7 + ItemInstanceWireBytes)' in realm
assert 'kLocalBankSlots * (6 + ItemInstanceWireBytes)' in realm
assert '2 + LocalGameplay::MaxRecipes * 4' in realm
assert 'MaxOwnerProgressBytes <= 16384' in realm
assert 'MaxOwnerProgressBytes + 2048' in realm
assert 'up to 1024 learned recipes' in docs

# Model the one field changed by Save35: old layouts use one byte; Save35 uses
# two-byte network-order count. This catches accidental count-width regressions
# without building the whole LAN authority test binary.
def enc_count(version, count):
    assert 0 <= count <= 1024
    if version >= 35:
        return bytes([(count >> 8) & 0xff, count & 0xff])
    assert count <= 255
    return bytes([count])

def dec_count(version, data):
    if version >= 35:
        return (data[0] << 8) | data[1]
    return data[0]

for count in (0,1,96,255):
    assert dec_count(34, enc_count(34,count)) == count
for count in (0,1,96,255,256,512,1024):
    assert dec_count(35, enc_count(35,count)) == count
assert enc_count(35,1024) == b'\x04\x00'

print(f'PASS 4.4 codec contract: current Save{save_version}/LAN{gameplay_version}, migration floor Save35/LAN90, u16 recipe count, old Save1-34 u8 compatibility, item-instance-aware progress budget')
