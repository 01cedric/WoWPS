#include "game/local_realm.hpp"
#include "game/local_auction_catalog.hpp"
#include "game/lan_discovery.hpp"
#include "game/local_day_clock.hpp"
#include "core/local_time.hpp"
#include "core/snapshot_writer.hpp"
#include <ctime>
#ifdef WOWEE_PS4
#include <orbis/Rtc.h>
#endif
#include "game/local_services.hpp"
#include "game/local_talents.hpp"
#include "game/local_stat_auras.hpp"
#include "game/local_aura_presentation.hpp"
#include "game/local_quest_dialogue.hpp"
#include "game/local_scripted_portals.hpp"
#include "game/local_world_catalog.hpp"
#include "network/net_platform.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>
#include <deque>
#include <optional>

namespace wowee::game {
namespace {
constexpr uint32_t WireMagic = 0x57504c52; // WPLR
constexpr uint32_t SaveMagic = 0x57505253; // WPRS
constexpr uint32_t IdentityMagic = 0x57504944; // WPID
constexpr uint8_t Version = lan::GameplayVersion; // Selected merchant stock and owner buyback.
constexpr uint8_t SaveVersion = 19;  // Buff casters and shield capacity; reads save versions 1-18.
constexpr size_t HeaderSize = 20, MaxPacket = 1400, MaxSavedPlayers = 128;
constexpr size_t PublicPlayerBytes = 49 + 34 + 4 * kLocalEquipmentSlotCount + 6 + 4;
constexpr size_t PlayersPerPage = 7;
constexpr size_t NpcWireBytes = 91, NpcsPerPage = 14;
constexpr size_t MaxNpcPages = (LocalGameplay::MaxNpcs + NpcsPerPage - 1) / NpcsPerPage;
static_assert(HeaderSize + 19 + NpcsPerPage * NpcWireBytes <= MaxPacket,
              "NPC deck offsets must not cause IP fragmentation");
constexpr size_t MaxPlayerPages = (LocalRealm::MaxPlayers + PlayersPerPage - 1) / PlayersPerPage;
static_assert(HeaderSize + 9 + PlayersPerPage * PublicPlayerBytes <= MaxPacket,
              "Player pages must fit one datagram without IP fragmentation");
constexpr size_t CastWireBytes = 45;
constexpr size_t WelcomeWireBytes = HeaderSize + 26 + PublicPlayerBytes + CastWireBytes + 1;
static_assert(WelcomeWireBytes <= MaxPacket, "Welcome carries only the owner");
// Cooldowns are counted at their own bound rather than the spellbook's. They
// used to be budgeted at MaxSpells, which was harmless while a character could
// know sixteen abilities and wrong the moment class trainers raised that to
// forty-eight: the sum reached 1493 bytes against a 1400-byte promise, and the
// assert below is what said so. A character may have many more abilities than
// it can have on cooldown at once, and MaxCooldowns is the number the reader
// already rejects a packet for exceeding.
constexpr size_t MaxOwnerProgressBytes = HeaderSize + 16 + 20 + 34 + 4 * kLocalEquipmentSlotCount + 14 +
    1 + LocalGameplay::MaxInventory * 7 + 1 + LocalGameplay::MaxQuests * 14 +
    1 + LocalGameplay::MaxSpells * 4 + 1 + LocalGameplay::MaxCooldowns * 8 + 25 +
    // Professions (count plus six bytes each), the recipes a profession taught
    // (count plus four bytes each) and the inn binding.
    1 + LocalGameplay::MaxProfessions * 6 + 1 + LocalGameplay::MaxRecipes * 4 +
    1 + 4 + 16 + 4 + 25 + 17 + 20 + 12 + 4 + kLocalBankSlots * 6 + LocalGameplay::MaxProfessions * 2 + 2 + 2 + 512 * 4 + 45 + 1 + 71 * 5 + 1 + kLocalMaxStatAuras * 28 + 1 + kLocalMaxHealingAuraViews * 20;
constexpr size_t ProgressChunkBytes = 1200;
constexpr size_t MaxProgressChunks = (MaxOwnerProgressBytes + ProgressChunkBytes - 1) / ProgressChunkBytes;
static_assert(MaxOwnerProgressBytes <= 8192 && MaxProgressChunks <= 8);
static_assert(HeaderSize + 10 + ProgressChunkBytes <= MaxPacket);
constexpr size_t HistoryPageEntries = 256;
constexpr size_t MaxHistoryPages = (LocalGameplay::MaxCompletedQuests + HistoryPageEntries - 1) / HistoryPageEntries;
// Every character may reach the documented history cap; never write a save
// larger than the parser can subsequently read.
constexpr size_t MaxSaveSize = MaxSavedPlayers * (LocalGameplay::MaxCompletedQuests * 4 + 8192) + 65536 +
    2 + LocalVendorInventory::MaxDepletedOffers * 28 + LocalMailbox::MaxMessages * 512;
constexpr double SendInterval = 0.1, HelloInterval = 0.5, JoinTimeout = 10.0;
constexpr double PeerTimeout = 8.0, LoadingTimeout = 180.0, SaveInterval = 5.0;
// A returning owner can replace its own silent session without waiting out a
// long world-load lease. Active clients continue to refresh at 10 Hz.
constexpr double ReconnectSilence = 2.0;
enum class Message : uint8_t { Hello = 1, Welcome, Position, Snapshot, Leave, Reject, Command, ActionResult, Progress, Npcs, History, HistoryAck, Clock, CharacterRequest, CharacterReply, AbortJoin, Auctions, MerchantQuery, MerchantState, PartyState, ChatRequest, ChatResult, ChatDelivery, ChatAck, SocialState, MailQuery, MailState };
static_assert(HeaderSize + 38 + LocalPartyDirector::MaxMembers * 66 <= MaxPacket,
              "A complete owner party snapshot must fit one datagram");
constexpr size_t MerchantOffersPerPage = 128, MaxMerchantPages = 2;
static_assert(HeaderSize + 20 + 5 + kLocalMaxBuyback * 14 + MerchantOffersPerPage * 8 <= MaxPacket,
              "Selected merchant stock and owner buyback must fit one datagram");
// How many listings fit one datagram. Each is 63 bytes on the wire, and the
// board is paged the same way the NPC list is, so a full board never depends
// on IP fragmentation to arrive.
constexpr size_t AuctionBytes = 4 + 4 + 2 + 4 + 4 + 8 + 17 + 4 + 4 + 8;
constexpr size_t AuctionsPerPage = 16;
static_assert(HeaderSize + 8 + AuctionsPerPage * AuctionBytes <= MaxPacket,
              "Auction pages must fit one datagram without IP fragmentation");
constexpr size_t MaxAuctionPages = (LocalBotDirector::MaxAuctions + AuctionsPerPage - 1) / AuctionsPerPage;

struct Writer {
    std::vector<uint8_t> bytes;
    void u8(uint8_t n) { bytes.push_back(n); }
    void u16(uint16_t n) { u8(n >> 8); u8(n); }
    void u32(uint32_t n) { u16(n >> 16); u16(n); }
    void u64(uint64_t n) { u32(n >> 32); u32(n); }
    void f32(float f) { uint32_t n; std::memcpy(&n, &f, sizeof(n)); u32(n); }
    void text(const std::string& s) { const auto n = std::min(size_t(160), s.size()); u8(uint8_t(n)); for (size_t i = 0; i < n; ++i) u8(uint8_t(s[i])); }
    void name(const std::string& s) {
        for (size_t i = 0; i < 17; ++i) u8(i < s.size() ? uint8_t(s[i]) : 0);
    }
};

struct Reader {
    const uint8_t* data;
    size_t size, offset = 0;
    bool valid = true;
    Reader(const uint8_t* p, size_t n) : data(p), size(n) {}
    uint8_t u8() { if (offset >= size) { valid = false; return 0; } return data[offset++]; }
    uint16_t u16() { uint16_t hi = u8(); return uint16_t((hi << 8) | u8()); }
    uint32_t u32() { uint32_t hi = u16(); return (hi << 16) | u16(); }
    uint64_t u64() { uint64_t hi = u32(); return (hi << 32) | u32(); }
    float f32() { uint32_t n = u32(); float f; std::memcpy(&f, &n, sizeof(f)); return f; }
    std::string text() { const auto n = u8(); if (n > 160) valid = false; std::string s; for (unsigned i = 0; i < n; ++i) s.push_back(char(u8())); return s; }
    std::string name() {
        std::string result;
        bool ended = false;
        for (size_t i = 0; i < 17; ++i) {
            uint8_t c = u8();
            if (!c) ended = true;
            else if (ended || i == 16) valid = false;
            else result.push_back(char(c));
        }
        return result;
    }
    bool done() const { return valid && offset == size; }
};

bool validBuyback(uint32_t serial, const std::vector<LocalMerchantBuyback>& rows) {
    if (rows.size() > kLocalMaxBuyback) return false;
    uint64_t previous = uint64_t(serial) + 1;
    for (const auto& row : rows) {
        if (!row.id || row.id >= previous || !row.itemId || !row.count || row.price > 1000000000) return false;
        previous = row.id;
    }
    return true;
}
void writeBuyback(Writer& w, uint32_t serial, const std::vector<LocalMerchantBuyback>& rows) {
    w.u32(serial); w.u8(uint8_t(rows.size()));
    for (const auto& row : rows) { w.u32(row.id); w.u32(row.itemId); w.u32(row.price); w.u16(row.count); }
}
bool readBuyback(Reader& r, uint32_t& serial, std::vector<LocalMerchantBuyback>& rows) {
    serial = r.u32(); const auto count = r.u8(); if (count > kLocalMaxBuyback) return false;
    std::vector<LocalMerchantBuyback> incoming;
    for (unsigned i = 0; i < count; ++i) incoming.push_back({r.u32(), r.u32(), r.u32(), r.u16()});
    if (!r.valid || !validBuyback(serial, incoming)) return false;
    rows = std::move(incoming); return true;
}
struct MerchantOfferState {
    uint32_t itemId = 0; int32_t remaining = 0;
    bool operator==(const MerchantOfferState&) const = default;
};

bool validName(const std::string& s) {
    if (s.empty() || s.size() > 16) return false;
    for (unsigned char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
bool validPosition(uint32_t map, float x, float y, float z, float o) {
    return map <= 10000 && std::isfinite(x) && std::isfinite(y) &&
           std::isfinite(z) && std::isfinite(o) && std::abs(x) <= 100000 &&
           std::abs(y) <= 100000 && std::abs(z) <= 20000 && std::abs(o) <= 100000;
}
bool validPlayer(const LocalRealmPlayer& p) {
    return p.guid && p.guid <= 0x0000ffffffffffffULL && validName(p.name) &&
           validPosition(p.mapId, p.x, p.y, p.z, p.orientation) &&
           LocalGameplay::validCharacterOptions(p.race, p.classId, p.gender) && p.level >= 1 && p.level <= 80;
}
void writePosition(Writer& w, const LocalRealmPlayer& p) {
    w.u32(p.mapId); w.f32(p.x); w.f32(p.y); w.f32(p.z); w.f32(p.orientation);
}
void readPosition(Reader& r, LocalRealmPlayer& p) {
    p.mapId = r.u32(); p.x = r.f32(); p.y = r.f32(); p.z = r.f32(); p.orientation = r.f32();
}
void writePlayer(Writer& w, const LocalRealmPlayer& p) {
    w.u64(p.guid); w.name(p.name); writePosition(w, p);
    w.u8(p.race); w.u8(p.classId); w.u8(p.gender); w.u8(p.level);
}
LocalRealmPlayer readPlayer(Reader& r) {
    LocalRealmPlayer p;
    p.guid = r.u64(); p.name = r.name(); readPosition(r, p);
    p.race = r.u8(); p.classId = r.u8(); p.gender = r.u8(); p.level = r.u8();
    if (!validPlayer(p)) r.valid = false;
    return p;
}
void writeVitals(Writer& w, const LocalRealmPlayer& p) {
    w.u32(p.health); w.u32(p.maxHealth); w.u32(p.mana); w.u32(p.maxMana);
    w.u8(p.dead ? 1 : 0); w.u32(p.positionRevision);
    for (auto item : p.equipment) w.u32(item);
    w.u64(p.attackTarget); w.u8(uint8_t(p.resourceType)); w.u32(p.instanceId);
}
void readVitals(Reader& r, LocalRealmPlayer& p, uint8_t version = SaveVersion) {
    p.health = r.u32(); p.maxHealth = r.u32(); p.mana = r.u32(); p.maxMana = r.u32();
    const auto dead = r.u8(); p.dead = dead != 0; p.positionRevision = r.u32();
    p.equipment.fill(0);
    if (version < 5) {
        for (auto slot : kLegacyLocalEquipmentSlots) p.equipment[slot] = r.u32();
    } else for (auto& item : p.equipment) item = r.u32();
    p.attackTarget = r.u64();
    if (version >= 3) {
        const auto type = r.u8(); p.resourceType = LocalResourceType(type); p.instanceId = r.u32();
        if ((type != 0 && type != 1 && type != 3 && type != 6) || p.instanceId > 65535) r.valid = false;
    }
    if (dead > 1 || !p.maxHealth || p.maxHealth > 1000000 || p.maxMana > 1000000 ||
        p.health > p.maxHealth || p.mana > p.maxMana || p.dead != (p.health == 0)) r.valid = false;
}
// Active riding is session state, sent with wire 14 but never inserted into
// the save payload. The learned spell already uses the durable knownSpells list.
void writeNetworkVitals(Writer& w,const LocalRealmPlayer& p) {writeVitals(w,p);w.u32(p.mountSpellId);}
void readNetworkVitals(Reader& r,LocalRealmPlayer& p) {readVitals(r,p);p.mountSpellId=r.u32();}
void writeProgress(Writer& w, const LocalRealmPlayer& p, uint8_t version = SaveVersion) {
    writeVitals(w, p); w.u32(p.xp); w.u32(p.xpToLevel); w.u32(p.money); w.u8(p.level);
    w.u8(p.gameplayInitialized ? 1 : 0);
    w.u8(uint8_t(p.inventory.size()));
    for (const auto& item : p.inventory) { w.u32(item.itemId); w.u16(item.count); }
    w.u8(uint8_t(p.quests.size()));
    for (const auto& q : p.quests) {
        w.u32(q.id); w.u8(uint8_t(q.status)); w.u8(uint8_t(q.progress.size()));
        for (auto count : q.progress) w.u16(count);
    }
    w.u8(uint8_t(p.knownSpells.size())); for (auto id : p.knownSpells) w.u32(id);
    w.u8(uint8_t(p.cooldowns.size())); for (const auto& cd : p.cooldowns) { w.u32(cd.spellId); w.u32(cd.remainingMs); }
    w.u8(p.hasInstanceReturn ? 1 : 0); w.u32(p.returnMapId); w.u32(p.returnInstanceId);
    w.f32(p.returnX); w.f32(p.returnY); w.f32(p.returnZ); w.f32(p.returnOrientation);
    // Save version 7 / wire version 10.
    w.u8(uint8_t(p.professions.size()));
    for (const auto& skill : p.professions) { w.u16(skill.skillId); w.u16(skill.current); w.u16(skill.max); }
    // Recipes travel with the professions that taught them. They were checked
    // against MaxRecipes on load and never written by anything, so a learned
    // recipe survived exactly as long as the session it was learned in.
    w.u8(uint8_t(p.knownRecipes.size()));
    for (auto spellId : p.knownRecipes) w.u32(spellId);
    w.u8(p.hasHome ? 1 : 0); w.u32(p.homeMapId);
    w.f32(p.homeX); w.f32(p.homeY); w.f32(p.homeZ); w.f32(p.homeOrientation);
    w.f32(p.hearthCooldown);
    w.u32(p.transportEntry); w.f32(p.transportOffsetX); w.f32(p.transportOffsetY);
    w.f32(p.transportOffsetZ); w.f32(p.transportLastYaw);
    for(const auto remaining:p.runeCooldownMs) w.u16(remaining);
    if (version >= 12) {
        for (const auto& slot : p.bank) { w.u32(slot.itemId); w.u16(slot.count); }
        for (const auto& skill : p.professions) w.u16(skill.progress);
    }
    if(version>=15){const auto slots=localInventoryLayout(p);for(size_t i=0;i<p.inventory.size();++i)w.u8(slots[i]);}
    if(version>=16){
        w.u16(p.migrateLegacyRiding?65535:p.ridingSkill);w.u16(uint16_t(p.knownTaxiNodes.size()));for(auto id:p.knownTaxiNodes)w.u32(id);
        w.u8(p.flight.active?1:0);
        if(p.flight.active){const auto& f=p.flight;w.u32(f.pathId);w.u32(f.destinationNode);w.f32(f.travelled);w.f32(f.totalLength);w.f32(f.speed);
            w.u32(f.originMap);w.f32(f.originX);w.f32(f.originY);w.f32(f.originZ);w.f32(f.originOrientation);}
    }
    if(version>=18){w.u8(uint8_t(p.statAuras.size()));for(const auto& a:p.statAuras){w.u32(a.spellId);w.u32(a.remainingMs);w.u32(a.mapId);w.u32(a.instanceId);if(version>=19){w.u64(a.casterGuid);w.u32(a.absorbRemaining);}}}
    if(version>=17){w.u8(uint8_t(p.talents.size()));for(auto [id,rank]:p.talents){w.u32(id);w.u8(rank);}}
}
// CharSections indices retain their full uint8 domain; only the model selector
// is a boolean. Asset-specific option ranges are resolved by the character UI.
void writeAppearance(Writer& w, const LocalRealmPlayer& p) {
    w.u8(p.skin); w.u8(p.face); w.u8(p.hairStyle); w.u8(p.hairColor); w.u8(p.facialHair);
    w.u8(p.useFemaleModel ? 1 : 0);
}
bool readAppearance(Reader& r, LocalRealmPlayer& p) {
    p.skin = r.u8(); p.face = r.u8(); p.hairStyle = r.u8(); p.hairColor = r.u8(); p.facialHair = r.u8();
    const auto female = r.u8(); if (female > 1) return false;
    p.useFemaleModel = female != 0;
    return r.valid;
}
bool readProgress(Reader& r, LocalRealmPlayer& p, uint8_t version = SaveVersion) {
    readVitals(r, p, version); p.xp = r.u32(); p.xpToLevel = r.u32(); p.money = r.u32(); p.level = r.u8();
    const auto initialized = r.u8(); p.gameplayInitialized = initialized != 0;
    if (initialized > 1 || p.xp > 1000000000 || !p.xpToLevel || p.xpToLevel > 1000000 || p.money > 1000000000 || !p.level || p.level > 80) return false;
    uint8_t count = r.u8(); if (count > LocalGameplay::MaxInventory) return false;
    p.inventory.clear();
    for (unsigned i = 0; i < count; ++i) {
        LocalItemStack s; s.itemId = r.u32(); s.count = r.u16();
        if (!s.itemId || !s.count) return false;
        p.inventory.push_back(s);
    }
    count = r.u8(); if (count > LocalGameplay::MaxQuests) return false; p.quests.clear();
    std::vector<uint32_t> questIds;
    for (unsigned i = 0; i < count; ++i) {
        LocalQuestProgress q; q.id = r.u32(); const auto status = r.u8(); q.status = LocalQuestStatus(status);
        const auto objectives = r.u8(); if (!q.id || status > (version < 5 ? 2 : 1) || objectives > 4) return false;
        if (std::find(questIds.begin(), questIds.end(), q.id) != questIds.end()) return false;
        questIds.push_back(q.id);
        for (unsigned o = 0; o < objectives; ++o) q.progress.push_back(r.u16());
        if (q.status == LocalQuestStatus::Rewarded) p.completedQuestIds.push_back(q.id);
        else p.quests.push_back(std::move(q));
    }
    if (version < 5) std::sort(p.completedQuestIds.begin(), p.completedQuestIds.end());
    count = r.u8(); if (count > LocalGameplay::MaxSpells) return false; p.knownSpells.clear();
    for (unsigned i = 0; i < count; ++i) {
        const auto id = r.u32(); if (!id || std::find(p.knownSpells.begin(), p.knownSpells.end(), id) != p.knownSpells.end()) return false;
        p.knownSpells.push_back(id);
    }
    count = r.u8(); if (count > LocalGameplay::MaxCooldowns) return false; p.cooldowns.clear();
    for (unsigned i = 0; i < count; ++i) {
        LocalCooldown cd; cd.spellId = r.u32(); cd.remainingMs = r.u32();
        if (cd.remainingMs > 3600000 || std::find(p.knownSpells.begin(), p.knownSpells.end(), cd.spellId) == p.knownSpells.end()) return false;
        for (const auto& other : p.cooldowns) if (other.spellId == cd.spellId) return false;
        p.cooldowns.push_back(cd);
    }
    if (version >= 3) {
        const auto hasReturn = r.u8(); p.hasInstanceReturn = hasReturn != 0;
        p.returnMapId = r.u32(); p.returnInstanceId = r.u32();
        p.returnX = r.f32(); p.returnY = r.f32(); p.returnZ = r.f32(); p.returnOrientation = r.f32();
        if (hasReturn > 1 || p.returnInstanceId > 65535 || !validPosition(p.returnMapId, p.returnX, p.returnY, p.returnZ, p.returnOrientation)) return false;
    }
    if (version >= 7) {
        count = r.u8(); if (count > LocalGameplay::MaxProfessions) return false;
        p.professions.clear();
        for (unsigned i = 0; i < count; ++i) {
            LocalProfessionSkill skill; skill.skillId = r.u16(); skill.current = r.u16(); skill.max = r.u16();
            // Bounds only; whether the cap is a rank this realm ever sold is
            // LocalGameplay::validatePlayer's question, because it owns the
            // rank ladder and this reader must not duplicate it.
            if (!skill.skillId || !skill.current || skill.current > skill.max || skill.max > 450) return false;
            for (const auto& other : p.professions) if (other.skillId == skill.skillId) return false;
            p.professions.push_back(skill);
        }
        count = r.u8(); if (count > LocalGameplay::MaxRecipes) return false;
        p.knownRecipes.clear();
        for (unsigned i = 0; i < count; ++i) {
            const auto spellId = r.u32();
            if (!spellId || std::find(p.knownRecipes.begin(), p.knownRecipes.end(), spellId) !=
                            p.knownRecipes.end()) return false;
            p.knownRecipes.push_back(spellId);
        }
        const auto hasHome = r.u8(); p.hasHome = hasHome != 0;
        p.homeMapId = r.u32(); p.homeX = r.f32(); p.homeY = r.f32(); p.homeZ = r.f32();
        p.homeOrientation = r.f32(); p.hearthCooldown = r.f32();
        if (hasHome > 1 || (p.hasHome && !validPosition(p.homeMapId, p.homeX, p.homeY, p.homeZ, p.homeOrientation)) ||
            !std::isfinite(p.hearthCooldown) || p.hearthCooldown < 0 ||
            p.hearthCooldown > LocalGameplay::HearthCooldownSeconds) return false;
    }
    if (version >= 8) {
        p.transportEntry = r.u32(); p.transportOffsetX = r.f32(); p.transportOffsetY = r.f32();
        p.transportOffsetZ = r.f32(); p.transportLastYaw = r.f32();
        if (!std::isfinite(p.transportOffsetX) || !std::isfinite(p.transportOffsetY) ||
            !std::isfinite(p.transportOffsetZ) || !std::isfinite(p.transportLastYaw) ||
            std::abs(p.transportOffsetX) > 100 || std::abs(p.transportOffsetY) > 100 ||
            std::abs(p.transportOffsetZ) > 100) return false;
    }
    if (version >= 9) {
        for(auto& remaining:p.runeCooldownMs) remaining=r.u16();
        if(!validLocalRunes(p.runeCooldownMs)) return false;
    } else p.runeCooldownMs.fill(0);
    p.bank.fill({});
    if (version >= 12) {
        for (auto& slot : p.bank) {
            slot.itemId = r.u32(); slot.count = r.u16();
            if ((slot.itemId == 0) != (slot.count == 0)) return false;
        }
        for (auto& skill : p.professions) { skill.progress = r.u16(); if (skill.progress >= 1000) return false; }
    }
    if(version>=15){for(auto& item:p.inventory)item.bagSlot=r.u8();if(!validLocalInventoryLayout(p))return false;}
    else normalizeLocalInventory(p);
    p.flight={};p.knownTaxiNodes.clear();p.ridingSkill=0;p.migrateLegacyRiding=version<16;
    if(version>=16){
        p.ridingSkill=r.u16();if(p.ridingSkill==65535){p.migrateLegacyRiding=true;p.ridingSkill=0;}
        if(p.ridingSkill!=0 && p.ridingSkill!=75 && p.ridingSkill!=150)return false;
        const auto nodes=r.u16();if(nodes>512)return false;
        for(unsigned i=0;i<nodes;++i){const auto id=r.u32();if(!id || id>1000000 || std::find(p.knownTaxiNodes.begin(),p.knownTaxiNodes.end(),id)!=p.knownTaxiNodes.end())return false;p.knownTaxiNodes.push_back(id);}
        const auto active=r.u8();if(active>1)return false;p.flight.active=active;
        if(active){auto& f=p.flight;f.pathId=r.u32();f.destinationNode=r.u32();f.travelled=r.f32();f.totalLength=r.f32();f.speed=r.f32();
            f.originMap=r.u32();f.originX=r.f32();f.originY=r.f32();f.originZ=r.f32();f.originOrientation=r.f32();
            if(p.dead || p.instanceId || p.transportEntry || !f.pathId || !f.destinationNode ||
                !std::isfinite(f.travelled) || !std::isfinite(f.totalLength) || !std::isfinite(f.speed) ||
                f.travelled<0 || f.totalLength<=0 || f.totalLength>10000000 || f.travelled>f.totalLength || f.speed!=LocalTravelNetwork::DefaultFlightSpeed ||
                !validPosition(f.originMap,f.originX,f.originY,f.originZ,f.originOrientation))return false;
        }
    }
    p.healingAuras.clear();
    p.statAuras.clear();
    if(version>=18){const auto count=r.u8();if(count>kLocalMaxStatAuras)return false;
        for(unsigned i=0;i<count;++i){LocalStatAura a;a.spellId=r.u32();a.remainingMs=r.u32();a.mapId=r.u32();a.instanceId=r.u32();if(version>=19){a.casterGuid=r.u64();a.absorbRemaining=r.u32();}p.statAuras.push_back(a);}
        if(!validLocalStatAuras(p))return false;
    }
    p.talents.clear();
    if(version>=17){const auto count=r.u8();if(count>71)return false;for(unsigned i=0;i<count;++i){const auto id=r.u32();const auto rank=r.u8();p.talents.emplace_back(id,rank);}if(!validLocalTalents(p))return false;}
    return r.valid;
}
void writeHealingViews(Writer& w,const LocalRealmPlayer& p){
    w.u8(uint8_t(p.healingAuras.size()));for(const auto& a:p.healingAuras){w.u32(a.spellId);w.u64(a.casterGuid);w.u32(a.remainingMs);w.u32(a.durationMs);}
}
bool readHealingViews(Reader& r,LocalRealmPlayer& p){
    const auto count=r.u8();if(count>kLocalMaxHealingAuraViews)return false;
    p.healingAuras.clear();for(unsigned i=0;i<count;++i){LocalHealingAuraView a;a.spellId=r.u32();a.casterGuid=r.u64();a.remainingMs=r.u32();a.durationMs=r.u32();p.healingAuras.push_back(a);}
    return r.valid&&validLocalHealingAuraViews(p);
}
bool validHistory(const std::vector<uint32_t>& ids) {
    if (ids.size() > LocalGameplay::MaxCompletedQuests) return false;
    uint32_t previous = 0;
    for (auto id : ids) { if (!id || id <= previous) return false; previous = id; }
    return true;
}
bool readHistory(Reader& r, LocalRealmPlayer& p) {
    const auto count = r.u32();
    if (count > LocalGameplay::MaxCompletedQuests || count > (r.size - r.offset) / 4) return false;
    p.completedQuestIds.clear(); p.completedQuestIds.reserve(count);
    for (uint32_t i = 0; i < count; ++i) p.completedQuestIds.push_back(r.u32());
    if (!validHistory(p.completedQuestIds)) return false;
    for (const auto& quest : p.quests)
        if (std::binary_search(p.completedQuestIds.begin(), p.completedQuestIds.end(), quest.id)) return false;
    return r.valid;
}
void writeCast(Writer& w, const LocalRealmPlayer& p) {
    w.u32(p.castingSpellId); w.u64(p.castTarget); w.u32(p.castRemainingMs);
    w.u32(p.castTotalMs); w.u32(p.globalCooldownMs); w.u8(uint8_t(p.castStatus));
    w.u32(p.castRevision); w.u32(p.lastCastSpellId); w.u64(p.lastCastTarget);w.u32(p.mountSpellId);
}
bool readCast(Reader& r, LocalRealmPlayer& p) {
    p.castingSpellId = r.u32(); p.castTarget = r.u64(); p.castRemainingMs = r.u32();
    p.castTotalMs = r.u32(); p.globalCooldownMs = r.u32(); const auto status = r.u8();
    p.castRevision = r.u32(); p.lastCastSpellId = r.u32(); p.lastCastTarget = r.u64();p.mountSpellId=r.u32();
    p.castStatus = LocalCastStatus(status);
    return r.valid && status <= uint8_t(LocalCastStatus::Failed) &&
           (p.castRevision ? p.lastCastSpellId != 0 : !p.lastCastSpellId && !p.lastCastTarget) &&
           p.castTotalMs <= 3600000 && p.castRemainingMs <= p.castTotalMs && p.globalCooldownMs <= 3600000 &&
           (p.castStatus != LocalCastStatus::Casting || (p.castingSpellId && p.castRemainingMs));
}
/// Bits of the NPC flags field, which is a u16 since wire version 10: the six
/// original bits ran out when merchants, repair, training and innkeepers were
/// added. Named rather than written inline so writer and reader cannot drift.
enum NpcWireFlag : uint16_t {
    NpcDead = 1, NpcLootable = 2, NpcHostile = 4, NpcAggressive = 8,
    NpcFlightMaster = 16, NpcAuctioneer = 32, NpcVendor = 64, NpcRepairer = 128,
    NpcClassTrainer = 256, NpcProfessionTrainer = 512, NpcInnkeeper = 1024,
    NpcBanker = 2048, NpcFlagMask = 4095,
};
void writeNpc(Writer& w, const LocalRealmNpc& n) {
    w.u64(n.guid); w.u64(n.targetGuid); w.u64(n.lootOwner); w.u32(n.entry); w.u32(n.mapId);
    w.f32(n.x); w.f32(n.y); w.f32(n.z); w.f32(n.orientation);
    w.u32(n.health); w.u32(n.maxHealth); w.u8(n.level);
    // Bits 16 and up are the services this NPC offers. They travel rather than
    // being re-derived on the guest because the host resolves them against the
    // taxi network and the reconciled npcflag, and a guest that guessed for
    // itself could offer a flight, a shop or training the host would refuse.
    w.u16(uint16_t((n.dead ? NpcDead : 0) | (n.lootable ? NpcLootable : 0) |
                   (n.hostile ? NpcHostile : 0) | (n.aggressive ? NpcAggressive : 0) |
                   (n.flightMaster ? NpcFlightMaster : 0) | (n.auctioneer ? NpcAuctioneer : 0) |
                   (n.vendor ? NpcVendor : 0) | (n.repairer ? NpcRepairer : 0) |
                   (n.classTrainer ? NpcClassTrainer : 0) |
                   (n.banker ? NpcBanker : 0) | (n.professionTrainer ? NpcProfessionTrainer : 0) |
                   (n.innkeeper ? NpcInnkeeper : 0)));
    w.u32(n.instanceId); w.u32(n.taxiNodeId);
    // What the merchant carries and what the trainer teaches. A guest needs
    // both to draw the shop and the training list from its own content copy
    // without a round trip for every window it opens.
    w.u8(n.vendorCategories); w.u16(n.trainerSkill); w.u8(n.trainerClass);
    w.u32(n.transportEntry); w.f32(n.transportX); w.f32(n.transportY);
    w.f32(n.transportZ); w.f32(n.transportOrientation);
}
LocalRealmNpc readNpc(Reader& r, const LocalWorldContent& c) {
    LocalRealmNpc n; n.guid = r.u64(); n.targetGuid = r.u64(); n.lootOwner = r.u64(); n.entry = r.u32(); n.mapId = r.u32();
    n.x = r.f32(); n.y = r.f32(); n.z = r.f32(); n.orientation = r.f32();
    n.health = r.u32(); n.maxHealth = r.u32(); n.level = r.u8(); const auto flags = r.u16();
    n.dead = (flags & NpcDead) != 0; n.lootable = (flags & NpcLootable) != 0;
    n.hostile = (flags & NpcHostile) != 0; n.aggressive = (flags & NpcAggressive) != 0;
    n.flightMaster = (flags & NpcFlightMaster) != 0; n.auctioneer = (flags & NpcAuctioneer) != 0;
    n.vendor = (flags & NpcVendor) != 0; n.repairer = (flags & NpcRepairer) != 0;
    n.classTrainer = (flags & NpcClassTrainer) != 0;
    n.professionTrainer = (flags & NpcProfessionTrainer) != 0;
    n.innkeeper = (flags & NpcInnkeeper) != 0; n.banker = (flags & NpcBanker) != 0;
    n.instanceId = r.u32(); n.taxiNodeId = r.u32();
    n.vendorCategories = r.u8(); n.trainerSkill = r.u16(); n.trainerClass = r.u8();
    n.transportEntry=r.u32();n.transportX=r.f32();n.transportY=r.f32();
    n.transportZ=r.f32();n.transportOrientation=r.f32();
    const auto* def = c.npc(n.entry);
    if (!def || (n.guid & 0xffff000000000000ULL) != 0xf130000000000000ULL || n.instanceId > 65535 || uint32_t((n.guid >> 32) & 0xffff) != n.instanceId ||
        !validPosition(n.mapId, n.x, n.y, n.z, n.orientation) || flags > NpcFlagMask || !n.level || n.level > 83 ||
        !n.maxHealth || n.maxHealth > 1000000000 || n.health > n.maxHealth || n.dead != (n.health == 0) ||
        // A taxi node only means something on a flight master, and one without
        // a node would offer an empty destination list.
        (n.flightMaster != (n.taxiNodeId != 0)) ||
        // The same rule for the other services: goods on something that is not
        // a merchant, or a subject on something that is not that trainer, is a
        // malformed packet rather than an NPC.
        n.vendorCategories > 31 || (n.vendorCategories != 0) != n.vendor ||
        (n.trainerSkill && !n.professionTrainer) || (n.trainerClass && !n.classTrainer) ||
        n.trainerClass > 11 ||
        !std::isfinite(n.transportX) || !std::isfinite(n.transportY) ||
        !std::isfinite(n.transportZ) || !std::isfinite(n.transportOrientation) ||
        std::abs(n.transportX)>128 || std::abs(n.transportY)>128 ||
        std::abs(n.transportZ)>128 || std::abs(n.transportOrientation)>100 ||
        (n.transportEntry && (n.instanceId || n.transportEntry>10000000)) ||
        (!n.transportEntry && (n.transportX || n.transportY || n.transportZ || n.transportOrientation))) r.valid = false;
    if (def) { n.displayId = def->displayId; n.name = def->name; n.questGiver = def->questGiver; }
    return n;
}
void writeAuction(Writer& w, const LocalAuction& a) {
    w.u32(a.id); w.u32(a.itemId); w.u16(a.count); w.u32(a.bid); w.u32(a.buyout);
    w.u64(a.seller); w.name(a.sellerName); w.f32(a.remainingSeconds);
    w.u32(a.highestBid); w.u64(a.highestBidder);
}
LocalAuction readAuction(Reader& r, const LocalWorldContent& c, bool requireKnownItem = true) {
    LocalAuction a;
    a.id = r.u32(); a.itemId = r.u32(); a.count = r.u16(); a.bid = r.u32(); a.buyout = r.u32();
    a.seller = r.u64(); a.sellerName = r.name(); a.remainingSeconds = r.f32();
    a.highestBid = r.u32(); a.highestBidder = r.u64();
    // Network traffic and a fully started realm must reference a real item. A
    // character-roster scan, however, intentionally runs without loading the
    // world catalog. Do not make an unrelated auction listing hide every saved
    // character merely because that lightweight scan cannot resolve item ids.
    if (!a.id || !a.itemId || !a.count || !a.bid || !a.seller || a.sellerName.empty() ||
        (requireKnownItem && !c.item(a.itemId)) ||
        a.bid > 1000000000u || a.buyout > 1000000000u || a.highestBid > 1000000000u ||
        (a.buyout && (a.bid > a.buyout || a.highestBid >= a.buyout)) ||
        !(a.remainingSeconds >= 0.0f) ||
        a.remainingSeconds > 172800.0f ||
        (a.highestBid != 0) != (a.highestBidder != 0)) r.valid = false;
    return a;
}
bool mailActionKind(LocalAction action) {return action>=LocalAction::MailSend && action<=LocalAction::MailRead;}
void writeMail(Writer& w,const LocalMail& m) {
    w.u32(m.id);w.u64(m.sender);w.u64(m.recipient);w.name(m.senderName);w.text(m.subject);w.text(m.body);
    w.u32(m.money);w.u32(m.cod);w.u8(m.read | (m.returned<<1) | (m.system<<2));
    for(const auto& item:m.items){w.u32(item.itemId);w.u16(item.count);}
}
LocalMail readMail(Reader& r) {
    LocalMail m;m.id=r.u32();m.sender=r.u64();m.recipient=r.u64();m.senderName=r.name();m.subject=r.text();m.body=r.text();
    m.money=r.u32();m.cod=r.u32();const auto flags=r.u8();m.read=flags&1;m.returned=flags&2;m.system=flags&4;if(flags>7)r.valid=false;
    for(auto& item:m.items){item.itemId=r.u32();item.count=r.u16();}
    if(!localMailTextValid(m.subject,64) || !localMailTextValid(m.body,160))r.valid=false;
    return m;
}
uint32_t checksum(const uint8_t* p, size_t n) {
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < n; ++i) hash = (hash ^ p[i]) * 16777619U;
    return hash;
}
uint64_t uniqueId() {
    uint64_t id = 0;
    if (FILE* f = std::fopen("/dev/urandom", "rb")) {
        const size_t count = std::fread(&id, sizeof(id), 1, f);
        std::fclose(f);
        if (count == 1 && id) return id;
    }
    // These identify a trusted local session; they are not authentication keys.
    static std::atomic<uint64_t> counter{0x6d2b79f5};
    id = uint64_t(std::chrono::high_resolution_clock::now().time_since_epoch().count()) ^
         counter.fetch_add(0x9e3779b97f4a7c15ULL) ^ uint64_t(reinterpret_cast<uintptr_t>(&id));
    id = (id ^ (id >> 30)) * 0xbf58476d1ce4e5b9ULL;
    id = (id ^ (id >> 27)) * 0x94d049bb133111ebULL;
    return (id ^ (id >> 31)) | 1;
}
bool sameAddress(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_family == b.sin_family && a.sin_port == b.sin_port &&
           a.sin_addr.s_addr == b.sin_addr.s_addr;
}
void initAddress(sockaddr_in& address) {
    address = {};
    address.sin_family = AF_INET;
#if defined(WOWEE_PS4) || defined(__FreeBSD__) || defined(__APPLE__)
    address.sin_len = sizeof(address);
#endif
}
bool readFile(const std::string& path, std::vector<uint8_t>& bytes, size_t maxSize) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamoff length = file.tellg();
    if (length < 0 || uint64_t(length) > maxSize) return false;
    bytes.resize(size_t(length));
    file.seekg(0);
    return bytes.empty() || bool(file.read(reinterpret_cast<char*>(bytes.data()), length));
}
bool copyFileBytes(const std::string& from, const std::string& to) {
    FILE* in = std::fopen(from.c_str(), "rb");
    if (!in) return false;
    const std::string temp = to + ".tmp";
    FILE* out = std::fopen(temp.c_str(), "wb");
    if (!out) { std::fclose(in); return false; }
    bool ok = true;
    char buffer[16384];
    for (;;) {
        const size_t n = std::fread(buffer, 1, sizeof(buffer), in);
        if (n && std::fwrite(buffer, 1, n, out) != n) { ok = false; break; }
        if (n != sizeof(buffer)) { ok = !std::ferror(in); break; }
    }
    if (std::fclose(in) != 0) ok = false;
    if (std::fflush(out) != 0) ok = false;
#ifndef _WIN32
    if (::fsync(fileno(out)) != 0) ok = false;
#endif
    if (std::fclose(out) != 0) ok = false;
    if (ok && std::rename(temp.c_str(), to.c_str()) != 0) ok = false;
    if (!ok) std::remove(temp.c_str());
    return ok;
}
bool atomicWrite(const std::string& path, const std::vector<uint8_t>& bytes, bool backup) {
    const std::string temp = path + ".new";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (std::fflush(f) != 0) ok = false;
#ifndef _WIN32
    if (::fsync(fileno(f)) != 0) ok = false;
#endif
    if (std::fclose(f) != 0) ok = false;
    if (ok && backup) {
        // Plain read/write copy rather than std::filesystem::copy_file: on the
        // console copy_file has failed (the B3 metadata copy moved off it for
        // the same reason), and a failed rotation here used to discard the
        // save that had just been written - every save after the first.
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            if (!copyFileBytes(path, path + ".bak")) ok = false;
        }
    }
    if (ok && std::rename(temp.c_str(), path.c_str()) != 0) ok = false;
    if (!ok) std::remove(temp.c_str());
    return ok;
}
bool ensureDirectory(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec && std::filesystem::is_directory(path, ec);
}
bool newer(uint32_t value, uint32_t previous) { return int32_t(value - previous) > 0; }
} // namespace

struct LocalRealm::Impl {
    core::SnapshotWriter autosave{[](const std::string& path, const std::vector<uint8_t>& bytes) {
        return atomicWrite(path, bytes, true);
    }};
    struct Identity {
        uint64_t a = 0, b = 0;
        bool operator==(const Identity& rhs) const { return a == rhs.a && b == rhs.b; }
    };
    struct SavedPlayer { Identity identity; LocalRealmPlayer player; };
    struct ChatDelivery {uint32_t id=0;LocalChatLine line;double lastSent=-1;};
    struct ChatRequest {uint32_t id=0;LocalChatChannel channel;std::string text,target;double lastSent=-1,enqueued=0;};
    struct Peer {
        sockaddr_in address{};
        Identity identity;
        uint64_t guid = 0, session = 0, joinNonce = 0;
        uint32_t sequence = 0, lastCommand = 0;
        uint32_t lastChat=0,chatSerial=0;
        bool lastChatSuccess=false;
        std::string lastChatStatus;
        LocalChatRate chatRate;
        std::vector<ChatDelivery> chatOut;
        bool lastCommandSuccess = false;
        std::string lastCommandStatus;
        double lastSeen = 0, lastSnapshot = -1, lastClock = -1;
        double lastMailQuery=-1;
        bool loading = true;
        double loadingSince = 0;
        uint32_t historyRevision = 0, historyCount = 0;
        size_t historyCursor = 0;
        std::vector<bool> historyAcked;
        std::vector<double> historySent;
        uint64_t merchantGuid = 0;
        uint32_t merchantRequest = 0;
        double lastMerchantQuery = -1, lastMerchantSnapshot = -1, lastPartySnapshot = -1, lastSocialSnapshot=-1;
    };
    LocalRealmState state = LocalRealmState::Stopped;
    std::string realmName = "LAN Realm";
    double discoveryWindow = -1; unsigned discoveryReplies = 0;
    std::string error, status = "Local realm stopped", directory, actionStatus;
    uint64_t actionStatusRevision = 0;
    LocalGameplay gameplay;
    LocalPartyDirector partyDirector;
    LocalPartyView localParty;
    std::vector<std::string> ignoreNames;
    bool ignoreWritable=true;
    std::vector<LocalReadyCheck> readyChecks;
    std::vector<LocalTrade> trades;
    LocalReadyCheck localReady;
    LocalTrade localTrade;
    uint32_t nextReady=0,nextTrade=0,socialSerial=0,socialReceived=0;
    uint64_t socialRevision=0;
    LocalChatRate localChatRate;
    std::vector<LocalChatLine> chatInbox;
    std::deque<ChatRequest> chatPending;
    uint32_t chatRequestSerial=0,chatReceived=0;
    size_t chatCursor=0;
    void clearChat() {
        chatInbox.clear();chatPending.clear();chatRequestSerial=chatReceived=0;chatCursor=0;
        localChatRate={};for(auto& peer:peers){peer.chatOut.clear();peer.lastChat=peer.chatSerial=0;peer.chatRate={};}
    }
    uint64_t partyRevision = 0, partyRosterRevision = 0;
    uint32_t partyTick = 0, partySequence = 0;
    double lastPartyRefresh = -1;
    // Playerbots. They are LocalRealmPlayers with nobody at the controls, held
    // beside the roster rather than inside it: a bot must not occupy a console
    // slot, must not appear in the character list, and must not be saved as
    // somebody's hero - but it does have to be simulated and replicated like
    // any other player, which is what activePlayers() and refreshPlayers() do
    // with them below.
    LocalBotDirector botDirector;
    std::vector<LocalRealmPlayer> botPlayers;
    /// Whether the tick has already said what it decided about bots. One line
    /// per realm, not one per frame.
    bool botsReported = false;
    uint8_t requestedRace = 1, requestedClass = 1, requestedGender = 0, characterSlot = 0;
    uint8_t requestedSkin = 0, requestedFace = 0, requestedHairStyle = 0, requestedHairColor = 0, requestedFacialHair = 0;
    bool requestedFemaleModel = false;
    std::vector<LocalInstanceState> restoredInstances;
    mutable std::vector<LocalRealmNpc> npcView;
    struct PendingCommand { uint32_t id; LocalRealmCommand command; double lastSent = -1; double enqueued = 0; };
    std::deque<PendingCommand> pendingCommands;
    uint32_t nextCommand = 0, progressSequence = 0, vitalsSequence = 0, worldSequence = 0, worldTick = 0, collectingWorld = 0;
    uint32_t historyRevision = 0, collectingHistory = 0, collectingHistoryCount = 0;
    std::vector<uint32_t> historyIds;
    std::vector<bool> historyReceived;
    struct PendingProgress { LocalRealmPlayer player; uint32_t sequence, historyRevision, historyCount; };
    std::optional<PendingProgress> pendingProgress;
    uint32_t collectingProgress = 0;
    uint16_t progressTotal = 0;
    std::array<uint8_t, MaxOwnerProgressBytes> progressBytes{};
    std::array<bool, MaxProgressChunks> progressReceived{};
    uint8_t worldParts = 0;
    std::array<std::vector<LocalRealmNpc>, MaxNpcPages> worldChunks;
    std::array<bool, MaxNpcPages> worldReceived{};
    // The auction board as a guest sees it. The host's own board lives in
    // botDirector; a guest has no director, so the replicated copy is what
    // auctions() returns there. Assembled page by page like the NPC list, and
    // only swapped in once every page of a tick has arrived - a half-received
    // board would flicker listings in and out of the browse window.
    std::vector<LocalAuction> remoteAuctions;
    LocalMailbox mailbox;
    uint64_t mailRevision=1,mailResultRevision=0;
    bool mailResultSuccess=false;
    uint32_t mailTick=0,mailSequence=0,collectingMail=0;
    uint8_t mailParts=0;
    std::vector<LocalMail> remoteMail;
    std::array<std::vector<LocalMail>,32> mailChunks;
    std::array<bool,32> mailReceived{};
    double lastMailRequest=-1;
    uint32_t auctionTick = 0, auctionSequence = 0, collectingAuctions = 0;
    uint8_t auctionParts = 0;
    std::array<std::vector<LocalAuction>, MaxAuctionPages> auctionChunks;
    std::array<bool, MaxAuctionPages> auctionReceived{};
    // Only the selected vendor is retained, and only complete snapshot pages
    // commit. The buyback copy belongs to this owner and never public Progress.
    uint64_t merchantGuid = 0;
    uint32_t merchantRequest = 0, merchantTick = 0, merchantSequence = 0, collectingMerchant = 0;
    double lastMerchantRequestSend = -1;
    uint8_t merchantParts = 0;
    bool collectingMerchantAllowed = false;
    uint32_t collectingBuybackSerial = 0;
    std::vector<LocalMerchantBuyback> remoteBuyback, collectingBuyback;
    std::vector<MerchantOfferState> remoteMerchant;
    std::array<std::vector<MerchantOfferState>, MaxMerchantPages> merchantChunks;
    std::array<bool, MaxMerchantPages> merchantReceived{};
    socket_t socket = INVALID_SOCK;
    sockaddr_in host{};
    uint16_t port = 0;
    uint64_t realmId = 0, session = 0, joinNonce = 0, legacyProbeNonce = 0;
    Identity identity;
    std::array<Identity, LocalRealm::MaxCharacterSlots> lobbyIdentities{};
    std::vector<LocalSavedCharacter> remoteCharacters;
    // One bounded, retransmitted request at a time. No world handshake during selection.
    uint64_t lobbyNonce = 0, expectedRealmId = 0, selectedGuid = 0, rosterRevision = 0;
    uint8_t lobbyOperation = 0, lobbySlot = 0; // 0=query, 1=create, 2=delete
    bool lobbyPending = false, rosterReady = false, worldLoading = false;
    double lobbyStarted = 0, lobbySent = -1, joinStarted = 0;
    Writer lobbyPayload;
    struct LobbyReplyCache { Identity identity; uint64_t nonce; sockaddr_in address; Writer reply; double time; };
    std::deque<LobbyReplyCache> lobbyReplies;
    LocalRealmPlayer self;
    std::vector<LocalRealmPlayer> players;
    std::vector<LocalRealmPlayer*> activePlayerScratch;
    std::vector<SavedPlayer> saved;
    std::vector<Peer> peers;
    struct CancelledJoin { Identity identity; uint64_t nonce; double time; };
    std::deque<CancelledJoin> cancelledJoins;
    void rememberCancelled(const Identity& id,uint64_t nonce) {
        while(!cancelledJoins.empty() && now-cancelledJoins.front().time>30.0)cancelledJoins.pop_front();
        for(const auto& old:cancelledJoins)if(old.identity==id && old.nonce==nonce)return;
        if(cancelledJoins.size()>=256)cancelledJoins.pop_front();
        cancelledJoins.push_back({id,nonce,now});
    }
    bool joinCancelled(const Identity& id,uint64_t nonce) const {
        for(const auto& old:cancelledJoins)if(old.identity==id && old.nonce==nonce && now-old.time<=30.0)return true;
        return false;
    }
    void resetSavedSession(uint64_t guid) {
        if(auto* record=findSaved(guid)){
            auto& player=record->player;
            player.attackTarget=0;player.attackTimer=0;
            player.castingSpellId=player.castRemainingMs=player.castTotalMs=0;
            player.castTarget=0;player.castStatus=LocalCastStatus::None;
            dirty=true;
        }
    }
    void sendDeparture() {
        if(socket==INVALID_SOCK)return;
        // Small redundant datagrams, no blocking wait/sleep in render or error
        // callbacks. Session/nonce validation makes delayed duplicates harmless.
        if(state==LocalRealmState::Connected && session){
            Writer w;writePosition(w,self);w.u32(self.positionRevision);w.u32(self.instanceId);
            for(int n=0;n<3;++n)send(Message::Leave,session,w,host);
        } else if(state==LocalRealmState::Connecting && joinNonce && identity.a && identity.b){
            Writer w;w.u64(identity.a);w.u64(identity.b);
            for(int n=0;n<3;++n)send(Message::AbortJoin,joinNonce,w,host);
        } else if(state==LocalRealmState::Hosting){
            Writer empty;
            for(const auto& peer:peers)for(int n=0;n<3;++n)send(Message::Leave,peer.session,empty,peer.address);
        }
    }
    uint32_t sequence = 0, incomingSequence = 0;
    size_t playerLimit = LocalRealm::DefaultPlayers, sendCursor = 0;
    unsigned helloBudget = 4;
    uint32_t playerTick = 0, playerSequence = 0, collectingPlayers = 0, collectingPlayerSequence = 0;
    uint8_t playerParts = 0, playerTotal = 0;
    std::array<std::vector<LocalRealmPlayer>, MaxPlayerPages> playerChunks;
    std::array<bool, MaxPlayerPages> playerReceived{};
    double now = 0, lastSend = -1, lastHello = -1, lastSeen = 0, lastSave = 0;
    std::chrono::steady_clock::time_point lastPump = std::chrono::steady_clock::now();
    LocalDayClock dayClock;
    uint32_t clockSequence = 0;
    double lastClockSend = -1;
    Impl() {
        const auto local = core::localTime(std::time(nullptr));
        float hour = float(local.tm_hour) + float(local.tm_min)/60 + float(local.tm_sec)/3600;
#ifdef WOWEE_PS4
        // Console libc time can report a 1970/uptime epoch. Seed from SceRtc.
        // This SDK declares the subsecond field too narrowly: reserve aligned
        // trailing storage for the native write; only the calendar fields up
        // to second are consumed here.
        struct alignas(8) ClockStorage { TimeTable value; uint8_t tail[32]; } rtc{};
        if (sceRtcGetCurrentClockLocalTime(&rtc.value) == 0 &&
            rtc.value.hour < 24 && rtc.value.minute < 60 && rtc.value.second < 60) {
            hour = float(rtc.value.hour) + float(rtc.value.minute)/60 + float(rtc.value.second)/3600;
        }
#endif
        dayClock.synchronize(hour, 0);
    }
    bool dirty = false;
    bool authoritative() const {
        return state == LocalRealmState::SinglePlayer || state == LocalRealmState::Hosting;
    }
    bool fail(const std::string& reason) {
        sendDeparture();
        error = reason; status = reason; state = LocalRealmState::Error;
        clearSocial();clearChat();clearMail();pendingCommands.clear();pendingProgress.reset();lobbyPending=false;worldLoading=false;
        players.clear();npcView.clear();
        partyDirector=LocalPartyDirector{};commitParty({});
        gameplay.setPartyMembership({});
        LOG_ERROR("[local_realm] ", reason);
        if (socket != INVALID_SOCK) { net::closeSocket(socket); socket = INVALID_SOCK; }
        return false;
    }
    SavedPlayer* findSaved(uint64_t guid) {
        for (auto& record : saved) if (record.player.guid == guid) return &record;
        return nullptr;
    }
    SavedPlayer* findIdentity(const Identity& id) {
        for (auto& record : saved) if (record.identity == id) return &record;
        return nullptr;
    }
    std::string ignorePath()const{return directory+"/ignore_slot_"+std::to_string(characterSlot)+".dat";}
    bool ignored(const std::string& name)const {for(const auto& row:ignoreNames)if(localChatNameEqual(row,name))return true;return false;}
    void loadIgnores() {
        ignoreNames.clear();ignoreWritable=true;++socialRevision;
        std::error_code ec;if(!std::filesystem::exists(ignorePath(),ec)){if(ec)ignoreWritable=false;return;}
        std::vector<uint8_t> bytes;
        if(readFile(ignorePath(),bytes,1024) && bytes.size()>=25) {
            Reader r(bytes.data(),bytes.size());const auto magic=r.u32();const Identity owner{r.u64(),r.u64()};const auto count=r.u8();std::vector<std::string> names;
            for(unsigned i=0;i<count && i<50;++i){auto name=r.name();if(!validName(name))r.valid=false;for(const auto& old:names)if(localChatNameEqual(old,name))r.valid=false;names.push_back(std::move(name));}
            const auto sum=r.u32();
            if(magic==0x57494731 && count<=50 && r.done() && sum==checksum(bytes.data(),bytes.size()-4)){if(owner==identity)ignoreNames=std::move(names);return;}
        }
        ignoreWritable=false;LOG_WARNING("[LOCAL_IGNORE] unreadable list preserved slot=",unsigned(characterSlot));
    }
    LocalRealmPlayer* socialPlayer(uint64_t guid) {
        if(guid==self.guid)return worldLoading?nullptr:&self;
        for(const auto& peer:peers)if(peer.guid==guid && !peer.loading){auto* record=findSaved(guid);return record?&record->player:nullptr;}
        return nullptr;
    }
    bool tradeAvailable(const LocalRealmPlayer& p)const {
        if(!localTradeAvailable(p))return false;
        for(const auto& npc:gameplay.npcs())if(npc.health && npc.targetGuid==p.guid)return false;
        return true;
    }
    LocalTrade* activeTrade(uint64_t guid) {
        for(auto& trade:trades)if(trade.live() && trade.side(guid)>=0)return &trade;return nullptr;
    }
    void closeTrade(LocalTrade& trade,uint8_t state) {
        trade.state=state;trade.accepted={};trade.deadline=now+10;
        if(trade.revision<UINT32_MAX)++trade.revision;
    }
    void maintainSocial() {
        if(!authoritative())return;
        for(auto& check:readyChecks)if(check.state==1) {
            const auto* party=partyDirector.party(check.initiator);
            bool same=party && party->id==check.party && party->members.size()==check.members.size() && party->members.front()==check.initiator;
            if(same)for(const auto& member:check.members)if(std::find(party->members.begin(),party->members.end(),member.guid)==party->members.end() || !socialPlayer(member.guid))same=false;
            if(!same){check.state=3;check.deadline=now+10;}
            else if(now>=check.deadline){for(auto& m:check.members)if(!m.answer)m.answer=2;check.state=2;check.deadline=now+10;}
        }
        std::erase_if(readyChecks,[&](const auto& check){return check.state!=1 && now>=check.deadline;});
        for(auto& trade:trades)if(trade.live()) {
            auto* a=socialPlayer(trade.players[0]);auto* b=socialPlayer(trade.players[1]);
            if(!a || !b || !localTradeReach(*a,*b) || !tradeAvailable(*a) || !tradeAvailable(*b) || now>=trade.deadline || trade.revision==UINT32_MAX){closeTrade(trade,4);continue;}
            const std::array<LocalRealmPlayer*,2> participants{a,b};bool changed=false;
            for(unsigned side=0;side<2;++side) {
                const auto fingerprint=localTradeFingerprint(*participants[side]);
                if(fingerprint!=trade.fingerprints[side]) {
                    changed=true;trade.fingerprints[side]=fingerprint;
                    for(auto& item:trade.items[side])if(!localTradeItemValid(*participants[side],item,gameplay.content()))item={};
                    if(trade.money[side]>participants[side]->money)trade.money[side]=0;
                }
            }
            if(changed){trade.accepted={};++trade.revision;}
        }
        std::erase_if(trades,[&](const auto& trade){return !trade.live() && now>=trade.deadline;});
        LocalReadyCheck ready;for(const auto& check:readyChecks)for(const auto& member:check.members)if(member.guid==self.guid)ready=check;
        LocalTrade trade;for(const auto& row:trades)if(row.side(self.guid)>=0)trade=row;
        if(!(ready==localReady) || !(trade==localTrade)){localReady=std::move(ready);localTrade=std::move(trade);++socialRevision;}
    }
    void clearSocial(){readyChecks.clear();trades.clear();localReady={};localTrade={};++socialRevision;socialReceived=0;}
    bool executeSocial(LocalRealmPlayer& player,const LocalRealmCommand& cmd,std::string& result) {
        syncParty(true);maintainSocial();
        auto reject=[&](const char* text){result=text;return false;};
        if(cmd.action==LocalAction::ReadyStart) {
            const auto* party=partyDirector.party(player.guid);
            if(!party || party->members.front()!=player.guid)return reject("Only the party leader can start a ready check");
            for(const auto& old:readyChecks)if(old.party==party->id)return reject("Wait for the previous ready check to finish");
            if(readyChecks.size()>=50 || nextReady==UINT32_MAX)return reject("Ready check limit reached");
            LocalReadyCheck check;check.id=nextReady+1;check.party=party->id;check.initiator=player.guid;check.deadline=now+30;check.state=1;
            for(auto guid:party->members){auto* p=socialPlayer(guid);if(!p)return reject("A party member is loading or disconnected");check.members.push_back({guid,p->name,uint8_t(guid==player.guid?1:0)});}
            readyChecks.push_back(std::move(check));++nextReady;result="Ready check started";maintainSocial();return true;
        }
        if(cmd.action==LocalAction::ReadyAnswer) {
            if(cmd.target>1)return reject("Invalid ready check answer");
            for(auto& check:readyChecks)if(check.id==cmd.id && check.state==1)for(auto& member:check.members)if(member.guid==player.guid) {
                if(member.answer)return reject("This ready check answer is already final");
                member.answer=cmd.target?1:2;
                if(std::all_of(check.members.begin(),check.members.end(),[](const auto& m){return m.answer!=0;})){check.state=2;check.deadline=now+10;}
                result="Ready check answered";maintainSocial();return true;
            }
            return reject("This ready check is no longer active");
        }
        if(cmd.action==LocalAction::TradeRequest) {
            auto* peer=socialPlayer(cmd.target);auto* own=socialPlayer(player.guid);
            if(!peer || !own || peer==own || !localTradeReach(*own,*peer) || !tradeAvailable(*own) || !tradeAvailable(*peer))return reject("Choose a nearby available player of your faction");
            if(activeTrade(player.guid) || activeTrade(peer->guid))return reject("A participant is already trading");
            if(trades.size()>=50 || nextTrade==UINT32_MAX)return reject("Trade session limit reached");
            LocalTrade trade;trade.id=nextTrade+1;trade.revision=1;trade.state=1;trade.players={player.guid,peer->guid};trade.names={player.name,peer->name};
            trade.fingerprints={localTradeFingerprint(player),localTradeFingerprint(*peer)};trade.deadline=now+30;
            trades.push_back(std::move(trade));++nextTrade;result="Trade requested";maintainSocial();return true;
        }
        LocalTrade* current=nullptr;for(auto& trade:trades)if(trade.id==cmd.id && trade.side(player.guid)>=0)current=&trade;
        if(!current || !current->live())return reject("This trade is no longer active");
        auto& trade=*current;const auto side=unsigned(trade.side(player.guid));
        if(cmd.action==LocalAction::TradeCancel){closeTrade(trade,4);result="Trade cancelled";maintainSocial();return true;}
        if(cmd.bid!=trade.revision)return reject("Trade changed; review the current offer");
        if(cmd.action==LocalAction::TradeOpen){
            if(trade.state!=1 || side!=1)return reject("Only the invited player can open this trade");
            trade.state=2;++trade.revision;trade.deadline=now+120;result="Trade opened";maintainSocial();return true;
        }
        if(trade.state!=2)return reject("Wait for the other player to open the trade");
        if(cmd.action==LocalAction::TradeOffer) {
            if(cmd.durationMinutes>=6 || cmd.buyout>=LocalGameplay::MaxInventory || cmd.target>65535)return reject("Invalid trade slot or quantity");
            LocalTradeItem item;
            if(cmd.target){item={uint32_t(cmd.serviceNpcGuid>>32),uint16_t(cmd.target),uint16_t(cmd.serviceNpcGuid),uint8_t(cmd.buyout)};
                if(!localTradeItemValid(player,item,gameplay.content()))return reject("Choose an unbound, unequipped stack with known item data");
                for(unsigned i=0;i<6;++i)if(i!=cmd.durationMinutes && trade.items[side][i].item && trade.items[side][i].bag==item.bag)return reject("This stack is already offered");
            }
            trade.items[side][cmd.durationMinutes]=item;trade.accepted={};++trade.revision;trade.deadline=now+120;
        } else if(cmd.action==LocalAction::TradeMoney) {
            if(cmd.target>player.money || cmd.target>1000000000)return reject("Not enough money for that offer");
            trade.money[side]=uint32_t(cmd.target);trade.accepted={};++trade.revision;trade.deadline=now+120;
        } else if(cmd.action==LocalAction::TradeUnaccept) {trade.accepted={};++trade.revision;}
        else if(cmd.action==LocalAction::TradeAccept) {
            trade.accepted[side]=true;
            if(trade.accepted[0] && trade.accepted[1]) {
                auto* a=socialPlayer(trade.players[0]);auto* b=socialPlayer(trade.players[1]);LocalRealmPlayer nextA,nextB;
                if(!a || !b || !prepareLocalTrade(trade,*a,*b,gameplay.content(),nextA,nextB,result)){trade.accepted={};++trade.revision;maintainSocial();return false;}
                gameplay.refreshInventoryObjectives(nextA);gameplay.refreshInventoryObjectives(nextB);
                auto* savedSelf=findSaved(self.guid);LocalRealmPlayer previousSelf=savedSelf?savedSelf->player:LocalRealmPlayer{};
                std::swap(*a,nextA);std::swap(*b,nextB);
                if(!saveRealm()){
                    std::swap(*a,nextA);std::swap(*b,nextB);if(savedSelf)savedSelf->player=std::move(previousSelf);
                    trade.accepted={};++trade.revision;result="Trade was not saved; no items or money changed";maintainSocial();return false;
                }
                closeTrade(trade,3);result="Trade completed and saved";
                for(auto& peer:peers)if(trade.side(peer.guid)>=0)progress(peer);
                maintainSocial();return true;
            }
        } else return reject("Unknown social action");
        result="Trade updated";maintainSocial();return true;
    }
    void sendSocial(Peer& peer) {
        static_assert(HeaderSize+6+19+5*26+8+2*(30+6*9)<=MaxPacket,"Owner social state must fit one LAN datagram");
        Writer w;w.u32(++socialSerial);
        const LocalReadyCheck* check=nullptr;for(const auto& row:readyChecks)for(const auto& member:row.members)if(member.guid==peer.guid)check=&row;
        w.u8(check?check->state:0);
        if(check){w.u32(check->id);w.u32(check->party);w.u64(check->initiator);w.u16(uint16_t(std::clamp((check->deadline-now)*1000.0,0.0,30000.0)));w.u8(uint8_t(check->members.size()));for(const auto& m:check->members){w.u64(m.guid);w.name(m.name);w.u8(m.answer);}}
        const LocalTrade* trade=nullptr;for(const auto& row:trades)if(row.side(peer.guid)>=0)trade=&row;
        w.u8(trade?trade->state:0);
        if(trade){w.u32(trade->id);w.u32(trade->revision);for(unsigned side=0;side<2;++side){w.u64(trade->players[side]);w.name(trade->names[side]);w.u32(trade->money[side]);w.u8(trade->accepted[side]?1:0);for(const auto& item:trade->items[side]){w.u32(item.item);w.u16(item.count);w.u16(item.sourceCount);w.u8(item.bag);}}}
        send(Message::SocialState,peer.session,w,peer.address);peer.lastSocialSnapshot=now;
    }
    void receiveSocial(Reader& r) {
        const auto tick=r.u32();LocalReadyCheck ready;LocalTrade trade;ready.state=r.u8();
        if(!tick || !newer(tick,socialReceived) || ready.state>3)return;
        if(ready.state){ready.id=r.u32();ready.party=r.u32();ready.initiator=r.u64();const auto ms=r.u16();const auto count=r.u8();ready.deadline=now+ms/1000.0;
            if(!ready.id || !ready.party || ms>30000 || count<2 || count>5)return;
            bool owner=false,leader=false;
            for(unsigned i=0;i<count;++i){LocalReadyMember m;m.guid=r.u64();m.name=r.name();m.answer=r.u8();if(!m.guid || !validName(m.name) || m.answer>2)return;for(const auto& old:ready.members)if(old.guid==m.guid)return;owner|=m.guid==self.guid;leader|=m.guid==ready.initiator;ready.members.push_back(std::move(m));}
            if(!owner || !leader)return;
        }
        trade.state=r.u8();if(trade.state>4)return;
        if(trade.state){trade.id=r.u32();trade.revision=r.u32();if(!trade.id || !trade.revision)return;
            for(unsigned side=0;side<2;++side){trade.players[side]=r.u64();trade.names[side]=r.name();trade.money[side]=r.u32();const auto accepted=r.u8();if(!trade.players[side] || !validName(trade.names[side]) || trade.money[side]>1000000000 || accepted>1)return;trade.accepted[side]=accepted!=0;
                for(auto& item:trade.items[side]){item.item=r.u32();item.count=r.u16();item.sourceCount=r.u16();item.bag=r.u8();if(item.bag>=24 || (item.item && (!item.count || item.count>item.sourceCount || !gameplay.content().item(item.item))) || (!item.item && (item.count || item.sourceCount)))return;}}
            if(trade.players[0]==trade.players[1] || trade.side(self.guid)<0)return;
        }
        if(!r.done())return;
        localReady=std::move(ready);localTrade=std::move(trade);socialReceived=tick;++socialRevision;lastSeen=now;
    }

    bool loadIdentity() {
        const std::string file = directory + (characterSlot ? "/console_slot_" + std::to_string(characterSlot) + ".identity" : "/console.identity");
        std::error_code ec;
        if (std::filesystem::exists(file, ec)) {
            std::vector<uint8_t> bytes;
            if (!readFile(file, bytes, 32)) return fail("Cannot read local console identity");
            Reader r(bytes.data(), bytes.size());
            if (r.u32() != IdentityMagic) return fail("Invalid local console identity");
            identity = {r.u64(), r.u64()};
            uint32_t sum = r.u32();
            if (!r.done() || !identity.a || !identity.b || bytes.size() != 24 ||
                sum != checksum(bytes.data(), 20)) return fail("Damaged local console identity");
            loadIgnores();return true;
        }
        identity = {uniqueId(), uniqueId()};
        Writer w; w.u32(IdentityMagic); w.u64(identity.a); w.u64(identity.b);
        w.u32(checksum(w.bytes.data(), w.bytes.size()));
        if(!atomicWrite(file,w.bytes,false))return fail("Cannot save local console identity");
        loadIgnores();return true;
    }
    bool parseSave(const std::string& path) try {
        std::vector<uint8_t> bytes;
        if (!readFile(path, bytes, MaxSaveSize) || bytes.size() < 19) return false;
        Reader r(bytes.data(), bytes.size());
        if (r.u32() != SaveMagic) return false;
        const auto saveVersion = r.u8(); if (saveVersion < 1 || saveVersion > SaveVersion) return false;
        uint64_t readRealmId = r.u64();
        uint16_t count = r.u16();
        if (!readRealmId || !count || count > MaxSavedPlayers) return false;
        std::vector<SavedPlayer> records;
        for (uint16_t i = 0; i < count; ++i) {
            SavedPlayer record;
            record.identity = {r.u64(), r.u64()}; record.player = readPlayer(r);
            if (saveVersion >= 2 && !readProgress(r, record.player, saveVersion)) return false;
            if (saveVersion >= 4 && !readAppearance(r, record.player)) return false;
           if (saveVersion >= 5 && !readHistory(r, record.player)) return false;
            if (saveVersion >= 6) {
                const auto seen = r.u8(); if (seen > 1) return false;
                record.player.introSeen = seen != 0;
            }
            if (saveVersion >= 10 && !readBuyback(r, record.player.buybackSerial, record.player.buyback)) return false;
            if (!r.valid || !record.identity.a || !record.identity.b) return false;
            for (const auto& other : records)
                if (other.identity == record.identity || other.player.guid == record.player.guid) return false;
            records.push_back(std::move(record));
        }
        std::vector<LocalInstanceState> loadedInstances;
        if (saveVersion >= 3) {
            const auto countInstances = r.u8(); if (countInstances > LocalGameplay::MaxInstances) return false;
            for (unsigned i = 0; i < countInstances; ++i) {
                LocalInstanceState instance; instance.id = r.u32(); instance.mapId = r.u32(); instance.groupId = r.u64();
                if (!instance.id || instance.id > 65535 || instance.mapId > 10000) return false;
                loadedInstances.push_back(instance);
            }
        }
        std::vector<LocalAuction> loadedAuctions;
        std::vector<LocalAuctionDelivery> loadedDeliveries;
        double loadedTransportTime=0;
        if (saveVersion >= 8) {
            loadedTransportTime=double(r.u64())/1000.0;
            if(loadedTransportTime>31557600000.0)return false;
            const auto n = r.u16(); if (n > LocalBotDirector::MaxAuctions) return false;
            // savedCharacters()/freeCharacterSlot() use a temporary Impl that
            // deliberately does not load world.json or the item catalog. In
            // that mode validate the auction structurally here and defer the
            // item-reference check until a real realm start has loaded content.
            const auto& world = gameplay.content();
            const bool worldReferencesAvailable = !world.sourcePath.empty() || world.catalog || !world.items.empty();
            for (unsigned i = 0; i < n; ++i) {
                auto a = readAuction(r, world, worldReferencesAvailable);
                if (!r.valid) return false;
                loadedAuctions.push_back(std::move(a));
            }
            const auto d = r.u16(); if (d > 1024) return false;
            for (unsigned i = 0; i < d; ++i) {
                LocalAuctionDelivery entry;
                entry.recipient = r.u64(); entry.itemId = r.u32(); entry.money = r.u32(); entry.count = r.u16();
                loadedDeliveries.push_back(entry);
            }
        }
        std::vector<LocalVendorStockRecord> loadedVendorStock;
        if (saveVersion >= 11) {
            const auto stockCount = r.u16();
            if (stockCount > LocalVendorInventory::MaxDepletedOffers) return false;
            loadedVendorStock.reserve(stockCount);
            for (unsigned i = 0; i < stockCount; ++i) {
                LocalVendorStockRecord row;
                row.npcGuid = r.u64(); row.entry = r.u32(); row.itemId = r.u32();
                row.remaining = r.u32(); row.elapsedMs = r.u64();
                loadedVendorStock.push_back(row);
            }
        }
        const uint32_t loadedAuctionSequence = saveVersion >= 13 ? r.u32() : 0;
        LocalMailbox loadedMail;
        if(saveVersion>=14){
            loadedMail.nextId=r.u32();const auto countMail=r.u16();if(countMail>LocalMailbox::MaxMessages)return false;
            for(unsigned i=0;i<countMail;++i)loadedMail.messages.push_back(readMail(r));
            if(!loadedMail.valid())return false;
        }
        uint32_t sum = r.u32();
        if (!r.done() || sum != checksum(bytes.data(), bytes.size() - 4)) return false;
        LocalBotDirector validated;
        std::string validationError;
        if (!validated.restoreAuctions(loadedAuctions, validationError) ||
            !validated.restoreDeliveries(loadedDeliveries) ||
            (saveVersion >= 13 && !validated.restoreAuctionSequence(loadedAuctionSequence, validationError))) return false;
        if (!gameplay.restoreVendorStock(loadedVendorStock)) return false;
        gameplay.setTransportTime(loadedTransportTime);
        botDirector.restoreAuctions(loadedAuctions, validationError);
        botDirector.restoreDeliveries(loadedDeliveries);
        mailbox=std::move(loadedMail);++mailRevision;
        if (saveVersion >= 13) botDirector.restoreAuctionSequence(loadedAuctionSequence, validationError);
        restoredInstances = std::move(loadedInstances);
        realmId = readRealmId; saved = std::move(records);
        LOG_INFO("[local_realm] Restored depleted merchant offers=", loadedVendorStock.size(), " restock=active-simulation-time");
        if (saveVersion < SaveVersion) LOG_INFO("[local_realm] Migrated realm save version ", int(saveVersion), " to ", int(SaveVersion), " preserving identity/position/appearance");
        return true;
    }
    catch (const std::exception& exception) {
        LOG_WARNING("[local_realm] Cannot parse save: ", exception.what()); return false;
    }
    bool loadRealm() {
        autosave.flush();
        const std::string path = directory + "/realm.wprs";
        std::error_code ec;
        const bool exists = std::filesystem::exists(path, ec);
        const bool backupExists = std::filesystem::exists(path + ".bak", ec);
        if (!exists && !backupExists) { realmId = uniqueId(); return true; }
        if (exists && parseSave(path)) return true;
        if (backupExists && parseSave(path + ".bak")) {
            // Preserve the known-good backup; never copy damaged primary over it.
            std::vector<uint8_t> recovered;
            if (!readFile(path + ".bak", recovered, MaxSaveSize) || !atomicWrite(path, recovered, false))
                return fail("Could not restore local realm backup");
            LOG_WARNING("[local_realm] Restored realm.wprs from valid backup");
            return true;
        }
        return fail("Local realm save is damaged; original files were preserved");
    }
    bool saveRealm(bool asynchronous = false) try {
        if (!authoritative()) return true;
        // Preserve disk ordering: an older worker snapshot must never overwrite
        // a newer explicit save, purchase or auction transaction.
        if (!asynchronous) { autosave.flush(); (void)autosave.takeFailure(); }
        if (auto* local = findSaved(self.guid)) local->player = self;
        for (const auto& record : saved) {
            if (record.player.inventory.size() > LocalGameplay::MaxInventory || record.player.quests.size() > LocalGameplay::MaxQuests ||
                record.player.knownSpells.size() > LocalGameplay::MaxSpells || record.player.cooldowns.size() > LocalGameplay::MaxCooldowns ||
                record.player.knownRecipes.size() > LocalGameplay::MaxRecipes || record.player.knownTaxiNodes.size()>512 ||
                record.player.professions.size() > LocalGameplay::MaxProfessions ||
                !validBuyback(record.player.buybackSerial, record.player.buyback) ||
                !validLocalStatAuras(record.player) || !validLocalTalents(record.player) || !validHistory(record.player.completedQuestIds) || !validLocalRunes(record.player.runeCooldownMs)) {
                if (error.empty()) error = "Cannot save invalid completed quest history";
                LOG_ERROR("[local_realm] ", error); return false;
            }
        }
        Writer w; w.u32(SaveMagic); w.u8(SaveVersion); w.u64(realmId); w.u16(uint16_t(saved.size()));
        for (const auto& record : saved) {
            w.u64(record.identity.a); w.u64(record.identity.b); writePlayer(w, record.player); writeProgress(w, record.player);
            writeAppearance(w, record.player);
            w.u32(uint32_t(record.player.completedQuestIds.size()));
            for (auto id : record.player.completedQuestIds) w.u32(id);
            w.u8(record.player.introSeen ? 1 : 0);
            writeBuyback(w, record.player.buybackSerial, record.player.buyback);
        }
        w.u8(uint8_t(gameplay.instances().size()));
        for (const auto& instance : gameplay.instances()) { w.u32(instance.id); w.u32(instance.mapId); w.u64(instance.groupId); }
        w.u64(uint64_t(gameplay.transportTime()*1000.0));
        w.u16(uint16_t(botDirector.auctions().size()));
        for (const auto& a : botDirector.auctions()) writeAuction(w, a);
        w.u16(uint16_t(botDirector.deliveries().size()));
        for (const auto& d : botDirector.deliveries()) {
            w.u64(d.recipient); w.u32(d.itemId); w.u32(d.money); w.u16(d.count);
        }
        const auto vendorStock = gameplay.savedVendorStock();
        w.u16(uint16_t(vendorStock.size()));
        for (const auto& row : vendorStock) {
            w.u64(row.npcGuid); w.u32(row.entry); w.u32(row.itemId);
            w.u32(row.remaining); w.u64(row.elapsedMs);
        }
        w.u32(botDirector.nextAuctionId());
        w.u32(mailbox.nextId);w.u16(uint16_t(mailbox.messages.size()));
        for(const auto& mail:mailbox.messages)writeMail(w,mail);
        w.u32(checksum(w.bytes.data(), w.bytes.size()));
        if (w.bytes.size() > MaxSaveSize) {
            error = "Local realm snapshot exceeds save limit";
            return false;
        }
        if (asynchronous) {
            autosave.submit(directory + "/realm.wprs", std::move(w.bytes));
            dirty = false; lastSave = now;
            LOG_DEBUG("[local_realm] Autosave snapshot queued");
            return true;
        }
        if (!atomicWrite(directory + "/realm.wprs", w.bytes, true)) {
            error = "Local realm save failed; check free space and directory permissions";
            LOG_ERROR("[local_realm] ", error);
            return false;
        }
        dirty = false; lastSave = now; error.clear();
        LOG_INFO("[local_realm] Saved realm=", realmId, " characters=", saved.size());
        return true;
    }
    catch (const std::exception& exception) {
        error = "Local realm save failed; original save preserved";
        LOG_ERROR("[local_realm] ", error, ": ", exception.what()); return false;
    }
    SavedPlayer* createPlayer(const Identity& id, const std::string& name, uint8_t race = 0, uint8_t cls = 0, uint8_t gender = 255) {
        if (saved.size() >= MaxSavedPlayers) return nullptr;
        SavedPlayer record; record.identity = id; record.player.name = name;
        record.player.introSeen = false;
        do { record.player.guid = uniqueId() & 0x0000ffffffffffffULL; }
        while (!record.player.guid || findSaved(record.player.guid));
        record.player.race = race ? race : requestedRace; record.player.classId = cls ? cls : requestedClass; record.player.gender = gender == 255 ? requestedGender : gender;
        if (!race) {
            // This console's own new character: the appearance it chose. A
            // joining peer's arrives with its Hello and is applied there.
            record.player.skin = requestedSkin; record.player.face = requestedFace; record.player.hairStyle = requestedHairStyle;
            record.player.hairColor = requestedHairColor; record.player.facialHair = requestedFacialHair; record.player.useFemaleModel = requestedFemaleModel;
        }
        gameplay.initializePlayer(record.player, true);
        saved.push_back(std::move(record)); dirty = true;
        return &saved.back();
    }
    const std::vector<LocalRealmPlayer*>& activePlayers() {
        activePlayerScratch.clear();
        activePlayerScratch.push_back(&self);
        for (const auto& peer : peers) if (auto* record = findSaved(peer.guid)) activePlayerScratch.push_back(&record->player);
        // Bots are simulated exactly like players: creatures aggro them, they
        // take damage, they die and respawn. Leaving them out here would have
        // produced characters that walk through hostile territory untouched.
        for (auto& bot : botPlayers) activePlayerScratch.push_back(&bot);
        return activePlayerScratch;
    }
    const LocalRealmPlayer* realPlayer(uint64_t guid) const {
        if (self.guid == guid) return &self;
        for (const auto& peer : peers) if (peer.guid == guid)
            for (const auto& p : saved) if (p.player.guid==guid) return &p.player;
        return nullptr;
    }
    std::vector<LocalPartyActor> partyActors() const {
        std::vector<LocalPartyActor> actors; actors.reserve(peers.size()+1);
        if (self.guid) actors.push_back({self.guid,self.race});
        for (const auto& peer : peers) if (const auto* p=realPlayer(peer.guid)) actors.push_back({p->guid,p->race});
        return actors;
    }
    static LocalPartyMember partyMember(const LocalRealmPlayer& p) {
        return {p.guid,p.name,p.mapId,p.instanceId,p.health,p.maxHealth,p.mana,p.maxMana,
            p.x,p.y,p.z,p.race,p.classId,p.level,uint8_t(p.resourceType),p.dead};
    }
    LocalPartyView partyViewFor(uint64_t guid) const {
        LocalPartyView view;
        if (const auto* party=partyDirector.party(guid)) {
            view.partyId=party->id;
            for (auto id:party->members) if (const auto* p=realPlayer(id)) view.members.push_back(partyMember(*p));
        }
        if (const auto* invite=partyDirector.invitation(guid)) if (const auto* inviter=realPlayer(invite->from)) {
            view.inviteId=invite->id;view.inviter=invite->from;view.inviterName=inviter->name;
        }
        return view;
    }
    void commitParty(LocalPartyView view) {
        if (view==localParty) return;
        bool roster=view.partyId!=localParty.partyId || view.members.size()!=localParty.members.size();
        if (!roster) for(size_t i=0;i<view.members.size();++i)
            if(view.members[i].guid!=localParty.members[i].guid){roster=true;break;}
        if(roster)++partyRosterRevision;
        localParty=std::move(view);++partyRevision;
    }
    void syncParty(bool force=false) {
        if(!authoritative() || (!force && now-lastPartyRefresh<0.2))return;
        lastPartyRefresh=now;partyDirector.prune(now,partyActors());
        gameplay.setPartyMembership(partyDirector.parties());commitParty(partyViewFor(self.guid));
    }
    bool executeParty(LocalRealmPlayer& player,const LocalRealmCommand& cmd,std::string& result) {
        const unsigned action=unsigned(cmd.action)-unsigned(LocalAction::PartyInvite);
        if(action>5 || cmd.bid || cmd.buyout || cmd.durationMinutes || cmd.serviceNpcGuid || cmd.bankSourceCount || cmd.bankDestinationCount ||
            ((action==1 || action==2) ? cmd.target!=0 : cmd.id!=0) || (action==3 && cmd.target)) {
            result="Invalid party command";return false;
        }
        const bool ok=partyDirector.execute(LocalPartyAction(action),player.guid,cmd.target,cmd.id,now,partyActors(),result);
        syncParty(true);
        LOG_INFO("[LOCAL_PARTY] player=",player.guid," action=",action," target=",cmd.target," invite=",cmd.id," ok=",ok," result=",result);
        return ok;
    }
    void sendParty(Peer& peer) {
        syncParty();const auto view=partyViewFor(peer.guid);
        Writer w;if(!++partyTick)++partyTick;w.u32(partyTick);
        w.u32(view.partyId);w.u32(view.inviteId);w.u64(view.inviter);w.name(view.inviterName);
        w.u8(uint8_t(view.members.size()));
        for(const auto& m:view.members){
            w.u64(m.guid);w.name(m.name);w.u32(m.mapId);w.u32(m.instanceId);
            w.u32(m.health);w.u32(m.maxHealth);w.u32(m.power);w.u32(m.maxPower);
            w.f32(m.x);w.f32(m.y);w.f32(m.z);
            w.u8(m.race);w.u8(m.classId);w.u8(m.level);w.u8(m.powerType);w.u8(m.dead?1:0);
        }
        send(Message::PartyState,peer.session,w,peer.address);peer.lastPartySnapshot=now;
    }
    void receiveParty(Reader& r) {
        const auto tick=r.u32();if(!tick || !newer(tick,partySequence))return;
        LocalPartyView view;view.partyId=r.u32();view.inviteId=r.u32();view.inviter=r.u64();view.inviterName=r.name();
        const auto count=r.u8();
        if(count>LocalPartyDirector::MaxMembers || (view.partyId ? count<2 : count!=0) ||
            (view.inviteId ? view.partyId || !view.inviter || view.inviter==self.guid || view.inviter>0x0000ffffffffffffULL || !validName(view.inviterName) : view.inviter || !view.inviterName.empty()))return;
        bool own=false;
        for(unsigned i=0;i<count;++i){
            LocalPartyMember m;m.guid=r.u64();m.name=r.name();m.mapId=r.u32();m.instanceId=r.u32();
            m.health=r.u32();m.maxHealth=r.u32();m.power=r.u32();m.maxPower=r.u32();
            m.x=r.f32();m.y=r.f32();m.z=r.f32();m.race=r.u8();m.classId=r.u8();m.level=r.u8();m.powerType=r.u8();
            const auto dead=r.u8();m.dead=dead!=0;
            if(!m.guid || m.guid>0x0000ffffffffffffULL || !validName(m.name) || !validPosition(m.mapId,m.x,m.y,m.z,0) ||
                m.instanceId>65535 || !LocalGameplay::validCharacterOptions(m.race,m.classId,0) || !m.level || m.level>80 ||
                !m.maxHealth || m.maxHealth>1000000 || m.health>m.maxHealth || m.maxPower>1000000 || m.power>m.maxPower ||
                dead>1 || m.dead!=(m.health==0) || (m.powerType!=0 && m.powerType!=1 && m.powerType!=3 && m.powerType!=6) ||
                std::any_of(view.members.begin(),view.members.end(),[&](const auto& p){return p.guid==m.guid;}))return;
            own=own || m.guid==self.guid;view.members.push_back(std::move(m));
        }
        if(!r.done() || (count && !own))return;
        partySequence=tick;lastSeen=now;commitParty(std::move(view));
    }
    std::vector<LocalChatActor> chatActors() const {
        std::vector<LocalChatActor> actors;actors.reserve(peers.size()+1);
        auto append=[&](const LocalRealmPlayer& p,bool loading){
            const auto* party=partyDirector.party(p.guid);
            actors.push_back({p.guid,p.name,p.mapId,p.instanceId,party?party->id:0,p.race,p.x,p.y,p.z,loading,p.dead});
        };
        append(self,worldLoading);
        for(const auto& peer:peers)for(const auto& savedPlayer:saved)if(savedPlayer.player.guid==peer.guid){append(savedPlayer.player,peer.loading);break;}
        return actors;
    }
    bool routeChat(uint64_t sender,LocalChatChannel channel,const std::string& text,const std::string& target,
            LocalChatRate& rate,std::string& result) {
        try {
            const auto actors=chatActors();
            const auto route=routeLocalChat(actors,sender,channel,text,target);
            if(!route.error.empty()){result=route.error;return false;}
            if(!rate.consume(now)){result="Chat is throttled; wait a moment";return false;}
            const auto from=std::find_if(actors.begin(),actors.end(),[&](const auto& a){return a.guid==sender;});
            LocalChatLine line{channel,sender,from->name,route.receiver,text};
            struct Pending {Peer* peer;std::vector<ChatDelivery> messages;};
            std::vector<Pending> staged;staged.reserve(route.recipients.size());
            auto inbox=chatInbox;
            for(const auto id:route.recipients) {
                auto delivery=line;
                if(channel==LocalChatChannel::Whisper && id==sender)delivery.channel=LocalChatChannel::WhisperInform;
                if(id==self.guid) {
                    if(delivery.channel!=LocalChatChannel::WhisperInform && ignored(delivery.senderName))continue;
                    if(inbox.size()>=128){result="A recipient chat queue is full; try again later";return false;}
                    inbox.push_back(std::move(delivery));continue;
                }
                auto peer=std::find_if(peers.begin(),peers.end(),[&](const auto& p){return p.guid==id;});
                if(peer==peers.end() || peer->chatOut.size()>=32 || peer->chatSerial==UINT32_MAX){result="A recipient chat queue is full; try again later";return false;}
                auto messages=peer->chatOut;
                messages.push_back({peer->chatSerial+1,std::move(delivery),-1});
                staged.push_back({&*peer,std::move(messages)});
            }
            // Every fallible copy completes before any recipient owns the line.
            result="Chat accepted";
            LOG_INFO("[LOCAL_CHAT] accepted sender=",sender," channel=",unsigned(channel)," recipients=",route.recipients.size()," bytes=",text.size());
            chatInbox.swap(inbox);
            for(auto& item:staged){item.peer->chatOut.swap(item.messages);++item.peer->chatSerial;}
            return true;
        } catch(const std::bad_alloc&) {result="Not enough memory to queue chat";return false;}
    }
    void receiveChatRequest(Peer& peer,Reader& r) {
        const auto id=r.u32();const auto channel=LocalChatChannel(r.u8());const auto target=r.name();const auto text=r.text();
        if(!r.done() || !id)return;
        if(id==peer.lastChat) {
            Writer w;w.u32(id);w.u8(peer.lastChatSuccess?1:0);w.text(peer.lastChatStatus);send(Message::ChatResult,peer.session,w,peer.address);return;
        }
        if(peer.lastChat==UINT32_MAX || id!=peer.lastChat+1)return;
        peer.lastChatSuccess=routeChat(peer.guid,channel,text,target,peer.chatRate,peer.lastChatStatus);
        peer.lastChat=id;peer.lastSeen=now;
        Writer w;w.u32(id);w.u8(peer.lastChatSuccess?1:0);w.text(peer.lastChatStatus);send(Message::ChatResult,peer.session,w,peer.address);
    }
    void receiveChatDelivery(Reader& r) {
        const auto id=r.u32();LocalChatLine line;line.channel=LocalChatChannel(r.u8());line.sender=r.u64();
        line.senderName=r.name();line.receiverName=r.name();line.text=r.text();
        if(!r.done() || !id || !line.sender || !validName(line.senderName) || !validLocalChatText(line.text))return;
        if(line.channel!=LocalChatChannel::Say && line.channel!=LocalChatChannel::Party && line.channel!=LocalChatChannel::Yell &&
           line.channel!=LocalChatChannel::Whisper && line.channel!=LocalChatChannel::WhisperInform)return;
        const bool whisper=line.channel==LocalChatChannel::Whisper || line.channel==LocalChatChannel::WhisperInform;
        if(whisper && !validName(line.receiverName))return;
        if(!whisper && !line.receiverName.empty())return;
        if(line.channel==LocalChatChannel::Whisper && !localChatNameEqual(line.receiverName,self.name))return;
        if(line.channel==LocalChatChannel::WhisperInform && line.sender!=self.guid)return;
        if(id>chatReceived) {
            if(chatReceived==UINT32_MAX || id!=chatReceived+1 || (chatInbox.size()>=128 && (line.channel==LocalChatChannel::WhisperInform || !ignored(line.senderName))))return;
            try{if(line.channel==LocalChatChannel::WhisperInform || !ignored(line.senderName))chatInbox.push_back(std::move(line));}catch(const std::bad_alloc&){return;}
            chatReceived=id;
        }
        Writer w;w.u32(id);send(Message::ChatAck,session,w,host);lastSeen=now;
    }
    void pumpChat() {
        if(state==LocalRealmState::Connected && !chatPending.empty()) {
            auto& request=chatPending.front();
            if(now-request.lastSent>=0.5) {
                Writer w;w.u32(request.id);w.u8(uint8_t(request.channel));w.name(request.target);w.text(request.text);
                send(Message::ChatRequest,session,w,host);request.lastSent=now;
            }
        }
        if(state!=LocalRealmState::Hosting)return;
        const auto count=peers.size();size_t sent=0;
        for(size_t i=0;i<count && sent<4;++i) {
            chatCursor%=count;auto& peer=peers[chatCursor++];
            if(peer.chatOut.empty())continue;
            auto& delivery=peer.chatOut.front();if(now-delivery.lastSent<0.5)continue;
            Writer w;w.u32(delivery.id);w.u8(uint8_t(delivery.line.channel));w.u64(delivery.line.sender);
            w.name(delivery.line.senderName);w.name(delivery.line.receiverName);w.text(delivery.line.text);
            send(Message::ChatDelivery,peer.session,w,peer.address);delivery.lastSent=now;++sent;
        }
    }

    void retainConfiguration(const Impl& previous) {
        realmName = previous.realmName;
        botDirector.setEnabled(previous.botDirector.enabled());
        botDirector.setBotCount(previous.botDirector.botCount());
        gameplay.useContent(previous.gameplay.sharedContent());
        requestedRace = previous.requestedRace; requestedClass = previous.requestedClass; requestedGender = previous.requestedGender; characterSlot = previous.characterSlot;
        requestedSkin = previous.requestedSkin; requestedFace = previous.requestedFace; requestedHairStyle = previous.requestedHairStyle;
        requestedHairColor = previous.requestedHairColor; requestedFacialHair = previous.requestedFacialHair; requestedFemaleModel = previous.requestedFemaleModel;
        std::string ignored;
        gameplay.setAreaTriggers(previous.gameplay.areaTriggers(), ignored);
        gameplay.setFactionTemplates(previous.gameplay.factionTemplates(), previous.gameplay.raceFactionTemplates(), ignored);
        // Everything else the application reads out of the client's own DBCs
        // before a realm starts. Starting one replaces this Impl wholesale, so
        // anything not carried across here is silently lost - which is what had
        // been happening to the taxi network: it is installed once, before the
        // first start, and every flight master and transport disappeared with
        // it the moment the world opened.
        gameplay.useTravelNetwork(previous.gameplay.travel());
        gameplay.setClientMaps(previous.gameplay.clientMaps(), ignored);
        // skillLines() answers with the built-in professions when the client's
        // own rows were never installed, so re-installing what it returns is
        // exactly what the previous realm was using either way.
        gameplay.setSkillLines(previous.gameplay.skillLines(), ignored);
    }
    void prepareHistory(Peer& peer, const LocalRealmPlayer& player) {
        // Completed history only grows during a session. Save migration happens
        // before peers exist, so count changes are an exact revision trigger.
        const auto count = uint32_t(player.completedQuestIds.size());
        if (peer.historyRevision && peer.historyCount == count) return;
        if (!++peer.historyRevision) ++peer.historyRevision;
        peer.historyCount = count; peer.historyCursor = 0;
        const size_t pages = std::max(size_t(1), (count + HistoryPageEntries - 1) / HistoryPageEntries);
        peer.historyAcked.assign(pages, false); peer.historySent.assign(pages, -1);
    }
    void history(Peer& peer) {
        const auto* record = findSaved(peer.guid); if (!record) return;
        prepareHistory(peer, record->player);
        const auto& ids = record->player.completedQuestIds;
        size_t sent = 0;
        for (size_t scanned = 0; scanned < peer.historyAcked.size() && sent < 4; ++scanned) {
            const size_t page = peer.historyCursor++ % peer.historyAcked.size();
            if (peer.historyAcked[page] || now - peer.historySent[page] < 0.3) continue;
            const size_t begin = page * HistoryPageEntries, end = std::min(begin + HistoryPageEntries, ids.size());
            Writer w; w.u64(peer.guid); w.u32(peer.historyRevision); w.u32(peer.historyCount);
            w.u16(uint16_t(page)); w.u16(uint16_t(end - begin));
            for (size_t i = begin; i < end; ++i) w.u32(ids[i]);
            send(Message::History, peer.session, w, peer.address);
            peer.historySent[page] = now; ++sent;
        }
    }
    void progress(Peer& peer) {
        const auto* record = findSaved(peer.guid); if (!record) return;
        prepareHistory(peer, record->player);
        Writer w; w.u64(peer.guid); w.u32(peer.historyRevision); w.u32(peer.historyCount);
        writePosition(w, record->player); writeProgress(w, record->player); writeCast(w, record->player);writeHealingViews(w,record->player);
        w.u8(record->player.introSeen ? 1 : 0);
        if (w.bytes.size() > MaxOwnerProgressBytes) { LOG_ERROR("[LOCAL_PROGRESS] snapshot exceeds bound"); return; }
        const auto parts = uint8_t((w.bytes.size() + ProgressChunkBytes - 1) / ProgressChunkBytes);
        const uint32_t snapshot = sequence + parts;
        for (uint8_t part = 0; part < parts; ++part) {
            const size_t begin = part * ProgressChunkBytes, length = std::min(ProgressChunkBytes, w.bytes.size()-begin);
            Writer page; page.u32(snapshot); page.u8(part); page.u8(parts);
            page.u16(uint16_t(w.bytes.size())); page.u16(uint16_t(length));
            page.bytes.insert(page.bytes.end(),w.bytes.begin()+begin,w.bytes.begin()+begin+length);
            send(Message::Progress,peer.session,page,peer.address);
        }
    }
    void clearWorldForTravel(const LocalRealmPlayer& next) {
        if(next.mapId==self.mapId && next.instanceId==self.instanceId && next.positionRevision==self.positionRevision)return;
        gameplay.setRemoteNpcs({});collectingWorld=0;worldParts=0;worldReceived.fill(false);
        for(auto& page:worldChunks)page.clear();
        LOG_INFO("[LOCAL_TRAVEL_SYNC] cleared NPC view map=",next.mapId," instance=",next.instanceId," revision=",next.positionRevision);
    }
    void applyProgress(LocalRealmPlayer updated, uint32_t seq) {
        if (!newer(seq, progressSequence)) return;
        for (const auto& quest : updated.quests)
            if (std::binary_search(self.completedQuestIds.begin(), self.completedQuestIds.end(), quest.id)) return;
        // History pages are committed atomically, independent of unreliable
        // public snapshots and owner progress. Never restore an older copy.
        updated.completedQuestIds = self.completedQuestIds;
        if(newer(self.positionRevision,updated.positionRevision)) {
            updated.hasInstanceReturn=self.hasInstanceReturn;updated.returnMapId=self.returnMapId;updated.returnInstanceId=self.returnInstanceId;
            updated.returnX=self.returnX;updated.returnY=self.returnY;updated.returnZ=self.returnZ;updated.returnOrientation=self.returnOrientation;
            updated.flight=self.flight;updated.transportEntry=self.transportEntry;
            updated.transportOffsetX=self.transportOffsetX;updated.transportOffsetY=self.transportOffsetY;updated.transportOffsetZ=self.transportOffsetZ;
            updated.transportLastYaw=self.transportLastYaw;
            updated.castingSpellId=0;updated.castTarget=0;updated.castRemainingMs=updated.castTotalMs=0;updated.castStatus=LocalCastStatus::Interrupted;
        }
        if (updated.positionRevision == self.positionRevision || !newer(updated.positionRevision, self.positionRevision)) {
            updated.mapId = self.mapId; updated.x = self.x; updated.y = self.y; updated.z = self.z;
            updated.orientation = self.orientation; updated.positionRevision = self.positionRevision;
            updated.instanceId = self.instanceId;
            if(updated.transportEntry && updated.transportEntry==self.transportEntry) {
                updated.transportOffsetX=self.transportOffsetX;updated.transportOffsetY=self.transportOffsetY;
                updated.transportOffsetZ=self.transportOffsetZ;updated.transportLastYaw=self.transportLastYaw;
            }
        }
        if (!newer(seq, vitalsSequence)) {
            updated.health = self.health; updated.maxHealth = self.maxHealth; updated.mana = self.mana;
            updated.maxMana = self.maxMana; updated.dead = self.dead; updated.level = self.level;
            updated.equipment = self.equipment; updated.attackTarget = self.attackTarget;
            updated.resourceType = self.resourceType;
            updated.mountSpellId = self.mountSpellId;
            updated.xpToLevel = uint32_t(updated.level) * uint32_t(updated.level) * 100 + 300;
        } else vitalsSequence = seq;
        clearWorldForTravel(updated);
        self = std::move(updated); progressSequence = seq; lastSeen = now;
        for (auto& p : players) if (p.guid == self.guid) p = self;
    }
    void commitHistoryProgress() {
        if (!pendingProgress || pendingProgress->historyRevision != collectingHistory ||
            pendingProgress->historyCount != collectingHistoryCount || historyReceived.empty() ||
            !std::all_of(historyReceived.begin(), historyReceived.end(), [](bool value) { return value; }) ||
            !validHistory(historyIds) ||
            !std::includes(historyIds.begin(), historyIds.end(), self.completedQuestIds.begin(), self.completedQuestIds.end())) return;
        for (const auto& quest : pendingProgress->player.quests)
            if (std::binary_search(historyIds.begin(), historyIds.end(), quest.id)) return;
        self.completedQuestIds = std::move(historyIds); historyRevision = collectingHistory;
        collectingHistory = 0; historyReceived.clear();
        auto pending = std::move(*pendingProgress); pendingProgress.reset();
        applyProgress(std::move(pending.player), pending.sequence);
    }
    void receiveHistory(Reader& r) {
        const auto guid = r.u64(); const auto revision = r.u32(), count = r.u32();
        const auto page = r.u16(), entries = r.u16();
        const size_t pages = std::max(size_t(1), (size_t(count) + HistoryPageEntries - 1) / HistoryPageEntries);
        if (guid != self.guid || !revision || count > LocalGameplay::MaxCompletedQuests ||
            pages > MaxHistoryPages || page >= pages || entries > HistoryPageEntries ||
            size_t(entries) != std::min(HistoryPageEntries, size_t(count) - page * HistoryPageEntries)) return;
        std::vector<uint32_t> chunk; chunk.reserve(entries);
        for (unsigned i = 0; i < entries; ++i) chunk.push_back(r.u32());
        if (!r.done() || !validHistory(chunk)) return;
        if (revision == historyRevision) {
            // A lost ACK causes a retransmission after the completed buffer was
            // released. Verify it against the committed history before ACKing.
            if (count != self.completedQuestIds.size() ||
                !std::equal(chunk.begin(), chunk.end(), self.completedQuestIds.begin() + page * HistoryPageEntries)) return;
        } else {
            if (historyRevision && !newer(revision, historyRevision)) return;
            if (revision != collectingHistory) {
                if (collectingHistory && !newer(revision, collectingHistory)) return;
                collectingHistory = revision; collectingHistoryCount = count;
                historyIds.assign(count, 0); historyReceived.assign(pages, false);
            }
            if (count != collectingHistoryCount || pages != historyReceived.size()) return;
            const size_t begin = page * HistoryPageEntries;
            if (historyReceived[page] && !std::equal(chunk.begin(), chunk.end(), historyIds.begin() + begin)) return;
            std::copy(chunk.begin(), chunk.end(), historyIds.begin() + begin); historyReceived[page] = true;
            if (std::all_of(historyReceived.begin(), historyReceived.end(), [](bool value) { return value; })) {
                if (!validHistory(historyIds)) { historyReceived.assign(pages, false); return; }
                // Completion is monotonic; a new revision cannot remove IDs.
                if (!std::includes(historyIds.begin(), historyIds.end(), self.completedQuestIds.begin(), self.completedQuestIds.end())) return;
                // Keep fully received pages until the matching owner progress
                // is available; active log and completed history change together.
                commitHistoryProgress();
            }
        }
        Writer ack; ack.u32(revision); ack.u16(page); send(Message::HistoryAck, session, ack, host); lastSeen = now;
    }
    void world(const Peer& peer, uint32_t tick) {
        // Each independently checked datagram fits below Ethernet's usual MTU.
        // Bounded pages are assembled atomically by the client.
        std::vector<LocalRealmNpc> actors;
        const auto* player = findSaved(peer.guid); if (!player) return;
        for (const auto& npc : gameplay.npcs()) if (npc.mapId == player->player.mapId && npc.instanceId == player->player.instanceId) {
            auto copy = npc; copy.hostile = gameplay.canAttack(player->player, npc); copy.aggressive = gameplay.isAggressive(player->player, npc);
            actors.push_back(std::move(copy));
        }
        const size_t parts = std::max(size_t(1), (actors.size() + NpcsPerPage - 1) / NpcsPerPage);
        for (size_t part = 0; part < parts; ++part) {
            Writer w; w.u32(tick); w.u8(uint8_t(part)); w.u8(uint8_t(parts));
            const size_t begin = part * NpcsPerPage, end = std::min(begin + NpcsPerPage, actors.size());
            w.u8(uint8_t(end - begin));
            w.u32(player->player.mapId);w.u32(player->player.instanceId);w.u32(player->player.positionRevision);
            for (size_t index = begin; index < end; ++index) writeNpc(w, actors[index]);
            send(Message::Npcs, peer.session, w, peer.address);
        }
    }
    void actionResult(const Peer& peer) {
        Writer w; w.u32(peer.lastCommand); w.u8(peer.lastCommandSuccess ? 1 : 0); w.text(peer.lastCommandStatus);
        send(Message::ActionResult, peer.session, w, peer.address);
    }
    void merchantState(Peer& peer) {
        const auto* record = findSaved(peer.guid);
        if (!record || !peer.merchantGuid || !peer.merchantRequest || now - peer.lastMerchantQuery > 3.0) return;
        const auto& player = record->player;
        const bool allowed = !player.dead && !player.flight.active &&
            gameplay.serviceNpc(player, kLocalNpcFlagAnyVendor, peer.merchantGuid);
        const auto stock = allowed ? gameplay.vendorStock(player, peer.merchantGuid) : std::vector<uint32_t>{};
        if (stock.size() > MerchantOffersPerPage * MaxMerchantPages) return;
        const size_t parts = std::max(size_t(1), (stock.size() + MerchantOffersPerPage - 1) / MerchantOffersPerPage);
        const uint32_t tick = ++merchantTick ? merchantTick : ++merchantTick;
        for (size_t part = 0; part < parts; ++part) {
            const auto begin = part * MerchantOffersPerPage, end = std::min(begin + MerchantOffersPerPage, stock.size());
            Writer w; w.u32(peer.merchantRequest); w.u64(peer.merchantGuid); w.u32(tick);
            w.u8(allowed ? 1 : 0); w.u8(uint8_t(part)); w.u8(uint8_t(parts)); w.u8(uint8_t(end - begin));
            writeBuyback(w, allowed ? player.buybackSerial : 0, allowed ? player.buyback : std::vector<LocalMerchantBuyback>{});
            for (size_t i = begin; i < end; ++i) {
                w.u32(stock[i]); w.u32(uint32_t(gameplay.vendorRemaining(player, stock[i], peer.merchantGuid)));
            }
            send(Message::MerchantState, peer.session, w, peer.address);
        }
        peer.lastMerchantSnapshot = now;
    }
    void receiveMerchantQuery(Peer& peer, Reader& r) {
        const auto request = r.u32(); const auto guid = r.u64();
        if (!r.done() || !request || (request != peer.merchantRequest && !newer(request, peer.merchantRequest)) ||
            (request == peer.merchantRequest && guid != peer.merchantGuid)) return;
        // Bound work even when a connected owner floods valid queries. A
        // changed selection will be retried by its normal one-second refresh.
        if (peer.lastMerchantQuery >= 0 && now - peer.lastMerchantQuery < 0.1) return;
        peer.merchantRequest = request; peer.merchantGuid = guid;
        peer.lastMerchantQuery = now; peer.lastSeen = now;
        merchantState(peer);
    }
    void receiveMerchantState(Reader& r) {
        const auto request = r.u32(); const auto guid = r.u64(); const auto tick = r.u32();
        const auto allowed = r.u8(), part = r.u8(), parts = r.u8(), count = r.u8();
        if (request != merchantRequest || !guid || guid != merchantGuid || !tick || !newer(tick, merchantSequence) ||
            allowed > 1 || !parts || parts > MaxMerchantPages || part >= parts || count > MerchantOffersPerPage ||
            (part + 1 < parts && count != MerchantOffersPerPage) || (parts > 1 && !count)) return;
        uint32_t serial = 0; std::vector<LocalMerchantBuyback> buyback;
        if (!readBuyback(r, serial, buyback) || (!allowed && (count || serial || !buyback.empty() || parts != 1))) return;
        for (const auto& row : buyback) if (!gameplay.content().item(row.itemId)) return;
        std::vector<MerchantOfferState> chunk;
        for (unsigned i = 0; i < count; ++i) {
            const auto item = r.u32(), value = r.u32();
            if (!item || !gameplay.content().item(item) || (value > INT32_MAX && value != UINT32_MAX)) return;
            chunk.push_back({item, value == UINT32_MAX ? -1 : int32_t(value)});
        }
        if (!r.done()) return;
        if (tick != collectingMerchant) {
            if (collectingMerchant && !newer(tick, collectingMerchant)) return;
            collectingMerchant = tick; merchantParts = parts; merchantReceived.fill(false);
            collectingMerchantAllowed = allowed != 0; collectingBuybackSerial = serial; collectingBuyback = buyback;
            for (auto& rows : merchantChunks) rows.clear();
        }
        if (parts != merchantParts || collectingMerchantAllowed != (allowed != 0) ||
            collectingBuybackSerial != serial || collectingBuyback != buyback) return;
        merchantChunks[part] = std::move(chunk); merchantReceived[part] = true;
        std::vector<MerchantOfferState> all;
        for (unsigned i = 0; i < parts; ++i) {
            if (!merchantReceived[i]) return;
            for (const auto& row : merchantChunks[i]) {
                for (const auto& previous : all) if (previous.itemId == row.itemId) return;
                all.push_back(row);
            }
        }
        remoteMerchant = std::move(all); remoteBuyback = std::move(buyback);
        merchantSequence = tick; lastSeen = now;
    }
    /// Push the auction board to one guest, paged. Sent from the same place the
    /// NPC list is, at the same rate: the board changes when a bot lists or a
    /// listing expires, both of which a browsing player needs to see.
    void auctionBoard(const Peer& peer, uint32_t tick) {
        const auto& board = botDirector.auctions();
        const size_t parts = std::max(size_t(1), (board.size() + AuctionsPerPage - 1) / AuctionsPerPage);
        for (size_t part = 0; part < parts; ++part) {
            Writer w; w.u32(tick); w.u8(uint8_t(part)); w.u8(uint8_t(parts));
            const size_t begin = part * AuctionsPerPage;
            const size_t end = std::min(begin + AuctionsPerPage, board.size());
            w.u8(uint8_t(end - begin));
            for (size_t index = begin; index < end; ++index) writeAuction(w, board[index]);
            send(Message::Auctions, peer.session, w, peer.address);
        }
    }
    void receiveAuctions(Reader& r) {
        const auto tick = r.u32(); const auto part = r.u8(), parts = r.u8(), count = r.u8();
        if (!tick || !parts || parts > MaxAuctionPages || part >= parts ||
            count > AuctionsPerPage || !newer(tick, auctionSequence)) return;
        std::vector<LocalAuction> chunk;
        for (unsigned index = 0; index < count; ++index) {
            auto a = readAuction(r, gameplay.content()); if (!r.valid) return; chunk.push_back(std::move(a));
        }
        if (!r.done()) return;
        if (tick != collectingAuctions) {
            if (collectingAuctions && !newer(tick, collectingAuctions)) return;
            collectingAuctions = tick; auctionParts = parts; auctionReceived.fill(false);
            for (auto& entries : auctionChunks) entries.clear();
        }
        if (parts != auctionParts) return;
        auctionChunks[part] = std::move(chunk); auctionReceived[part] = true;
        for (unsigned index = 0; index < parts; ++index) if (!auctionReceived[index]) return;
        std::vector<LocalAuction> all;
        for (unsigned index = 0; index < parts; ++index) for (auto& a : auctionChunks[index]) {
            // Ids identify a listing for the whole of its life; two with the
            // same id would make bid and buyout ambiguous.
            for (const auto& prior : all) if (prior.id == a.id) return;
            all.push_back(std::move(a));
        }
        if (all.size() > LocalBotDirector::MaxAuctions) return;
        remoteAuctions = std::move(all); auctionSequence = tick; lastSeen = now;
    }
    /// Run one command against the authority. Every action a client can send
    /// arrives here, whether it came over the wire from a guest or straight
    /// from the host's own interface, so the rules are applied once.
    ///
    /// The auction actions are separated out because the board is not part of
    /// the gameplay ruleset: it belongs to the bot director, which owns the
    /// listings and the gold that moves between them.
#include "local_mail_authority.inc"
    bool runCommand(LocalRealmPlayer& player, const LocalRealmCommand& cmd, std::string& result) {
        if(cmd.action>=LocalAction::ReadyStart && cmd.action<=LocalAction::TradeCancel)return executeSocial(player,cmd,result);
        maintainSocial();
        if(activeTrade(player.guid) && cmd.action!=LocalAction::StopAttack && cmd.action!=LocalAction::CancelCast){result="Finish or cancel the trade first";return false;}
        if(mailActionKind(cmd.action))return executeMail(player,cmd,result);
        const bool portal=(cmd.action==LocalAction::EnterPortal && !localScriptedPortal(cmd.id)) || cmd.action==LocalAction::LeaveInstance ||
            cmd.action==LocalAction::ReturnHome || cmd.action==LocalAction::SetHome ||
            cmd.action==LocalAction::BoardTransport || cmd.action==LocalAction::LeaveTransport;
        const bool merchant=cmd.action==LocalAction::SellToVendor || cmd.action==LocalAction::BuyFromVendor || cmd.action==LocalAction::BuybackItem;
        const bool auction=cmd.action==LocalAction::ListAuction || cmd.action==LocalAction::CancelAuction ||
            cmd.action==LocalAction::BidAuction || cmd.action==LocalAction::BuyoutAuction;
        const auto* mountItem=cmd.action==LocalAction::UseItem?localAuctionMetadata(cmd.id):nullptr;
        const bool mountLearning=mountItem && mountItem->mountSpell;
        const bool financial=cmd.action==LocalAction::LearnTalent || cmd.action==LocalAction::ResetTalents || cmd.action==LocalAction::TrainRiding || cmd.action==LocalAction::DiscoverTaxi || cmd.action==LocalAction::TakeFlight || cmd.action==LocalAction::BankDepositFromSlot || cmd.action==LocalAction::BackpackMove || cmd.action==LocalAction::BankWithdrawSlot || cmd.action==LocalAction::EquipItem || cmd.action==LocalAction::UnequipItem || mountLearning || cmd.action==LocalAction::Loot || cmd.action==LocalAction::TurnInQuest || merchant || auction || cmd.action==LocalAction::BankDeposit || cmd.action==LocalAction::BankWithdraw || cmd.action==LocalAction::BankMove || cmd.action==LocalAction::BankDepositSlot ||
            cmd.action==LocalAction::CraftItem || cmd.action==LocalAction::UnlearnProfession ||
            cmd.action==LocalAction::LearnRecipe || cmd.action==LocalAction::LearnProfession ||
            cmd.action==LocalAction::TrainProfessionRank || cmd.action==LocalAction::LearnSpell;
        if(!financial && !portal)return executeCommand(player,cmd,result);
        // Persist ownership, escrow and bags as one save before acknowledging a
        // successful transaction. Roll back RAM too when the atomic write fails.
        const auto priorPlayer=player;
        std::optional<std::vector<LocalInstanceState>> priorInstances;
        if(portal)priorInstances.emplace(gameplay.instances());
        bool priorLootable=false;
        // Loot may credit several wallets. Capture just their copper values,
        // not copies of every inventory/quest history in the active realm.
        std::vector<std::pair<LocalRealmPlayer*,uint32_t>> priorLootMoney;
        if(cmd.action==LocalAction::Loot)for(auto* member:activePlayers())
            priorLootMoney.emplace_back(member,member->money);
        if(cmd.action==LocalAction::Loot)for(const auto& n:gameplay.npcs())if(n.guid==cmd.target)priorLootable=n.lootable;
        std::optional<LocalBotDirector> priorDirector;
        if(auction)priorDirector.emplace(botDirector);
        std::optional<LocalVendorInventory> priorStock;
        if(cmd.action==LocalAction::BuyFromVendor)priorStock.emplace(gameplay.vendorInventorySnapshot());
        const auto* localRecord=findSaved(self.guid);
        const auto priorSavedSelf=localRecord?localRecord->player:LocalRealmPlayer{};
        const bool bankAction=cmd.action==LocalAction::BankMove || cmd.action==LocalAction::BankDepositSlot || cmd.action==LocalAction::BankDeposit || cmd.action==LocalAction::BankWithdraw;
        if(!executeCommand(player,cmd,result)) {
            if(bankAction)LOG_INFO("[LOCAL_BANK] rejected player=",player.guid," action=",int(cmd.action),
                " source=",cmd.id," quantity=",cmd.target," result=",result);
            return false;
        }
        if(saveRealm()) {
            if(cmd.action==LocalAction::TrainRiding || cmd.action==LocalAction::DiscoverTaxi || cmd.action==LocalAction::TakeFlight)
                LOG_INFO("[LOCAL_TRAVEL_TRANSACTION] saved player=",player.guid," action=",int(cmd.action)," riding=",player.ridingSkill," nodes=",player.knownTaxiNodes.size()," flight=",player.flight.pathId);
            if(mountLearning)LOG_INFO("[LOCAL_MOUNT_LEARN] saved player=",player.guid," item=",cmd.id," spell=",mountItem->mountSpell);
            if(portal)LOG_INFO("[LOCAL_PARTY_INSTANCE] saved player=",player.guid," map=",player.mapId," instance=",player.instanceId);
            if(cmd.action==LocalAction::Loot)LOG_INFO("[LOCAL_GROUP_MONEY] saved npc=",cmd.target," collector=",player.guid);
            if(auction)LOG_INFO("[LOCAL_AUCTION] saved player=",player.guid," action=",int(cmd.action)," id=",cmd.id," next=",botDirector.nextAuctionId()," pending=",botDirector.deliveries().size());
            if(bankAction)LOG_INFO("[LOCAL_BANK] saved player=",player.guid," action=",int(cmd.action),
                " source=",cmd.id," destination=",(cmd.action==LocalAction::BankMove || cmd.action==LocalAction::BankDepositSlot)?cmd.buyout:0," quantity=",cmd.target);
            return true;
        }
        player=priorPlayer;
        if(priorInstances) {
            std::string restoreError;
            if(!gameplay.restoreInstances(*priorInstances,restoreError))LOG_ERROR("[LOCAL_PARTY_INSTANCE] rollback failed: ",restoreError);
        }
        for(const auto& [member,money]:priorLootMoney)member->money=money;
        if(cmd.action==LocalAction::Loot)gameplay.restoreLootable(cmd.target,priorLootable);
        if(priorDirector)botDirector=std::move(*priorDirector);
        if(priorStock)gameplay.restoreVendorInventory(std::move(*priorStock));
        if(auto* restored=findSaved(self.guid))restored->player=priorSavedSelf;
        result=portal ? "Travel was not saved; you remain at your previous location" : "Transaction was not saved; no items or money were changed";
        LOG_ERROR("[LOCAL_TRANSACTION] rolled back: ",error);
        return false;
    }
    bool executeCommand(LocalRealmPlayer& player, const LocalRealmCommand& cmd, std::string& result) {
        if(cmd.action==LocalAction::EnterPortal)syncParty(true);
        if(cmd.action>=LocalAction::PartyInvite && cmd.action<=LocalAction::PartyPromote)
            return executeParty(player,cmd,result);
        const bool auctionAction = cmd.action == LocalAction::BuyoutAuction || cmd.action == LocalAction::BidAuction ||
            cmd.action == LocalAction::ListAuction || cmd.action == LocalAction::CancelAuction;
        if (auctionAction) {
            // Service access is relative to this player's faction, including
            // LAN guests whose race differs from the host's.
            if (!gameplay.serviceNpc(player,kLocalNpcFlagAuctioneer,cmd.serviceNpcGuid) || player.dead || player.flight.active) {
                result="Stand at a friendly auctioneer"; return false;
            }
        }
        switch (cmd.action) {
        case LocalAction::CancelAuction:
            return botDirector.cancelAuction(cmd.id, player, result);
        case LocalAction::BuyoutAuction:
            return botDirector.buyout(cmd.id, player, gameplay.content(), result);
        case LocalAction::BidAuction:
            // A bid above 32 bits is not a bid, it is a malformed packet.
            if (cmd.target > 0xffffffffULL) { result = "Bid out of range"; return false; }
            return botDirector.placeBid(cmd.id, player, uint32_t(cmd.target), result);
        case LocalAction::ListAuction:
            if (!cmd.target || cmd.target > 0xffffULL) { result = "Invalid stack size"; return false; }
            return cmd.durationMinutes ? botDirector.listStacksPriced(player, cmd.id, uint16_t(cmd.target), cmd.auctionCount,
                cmd.bid, cmd.buyout, cmd.durationMinutes, gameplay.content(), result)
                : botDirector.listItem(player, cmd.id, uint16_t(cmd.target), gameplay.content(), result);
        default:
            // Everything else - including the merchant, repair, trainer and
            // innkeeper actions - is a ruleset action: it needs the world, the
            // NPC the player is standing at and the character's own bags, all
            // of which live in LocalGameplay rather than here.
            return gameplay.execute(player, cmd, activePlayers(), result);
        }
    }
    void receiveCommand(Peer& peer, Reader& r) {
        const auto id = r.u32(); LocalRealmCommand cmd;
        cmd.action = LocalAction(r.u8()); cmd.target = r.u64(); cmd.id = r.u32();
        cmd.bid = r.u32(); cmd.buyout = r.u32(); cmd.durationMinutes = r.u32();
        if (cmd.action == LocalAction::ListAuction) cmd.auctionCount = r.u16();
        if (cmd.action == LocalAction::BankMove || cmd.action == LocalAction::BankDepositSlot || cmd.action==LocalAction::BackpackMove || cmd.action==LocalAction::BankWithdrawSlot) { cmd.bankSourceCount = r.u16(); cmd.bankDestinationCount = r.u16(); }
        cmd.serviceNpcGuid = r.u64();
        if(cmd.action==LocalAction::MailSend){
            cmd.mailRecipient=r.name();cmd.mailSubject=r.text();cmd.mailBody=r.text();
            const auto count=r.u8();if(count>12)return;
            for(unsigned i=0;i<count;++i){LocalTradeItem item;item.item=r.u32();item.count=r.u16();item.sourceCount=r.u16();item.bag=r.u8();cmd.mailAttachments.push_back(item);}
        }
        if (!r.done() || !id || uint8_t(cmd.action) < 1 || uint8_t(cmd.action) > uint8_t(kLocalActionMax)) return;
        if (id == peer.lastCommand) { peer.lastSeen = now; actionResult(peer); return; }
        // Only one client command is in flight, so gaps are not executable.
        if (id != peer.lastCommand + 1) return;
        auto* record = findSaved(peer.guid); if (!record) return;
        peer.lastCommandSuccess = runCommand(record->player, cmd, peer.lastCommandStatus);
        peer.lastCommand = id; peer.lastSeen = now;
        dirty = dirty || peer.lastCommandSuccess;
        LOG_INFO("[local_realm] Action peer=", peer.guid, " id=", id, " kind=", int(cmd.action), " ok=", peer.lastCommandSuccess, " result=", peer.lastCommandStatus);
        actionResult(peer); progress(peer); merchantState(peer); refreshPlayers();maintainSocial();
        if(cmd.action>=LocalAction::ReadyStart && cmd.action<=LocalAction::TradeCancel)for(auto& recipient:peers)sendSocial(recipient);
        if(cmd.action>=LocalAction::PartyInvite && cmd.action<=LocalAction::PartyPromote)sendParty(peer);
    }
    void receiveWorld(Reader& r) {
        const auto tick = r.u32(); const auto part = r.u8(), parts = r.u8(), count = r.u8();
        const auto map=r.u32(),instance=r.u32(),revision=r.u32();
        if (!r.valid || map!=self.mapId || instance!=self.instanceId || revision!=self.positionRevision ||
            !tick || !parts || parts > MaxNpcPages || part >= parts || count > NpcsPerPage || !newer(tick, worldSequence) ||
            (part+1<parts && count!=NpcsPerPage) || (parts>1 && !count) || size_t(part)*NpcsPerPage+count>LocalGameplay::MaxNpcs) return;
        std::vector<LocalRealmNpc> chunk;
        for (unsigned index = 0; index < count; ++index) {
            auto n = readNpc(r, gameplay.content()); if (!r.valid || n.mapId!=map || n.instanceId!=instance) return; chunk.push_back(std::move(n));
        }
        if (!r.done()) return;
        if (tick != collectingWorld) {
            if (collectingWorld && !newer(tick, collectingWorld)) return;
            collectingWorld = tick; worldParts = parts; worldReceived.fill(false);
            for (auto& entries : worldChunks) entries.clear();
        }
        if (parts != worldParts) return;
        if(worldReceived[part])return;
        worldChunks[part] = std::move(chunk); worldReceived[part] = true;
        for (unsigned index = 0; index < parts; ++index) if (!worldReceived[index]) return;
        std::vector<LocalRealmNpc> all;
        for (unsigned index = 0; index < parts; ++index) for (auto& n : worldChunks[index]) {
            for (const auto& prior : all) if (prior.guid == n.guid) return;
            all.push_back(std::move(n));
        }
        gameplay.setRemoteNpcs(std::move(all)); worldSequence = tick; lastSeen = now;
    }
    void refreshPlayers() {
        // These are presentation copies, rebuilt several times per frame.
        // Reassign existing rows so their names, quest vectors and inventory
        // buffers retain capacity. clear()+push_back() freed every nested
        // allocation and then recreated it, even with an unchanged roster.
        size_t npcCount = 0;
        for (const auto& npc : gameplay.npcs()) if (npc.mapId == self.mapId && npc.instanceId == self.instanceId) {
            if (npcCount == npcView.size()) npcView.emplace_back();
            auto& copy = npcView[npcCount++];
            copy = npc;
            copy.hostile = gameplay.canAttack(self, npc);
            copy.aggressive = gameplay.isAggressive(self, npc);
        }
        npcView.resize(npcCount);
        size_t playerCount = 0;
        const auto append = [&](const LocalRealmPlayer& player) {
            if (playerCount == players.size()) players.emplace_back();
            players[playerCount++] = player;
        };
        append(self);
        for (const auto& peer : peers)
            if (const auto* p = findSaved(peer.guid)) append(p->player);
        // Appended after the real ones, so a guest sees them as ordinary
        // players and renders them as characters without a second path.
        for (const auto& bot : botPlayers) append(bot);
        players.resize(playerCount);
        syncParty();
    }
    void expirePeers() {
        if (state != LocalRealmState::Hosting) return;
        const auto oldCount = peers.size();
        peers.erase(std::remove_if(peers.begin(), peers.end(), [&](const Peer& peer) {
            if (now - peer.lastSeen <= PeerTimeout ||
                (peer.loading && now - peer.loadingSince <= LoadingTimeout)) return false;
            LOG_WARNING("[local_realm] Guest timeout guid=", peer.guid);
            rememberCancelled(peer.identity,peer.joinNonce);resetSavedSession(peer.guid);return true;
        }), peers.end());
        if (oldCount != peers.size()) {
            // Do this before gameplay and before publishing another party view.
            syncParty(true);refreshPlayers();refreshStatus();dirty=true;saveRealm();
        }
    }
    void refreshStatus() {
        if (state == LocalRealmState::Hosting)
            status = realmName + ": " + std::to_string(peers.size()+1) + "/" + std::to_string(playerLimit) + " consoles";
        else if (state == LocalRealmState::Connected)
            status = "LAN connected: " + std::to_string(std::count_if(players.begin(),players.end(),[&](const auto& p){return !botDirector.isBot(p.guid);})) + "/" + std::to_string(playerLimit) + " consoles";
        else if (state == LocalRealmState::SinglePlayer) status = "Singleplayer local realm (saved locally)";
    }
    bool openSocket(uint16_t requestedPort) {
        net::ensureInit();
        const auto socketFailure = [&](const char* operation, const std::string& reason) {
            // Save errno before logging or closing the descriptor changes it.
            const int code = net::lastError();
            LOG_ERROR("[local_realm] UDP setup failed operation=", operation,
                      " fd=", socket, " requested_port=", requestedPort,
                      " errno=", code, " (", net::errorString(code), ")");
            return fail(reason + " (errno " + std::to_string(code) + ": " + net::errorString(code) + ")");
        };
        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket == INVALID_SOCK) return socketFailure("socket", "Could not create local realm UDP socket");
        if (!net::configureDatagramNonBlocking(socket))
            return socketFailure("nonblocking", "Could not make local realm socket nonblocking");
        sockaddr_in address{};
        initAddress(address); address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(requestedPort);
        if (::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
            return socketFailure("bind", "Could not bind local realm UDP port " + std::to_string(requestedPort));
        socklen_t size = sizeof(address);
        if (::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) != 0)
            return socketFailure("getsockname", "Could not read local realm UDP port");
        port = ntohs(address.sin_port);
        if (size != sizeof(address) || address.sin_family != AF_INET || !port)
            return fail("Invalid local realm UDP socket address");
#ifdef WOWEE_PS4
        LOG_INFO("[local_realm] UDP ready fd=", socket, " port=", port,
                 " nonblocking=per_call_BSD_MSG_DONTWAIT transport=kernel_socket");
#else
        LOG_INFO("[local_realm] UDP ready fd=", socket, " port=", port, " nonblocking=1");
#endif
        return true;
    }
    void send(Message type, uint64_t token, const Writer& payload, const sockaddr_in& address, uint8_t wireVersion = Version) {
        if (socket == INVALID_SOCK) return;
        if (payload.bytes.size() + HeaderSize > MaxPacket) {
            LOG_ERROR("[local_realm] Refused oversized LAN message type=", int(type), " bytes=", payload.bytes.size() + HeaderSize);
            return;
        }
        Writer w; w.u32(WireMagic); w.u8(wireVersion); w.u8(uint8_t(type));
        w.u16(uint16_t(HeaderSize + payload.bytes.size())); w.u32(++sequence); w.u64(token);
        w.bytes.insert(w.bytes.end(), payload.bytes.begin(), payload.bytes.end());
        const auto result = ::sendto(socket, reinterpret_cast<const char*>(w.bytes.data()),
                                    int(w.bytes.size()), net::datagramFlags(), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        if (result < 0) {
            const int code = net::lastError();
            if (!net::isWouldBlock(code))
                LOG_WARNING("[local_realm] UDP send failed fd=", socket, " errno=", code,
                            " (", net::errorString(code), ")");
        }
    }
    void clock(const Peer& peer) {
        Writer w; w.f32(dayClock.hours(now)); w.u64(uint64_t(gameplay.transportTime()*1000.0));
        send(Message::Clock, peer.session, w, peer.address);
    }
    std::vector<Writer> snapshotPages() {
        const auto tick = ++playerTick;
        const auto parts = (players.size() + PlayersPerPage - 1) / PlayersPerPage;
        std::vector<Writer> pages(parts);
        for (size_t part = 0; part < parts; ++part) {
            auto& w = pages[part];
            const size_t begin = part * PlayersPerPage, end = std::min(begin + PlayersPerPage, players.size());
            w.u32(tick); w.u8(uint8_t(part)); w.u8(uint8_t(parts));
            w.u8(uint8_t(players.size())); w.u8(uint8_t(playerLimit)); w.u8(uint8_t(end - begin));
            for (size_t i = begin; i < end; ++i) {
                writePlayer(w, players[i]); writeNetworkVitals(w, players[i]); writeAppearance(w, players[i]);
            }
        }
        return pages;
    }
    void welcome(Peer& peer) {
        refreshPlayers();
        const auto* record = findSaved(peer.guid); if (!record) return;
        Writer w; w.u64(peer.joinNonce); w.u64(realmId); w.u64(peer.guid); w.u8(uint8_t(playerLimit));
        w.u8(1); writePlayer(w, record->player); writeNetworkVitals(w, record->player); writeAppearance(w, record->player);
        writeCast(w, record->player); w.u8(record->player.introSeen ? 1 : 0);
        send(Message::Welcome, peer.session, w, peer.address);
        clock(peer); progress(peer); history(peer); world(peer, ++worldTick);
        auctionBoard(peer, ++auctionTick);
        // The complete roster follows through the budgeted snapshot path.
        peer.lastSnapshot = -1;
    }
    void reject(const sockaddr_in& address, uint64_t nonce, uint8_t reason) {
        Writer w; w.u8(reason); send(Message::Reject, nonce, w, address);
    }
    void sendLobbyRequest(uint8_t operation, uint8_t slot, const LocalRealmPlayer& character = {}) {
        lobbyOperation = operation; lobbySlot = slot; lobbyNonce = uniqueId();
        lobbyPending = true; lobbyStarted = now; lobbySent = -1;
        lobbyPayload.bytes.clear();
        lobbyPayload.u8(operation); lobbyPayload.u8(slot);
        const auto id = lobbyIdentities[slot];
        lobbyPayload.u64(id.a); lobbyPayload.u64(id.b);
        lobbyPayload.u64(expectedRealmId); lobbyPayload.u32(gameplay.content().fingerprint);
        if (operation == 1) {
            lobbyPayload.name(character.name); lobbyPayload.u8(character.race);
            lobbyPayload.u8(character.classId); lobbyPayload.u8(character.gender);
            writeAppearance(lobbyPayload, character);
        } else if (operation == 2) lobbyPayload.u64(character.guid);
    }
    void restartRoster() {
        rosterReady = false; remoteCharacters.clear();
        status = "Reading characters from " + realmName + "...";
        sendLobbyRequest(0, 0);
    }
    void handleCharacterRequest(Reader& r, const sockaddr_in& address, uint64_t nonce) {
        const auto operation = r.u8(), slot = r.u8();
        Identity id{r.u64(), r.u64()};
        const auto expected = r.u64(); const auto fingerprint = r.u32();
        LocalRealmPlayer incoming;
        if (operation == 1) {
            incoming.name = r.name(); incoming.race = r.u8(); incoming.classId = r.u8(); incoming.gender = r.u8();
            if (!readAppearance(r, incoming) || !validName(incoming.name) ||
                !LocalGameplay::validCharacterOptions(incoming.race, incoming.classId, incoming.gender)) return;
        } else if (operation == 2) incoming.guid = r.u64();
        if (!r.done() || !nonce || !id.a || !id.b || operation > 2 || slot >= LocalRealm::MaxCharacterSlots) return;
        while (!lobbyReplies.empty() && now - lobbyReplies.front().time > 120.0) lobbyReplies.pop_front();
        for (const auto& cached : lobbyReplies) {
            if (cached.nonce == nonce && cached.identity == id && sameAddress(cached.address, address)) {
                send(Message::CharacterReply, nonce, cached.reply, address); return;
            }
        }
        if (!helloBudget) return;
        --helloBudget;
        uint8_t result = 0; // 1=content/realm mismatch, 2=busy/ownership, 3=capacity, 4=disk, 5=stale selection
        if ((expected && expected != realmId) || fingerprint != gameplay.content().fingerprint) result = 1;
        auto* record = findIdentity(id);
        bool online = id == identity,ownsSession=online;
        for(const auto& peer:peers)if(peer.identity==id){
            ownsSession=true;online=online || now-peer.lastSeen<ReconnectSilence;
        }
        if(!result && operation!=0 && ownsSession)result=2;
        if (!result && operation == 1) {
            if (record) {
                // A lost reply can be retried; never overwrite an existing character.
                if (record->player.name != incoming.name || record->player.race != incoming.race ||
                    record->player.classId != incoming.classId || record->player.gender != incoming.gender) result = 5;
            } else {
                record = createPlayer(id, incoming.name, incoming.race, incoming.classId, incoming.gender);
                if (!record) result = 3;
                else {
                    record->player.skin = incoming.skin; record->player.face = incoming.face;
                    record->player.hairStyle = incoming.hairStyle; record->player.hairColor = incoming.hairColor;
                    record->player.facialHair = incoming.facialHair; record->player.useFemaleModel = incoming.useFemaleModel;
                    dirty = true;
                    if (!saveRealm()) { saved.pop_back(); record = nullptr; result = 4; }
                    else LOG_INFO("[LAN_LOBBY] character created on host slot=", int(slot), " guid=", record->player.guid);
                }
            }
        } else if (!result && operation == 2) {
            if (!record || record->player.guid != incoming.guid) result = 5;
            else if(hasMailAssets(record->player.guid))result=2;
            else {
                const auto index = size_t(record - saved.data());
                SavedPlayer removed = saved[index];
                saved.erase(saved.begin() + index); record = nullptr; dirty = true;
                if (!saveRealm()) { saved.insert(saved.begin() + index, std::move(removed)); result = 4; }
                else LOG_INFO("[LAN_LOBBY] character deleted on host slot=", int(slot));
            }
        }
        // Failed requests disclose no player state; ownership is established by
        // the unguessable per-slot credential, not by a display name or GUID.
        Writer reply; reply.u8(operation); reply.u8(slot); reply.u8(result); reply.u64(realmId);
        const bool occupied = !result && record;
        reply.u8(occupied ? 1 : 0); reply.u8(online ? 1 : 0);
        if (occupied) { writePlayer(reply, record->player); writeNetworkVitals(reply, record->player); writeAppearance(reply, record->player); }
        if (lobbyReplies.size() >= 64) lobbyReplies.pop_front();
        lobbyReplies.push_back({id, nonce, address, reply, now});
        send(Message::CharacterReply, nonce, reply, address);
    }
    void handleCharacterReply(Reader& r, const sockaddr_in& address, uint64_t nonce) {
        if (state != LocalRealmState::Browsing || !lobbyPending || nonce != lobbyNonce || !sameAddress(address, host)) return;
        const auto operation = r.u8(), slot = r.u8(), result = r.u8(); const auto rid = r.u64();
        const auto occupied = r.u8(), online = r.u8();
        LocalSavedCharacter character; character.slot = slot; character.online = online != 0;
        if (occupied == 1) {
            character.player = readPlayer(r); readNetworkVitals(r, character.player);
            if (!readAppearance(r, character.player)) return;
        }
        if (!r.done() || operation != lobbyOperation || slot != lobbySlot || result > 5 || occupied > 1 || online > 1 || !rid) return;
        lobbyPending = false;
        if (result || (expectedRealmId && expectedRealmId != rid)) {
            static const char* errors[] = {"Realm changed; refresh the realm list", "Realm or game data differs; refresh and use the same build/data",
                "Character is already in use", "The host character limit has been reached", "Host could not save; no success was committed",
                "Character selection changed; refresh the character list"};
            error = errors[result]; status = error; ++rosterRevision; return;
        }
        expectedRealmId = rid; error.clear();
        if (operation) { restartRoster(); return; }
        if (occupied) remoteCharacters.push_back(std::move(character));
        if (slot + 1 < LocalRealm::MaxCharacterSlots) sendLobbyRequest(0, uint8_t(slot + 1));
        else {
            rosterReady = true; ++rosterRevision;
            status = remoteCharacters.empty() ? "No characters on this realm. Create a character." : "Choose a character, then Enter World.";
            LOG_INFO("[LAN_LOBBY] host roster ready characters=", remoteCharacters.size(), " realm=", rid, " world_connected=0");
        }
    }
    void handleHello(Reader& r, const sockaddr_in& address, uint64_t nonce) {
        Identity id{r.u64(), r.u64()}; const std::string name = r.name();
        const auto race = r.u8(), classId = r.u8(), gender = r.u8(); const uint32_t contentHash = r.u32();
        LocalRealmPlayer appearance; if (!readAppearance(r, appearance)) return;
        const uint64_t requestedGuid = r.u64();
        if (!r.done() || !nonce || !id.a || !id.b || !validName(name) || !LocalGameplay::validCharacterOptions(race, classId, gender)) return;
        if (contentHash != gameplay.content().fingerprint) { reject(address, nonce, 5); return; }
        if (id == identity) { reject(address, nonce, 2); return; }
        if(joinCancelled(id,nonce))return; // cancelled attempts cannot spawn later
        for(auto it=peers.begin();it!=peers.end();++it){
            if(!(it->identity==id))continue;
            if(sameAddress(it->address,address) && it->joinNonce==nonce){
                it->lastSeen=now;welcome(*it);return;
            }
            // A second live session still cannot take over. An owner with the
            // same secret identity can reclaim a silent loading/failed session.
            if(now-it->lastSeen<ReconnectSilence){reject(address,nonce,2);return;}
            const auto* record=findIdentity(id);
            if(!record || (requestedGuid && requestedGuid!=it->guid)){reject(address,nonce,3);return;}
            LOG_INFO("[LAN_RECOVERY] replacing silent owner session guid=",it->guid," silence=",now-it->lastSeen);
            rememberCancelled(it->identity,it->joinNonce);resetSavedSession(it->guid);
            peers.erase(it);syncParty(true);refreshPlayers();break;
        }
        if (peers.size() + 1 >= playerLimit) { reject(address, nonce, 1); return; }
        if (!helloBudget) return; // HELLO retries; bound synchronous saves/joins per pump.
        --helloBudget;
        auto* record = findIdentity(id);
        if (requestedGuid && (!record || record->player.guid != requestedGuid)) { reject(address, nonce, 3); return; }
        if (!record) {
            record = createPlayer(id, name, race, classId, gender);
            if (!record) { reject(address, nonce, 3); return; }
            record->player.skin = appearance.skin; record->player.face = appearance.face; record->player.hairStyle = appearance.hairStyle;
            record->player.hairColor = appearance.hairColor; record->player.facialHair = appearance.facialHair;
            record->player.useFemaleModel = appearance.useFemaleModel;
        }
        // The host owns character state; HELLO never supplies position or level.
        // A new owner session starts without replayable casts from the previous
        // connection. Duplicate HELLOs above retain the existing live session.
        record->player.castingSpellId = record->player.castRemainingMs = record->player.castTotalMs = 0;
        record->player.castTarget = 0; record->player.globalCooldownMs = 0;
        record->player.castStatus = LocalCastStatus::None;
        record->player.castRevision = record->player.lastCastSpellId = 0;
        record->player.lastCastTarget = 0;
        const uint64_t guid = record->player.guid;
        dirty = true;
        if (!saveRealm()) { reject(address, nonce, 4); return; }
        Peer peer; peer.address = address; peer.identity = id; peer.guid = guid;
        peer.session = uniqueId(); peer.joinNonce = nonce; peer.lastSeen = now; peer.loadingSince = now;
        peers.push_back(peer); welcome(peers.back()); refreshStatus();
        LOG_INFO("[local_realm] Guest joined guid=", guid, " players=", players.size());
    }
    void handleHost(Message type, Reader& r, const sockaddr_in& address, uint64_t token, uint32_t seq) {
        if(type==Message::AbortJoin){
            Identity id{r.u64(),r.u64()};
            if(!r.done() || !token || !id.a || !id.b || !helloBudget)return;
            --helloBudget;
            const auto it=std::find_if(peers.begin(),peers.end(),[&](const Peer& peer){
                return peer.identity==id && peer.joinNonce==token && sameAddress(peer.address,address);
            });
            // Remember before erasing: an in-flight HELLO may follow the abort.
            // Unknown identities cannot reserve records or world slots here.
            rememberCancelled(id,token);
            if(it!=peers.end()){
                LOG_INFO("[LAN_RECOVERY] cancelled incomplete join guid=",it->guid);
                resetSavedSession(it->guid);peers.erase(it);syncParty(true);saveRealm();refreshPlayers();refreshStatus();
            }
            return;
        }
        if (type == Message::CharacterRequest) { handleCharacterRequest(r, address, token); return; }
        if (type == Message::Hello) { handleHello(r, address, token); return; }
        auto peer = std::find_if(peers.begin(), peers.end(), [&](const Peer& p) {
            return p.session == token && sameAddress(p.address, address);
        });
        if (peer == peers.end()) return;
        if (type == Message::ChatRequest) { receiveChatRequest(*peer,r);return; }
        if (type == Message::ChatAck) {
            const auto id=r.u32();
            if(r.done() && !peer->chatOut.empty() && peer->chatOut.front().id==id && peer->chatOut.front().lastSent>=0){peer->chatOut.erase(peer->chatOut.begin());peer->lastSeen=now;}
            return;
        }
        if (type == Message::Command) { receiveCommand(*peer, r); return; }
        if (type == Message::MerchantQuery) { receiveMerchantQuery(*peer, r); return; }
        if (type == Message::MailQuery) { receiveMailQuery(*peer,r);return; }
        if (type == Message::HistoryAck) {
            const auto revision = r.u32(); const auto page = r.u16();
            if (r.done() && revision == peer->historyRevision && page < peer->historyAcked.size() && peer->historySent[page] >= 0) {
                peer->historyAcked[page] = true; peer->lastSeen = now;
            }
            return;
        }
        const bool freshPosition=newer(seq,peer->sequence);
        if(!freshPosition && type!=Message::Leave)return;
        if (type == Message::Position || type == Message::Leave) {
            LocalRealmPlayer incoming; readPosition(r, incoming); const auto positionRevision = r.u32(); const auto instanceId = r.u32();
            const auto loading = type == Message::Position ? r.u8() : 0;
            // How the guest says it is moving, so the host can measure its fall.
            // Refused outright rather than masked when it carries a bit this
            // build has no meaning for: an unknown flag is a version the
            // handshake should already have rejected.
            const auto movement = type == Message::Position ? r.u8() : 0;
            const uint32_t deck=type==Message::Position?r.u32():0;
            const float ox=type==Message::Position?r.f32():0, oy=type==Message::Position?r.f32():0, oz=type==Message::Position?r.f32():0;
            if(!std::isfinite(ox)||!std::isfinite(oy)||!std::isfinite(oz)||std::abs(ox)>100||std::abs(oy)>100||std::abs(oz)>100)return;
            if (!r.done() || loading > 1 || (movement & ~unsigned(kLocalMovementMask)) != 0 ||
                !validPosition(incoming.mapId, incoming.x, incoming.y, incoming.z, incoming.orientation)) return;
            if (loading && !peer->loading) peer->loadingSince = now;
            peer->loading = loading != 0;
            auto* record = findSaved(peer->guid);
            if (!record) return;
            if (freshPosition && !loading && !record->player.dead && !record->player.flight.active && incoming.mapId==record->player.mapId &&
                positionRevision == record->player.positionRevision && instanceId == record->player.instanceId) {
                dirty = dirty || record->player.mapId != incoming.mapId || record->player.x != incoming.x ||
                        record->player.y != incoming.y || record->player.z != incoming.z || record->player.orientation != incoming.orientation;
                record->player.mapId = incoming.mapId; record->player.x = incoming.x;
                record->player.y = incoming.y; record->player.z = incoming.z;
                record->player.orientation = incoming.orientation;
                if(deck && deck==record->player.transportEntry) {
                    record->player.transportOffsetX=ox;record->player.transportOffsetY=oy;record->player.transportOffsetZ=oz;
                }
                // Only with a position the host accepted. A report the guard
                // above rejected is stale - the host has moved this character
                // since - and adopting its movement bits alone would end a fall
                // the character is no longer having.
                record->player.movementState = uint8_t(movement);
            }
            peer->sequence = seq; peer->lastSeen = now;
            if (type == Message::Leave) {
                LOG_INFO("[local_realm] Guest left guid=", peer->guid);
                rememberCancelled(peer->identity,peer->joinNonce);resetSavedSession(peer->guid);
                peers.erase(peer); syncParty(true); saveRealm(); refreshPlayers(); refreshStatus();
            }
        }
    }
    bool readSnapshot(Reader& r, uint64_t guid, std::vector<LocalRealmPlayer>& out) {
        const uint8_t count = r.u8();
        if (!count || count > LocalRealm::MaxPlayers) return false;
        bool containsSelf = false;
        for (uint8_t i = 0; i < count; ++i) {
            auto p = readPlayer(r); readNetworkVitals(r, p);
            if (!readAppearance(r, p)) return false;
            for (const auto& previous : out) if (previous.guid == p.guid) return false;
            if (p.guid == guid) {
                containsSelf = true;
                if (!readCast(r, p)) return false;
                const auto seen = r.u8(); if (seen > 1) return false;
                p.introSeen = seen != 0;
            }
            out.push_back(std::move(p));
        }
        return r.done() && containsSelf;
    }
    bool readPlayerPage(Reader& r, uint32_t seq, std::vector<LocalRealmPlayer>& out) {
        const auto tick = r.u32(); const auto part = r.u8(), parts = r.u8(), total = r.u8(), capacity = r.u8(), count = r.u8();
        if (!tick || !newer(tick, playerSequence) || capacity != playerLimit || !total || total > capacity ||
            !parts || parts > MaxPlayerPages || parts != (total + PlayersPerPage - 1) / PlayersPerPage || part >= parts ||
            count != std::min(PlayersPerPage, size_t(total) - size_t(part) * PlayersPerPage)) return false;
        std::vector<LocalRealmPlayer> chunk; chunk.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            auto p = readPlayer(r); readNetworkVitals(r, p); if (!readAppearance(r, p)) return false;
            chunk.push_back(std::move(p));
        }
        if (!r.done()) return false;
        if (collectingPlayers != tick) {
            if (collectingPlayers && !newer(tick, collectingPlayers)) return false;
            collectingPlayers = tick; playerParts = parts; playerTotal = total; playerReceived.fill(false);
            collectingPlayerSequence = seq;
            for (auto& page : playerChunks) page.clear();
        }
        if (playerParts != parts || playerTotal != total) return false;
        if (newer(seq, collectingPlayerSequence)) collectingPlayerSequence = seq;
        playerChunks[part] = std::move(chunk); playerReceived[part] = true;
        for (unsigned i = 0; i < parts; ++i) if (!playerReceived[i]) return false;
        bool own = false; out.reserve(total);
        for (unsigned i = 0; i < parts; ++i) for (const auto& p : playerChunks[i]) {
            for (const auto& prior : out) if (prior.guid == p.guid) { out.clear(); return false; }
            own = own || p.guid == self.guid; out.push_back(p);
        }
        if (!own || out.size() != total) { out.clear(); return false; }
        playerSequence = tick;
        return true;
    }
    void commitPlayers(std::vector<LocalRealmPlayer> list, uint32_t seq) {
            for (auto& p : list) if (p.guid == self.guid) {
                // Only the server can relocate a character (revive). Ordinary
                // snapshots keep local movement prediction, never old stats.
                if (newer(p.positionRevision, self.positionRevision)) {
                    clearWorldForTravel(p);
                    self.mapId = p.mapId; self.x = p.x; self.y = p.y; self.z = p.z;
                    self.orientation = p.orientation; self.positionRevision = p.positionRevision; self.instanceId = p.instanceId;
                }
                if (newer(seq, vitalsSequence)) {
                    vitalsSequence = seq;
                    self.health = p.health; self.maxHealth = p.maxHealth; self.mana = p.mana; self.maxMana = p.maxMana;
                    self.mountSpellId=p.mountSpellId;self.dead = p.dead; self.level = p.level; self.equipment = p.equipment; self.attackTarget = p.attackTarget; self.resourceType = p.resourceType;
                }
                p = self;
            }
            players = std::move(list); incomingSequence = seq; lastSeen = now; refreshStatus();
    }
    void handleClient(Message type, Reader& r, const sockaddr_in& address, uint64_t token, uint32_t seq) {
        if (!sameAddress(address, host)) return;
        if (state == LocalRealmState::Connecting && type == Message::Reject && token == joinNonce) {
            uint8_t reason = r.u8();
            if (!r.done()) return;
            if (reason == 1) fail("LAN realm is full (host-selected player limit)");
            else if (reason == 2) fail("This console identity is already connected to the realm");
            else if (reason == 3) fail("Character selection is stale or the realm has no free character slot; refresh the roster");
            else if (reason == 6) fail("LAN protocol version differs; install the same game build on every console");
            else if (reason == 5) fail("LAN world content differs; install the same content on every console");
            else fail("LAN host could not save the joining character");
            return;
        }
        if (state == LocalRealmState::Connecting && type == Message::Welcome && token) {
            const uint64_t nonce = r.u64(), receivedRealm = r.u64(), guid = r.u64();
            const auto capacity = r.u8();
            std::vector<LocalRealmPlayer> list;
            if (capacity < LocalRealm::MinPlayers || capacity > LocalRealm::MaxPlayers) return;
            if (nonce != joinNonce || !receivedRealm || (expectedRealmId && receivedRealm!=expectedRealmId) ||
                (selectedGuid && guid!=selectedGuid) || !readSnapshot(r, guid, list)) return;
            auto local = std::find_if(list.begin(), list.end(), [&](const LocalRealmPlayer& p) { return p.guid == guid; });
            playerLimit = capacity; self = *local; players = std::move(list); session = token; realmId = receivedRealm;
            incomingSequence = seq; vitalsSequence = seq; lastSeen = now; lastSend = -1;
            state = LocalRealmState::Connected; error.clear(); refreshStatus();
            LOG_INFO("[local_realm] Connected realm=", realmId, " guid=", self.guid);
        } else if (state == LocalRealmState::Connected && token == session) {
            if (type == Message::ChatDelivery) {receiveChatDelivery(r);return;}
            if (type == Message::ChatResult) {
                const auto id=r.u32();const auto success=r.u8();const auto result=r.text();
                if(!r.done() || success>1 || chatPending.empty() || chatPending.front().id!=id)return;
                if(!success){actionStatus=result;++actionStatusRevision;}
                chatPending.pop_front();lastSeen=now;return;
            }
            if (type == Message::ActionResult) {
                const auto id = r.u32(); const auto success = r.u8(); const auto result = r.text();
                if (!r.done() || success > 1 || pendingCommands.empty() || pendingCommands.front().id != id) return;
                actionStatus = result; ++actionStatusRevision;
                if(mailActionKind(pendingCommands.front().command.action)){mailResultSuccess=success;++mailResultRevision;}
                pendingCommands.pop_front(); lastSeen = now;
                LOG_INFO("[local_realm] Action response id=", id, " ok=", int(success), " result=", result);
                return;
            }
            if (type == Message::Npcs) { receiveWorld(r); return; }
            if (type == Message::Auctions) { receiveAuctions(r); return; }
            if (type == Message::MerchantState) { receiveMerchantState(r); return; }
            if (type == Message::MailState) { receiveMailState(r);return; }
            if (type == Message::PartyState) { receiveParty(r); return; }
            if (type == Message::SocialState) { receiveSocial(r); return; }
            if (type == Message::History) { receiveHistory(r); return; }
            if (type == Message::Progress) {
                const auto snapshot = r.u32(); const auto part = r.u8(), parts = r.u8();
                const auto total = r.u16(), length = r.u16();
                if (!r.valid || !total || total > MaxOwnerProgressBytes || !parts || parts > MaxProgressChunks ||
                    parts != (total+ProgressChunkBytes-1)/ProgressChunkBytes || part >= parts ||
                    length != std::min(ProgressChunkBytes,size_t(total)-part*ProgressChunkBytes) ||
                    r.size-r.offset != length || !newer(snapshot,progressSequence) ||
                    (pendingProgress && !newer(snapshot,pendingProgress->sequence))) return;
                if (snapshot != collectingProgress) {
                    if (collectingProgress && !newer(snapshot,collectingProgress)) return;
                    collectingProgress=snapshot;progressTotal=total;progressReceived.fill(false);
                }
                if (progressTotal != total) return;
                const auto begin=part*ProgressChunkBytes;
                if (progressReceived[part] && !std::equal(r.data+r.offset,r.data+r.size,progressBytes.begin()+begin)) return;
                std::copy(r.data+r.offset,r.data+r.size,progressBytes.begin()+begin);progressReceived[part]=true;
                if (!std::all_of(progressReceived.begin(),progressReceived.begin()+parts,[](bool b){return b;})) return;
                Reader complete(progressBytes.data(),total); r=complete;seq=snapshot;
                if (!newer(seq, progressSequence) || (pendingProgress && !newer(seq, pendingProgress->sequence))) return;
                const auto guid = r.u64(); const auto revision = r.u32(), count = r.u32();
                LocalRealmPlayer updated = self; readPosition(r, updated);
                if (guid != self.guid || !revision || count > LocalGameplay::MaxCompletedQuests ||
                    !readProgress(r, updated) || !readCast(r, updated) || !readHealingViews(r,updated) ||
                    !validPosition(updated.mapId, updated.x, updated.y, updated.z, updated.orientation)) return;
                const auto seen = r.u8(); if (seen > 1 || !r.done()) return;
                updated.introSeen = seen != 0;
                if (revision == historyRevision && count == self.completedQuestIds.size()) {
                    for (const auto& quest : updated.quests)
                        if (std::binary_search(self.completedQuestIds.begin(), self.completedQuestIds.end(), quest.id)) return;
                    applyProgress(std::move(updated), seq);
                } else if ((!historyRevision || newer(revision, historyRevision)) && count >= self.completedQuestIds.size()) {
                    pendingProgress = PendingProgress{std::move(updated), seq, revision, count};
                    commitHistoryProgress(); lastSeen = now;
                }
                return;
            }
            // Independent sequence: clock packets must not suppress player snapshots
            // arriving out of order. Older clients simply ignore this optional message.
            if (type == Message::Clock) {
                const float hours = r.f32();
                const double travelTime=double(r.u64())/1000.0;
                if (travelTime>31557600000.0) return;
                if (r.done() && newer(seq, clockSequence) && dayClock.synchronize(hours, now)) {
                    gameplay.setTransportTime(travelTime);
                    clockSequence = seq; lastSeen = now;
                }
                return;
            }
            if (type != Message::Snapshot && !newer(seq, incomingSequence)) return;
            if (type == Message::Leave && r.done()) {
                fail("LAN host closed the realm; character is saved on the host"); return;
            }
            if (type != Message::Snapshot) return;
            std::vector<LocalRealmPlayer> list;
            if (!readPlayerPage(r, seq, list)) return;
            commitPlayers(std::move(list), collectingPlayerSequence);
        }
    }

    void receive() {
        // One over the protocol limit detects truncation instead of accepting a
        // valid prefix from a larger datagram. Per-frame work is strictly bounded.
        helloBudget = 4;
        std::array<uint8_t, MaxPacket + 1> buffer{};
        for (size_t count = 0; count < 64 && socket != INVALID_SOCK; ++count) {
            sockaddr_in address{}; socklen_t length = sizeof(address);
            const auto received = ::recvfrom(socket, reinterpret_cast<char*>(buffer.data()), int(buffer.size()), net::datagramFlags(),
                                             reinterpret_cast<sockaddr*>(&address), &length);
            if (received < 0) {
                const int code = net::lastError();
                if (net::isWouldBlock(code)) break;
                LOG_WARNING("[local_realm] UDP receive failed fd=", socket, " errno=", code,
                            " (", net::errorString(code), ")"); break;
            }
            uint64_t discoveryNonce = 0;
            if (state == LocalRealmState::Hosting && length == sizeof(address) &&
                address.sin_family == AF_INET && lan::readQuery(buffer.data(), size_t(received), discoveryNonce)) {
                // A bounded responder shares the existing UDP socket. Discovery never
                // changes realm/player state and cannot delay gameplay without bound.
                if (now - discoveryWindow >= 0.1) { discoveryWindow = now; discoveryReplies = 0; }
                if (discoveryReplies++ < 8) {
                    lan::Advertisement info; info.realmId=realmId; info.port=port;
                    info.players=uint16_t(peers.size()+1); info.capacity=uint16_t(playerLimit); info.name=realmName;
                    const auto response=lan::reply(discoveryNonce,info);
                    ::sendto(socket,reinterpret_cast<const char*>(response.data()),int(response.size()),net::datagramFlags(),
                             reinterpret_cast<const sockaddr*>(&address),sizeof(address));
                }
                continue;
            }
            if (received < int(HeaderSize) || received > int(MaxPacket) || length != sizeof(address)) continue;
            Reader r(buffer.data(), size_t(received));
            if (r.u32() != WireMagic) continue;
            const auto wireVersion = r.u8();
            const Message type = Message(r.u8());
            if (r.u16() != received) continue;
            const uint32_t seq = r.u32(); const uint64_t token = r.u64();
            if (wireVersion != Version) {
                if (state == LocalRealmState::Connecting && sameAddress(address, host) && wireVersion == 3 && legacyProbeNonce) {
                    // B10 discards an unknown HELLO version. Its known v3
                    // fingerprint-reject response identifies it without
                    // waiting for the normal unreachable-host timeout.
                    if (type == Message::Reject && token == legacyProbeNonce) {
                        const auto reason = r.u8();
                        if (r.done() && reason != 6) fail("LAN protocol version differs; install the same game build on every console");
                        continue; // A current host rejects the probe with 6.
                    }
                    if (type == Message::Welcome && token && r.u64() == legacyProbeNonce) {
                        fail("LAN protocol version differs; install the same game build on every console"); continue;
                    }
                }
                if (state == LocalRealmState::Hosting && type == Message::Hello && token) {
                    // The common header/reject layout is stable. Reply in the
                    // caller's version so old peers can receive the rejection.
                    Writer reason; reason.u8(6); send(Message::Reject, token, reason, address, wireVersion);
                } else if (sameAddress(address, host) &&
                           ((state == LocalRealmState::Connecting && token == joinNonce && type == Message::Reject) ||
                            (state == LocalRealmState::Connected && token == session)))
                    fail("LAN protocol version differs; install the same game build on every console");
                continue;
            }
            if (state == LocalRealmState::Hosting) handleHost(type, r, address, token, seq);
            else if (type == Message::CharacterReply) handleCharacterReply(r, address, token);
            else handleClient(type, r, address, token, seq);
        }
    }
    bool prepare(const std::string& path, const std::string& name) {
        if (!validName(name)) return fail("Player name must contain 1-16 letters, numbers, '_' or '-'");
        if (!ensureDirectory(path)) return fail("Cannot create local realm save directory");
        directory = path; self.name = name;
        if (gameplay.content().sourcePath.empty()) {
            for (const auto& candidate : {std::string("/data/wow_ps/realm/world.json"), std::string("/app0/assets/local_realm/world.json"), std::string("assets/local_realm/world.json")}) {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec)) {
                    if (!gameplay.loadContent(candidate, error)) return fail(error);
                    LOG_INFO("[local_realm] Loaded world ", candidate, " NPC spawns=", gameplay.content().spawns.size(), " quests=", gameplay.content().quests.size());
                    break;
                }
            }
        }
        return loadIdentity();
    }
    bool startAuthority(const std::string& path, const std::string& name, bool networked, uint16_t requestedPort) try {
        if (!prepare(path, name) || !loadRealm()) return false;
        if (!gameplay.restoreInstances(restoredInstances, error)) return fail(error);
        auto* record = findIdentity(identity);
        if (!record) record = createPlayer(identity, name);
        if (!record) return fail("Local realm saved-character capacity reached");
        for (const auto& savedPlayer : saved) if (!gameplay.validatePlayer(savedPlayer.player, error)) return fail(error);
        for (auto& savedPlayer : saved) gameplay.initializePlayer(savedPlayer.player, false);
        self = record->player;
        if (networked && !openSocket(requestedPort)) return false;
        state = networked ? LocalRealmState::Hosting : LocalRealmState::SinglePlayer;
        refreshPlayers(); refreshStatus(); dirty = true; gameplay.tick(0, activePlayers()); refreshPlayers();
        if (!saveRealm()) return fail(error);
        LOG_INFO("[local_realm] Started ", networked ? "host" : "singleplayer", " realm=", realmId,
                 " map=", self.mapId, " xyz=", self.x, ",", self.y, ",", self.z, " port=", port);
        return true;
    } catch (const std::exception& exception) {
        LOG_ERROR("[local_realm] Realm start failed: ", exception.what());
        return fail("Could not load the local realm; original save files were preserved");
    }
};

LocalRealm::LocalRealm() : impl_(std::make_unique<Impl>()) {}
LocalRealm::~LocalRealm() { stop(); }
bool LocalRealm::loadContent(const std::string& path) {
    if (ready() || state() == LocalRealmState::Connecting) { impl_->error = "Stop the realm before changing world content"; return false; }
    if (!impl_->gameplay.loadContent(path, impl_->error)) return false;
    LOG_INFO("[local_realm] Content loaded path=", path, " spawns=", content().spawns.size(), " quests=", content().quests.size());
    return true;
}
bool LocalRealm::loadCatalog(const std::string& directory) {
    if (ready() || state() == LocalRealmState::Connecting) { impl_->error = "Stop the realm before changing the catalog"; return false; }
    if (impl_->gameplay.content().catalog) return true;
    return impl_->gameplay.loadCatalog(directory, impl_->error);
}
bool LocalRealm::setAreaTriggers(const std::vector<LocalAreaTriggerVolume>& volumes) {
    return impl_->gameplay.setAreaTriggers(volumes, impl_->error);
}
bool LocalRealm::setStarterSpells(const std::vector<LocalSpellDefinition>& spells, const std::string& diagnostic) {
    if (ready() || state() == LocalRealmState::Connecting) { impl_->error = "Stop the realm before changing starter spells"; return false; }
    return impl_->gameplay.setStarterSpells(spells, diagnostic, impl_->error);
}
bool LocalRealm::setFactionTemplates(const std::vector<LocalFactionTemplate>& rows, const std::array<uint32_t, 12>& races) {
    return impl_->gameplay.setFactionTemplates(rows, races, impl_->error);
}
bool LocalRealm::setCharacterSlot(uint8_t slot) {
    if (ready() || state() == LocalRealmState::Connecting || slot > 9) return false;
    impl_->characterSlot = slot; return true;
}
bool LocalRealm::setCharacterOptions(uint8_t race, uint8_t classId, uint8_t gender) {
    if (ready() || state() == LocalRealmState::Connecting || !LocalGameplay::validCharacterOptions(race, classId, gender)) return false;
    impl_->requestedRace = race; impl_->requestedClass = classId; impl_->requestedGender = gender;
    return true;
}
bool LocalRealm::setCharacterAppearance(uint8_t skin, uint8_t face, uint8_t hairStyle, uint8_t hairColor,
                                        uint8_t facialHair, bool useFemaleModel) {
    if (ready() || state() == LocalRealmState::Connecting) return false;
    impl_->requestedSkin = skin; impl_->requestedFace = face; impl_->requestedHairStyle = hairStyle;
    impl_->requestedHairColor = hairColor; impl_->requestedFacialHair = facialHair; impl_->requestedFemaleModel = useFemaleModel;
    return true;
}
namespace {
std::string identityFileFor(const std::string& dir, uint8_t slot) {
    return dir + (slot ? "/console_slot_" + std::to_string(slot) + ".identity" : "/console.identity");
}
}
std::vector<LocalSavedCharacter> LocalRealm::savedCharacters(const std::string& dir) {
    std::vector<LocalSavedCharacter> out;
    std::error_code ec;
    // A scanning Impl: reads the save and the identity files, writes nothing
    // (loadIdentity only creates an identity when the file is missing, and
    // only slots whose file exists are asked).
    Impl scan; scan.directory = dir;
    const std::string path = dir + "/realm.wprs";
    const bool loaded = (std::filesystem::exists(path, ec) && scan.parseSave(path)) ||
                        (std::filesystem::exists(path + ".bak", ec) && scan.parseSave(path + ".bak"));
    if (!loaded) {
        if (std::filesystem::exists(path, ec) || std::filesystem::exists(path + ".bak", ec))
            LOG_WARNING("[local_realm] Character roster scan could not parse realm save; preserving files and returning no characters");
        return out;
    }
    for (unsigned slot = 0; slot < MaxCharacterSlots; ++slot) {
        if (!std::filesystem::exists(identityFileFor(dir, uint8_t(slot)), ec)) continue;
        scan.characterSlot = uint8_t(slot);
        if (!scan.loadIdentity()) continue;
        if (const auto* record = scan.findIdentity(scan.identity)) out.push_back({uint8_t(slot), record->player});
    }
    return out;
}
int LocalRealm::freeCharacterSlot(const std::string& dir) {
    const auto used = savedCharacters(dir);
    for (unsigned slot = 0; slot < MaxCharacterSlots; ++slot) {
        bool taken = false;
        for (const auto& c : used) if (c.slot == slot) { taken = true; break; }
        if (!taken) return int(slot);
    }
    return -1;
}
bool LocalRealm::deleteSavedCharacter(const std::string& dir, uint8_t slot) {
    std::error_code ec;
    const std::string identityFile = identityFileFor(dir, slot);
    if (slot >= MaxCharacterSlots || !std::filesystem::exists(identityFile, ec)) return false;
    Impl scan; scan.directory = dir; scan.characterSlot = slot;
    if (!scan.loadIdentity() || !scan.loadRealm()) return false;
    if (!scan.gameplay.restoreInstances(scan.restoredInstances, scan.error)) return false;
    auto it = std::find_if(scan.saved.begin(), scan.saved.end(),
                           [&](const Impl::SavedPlayer& r) { return r.identity == scan.identity; });
    if (it != scan.saved.end()) {
        if(scan.hasMailAssets(it->player.guid))return false;
        scan.saved.erase(it);
        if (scan.saved.empty()) {
            // An empty save is not a valid save; the next start makes a fresh realm.
            std::filesystem::remove(dir + "/realm.wprs", ec);
            std::filesystem::remove(dir + "/realm.wprs.bak", ec);
        } else {
            scan.state = LocalRealmState::SinglePlayer;   // saveRealm writes only for an authority
            const bool ok = scan.saveRealm();
            scan.state = LocalRealmState::Stopped;
            if (!ok) return false;
        }
    }
    std::filesystem::remove(identityFile, ec);
    LOG_INFO("[local_realm] Deleted saved character slot=", int(slot));
    return true;
}
bool LocalRealm::setRealmName(const std::string& name) {
    if (!lan::validName(name) || ready() || state()==LocalRealmState::Connecting) return false;
    impl_->realmName=name; return true;
}
bool LocalRealm::startSinglePlayer(const std::string& dir, const std::string& name) {
    stop(); auto next = std::make_unique<Impl>(); next->retainConfiguration(*impl_); impl_ = std::move(next); return impl_->startAuthority(dir, name, false, 0);
}
bool LocalRealm::startHost(const std::string& dir, const std::string& name, uint16_t port, size_t playerLimit) {
    if (playerLimit < MinPlayers || playerLimit > MaxPlayers) return false;
    stop(); auto next = std::make_unique<Impl>(); next->retainConfiguration(*impl_); impl_ = std::move(next);
    impl_->playerLimit = playerLimit;
    return impl_->startAuthority(dir, name, true, port);
}
bool LocalRealm::joinHost(const std::string& ipv4, const std::string& dir, const std::string& name, uint16_t port) {
    stop(); auto next = std::make_unique<Impl>(); next->retainConfiguration(*impl_); impl_ = std::move(next); auto& p = *impl_;
    if (!port) return p.fail("Invalid LAN port");
    if (!p.prepare(dir, name)) return false;
    initAddress(p.host); p.host.sin_port = htons(port);
    if (::inet_pton(AF_INET, ipv4.c_str(), &p.host.sin_addr) != 1 ||
        p.host.sin_addr.s_addr == htonl(INADDR_ANY) || p.host.sin_addr.s_addr == htonl(INADDR_BROADCAST))
        return p.fail("The selected realm address is unavailable. Refresh the realm list.");
    if (!p.openSocket(0)) return false;
    p.joinNonce = uniqueId(); p.legacyProbeNonce = uniqueId(); p.state = LocalRealmState::Connecting;
    p.status = "Connecting to " + p.realmName + "...";
    LOG_INFO("[local_realm] Connecting to selected realm; network address hidden");
    update(0.0f); return true;
}
bool LocalRealm::browseHost(const std::string& ipv4, const std::string& dir, uint16_t port, uint64_t expected) {
    // Reuse the validated socket/address and credential setup, without sending HELLO.
    stop(); auto next = std::make_unique<Impl>(); next->retainConfiguration(*impl_); impl_ = std::move(next); auto& p = *impl_;
    if (!port || !p.prepare(dir, "Lobby")) return false;
    initAddress(p.host); p.host.sin_port = htons(port);
    if (::inet_pton(AF_INET, ipv4.c_str(), &p.host.sin_addr) != 1 ||
        p.host.sin_addr.s_addr == htonl(INADDR_ANY) || p.host.sin_addr.s_addr == htonl(INADDR_BROADCAST))
        return p.fail("Selected realm is unavailable; refresh the realm list");
    for (uint8_t slot = 0; slot < MaxCharacterSlots; ++slot) {
        p.characterSlot = slot; if (!p.loadIdentity()) return false;
        p.lobbyIdentities[slot] = p.identity;
    }
    p.characterSlot = 0; p.identity = p.lobbyIdentities[0];
    if (!p.openSocket(0)) return false;
    p.expectedRealmId = expected; p.state = LocalRealmState::Browsing; p.restartRoster();
    LOG_INFO("[LAN_LOBBY] reading host roster; address hidden; local files contain credentials only");
    return true;
}
const std::vector<LocalSavedCharacter>& LocalRealm::remoteCharacters() const { return impl_->remoteCharacters; }
bool LocalRealm::characterListReady() const { return impl_->rosterReady; }
bool LocalRealm::characterOperationPending() const { return impl_->lobbyPending; }
uint64_t LocalRealm::characterListRevision() const { return impl_->rosterRevision; }
bool LocalRealm::refreshRemoteCharacters() {
    if (state() != LocalRealmState::Browsing || impl_->lobbyPending) return false;
    impl_->error.clear(); impl_->restartRoster(); return true;
}
bool LocalRealm::createRemoteCharacter(uint8_t slot, const std::string& name) {
    auto& p = *impl_;
    if (state() != LocalRealmState::Browsing || !p.rosterReady || p.lobbyPending || slot >= MaxCharacterSlots || !validName(name)) return false;
    for (const auto& ch : p.remoteCharacters) if (ch.slot == slot) return false;
    LocalRealmPlayer ch; ch.name = name; ch.race = p.requestedRace; ch.classId = p.requestedClass; ch.gender = p.requestedGender;
    ch.skin = p.requestedSkin; ch.face = p.requestedFace; ch.hairStyle = p.requestedHairStyle; ch.hairColor = p.requestedHairColor;
    ch.facialHair = p.requestedFacialHair; ch.useFemaleModel = p.requestedFemaleModel;
    p.status = "Creating character on host..."; p.sendLobbyRequest(1, slot, ch); return true;
}
bool LocalRealm::deleteRemoteCharacter(uint8_t slot, uint64_t guid) {
    auto& p = *impl_;
    if (state() != LocalRealmState::Browsing || !p.rosterReady || p.lobbyPending || slot >= MaxCharacterSlots) return false;
    for (const auto& ch : p.remoteCharacters) if (ch.slot == slot && ch.player.guid == guid && !ch.online) {
        p.status = "Deleting character on host..."; p.sendLobbyRequest(2, slot, ch.player); return true;
    }
    return false;
}
bool LocalRealm::connectCharacter(uint8_t slot) {
    auto& p = *impl_;
    if (state() != LocalRealmState::Browsing || !p.rosterReady || p.lobbyPending) return false;
    for (const auto& ch : p.remoteCharacters) if (ch.slot == slot && !ch.online) {
        p.self = ch.player; p.identity = p.lobbyIdentities[slot]; p.characterSlot = slot; p.selectedGuid = ch.player.guid;
        p.requestedRace = ch.player.race; p.requestedClass = ch.player.classId; p.requestedGender = ch.player.gender;
        p.joinNonce = uniqueId(); p.legacyProbeNonce = uniqueId(); p.joinStarted = p.now;
        p.lastHello = -1; p.lastSeen = p.now; p.worldLoading = true; p.state = LocalRealmState::Connecting;
        p.status = "Connecting to " + p.realmName + "...";
        LOG_INFO("[LAN_LOBBY] Enter World selected slot=", int(slot), " guid=", p.selectedGuid); return true;
    }
    p.error = "Character unavailable or already online"; p.status = p.error; return false;
}
void LocalRealm::setWorldLoading(bool loading) { impl_->worldLoading = loading; }
void LocalRealm::update(float deltaTime) {
    auto& p = *impl_;
    if (p.autosave.takeFailure()) {
        p.dirty = true;
        p.error = "Autosave failed; previous save preserved. Will retry.";
        LOG_ERROR("[local_realm] ", p.error);
    }
    if (!std::isfinite(deltaTime) || deltaTime < 0) deltaTime = 0;
    const auto wallNow = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(wallNow - p.lastPump).count();
    p.lastPump = wallNow;
    // Loading callbacks pump with dt=0: real elapsed time still sends heartbeats
    // and expires dead peers while synthetic dt keeps loopback tests deterministic.
    const double clockStep=std::max(double(deltaTime),elapsed);
    p.now += clockStep;
    if(p.authoritative() || p.state==LocalRealmState::Connected) p.gameplay.advanceTransportTime(std::max(0.0,elapsed));
    p.expirePeers(); // Expired sessions cannot participate in queued commands either.
    if (p.socket != INVALID_SOCK) p.receive();
    p.maintainSocial();p.pumpChat();
    if (p.state == LocalRealmState::Browsing) {
        if (p.lobbyPending) {
            if (p.now - p.lobbyStarted > JoinTimeout) {
                p.lobbyPending = false; p.rosterReady = false; ++p.rosterRevision;
                p.error = "Host did not answer. Refresh the character list or go back."; p.status = p.error;
            } else if (p.now - p.lobbySent >= HelloInterval) {
                p.send(Message::CharacterRequest, p.lobbyNonce, p.lobbyPayload, p.host); p.lobbySent = p.now;
            }
        }
    } else if (p.state == LocalRealmState::Connecting) {
        if (p.now - p.joinStarted >= JoinTimeout) { p.fail("LAN host did not answer within 10 seconds"); return; }
        if (p.now - p.lastHello >= HelloInterval) {
            Writer w; w.u64(p.identity.a); w.u64(p.identity.b); w.name(p.self.name);
            w.u8(p.requestedRace); w.u8(p.requestedClass); w.u8(p.requestedGender); w.u32(p.gameplay.content().fingerprint);
            LocalRealmPlayer appearance; appearance.skin = p.requestedSkin; appearance.face = p.requestedFace;
            appearance.hairStyle = p.requestedHairStyle; appearance.hairColor = p.requestedHairColor;
            appearance.facialHair = p.requestedFacialHair; appearance.useFemaleModel = p.requestedFemaleModel;
            writeAppearance(w, appearance); w.u64(p.selectedGuid);
            p.send(Message::Hello, p.joinNonce, w, p.host);
            if (p.now >= HelloInterval) {
                Writer probe; probe.u64(p.identity.a); probe.u64(p.identity.b); probe.name(p.self.name);
                probe.u8(p.requestedRace); probe.u8(p.requestedClass); probe.u8(p.requestedGender);
                probe.u32(p.gameplay.content().fingerprint ^ 0xffffffffU);
                p.send(Message::Hello, p.legacyProbeNonce, probe, p.host, 3);
            }
            p.lastHello = p.now;
        }
    } else if (p.state == LocalRealmState::Connected) {
        if (p.now - p.lastSeen > (p.worldLoading ? LoadingTimeout : PeerTimeout)) { p.fail("LAN host connection timed out"); return; }
        if (p.now - p.lastSend >= SendInterval) {
            Writer w; writePosition(w, p.self); w.u32(p.self.positionRevision); w.u32(p.self.instanceId); w.u8(p.worldLoading ? 1 : 0); w.u8(p.self.movementState); w.u32(p.self.transportEntry);w.f32(p.self.transportOffsetX);w.f32(p.self.transportOffsetY);w.f32(p.self.transportOffsetZ); p.send(Message::Position, p.session, w, p.host); p.lastSend = p.now;
        }
        if (!p.pendingCommands.empty()) {
            auto& action = p.pendingCommands.front();
            if (p.now - action.enqueued > 10) { p.fail("LAN action acknowledgement timed out; reconnect to the saved realm"); return; }
            if (p.now - action.lastSent >= 0.2) {
                Writer w; w.u32(action.id); w.u8(uint8_t(action.command.action)); w.u64(action.command.target); w.u32(action.command.id);
                w.u32(action.command.bid); w.u32(action.command.buyout); w.u32(action.command.durationMinutes);
                if (action.command.action == LocalAction::ListAuction) w.u16(action.command.auctionCount);
                if (action.command.action == LocalAction::BankMove || action.command.action == LocalAction::BankDepositSlot || action.command.action==LocalAction::BackpackMove || action.command.action==LocalAction::BankWithdrawSlot) { w.u16(action.command.bankSourceCount); w.u16(action.command.bankDestinationCount); }
                w.u64(action.command.serviceNpcGuid);
                if(action.command.action==LocalAction::MailSend){
                    w.name(action.command.mailRecipient);w.text(action.command.mailSubject);w.text(action.command.mailBody);
                    w.u8(uint8_t(action.command.mailAttachments.size()));
                    for(const auto& item:action.command.mailAttachments){w.u32(item.item);w.u16(item.count);w.u16(item.sourceCount);w.u8(item.bag);}
                }
                p.send(Message::Command, p.session, w, p.host); action.lastSent = p.now;
            }
        }
    } else if (p.authoritative()) {
        p.syncParty();
        const float step = std::min(deltaTime, 0.25f);
        p.dirty = p.gameplay.tick(step, p.activePlayers()) || p.dirty;
        // Populate here rather than in setPlayerbots, because the host screen
        // calls that before the realm starts: at that moment the state is still
        // Stopped, so authoritative() is false, realmId is zero and `self` is
        // not a character yet. populate() was therefore skipped every single
        // time, the flag read as enabled, and the world had no bots in it - the
        // checkbox appeared to do nothing at all.
        //
        // Doing it on the first authoritative tick also covers the realm being
        // reloaded from a save, which no start path passes through.
        if (!p.botDirector.enabled() && !p.botsReported) {
            p.botsReported = true;
            LOG_INFO("[LOCAL_BOTS] not populating: playerbots are switched off for this realm");
        }
        if (p.botDirector.enabled() && p.botDirector.botCount() && p.botPlayers.empty() &&
            !p.self.instanceId && !p.self.flight.active && !p.self.dead) {
            p.botsReported = true;
            p.botDirector.setSeed(static_cast<uint32_t>(p.realmId ^ (p.realmId >> 32)));
            p.botDirector.populate(p.gameplay.content(), p.self, p.botPlayers);
            for (auto& bot : p.botPlayers) p.gameplay.initializePlayer(bot, false);
            p.refreshPlayers();
            p.dirty = true;
            LOG_INFO("[LOCAL_BOTS] populated count=", p.botPlayers.size(),
                     " seed=", p.realmId, " around map=", p.self.mapId,
                     " xyz=", p.self.x, ",", p.self.y, ",", p.self.z);
        }
        // After the world, so a bot reacts to the state the world just left it
        // in rather than to the previous frame's.
        p.dirty = p.botDirector.tick(step, p.gameplay.content(), p.botPlayers) || p.dirty;
        p.dirty = p.migrateAuctionMail() || p.dirty;
        p.refreshPlayers();p.maintainSocial();
        if (p.state == LocalRealmState::Hosting) {
            p.refreshPlayers(); p.refreshStatus();
            // At most four recipients per frame. Each full roster is at most
            // 15 small packets, not a fragmented 16KB datagram. Small realms
            // retain 10Hz updates; large realms trade update rate for bounded work.
            std::vector<Writer> pages;
            const size_t count = p.peers.size();
            size_t sent = 0;
            for (size_t checked = 0; checked < count && sent < 4; ++checked) {
                p.sendCursor %= count; auto& peer = p.peers[p.sendCursor++];
                if (p.now - peer.lastSnapshot < SendInterval) continue;
                if (pages.empty()) pages = p.snapshotPages();
                for (const auto& page : pages) p.send(Message::Snapshot, peer.session, page, peer.address);
                if (p.now - peer.lastClock >= 1.0) { p.clock(peer); peer.lastClock = p.now; }
                p.progress(peer); p.history(peer); p.world(peer, ++p.worldTick);
                p.auctionBoard(peer, ++p.auctionTick);
                if(p.now-peer.lastMerchantSnapshot>=0.5)p.merchantState(peer);
                if(p.now-peer.lastPartySnapshot>=0.5)p.sendParty(peer);
                if(p.now-peer.lastSocialSnapshot>=0.2)p.sendSocial(peer);
                peer.lastSnapshot = p.now; ++sent;
            }
        }
        if (p.dirty && p.now - p.lastSave >= SaveInterval) {
            // A failed disk must not trigger a synchronous save on every frame.
            p.lastSave = p.now; p.saveRealm(true);
        }
    }
}
bool LocalRealm::setLocalTransportOffset(uint32_t entry,float x,float y,float z,float heading) {
    auto& p=*impl_;
    if(!entry || p.self.transportEntry!=entry || p.worldLoading ||
       !std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(heading)||
       std::abs(x)>100||std::abs(y)>100||std::abs(z)>100)return false;
    p.self.transportOffsetX=x;p.self.transportOffsetY=y;p.self.transportOffsetZ=z;p.self.transportLastYaw=heading;
    if(p.authoritative()) {if(auto* r=p.findSaved(p.self.guid))r->player=p.self;p.dirty=true;}
    return true;
}

bool LocalRealm::setLocalPosition(uint32_t map, float x, float y, float z, float o, uint8_t movement) {
    auto& p = *impl_;
    if (!ready() || p.self.dead || (p.self.instanceId && map != p.self.mapId) || !validPosition(map, x, y, z, o)) return false;
    if (p.worldLoading || p.self.flight.active || (p.self.transportEntry && map != p.self.mapId)) return false;
    if (p.self.transportEntry) for (const auto& hull : p.gameplay.transports()) {
        if (hull.entry != p.self.transportEntry || hull.mapId != map) continue;
        const float c = std::cos(hull.orientation), s = std::sin(hull.orientation);
        const float dx = x - hull.x, dy = y - hull.y;
        p.self.transportOffsetX = c * dx + s * dy;
        p.self.transportOffsetY = -s * dx + c * dy;
        p.self.transportOffsetZ = z - hull.z;
        p.self.transportLastYaw = hull.orientation;
        break;
    }
    // The movement bits are part of the report even when the character has not
    // moved a millimetre: landing is a change of state rather than of place, and
    // the tick that sees the falling bit drop is the one that has to charge for
    // the fall. Returning early on an unchanged position used to swallow it.
    const uint8_t state = uint8_t(movement & kLocalMovementMask);
    if (p.self.mapId == map && p.self.x == x && p.self.y == y && p.self.z == z &&
        p.self.orientation == o && p.self.movementState == state) return true;
    p.self.movementState = state;
    p.self.mapId = map; p.self.x = x; p.self.y = y; p.self.z = z;
    p.self.orientation = std::fmod(o, 6.28318530718f);
    if (p.self.orientation < 0) p.self.orientation += 6.28318530718f;
    if (p.authoritative()) {
        if (auto* record = p.findSaved(p.self.guid)) record->player = p.self;
        p.dirty = true; p.refreshPlayers();
    } else for (auto& player : p.players) if (player.guid == p.self.guid) player = p.self;
    return true;
}
bool LocalRealm::command(const LocalRealmCommand& cmd) {
    auto& p = *impl_;
    ++p.actionStatusRevision;
    if(cmd.action==LocalAction::MailSend && (cmd.mailRecipient.size()>16 || !localMailTextValid(cmd.mailSubject,64) || !localMailTextValid(cmd.mailBody,160) || cmd.mailAttachments.size()>12)){p.actionStatus="Local mail supports 64 subject bytes, 160 body bytes and 12 attachments";return false;}
    if (!ready()) { p.actionStatus = "Local realm is not ready"; return false; }
    if (p.authoritative()) {
        const bool ok = p.runCommand(p.self, cmd, p.actionStatus);
        if(mailActionKind(cmd.action)){p.mailResultSuccess=ok;++p.mailResultRevision;}
        p.dirty = p.dirty || ok; p.refreshPlayers();
        LOG_INFO("[local_realm] Local action kind=", int(cmd.action), " ok=", ok, " result=", p.actionStatus);
        return ok;
    }
    if (p.pendingCommands.size() >= 16) { p.actionStatus = "Wait for pending LAN actions"; return false; }
    p.pendingCommands.push_back({++p.nextCommand, cmd, -1, p.now});
    p.actionStatus = "Waiting for host";
    return true;
}
bool LocalRealm::startReadyCheck(){return command({LocalAction::ReadyStart});}
bool LocalRealm::answerReadyCheck(uint32_t id,bool ready){return command({LocalAction::ReadyAnswer,ready?1u:0u,id});}
const LocalReadyCheck& LocalRealm::readyCheck()const{return impl_->localReady;}
double LocalRealm::readyTimeLeft()const{return impl_->localReady.state==1?std::clamp(impl_->localReady.deadline-impl_->now,0.0,30.0):0;}
const LocalTrade& LocalRealm::tradeView()const{return impl_->localTrade;}
uint64_t LocalRealm::socialRevision()const{return impl_->socialRevision;}
bool LocalRealm::tradeAction(LocalAction action,uint32_t trade,uint32_t revision,uint64_t value,uint32_t bag,uint32_t slot,uint32_t expectedItem,uint16_t expectedCount){
    if(action<LocalAction::TradeRequest || action>LocalAction::TradeCancel)return false;
    LocalRealmCommand cmd{action,value,trade,revision,bag,slot};cmd.serviceNpcGuid=(uint64_t(expectedItem)<<32)|expectedCount;return command(cmd);
}
const std::vector<std::string>& LocalRealm::ignoredNames()const{return impl_->ignoreNames;}
bool LocalRealm::isIgnored(const std::string& name)const{return impl_->ignored(name);}
bool LocalRealm::changeIgnore(const std::string& name,bool add) {
    auto& p=*impl_;auto reject=[&](const char* text){p.actionStatus=text;++p.actionStatusRevision;return false;};
    if(!ready() || !validName(name) || localChatNameEqual(name,p.self.name))return reject("Choose another valid player name");
    if(!p.ignoreWritable)return reject("Ignore list could not be read; its file was preserved");
    auto names=p.ignoreNames;auto it=std::find_if(names.begin(),names.end(),[&](const auto& old){return localChatNameEqual(old,name);});
    if(add){if(it!=names.end())return true;if(names.size()>=50)return reject("Ignore list is full");names.push_back(name);}
    else {if(it==names.end())return true;names.erase(it);}
    Writer w;w.u32(0x57494731);w.u64(p.identity.a);w.u64(p.identity.b);w.u8(uint8_t(names.size()));for(const auto& row:names)w.name(row);w.u32(checksum(w.bytes.data(),w.bytes.size()));
    if(!atomicWrite(p.ignorePath(),w.bytes,false))return reject("Ignore list was not saved; no setting changed");
    p.ignoreNames.swap(names);if(add)std::erase_if(p.chatInbox,[&](const auto& line){return line.channel!=LocalChatChannel::WhisperInform && p.ignored(line.senderName);});
    ++p.socialRevision;++p.actionStatusRevision;p.actionStatus=add?"Player ignored":"Player removed from ignore list";return true;
}
bool LocalRealm::sendChat(LocalChatChannel channel,const std::string& text,const std::string& target) {
    auto& p=*impl_;
    auto reject=[&](const char* reason){p.actionStatus=reason;++p.actionStatusRevision;return false;};
    if(!ready())return reject("The local chat session is not connected");
    if(!validLocalChatText(text) || target.size()>16)return reject("Chat needs 1-160 UTF-8 bytes and a valid target name");
    if(channel!=LocalChatChannel::Say && channel!=LocalChatChannel::Party && channel!=LocalChatChannel::Yell && channel!=LocalChatChannel::Whisper)
        return reject("This chat channel is not available in a local realm");
    if(p.authoritative()) {
        p.expirePeers();p.syncParty(true);
        std::string result;
        const bool accepted=p.routeChat(p.self.guid,channel,text,target,p.localChatRate,result);
        if(!accepted){p.actionStatus=result;++p.actionStatusRevision;}
        return accepted;
    }
    if(p.chatPending.size()>=8 || p.chatRequestSerial==UINT32_MAX)return reject("Chat queue is full; wait for confirmation");
    try{p.chatPending.push_back({p.chatRequestSerial+1,channel,text,target,-1,p.now});++p.chatRequestSerial;}
    catch(const std::bad_alloc&){return reject("Not enough memory to queue chat");}
    return true;
}
std::vector<LocalChatLine> LocalRealm::takeChatMessages(){std::vector<LocalChatLine> out;out.swap(impl_->chatInbox);return out;}
bool LocalRealm::partyCommand(LocalPartyAction action,uint64_t target,uint32_t inviteId) {
    if(unsigned(action)>5)return false;
    LocalRealmCommand cmd{LocalAction(unsigned(LocalAction::PartyInvite)+unsigned(action)),target,inviteId};return command(cmd);
}
uint64_t LocalRealm::partyPlayerByName(const std::string& name) const {
    const auto lower=[](std::string v){for(auto& c:v)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return v;};
    const auto key=lower(name);uint64_t found=0;
    // Group management must keep working when a member leaves the nearby-player
    // snapshot (another map or instance). Deduplicate the same GUID in both lists.
    for(const auto& p:impl_->localParty.members)if(lower(p.name)==key){if(found && found!=p.guid)return 0;found=p.guid;}
    for(const auto& p:impl_->players)if(lower(p.name)==key){if(found && found!=p.guid)return 0;found=p.guid;}
    return found;
}
const LocalPartyView& LocalRealm::partyView() const {return impl_->localParty;}
uint64_t LocalRealm::partyRevision() const {return impl_->partyRevision;}
uint64_t LocalRealm::partyRosterRevision() const {return impl_->partyRosterRevision;}
bool LocalRealm::attack(uint64_t guid) { return command({LocalAction::Attack, guid, 0}); }
bool LocalRealm::stopAttack() { return command({LocalAction::StopAttack, 0, 0}); }
bool LocalRealm::cancelStatAura(uint32_t spell) {return command({LocalAction::CancelStatAura,0,spell});}
bool LocalRealm::castSpell(uint32_t spell, uint64_t guid) { return command({LocalAction::CastSpell, guid, spell}); }
bool LocalRealm::acceptQuest(uint32_t quest, uint64_t guid) { return command({LocalAction::AcceptQuest, guid, quest}); }
bool LocalRealm::turnInQuest(uint32_t quest, uint64_t guid,uint32_t rewardChoice) {
    LocalRealmCommand cmd{LocalAction::TurnInQuest,guid,quest};cmd.bid=rewardChoice;return command(cmd);
}
bool LocalRealm::loot(uint64_t guid) { return command({LocalAction::Loot, guid, 0}); }
bool LocalRealm::equipItem(uint32_t item, uint8_t slot) { return command({LocalAction::EquipItem, slot == 255 ? 0ULL : uint64_t(slot) + 1, item}); }
bool LocalRealm::unequipItem(uint8_t slot) { return command({LocalAction::UnequipItem, 0, slot}); }
bool LocalRealm::abandonQuest(uint32_t quest) { return command({LocalAction::AbandonQuest, 0, quest}); }
bool LocalRealm::cancelCast() { return command({LocalAction::CancelCast, 0, 0}); }
bool LocalRealm::completeIntro() { return command({LocalAction::CompleteIntro, 0, 0}); }
bool LocalRealm::useItem(uint32_t item) { return command({LocalAction::UseItem, 0, item}); }
bool LocalRealm::dismount() {return command({LocalAction::Dismount,0,0});}
bool LocalRealm::respawn() { return command({LocalAction::Respawn, 0, 0}); }
bool LocalRealm::interact(uint64_t guid) { return command({LocalAction::Interact, guid, 0}); }
bool LocalRealm::enterPortal(uint32_t id, bool privateInstance) { return command({LocalAction::EnterPortal, privateInstance ? 1ULL : 0ULL, id}); }
bool LocalRealm::leaveInstance() { return command({LocalAction::LeaveInstance, 0, 0}); }
std::vector<LocalRealmPortal> LocalRealm::availablePortals() const {
    std::vector<LocalRealmPortal> result;
    if (!ready()) return result;
    for (const auto& portal : impl_->gameplay.portals()) if (impl_->gameplay.insidePortal(portal.id, impl_->self)) result.push_back(portal);
    return result;
}
std::vector<LocalQuestDefinition> LocalRealm::questsForNpc(uint32_t entry) const { return content().questsForNpc(entry); }

// --- Travel ----------------------------------------------------------------
//
// A guest keeps its own copy of the network for exactly two reasons: it needs
// to draw the flight list and to place the transports it can see. Neither is
// authority - every flight and every boarding is executed and validated on the
// host, which is why takeFlight below sends a command rather than moving
// anyone. The rows themselves come from each console's own client DBCs, which
// both ends must already have to render the world at all.
bool LocalRealm::setTravelNetwork(std::vector<LocalTaxiNode> nodes,
                                  std::vector<LocalTaxiPath> paths,
                                  std::vector<LocalTaxiWaypoint> waypoints) {
    if (!impl_) return false;
    std::string error;
    if (!impl_->gameplay.setTravelNetwork(std::move(nodes), std::move(paths),
                                          std::move(waypoints), error)) {
        impl_->status = "Taxi data unavailable: " + error;
        return false;
    }
    return true;
}

const LocalTravelNetwork& LocalRealm::travel() const { return impl_->gameplay.travel(); }

const std::vector<LocalTransportState>& LocalRealm::transports() const {
    return impl_->gameplay.transports();
}

const LocalRealmNpc* LocalRealm::nearbyFlightMaster() const {
    if (!ready()) return nullptr;
    const auto& self = impl_->self;
    const LocalRealmNpc* best = nullptr;
    float bestDistSq = 8.0f * 8.0f;   // the same reach as any other interaction
    for (const auto& npc : npcs()) {
        if (!npc.flightMaster || npc.dead) continue;
        if (npc.mapId != self.mapId || npc.instanceId != self.instanceId) continue;
        const float dx = npc.x - self.x, dy = npc.y - self.y, dz = npc.z - self.z;
        const float d = dx * dx + dy * dy + dz * dz;
        if (d <= bestDistSq) { bestDistSq = d; best = &npc; }
    }
    return best;
}

std::vector<uint32_t> LocalRealm::flightDestinations() const {
    if (!ready()) return {};
    const LocalRealmNpc* master = nearbyFlightMaster();
    if (!master) return {};
    // The node the player is standing at counts as discovered whether or not
    // it has been saved yet, so the list is not empty on a first visit.
    auto known = impl_->self.knownTaxiNodes;
    if (std::find(known.begin(), known.end(), master->taxiNodeId) == known.end()) {
        known.push_back(master->taxiNodeId);
    }
    auto player=impl_->self;player.knownTaxiNodes=std::move(known);return impl_->gameplay.flightDestinations(player,master->taxiNodeId);
}

// --- Playerbots and the auction house ---------------------------------------

void LocalRealm::setPlayerbots(bool enabled, size_t count) {
    if (!impl_) return;
    impl_->botDirector.setEnabled(enabled);
    impl_->botDirector.setBotCount(count);
    // Said out loud, both ways. Off is a perfectly ordinary answer here, and it
    // is indistinguishable in a log from the populate that used to be skipped -
    // which is exactly why that fault survived two builds: the roster was empty
    // and nothing anywhere said whether that was the player's choice or a bug.
    LOG_INFO("[LOCAL_BOTS] playerbots ", enabled ? "enabled" : "disabled",
             " count=", count);
    if (!enabled) {
        impl_->botDirector.clear(impl_->botPlayers);
        impl_->refreshPlayers();
        return;
    }
    // The roster itself is built on the first authoritative tick, not here.
    // The host screen calls this before the realm starts, when there is no
    // realm id to seed from and no character to gather around; see the tick.
}

bool LocalRealm::playerbotsEnabled() const {
    return impl_ && impl_->botDirector.enabled();
}





const std::vector<LocalAuction>& LocalRealm::auctions() const {
    // The host reads its own board; a guest reads the copy the host sent it.
    // Both are the same listings - the guest's is simply one tick behind.
    static const std::vector<LocalAuction> none;
    if (!impl_) return none;
    return impl_->authoritative() ? impl_->botDirector.auctions() : impl_->remoteAuctions;
}

// The three auction actions go through command() like every other action, so a
// guest's buyout is executed by the host and reported back through the ordinary
// action-result path rather than applied locally and undone by the next board
// update.
bool LocalRealm::buyoutAuction(uint32_t auctionId, uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::BuyoutAuction,0,auctionId};cmd.serviceNpcGuid=npcGuid;return command(cmd);
}

bool LocalRealm::bidAuction(uint32_t auctionId, uint32_t amount, uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::BidAuction,amount,auctionId};cmd.serviceNpcGuid=npcGuid;return command(cmd);
}

bool LocalRealm::listAuction(uint32_t itemId, uint16_t count) {
    return command({LocalAction::ListAuction, count, itemId});
}

// --- Instances, merchants, repair and trainers -------------------------------
//
// The lookups below are drawing aids: they answer "what is in front of me and
// what does it sell" so a panel can be built. None of them is authority. Every
// purchase, sale and lesson goes through command() to LocalRealm::Impl::
// runCommand and from there into the ruleset, where the NPC is found again on
// the authority - which is what stops a guest buying from a merchant it drew
// for itself.
bool LocalRealm::setClientMaps(std::vector<LocalMapDefinition> maps) {
    return impl_->gameplay.setClientMaps(std::move(maps), impl_->error);
}
const LocalMapDefinition* LocalRealm::clientMap(uint32_t mapId) const {
    return impl_->gameplay.clientMap(mapId);
}
const std::vector<LocalInstanceState>& LocalRealm::instances() const {
    return impl_->gameplay.instances();
}
bool LocalRealm::setSkillLines(const std::vector<LocalSkillLine>& lines) {
    return impl_->gameplay.setSkillLines(lines, impl_->error);
}
const std::vector<LocalSkillLine>& LocalRealm::skillLines() const {
    return impl_->gameplay.skillLines();
}
const LocalRealmNpc* LocalRealm::nearbyBanker(uint64_t npcGuid) const {
    return ready() ? impl_->gameplay.serviceNpc(impl_->self, kLocalNpcFlagBanker, npcGuid) : nullptr;
}
const LocalRealmNpc* LocalRealm::nearbyVendor(uint64_t npcGuid) const {
    return ready() ? impl_->gameplay.serviceNpc(impl_->self, kLocalNpcFlagAnyVendor, npcGuid) : nullptr;
}
const LocalRealmNpc* LocalRealm::nearbyRepairer(uint64_t npcGuid) const {
    return ready() ? impl_->gameplay.serviceNpc(impl_->self, kLocalNpcFlagRepair, npcGuid) : nullptr;
}
const LocalRealmNpc* LocalRealm::nearbyClassTrainer() const {
    return ready() ? impl_->gameplay.serviceNpc(impl_->self, kLocalNpcFlagTrainerClass) : nullptr;
}
const LocalRealmNpc* LocalRealm::nearbyProfessionTrainer() const {
    return ready() ? impl_->gameplay.serviceNpc(impl_->self, kLocalNpcFlagTrainerProfession) : nullptr;
}
const LocalRealmNpc* LocalRealm::nearbyInnkeeper() const {
    return ready() ? impl_->gameplay.serviceNpc(impl_->self, kLocalNpcFlagInnkeeper) : nullptr;
}
std::vector<uint32_t> LocalRealm::vendorStock(uint64_t npcGuid) const {
    if (impl_->state == LocalRealmState::Connected) {
        std::vector<uint32_t> rows;
        const auto* npc = nearbyVendor(npcGuid);
        if (npc && npc->guid == impl_->merchantGuid && !impl_->self.dead && !impl_->self.flight.active)
            for (const auto& row : impl_->remoteMerchant) rows.push_back(row.itemId);
        return rows;
    }
    return ready() ? impl_->gameplay.vendorStock(impl_->self, npcGuid) : std::vector<uint32_t>{};
}
int32_t LocalRealm::vendorRemaining(uint32_t itemId, uint64_t npcGuid) const {
    if (impl_->state == LocalRealmState::Connected) {
        const auto* npc = nearbyVendor(npcGuid);
        if (npc && npc->guid == impl_->merchantGuid && !impl_->self.dead && !impl_->self.flight.active)
            for (const auto& row : impl_->remoteMerchant) if (row.itemId == itemId) return row.remaining;
        return 0;
    }
    return ready() ? impl_->gameplay.vendorRemaining(impl_->self, itemId, npcGuid) : 0;
}
void LocalRealm::refreshMerchant(uint64_t npcGuid) {
    auto& p = *impl_; if (p.state != LocalRealmState::Connected) return;
    const bool changed = p.merchantGuid != npcGuid || !p.merchantRequest;
    if (!changed && p.lastMerchantRequestSend >= 0 && p.now - p.lastMerchantRequestSend < 1.0) return;
    if (changed) {
        p.merchantGuid = npcGuid; if (!++p.merchantRequest) ++p.merchantRequest;
        p.remoteMerchant.clear(); p.remoteBuyback.clear(); p.collectingMerchant = 0;
        p.merchantReceived.fill(false); for (auto& rows : p.merchantChunks) rows.clear();
    }
    Writer w; w.u32(p.merchantRequest); w.u64(npcGuid); p.send(Message::MerchantQuery, p.session, w, p.host);
    p.lastMerchantRequestSend = p.now;
}
std::vector<LocalMerchantBuyback> LocalRealm::vendorBuyback(uint64_t npcGuid) const {
    if (!ready() || impl_->self.dead || impl_->self.flight.active) return {};
    const auto* npc = nearbyVendor(npcGuid); if (!npc) return {};
    if (impl_->state == LocalRealmState::Connected)
        return npc->guid == impl_->merchantGuid ? impl_->remoteBuyback : std::vector<LocalMerchantBuyback>{};
    return impl_->self.buyback;
}
bool LocalRealm::buybackItem(uint32_t entryId, uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::BuybackItem, 0, entryId}; cmd.serviceNpcGuid = npcGuid; return command(cmd);
}
uint32_t LocalRealm::vendorBuyPrice(uint32_t itemId, uint16_t count) const {
    const auto* item = content().item(itemId);
    return item ? localVendorBuyPrice(*item, count) : 0;
}
uint32_t LocalRealm::vendorSellPrice(uint32_t itemId, uint16_t count) const {
    const auto* item = content().item(itemId);
    return item ? localVendorSellPrice(*item, count) : 0;
}
std::vector<uint32_t> LocalRealm::trainableSpells(uint64_t npcGuid) const {
    return ready() ? impl_->gameplay.trainableSpells(impl_->self,npcGuid) : std::vector<uint32_t>{};
}
bool LocalRealm::sellToVendor(uint32_t itemId, uint16_t count, uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::SellToVendor, count, itemId}; cmd.serviceNpcGuid = npcGuid;
    return command(cmd);
}
bool LocalRealm::buyFromVendor(uint32_t itemId, uint16_t count, uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::BuyFromVendor, count, itemId}; cmd.serviceNpcGuid = npcGuid;
    return command(cmd);
}
bool LocalRealm::repairEquipment(uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::RepairEquipment, 0, 0}; cmd.serviceNpcGuid = npcGuid;
    return command(cmd);
}
bool LocalRealm::learnTalent(uint32_t id,uint32_t rank){return command({LocalAction::LearnTalent,0,id,rank});}
bool LocalRealm::resetTalents(){return command({LocalAction::ResetTalents});}
bool LocalRealm::learnSpell(uint32_t spellId,uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::LearnSpell,0,spellId};cmd.serviceNpcGuid=npcGuid;return command(cmd);
}
bool LocalRealm::trainRiding(uint16_t rank,uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::TrainRiding,0,rank};cmd.serviceNpcGuid=npcGuid;return command(cmd);
}
bool LocalRealm::discoverTaxi(uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::DiscoverTaxi};cmd.serviceNpcGuid=npcGuid;return command(cmd);
}
bool LocalRealm::depositBankItem(uint32_t itemId,uint16_t count,uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::BankDeposit,count,itemId};cmd.serviceNpcGuid=npcGuid;return command(cmd);
}
bool LocalRealm::depositBankFromSlot(uint32_t bagSlot,uint16_t count,LocalItemStack expectedSource,uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::BankDepositFromSlot,count,bagSlot};
    cmd.bid=expectedSource.itemId;cmd.buyout=expectedSource.count;cmd.serviceNpcGuid=npcGuid;return command(cmd);
}
bool LocalRealm::moveBackpackItem(uint32_t source,uint32_t destination,uint16_t count,LocalItemStack expectedSource,LocalItemStack expectedDestination,uint64_t banker,bool fromBank){
    LocalRealmCommand cmd{fromBank?LocalAction::BankWithdrawSlot:LocalAction::BackpackMove,count,source};
    cmd.buyout=destination;cmd.bid=expectedSource.itemId;cmd.durationMinutes=expectedDestination.itemId;
    cmd.bankSourceCount=expectedSource.count;cmd.bankDestinationCount=expectedDestination.count;cmd.serviceNpcGuid=banker;return command(cmd);
}
bool LocalRealm::depositBankSlot(uint32_t bagSlot,uint32_t bankSlot,uint16_t count,
        LocalItemStack expectedSource,LocalItemStack expectedDestination,uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::BankDepositSlot,count,bagSlot};
    cmd.buyout=bankSlot;cmd.bid=expectedSource.itemId;cmd.durationMinutes=expectedDestination.itemId;
    cmd.bankSourceCount=expectedSource.count;cmd.bankDestinationCount=expectedDestination.count;
    cmd.serviceNpcGuid=npcGuid;return command(cmd);
}
bool LocalRealm::withdrawBankItem(uint32_t slot,uint16_t count,uint32_t expectedItem,uint64_t npcGuid,uint16_t expectedCount) {
    LocalRealmCommand cmd{LocalAction::BankWithdraw,count,slot};cmd.bid=expectedItem;cmd.buyout=expectedCount;cmd.serviceNpcGuid=npcGuid;return command(cmd);
}
bool LocalRealm::moveBankItem(uint32_t sourceSlot,uint32_t destinationSlot,uint16_t count,
        LocalItemStack expectedSource,LocalItemStack expectedDestination,uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::BankMove,count,sourceSlot};
    cmd.buyout=destinationSlot;cmd.bid=expectedSource.itemId;cmd.durationMinutes=expectedDestination.itemId;
    cmd.bankSourceCount=expectedSource.count;cmd.bankDestinationCount=expectedDestination.count;
    cmd.serviceNpcGuid=npcGuid;return command(cmd);
}
bool LocalRealm::craftRecipe(uint32_t recipeId,uint32_t count) {return command({LocalAction::CraftItem,count,recipeId});}
bool LocalRealm::unlearnProfession(uint32_t skillId) {return command({LocalAction::UnlearnProfession,0,skillId});}
bool LocalRealm::learnRecipe(uint32_t recipeId,uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::LearnRecipe,0,recipeId};cmd.serviceNpcGuid=npcGuid;return command(cmd);
}
bool LocalRealm::learnProfession(uint32_t skillId,uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::LearnProfession,0,skillId};cmd.serviceNpcGuid=npcGuid;return command(cmd);
}
bool LocalRealm::trainProfessionRank(uint32_t skillId,uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::TrainProfessionRank,0,skillId};cmd.serviceNpcGuid=npcGuid;return command(cmd);
}
bool LocalRealm::setHome(uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::SetHome,0,0};cmd.serviceNpcGuid=npcGuid;return command(cmd);
}
bool LocalRealm::returnHome() { return command({LocalAction::ReturnHome, 0, 0}); }

bool LocalRealm::takeFlight(uint32_t destinationNode) {
    return command({LocalAction::TakeFlight, 0, destinationNode});
}

bool LocalRealm::boardTransport(uint32_t transportEntry) {
    return command({LocalAction::BoardTransport, 0, transportEntry});
}

bool LocalRealm::leaveTransport() {
    return command({LocalAction::LeaveTransport, 0, 0});
}
const std::vector<LocalRealmNpc>& LocalRealm::npcs() const {
    return impl_->authoritative() ? impl_->npcView : impl_->gameplay.npcs();
}
const LocalWorldContent& LocalRealm::content() const { return impl_->gameplay.content(); }
const std::string& LocalRealm::actionStatus() const { return impl_->actionStatus; }
uint64_t LocalRealm::actionStatusRevision() const { return impl_->actionStatusRevision; }
bool LocalRealm::save() { return impl_->saveRealm(); }
void LocalRealm::stop() {
    if (!impl_) return;
    auto& p = *impl_;
    if (p.authoritative()) p.saveRealm();
    p.sendDeparture();
    if(p.socket!=INVALID_SOCK){net::closeSocket(p.socket);p.socket=INVALID_SOCK;}
    p.partyDirector=LocalPartyDirector{};p.commitParty({});p.gameplay.setPartyMembership({});
    p.clearSocial();p.clearChat();p.clearMail();p.pendingCommands.clear();p.pendingProgress.reset();p.collectingProgress=0;p.progressReceived.fill(false);p.lobbyPending=false;p.worldLoading=false;
    p.remoteAuctions.clear();p.auctionSequence=0;p.collectingAuctions=0;p.auctionParts=0;
    p.auctionReceived.fill(false);for(auto& page:p.auctionChunks)page.clear();
    p.session=p.joinNonce=0;
    p.state = LocalRealmState::Stopped; p.status = "Local realm stopped";
    p.players.clear(); p.peers.clear();
}
size_t LocalRealm::playerLimit() const { return impl_->playerLimit; }
float LocalRealm::worldTimeHours() const { return impl_->dayClock.hours(impl_->now); }
LocalRealmState LocalRealm::state() const { return impl_->state; }
bool LocalRealm::ready() const { return impl_->authoritative() || impl_->state == LocalRealmState::Connected; }
bool LocalRealm::isHost() const { return impl_->state == LocalRealmState::Hosting; }
const LocalRealmPlayer* LocalRealm::localPlayer() const { return ready() ? &impl_->self : nullptr; }
const std::vector<LocalRealmPlayer>& LocalRealm::players() const { return impl_->players; }
const std::string& LocalRealm::error() const { return impl_->error; }
const std::string& LocalRealm::status() const { return impl_->status; }
uint16_t LocalRealm::port() const { return impl_->port; }

} // namespace wowee::game

namespace wowee::game {
bool LocalRealm::listAuction(uint32_t itemId, uint16_t count, uint32_t bid, uint32_t buyout, uint32_t minutes) {
    return command({LocalAction::ListAuction, count, itemId, bid, buyout, minutes});
}
bool LocalRealm::listAuctionStacks(uint32_t itemId, uint16_t count, uint16_t stacks,
        uint32_t bid, uint32_t buyout, uint32_t minutes, uint64_t npcGuid) {
    LocalRealmCommand cmd{LocalAction::ListAuction,count,itemId,bid,buyout,minutes};
    cmd.serviceNpcGuid=npcGuid;
    cmd.auctionCount=stacks;
    return command(cmd);
}
bool LocalRealm::cancelAuction(uint32_t id, uint64_t npcGuid) {LocalRealmCommand cmd{LocalAction::CancelAuction,0,id};cmd.serviceNpcGuid=npcGuid;return command(cmd);}
std::vector<LocalMail> LocalRealm::inbox() const {
    if(!ready())return {};if(impl_->state==LocalRealmState::Connected)return impl_->remoteMail;
    std::vector<LocalMail> rows;for(const auto& m:impl_->mailbox.messages)if(m.recipient==impl_->self.guid)rows.push_back(m);return rows;
}
uint64_t LocalRealm::mailRevision()const{return impl_->mailRevision;}
uint64_t LocalRealm::mailResultRevision()const{return impl_->mailResultRevision;}
bool LocalRealm::mailResultSuccess()const{return impl_->mailResultSuccess;}
bool LocalRealm::mailAccess(uint64_t service)const {
    if(!ready())return false;
    if(impl_->state!=LocalRealmState::Connected)return impl_->mailReach(impl_->self,service);
    const auto& p=impl_->self;if(p.dead || p.flight.active || p.castingSpellId || p.transportEntry)return false;
    if(nearbyLocalMailbox(content(),p,service))return true;
    for(const auto& n:npcs())if(n.guid==service && n.innkeeper && localNpcInTalkRange(p,n))return true;return false;
}
void LocalRealm::requestMail(uint64_t service){
    auto& p=*impl_;if(p.state!=LocalRealmState::Connected || !mailAccess(service) || (p.lastMailRequest>=0 && p.now-p.lastMailRequest<1))return;
    Writer w;w.u64(service);p.send(Message::MailQuery,p.session,w,p.host);p.lastMailRequest=p.now;
}
bool LocalRealm::sendMail(uint64_t service,const std::string& recipient,const std::string& subject,const std::string& body,uint32_t money,uint32_t cod,const std::vector<LocalTradeItem>& attachments){
    LocalRealmCommand cmd{LocalAction::MailSend};cmd.serviceNpcGuid=service;cmd.mailRecipient=recipient;cmd.mailSubject=subject;cmd.mailBody=body;cmd.bid=money;cmd.buyout=cod;cmd.mailAttachments=attachments;return command(cmd);
}
bool LocalRealm::mailAction(LocalAction action,uint64_t service,uint32_t id,uint32_t slot){
    if(action<LocalAction::MailTakeMoney || action>LocalAction::MailRead)return false;
    LocalRealmCommand cmd{action,slot,id};cmd.serviceNpcGuid=service;return command(cmd);
}
} // namespace wowee::game
