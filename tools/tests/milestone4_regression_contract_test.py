#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
realm = (ROOT / 'src/game/local_realm.cpp').read_text()
game = (ROOT / 'include/game/local_gameplay.hpp').read_text()
lan = (ROOT / 'include/game/lan_discovery.hpp').read_text()
gameplay = (ROOT / 'src/game/local_gameplay.cpp').read_text()
quest_eligibility = (ROOT / 'include/game/local_quest_eligibility.hpp').read_text()
mail = (ROOT / 'src/game/local_mail_authority.inc').read_text()

save = int(re.search(r'constexpr uint8_t SaveVersion = (\d+)\b', realm).group(1))
protocol = int(re.search(r'GameplayVersion = (\d+)\b', lan).group(1))
assert save >= 35, save
assert protocol >= 90, protocol

# 4.1/4.2 item-instance floor: the full snapshot must remain on save/LAN moves.
assert re.search(r'constexpr size_t ItemInstanceWireBytes = 41\b', realm)
for field in ('instanceFlags', 'permanentEnchantId', 'temporaryEnchantId',
              'socketEnchantIds', 'curDurability', 'maxDurability',
              'randomPropertyId', 'suffixFactor', 'soulbound'):
    assert field in realm or field in game, field
assert realm.count('if(version>=33)') >= 4
assert 'readAuction(r, world, worldReferencesAvailable, saveVersion>=33)' in realm
assert 'readMail(r,saveVersion>=33)' in realm
assert 'writeItemInstance(w,a.instance)' in realm
assert 'writeItemInstance(w,item.instance)' in realm
assert 'const auto begin=part,end=std::min(begin+1,rows.size())' in mail
assert 'constexpr size_t AuctionsPerPage = 12;' in realm

# 4.3 reputation floor: Save34 table and authority gates must survive later saves.
assert re.search(r'kLocalMaxReputations\s*=\s*128\b', game)
assert 'if(version>=34)' in realm
assert 'p.migrateLegacyReputation = version < 34' in realm
assert 'localVendorDiscountedBuyTotal' in gameplay
assert 'Your reputation is too low for this quest' in quest_eligibility
assert '1 + kLocalMaxReputations * 8' in realm

# 4.4 profession floor: widened recipe book remains u16 from Save35 onward.
assert re.search(r'MaxRecipes\s*=\s*1024\b', game)
assert 'if(version>=35) w.u16(uint16_t(p.knownRecipes.size()))' in realm
assert 'const uint16_t recipeCount = version>=35 ? r.u16() : r.u8();' in realm
assert '2 + LocalGameplay::MaxRecipes * 4' in realm
assert 'LocalGameplay::MaxInventory * (7 + ItemInstanceWireBytes)' in realm
assert 'kLocalBankSlots * (6 + ItemInstanceWireBytes)' in realm

# Combined owner state must still be bounded and chunked below the LAN MTU.
assert 'MaxOwnerProgressBytes <= 16384' in realm
assert 'constexpr size_t ProgressChunkBytes = 1200;' in realm
assert 'HeaderSize + 10 + ProgressChunkBytes <= MaxPacket' in realm

print(f'PASS milestone-4 integration contract: Save{save}/LAN{protocol}, item instances + transfers + reputation + professions remain migration-compatible')
