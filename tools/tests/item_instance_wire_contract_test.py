from pathlib import Path
import re
root=Path(__file__).resolve().parents[2]
realm=(root/'src/game/local_realm.cpp').read_text()
lan=(root/'include/game/lan_discovery.hpp').read_text()
mail=(root/'src/game/local_mail_authority.inc').read_text()

save_version=int(re.search(r'constexpr uint8_t SaveVersion = (\d+)\b',realm).group(1))
gameplay_version=int(re.search(r'GameplayVersion = (\d+)\b',lan).group(1))
assert save_version >= 33, save_version
assert gameplay_version >= 88, gameplay_version
assert re.search(r'constexpr size_t ItemInstanceWireBytes = 41\b', realm)

write=re.search(r'void writeItemInstance\([^}]+\}',realm,re.S).group(0)
read=re.search(r'LocalItemInstanceState readItemInstance\([^}]+\}',realm,re.S).group(0)
for field in ['instanceFlags','permanentEnchantId','temporaryEnchantId','socketEnchantIds','curDurability','maxDurability','randomPropertyId','suffixFactor','soulbound']:
    assert field in write, f'missing write field {field}'
    assert field in read, f'missing read field {field}'

# Save migration must conditionally consume the new bytes, while new writes emit them.
assert realm.count('if(version>=33)writeItemInstance') >= 2
assert realm.count('if(version>=33)') >= 4
assert 'readAuction(r, world, worldReferencesAvailable, saveVersion>=33)' in realm
assert 'readMail(r,saveVersion>=33)' in realm
assert 'if(saveVersion>=33)entry.instance=readItemInstance(r)' in realm

# Datagram budget: one maximum mail and 12 auction rows remain under the local MTU.
max_packet=int(re.search(r'MaxPacket = (\d+)',realm).group(1))
header=int(re.search(r'HeaderSize = (\d+)',realm).group(1))
item_instance=41
max_mail=4+8+8+17+(1+64)+(1+160)+4+4+1+12*(4+2+item_instance)
auction=4+4+2+item_instance+4+4+8+17+4+4+8
assert header+15+max_mail <= max_packet
assert header+8+12*auction <= max_packet
assert 'constexpr size_t AuctionsPerPage = 12;' in realm
assert 'std::array<std::vector<LocalMail>,LocalMailbox::MaxInbox>' in realm
assert 'const auto begin=part,end=std::min(begin+1,rows.size())' in mail
assert 'count>1' in mail and 'parts>LocalMailbox::MaxInbox' in mail

# The public item-bearing codecs all carry the snapshot.
for marker in [
    'writeItemInstance(w,a.instance)',
    'writeItemInstance(w,item.instance)',
    'entry.instance=readItemInstance(r)',
]:
    assert marker in realm, marker
print(f'PASS 4.2 wire/save contract: current Save{save_version}/LAN{gameplay_version}, migration floor Save33/LAN88, full 41-byte instance codec, mail={header+15+max_mail}B, auction-page={header+8+12*auction}B <= {max_packet}B')
