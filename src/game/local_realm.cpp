#include "game/local_realm.hpp"
#include "game/lan_discovery.hpp"
#include "game/local_day_clock.hpp"
#include "core/local_time.hpp"
#include "core/snapshot_writer.hpp"
#include <ctime>
#ifdef WOWEE_PS4
#include <orbis/Rtc.h>
#endif
#include "game/local_services.hpp"
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
constexpr uint8_t Version = lan::GameplayVersion; // Transport offsets, clock and auction commands.
constexpr uint8_t SaveVersion = 9;   // Six durable rune cooldowns; reader preserves save versions 1-8.
constexpr size_t HeaderSize = 20, MaxPacket = 1400, MaxSavedPlayers = 128;
constexpr size_t PublicPlayerBytes = 49 + 34 + 4 * kLocalEquipmentSlotCount + 6 + 4;
constexpr size_t PlayersPerPage = 7;
constexpr size_t NpcWireBytes = 91, NpcsPerPage = 14;
constexpr size_t MaxNpcPages = (LocalGameplay::MaxNpcs + NpcsPerPage - 1) / NpcsPerPage;
static_assert(HeaderSize + 7 + NpcsPerPage * NpcWireBytes <= MaxPacket,
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
    1 + LocalGameplay::MaxInventory * 6 + 1 + LocalGameplay::MaxQuests * 14 +
    1 + LocalGameplay::MaxSpells * 4 + 1 + LocalGameplay::MaxCooldowns * 8 + 25 +
    // Professions (count plus six bytes each), the recipes a profession taught
    // (count plus four bytes each) and the inn binding.
    1 + LocalGameplay::MaxProfessions * 6 + 1 + LocalGameplay::MaxRecipes * 4 +
    1 + 4 + 16 + 4 + 25 + 17 + 20 + 12 + 4;
static_assert(MaxOwnerProgressBytes <= MaxPacket, "Bounded owner progress must fit one datagram");
constexpr size_t HistoryPageEntries = 256;
constexpr size_t MaxHistoryPages = (LocalGameplay::MaxCompletedQuests + HistoryPageEntries - 1) / HistoryPageEntries;
// Every character may reach the documented history cap; never write a save
// larger than the parser can subsequently read.
constexpr size_t MaxSaveSize = MaxSavedPlayers * (LocalGameplay::MaxCompletedQuests * 4 + 2048) + 65536;
constexpr double SendInterval = 0.1, HelloInterval = 0.5, JoinTimeout = 10.0;
constexpr double PeerTimeout = 8.0, LoadingTimeout = 180.0, SaveInterval = 5.0;
// A returning owner can replace its own silent session without waiting out a
// long world-load lease. Active clients continue to refresh at 10 Hz.
constexpr double ReconnectSilence = 2.0;
enum class Message : uint8_t { Hello = 1, Welcome, Position, Snapshot, Leave, Reject, Command, ActionResult, Progress, Npcs, History, HistoryAck, Clock, CharacterRequest, CharacterReply, AbortJoin, Auctions };
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
void writeProgress(Writer& w, const LocalRealmPlayer& p) {
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
    return r.valid;
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
    NpcFlagMask = 2047,
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
                   (n.professionTrainer ? NpcProfessionTrainer : 0) |
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
    n.innkeeper = (flags & NpcInnkeeper) != 0;
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
    struct Peer {
        sockaddr_in address{};
        Identity identity;
        uint64_t guid = 0, session = 0, joinNonce = 0;
        uint32_t sequence = 0, lastCommand = 0;
        bool lastCommandSuccess = false;
        std::string lastCommandStatus;
        double lastSeen = 0, lastSnapshot = -1, lastClock = -1;
        bool loading = true;
        double loadingSince = 0;
        uint32_t historyRevision = 0, historyCount = 0;
        size_t historyCursor = 0;
        std::vector<bool> historyAcked;
        std::vector<double> historySent;
    };
    LocalRealmState state = LocalRealmState::Stopped;
    std::string realmName = "LAN Realm";
    double discoveryWindow = -1; unsigned discoveryReplies = 0;
    std::string error, status = "Local realm stopped", directory, actionStatus;
    uint64_t actionStatusRevision = 0;
    LocalGameplay gameplay;
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
    uint8_t worldParts = 0;
    std::array<std::vector<LocalRealmNpc>, MaxNpcPages> worldChunks;
    std::array<bool, MaxNpcPages> worldReceived{};
    // The auction board as a guest sees it. The host's own board lives in
    // botDirector; a guest has no director, so the replicated copy is what
    // auctions() returns there. Assembled page by page like the NPC list, and
    // only swapped in once every page of a tick has arrived - a half-received
    // board would flicker listings in and out of the browse window.
    std::vector<LocalAuction> remoteAuctions;
    uint32_t auctionTick = 0, auctionSequence = 0, collectingAuctions = 0;
    uint8_t auctionParts = 0;
    std::array<std::vector<LocalAuction>, MaxAuctionPages> auctionChunks;
    std::array<bool, MaxAuctionPages> auctionReceived{};
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
        pendingCommands.clear();pendingProgress.reset();lobbyPending=false;worldLoading=false;
        players.clear();npcView.clear();
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
            return true;
        }
        identity = {uniqueId(), uniqueId()};
        Writer w; w.u32(IdentityMagic); w.u64(identity.a); w.u64(identity.b);
        w.u32(checksum(w.bytes.data(), w.bytes.size()));
        return atomicWrite(file, w.bytes, false) || fail("Cannot save local console identity");
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
        uint32_t sum = r.u32();
        if (!r.done() || sum != checksum(bytes.data(), bytes.size() - 4)) return false;
        LocalBotDirector validated;
        std::string validationError;
        if (!validated.restoreAuctions(loadedAuctions, validationError) ||
            !validated.restoreDeliveries(loadedDeliveries)) return false;
        gameplay.setTransportTime(loadedTransportTime);
        botDirector.restoreAuctions(loadedAuctions, validationError);
        botDirector.restoreDeliveries(loadedDeliveries);
        restoredInstances = std::move(loadedInstances);
        realmId = readRealmId; saved = std::move(records);
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
                record.player.knownRecipes.size() > LocalGameplay::MaxRecipes ||
                record.player.professions.size() > LocalGameplay::MaxProfessions ||
                !validHistory(record.player.completedQuestIds) || !validLocalRunes(record.player.runeCooldownMs)) {
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
    std::vector<LocalRealmPlayer*> activePlayers() {
        std::vector<LocalRealmPlayer*> active{&self};
        for (const auto& peer : peers) if (auto* record = findSaved(peer.guid)) active.push_back(&record->player);
        // Bots are simulated exactly like players: creatures aggro them, they
        // take damage, they die and respawn. Leaving them out here would have
        // produced characters that walk through hostile territory untouched.
        for (auto& bot : botPlayers) active.push_back(&bot);
        return active;
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
        writePosition(w, record->player); writeProgress(w, record->player); writeCast(w, record->player);
        w.u8(record->player.introSeen ? 1 : 0);
        send(Message::Progress, peer.session, w, peer.address);
    }
    void applyProgress(LocalRealmPlayer updated, uint32_t seq) {
        if (!newer(seq, progressSequence)) return;
        for (const auto& quest : updated.quests)
            if (std::binary_search(self.completedQuestIds.begin(), self.completedQuestIds.end(), quest.id)) return;
        // History pages are committed atomically, independent of unreliable
        // public snapshots and owner progress. Never restore an older copy.
        updated.completedQuestIds = self.completedQuestIds;
        if (updated.positionRevision == self.positionRevision || !newer(updated.positionRevision, self.positionRevision)) {
            updated.mapId = self.mapId; updated.x = self.x; updated.y = self.y; updated.z = self.z;
            updated.orientation = self.orientation; updated.positionRevision = self.positionRevision;
            if(updated.transportEntry && updated.transportEntry==self.transportEntry) {
                updated.transportOffsetX=self.transportOffsetX;updated.transportOffsetY=self.transportOffsetY;
                updated.transportOffsetZ=self.transportOffsetZ;updated.transportLastYaw=self.transportLastYaw;
            }
        }
        if (!newer(seq, vitalsSequence)) {
            updated.health = self.health; updated.maxHealth = self.maxHealth; updated.mana = self.mana;
            updated.maxMana = self.maxMana; updated.dead = self.dead; updated.level = self.level;
            updated.equipment = self.equipment; updated.attackTarget = self.attackTarget;
            updated.instanceId = self.instanceId; updated.resourceType = self.resourceType;
            updated.mountSpellId = self.mountSpellId;
            updated.xpToLevel = uint32_t(updated.level) * uint32_t(updated.level) * 100 + 300;
        } else vitalsSequence = seq;
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
            for (size_t index = begin; index < end; ++index) writeNpc(w, actors[index]);
            send(Message::Npcs, peer.session, w, peer.address);
        }
    }
    void actionResult(const Peer& peer) {
        Writer w; w.u32(peer.lastCommand); w.u8(peer.lastCommandSuccess ? 1 : 0); w.text(peer.lastCommandStatus);
        send(Message::ActionResult, peer.session, w, peer.address);
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
    bool runCommand(LocalRealmPlayer& player, const LocalRealmCommand& cmd, std::string& result) {
        const bool financial=cmd.action==LocalAction::ListAuction || cmd.action==LocalAction::CancelAuction ||
            cmd.action==LocalAction::BidAuction || cmd.action==LocalAction::BuyoutAuction;
        if(!financial)return executeCommand(player,cmd,result);
        // Persist ownership, escrow and bags as one save before acknowledging a
        // successful transaction. Roll back RAM too when the atomic write fails.
        const auto priorPlayer=player;
        const auto priorDirector=botDirector;
        const auto* localRecord=findSaved(self.guid);
        const auto priorSavedSelf=localRecord?localRecord->player:LocalRealmPlayer{};
        if(!executeCommand(player,cmd,result))return false;
        if(saveRealm())return true;
        player=priorPlayer;botDirector=priorDirector;
        if(auto* restored=findSaved(self.guid))restored->player=priorSavedSelf;
        result="Auction transaction was not saved; no items or money were changed";
        LOG_ERROR("[LOCAL_AUCTION] transaction rolled back: ",error);
        return false;
    }
    bool executeCommand(LocalRealmPlayer& player, const LocalRealmCommand& cmd, std::string& result) {
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
        if (cmd.action == LocalAction::BuyFromVendor || cmd.action == LocalAction::SellToVendor || cmd.action == LocalAction::RepairEquipment ||
            cmd.action == LocalAction::ListAuction || cmd.action == LocalAction::BidAuction ||
            cmd.action == LocalAction::BuyoutAuction || cmd.action == LocalAction::CancelAuction)
            cmd.serviceNpcGuid = r.u64();
        if (!r.done() || !id || uint8_t(cmd.action) < 1 || uint8_t(cmd.action) > uint8_t(kLocalActionMax)) return;
        if (id == peer.lastCommand) { peer.lastSeen = now; actionResult(peer); return; }
        // Only one client command is in flight, so gaps are not executable.
        if (id != peer.lastCommand + 1) return;
        auto* record = findSaved(peer.guid); if (!record) return;
        peer.lastCommandSuccess = runCommand(record->player, cmd, peer.lastCommandStatus);
        peer.lastCommand = id; peer.lastSeen = now;
        dirty = dirty || peer.lastCommandSuccess;
        LOG_INFO("[local_realm] Action peer=", peer.guid, " id=", id, " kind=", int(cmd.action), " ok=", peer.lastCommandSuccess, " result=", peer.lastCommandStatus);
        actionResult(peer); progress(peer); refreshPlayers();
    }
    void receiveWorld(Reader& r) {
        const auto tick = r.u32(); const auto part = r.u8(), parts = r.u8(), count = r.u8();
        if (!tick || !parts || parts > MaxNpcPages || part >= parts || count > NpcsPerPage || !newer(tick, worldSequence)) return;
        std::vector<LocalRealmNpc> chunk;
        for (unsigned index = 0; index < count; ++index) {
            auto n = readNpc(r, gameplay.content()); if (!r.valid) return; chunk.push_back(std::move(n));
        }
        if (!r.done()) return;
        if (tick != collectingWorld) {
            if (collectingWorld && !newer(tick, collectingWorld)) return;
            collectingWorld = tick; worldParts = parts; worldReceived.fill(false);
            for (auto& entries : worldChunks) entries.clear();
        }
        if (parts != worldParts) return;
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
        npcView.clear();
        for (const auto& npc : gameplay.npcs()) if (npc.mapId == self.mapId && npc.instanceId == self.instanceId) {
            auto copy = npc; copy.hostile = gameplay.canAttack(self, npc); copy.aggressive = gameplay.isAggressive(self, npc);
            npcView.push_back(std::move(copy));
        }
        players.clear(); players.push_back(self);
        for (const auto& peer : peers)
            if (const auto* p = findSaved(peer.guid)) players.push_back(p->player);
        // Appended after the real ones, so a guest sees them as ordinary
        // players and renders them as characters without a second path.
        for (const auto& bot : botPlayers) players.push_back(bot);
    }
    void refreshStatus() {
        if (state == LocalRealmState::Hosting)
            status = realmName + ": " + std::to_string(players.size()) + "/" + std::to_string(playerLimit) + " consoles";
        else if (state == LocalRealmState::Connected)
            status = "LAN connected: " + std::to_string(players.size()) + "/" + std::to_string(playerLimit) + " consoles";
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
            peers.erase(it);refreshPlayers();break;
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
                resetSavedSession(it->guid);peers.erase(it);saveRealm();refreshPlayers();refreshStatus();
            }
            return;
        }
        if (type == Message::CharacterRequest) { handleCharacterRequest(r, address, token); return; }
        if (type == Message::Hello) { handleHello(r, address, token); return; }
        auto peer = std::find_if(peers.begin(), peers.end(), [&](const Peer& p) {
            return p.session == token && sameAddress(p.address, address);
        });
        if (peer == peers.end()) return;
        if (type == Message::Command) { receiveCommand(*peer, r); return; }
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
            if (freshPosition && !loading && (!record->player.transportEntry || incoming.mapId==record->player.mapId) && !record->player.dead && positionRevision == record->player.positionRevision && instanceId == record->player.instanceId && (!instanceId || incoming.mapId == record->player.mapId)) {
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
                peers.erase(peer); saveRealm(); refreshPlayers(); refreshStatus();
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
            if (type == Message::ActionResult) {
                const auto id = r.u32(); const auto success = r.u8(); const auto result = r.text();
                if (!r.done() || success > 1 || pendingCommands.empty() || pendingCommands.front().id != id) return;
                actionStatus = result; ++actionStatusRevision;
                pendingCommands.pop_front(); lastSeen = now;
                LOG_INFO("[local_realm] Action response id=", id, " ok=", int(success), " result=", result);
                return;
            }
            if (type == Message::Npcs) { receiveWorld(r); return; }
            if (type == Message::Auctions) { receiveAuctions(r); return; }
            if (type == Message::History) { receiveHistory(r); return; }
            if (type == Message::Progress) {
                if (!newer(seq, progressSequence) || (pendingProgress && !newer(seq, pendingProgress->sequence))) return;
                const auto guid = r.u64(); const auto revision = r.u32(), count = r.u32();
                LocalRealmPlayer updated = self; readPosition(r, updated);
                if (guid != self.guid || !revision || count > LocalGameplay::MaxCompletedQuests ||
                    !readProgress(r, updated) || !readCast(r, updated) ||
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
    if (p.socket != INVALID_SOCK) p.receive();
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
                if (action.command.action == LocalAction::BuyFromVendor || action.command.action == LocalAction::SellToVendor || action.command.action == LocalAction::RepairEquipment ||
                    action.command.action == LocalAction::ListAuction || action.command.action == LocalAction::BidAuction ||
                    action.command.action == LocalAction::BuyoutAuction || action.command.action == LocalAction::CancelAuction)
                    w.u64(action.command.serviceNpcGuid);
                p.send(Message::Command, p.session, w, p.host); action.lastSent = p.now;
            }
        }
    } else if (p.authoritative()) {
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
        if (p.botDirector.enabled() && p.botPlayers.empty()) {
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
        p.dirty = p.botDirector.deliver(p.self, p.gameplay.content()) || p.dirty;
        for (auto& saved : p.saved) if (saved.player.guid != p.self.guid)
            p.dirty = p.botDirector.deliver(saved.player, p.gameplay.content()) || p.dirty;
        p.refreshPlayers();
        if (p.state == LocalRealmState::Hosting) {
            const auto oldCount = p.peers.size();
            p.peers.erase(std::remove_if(p.peers.begin(), p.peers.end(), [&](const Impl::Peer& peer) {
                if (p.now - peer.lastSeen <= PeerTimeout ||
                    (peer.loading && p.now - peer.loadingSince <= LoadingTimeout)) return false;
                LOG_WARNING("[local_realm] Guest timeout guid=", peer.guid);
                p.rememberCancelled(peer.identity,peer.joinNonce);p.resetSavedSession(peer.guid);return true;
            }), p.peers.end());
            if (oldCount != p.peers.size()) { p.dirty = true; p.saveRealm(); }
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
    if (p.worldLoading || (p.self.transportEntry && map != p.self.mapId)) return false;
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
    if (!ready()) { p.actionStatus = "Local realm is not ready"; return false; }
    if (p.authoritative()) {
        const bool ok = p.runCommand(p.self, cmd, p.actionStatus);
        p.dirty = p.dirty || ok; p.refreshPlayers();
        LOG_INFO("[local_realm] Local action kind=", int(cmd.action), " ok=", ok, " result=", p.actionStatus);
        return ok;
    }
    if (p.pendingCommands.size() >= 16) { p.actionStatus = "Wait for pending LAN actions"; return false; }
    p.pendingCommands.push_back({++p.nextCommand, cmd, -1, p.now});
    p.actionStatus = "Waiting for host";
    return true;
}
bool LocalRealm::attack(uint64_t guid) { return command({LocalAction::Attack, guid, 0}); }
bool LocalRealm::stopAttack() { return command({LocalAction::StopAttack, 0, 0}); }
bool LocalRealm::castSpell(uint32_t spell, uint64_t guid) { return command({LocalAction::CastSpell, guid, spell}); }
bool LocalRealm::acceptQuest(uint32_t quest, uint64_t guid) { return command({LocalAction::AcceptQuest, guid, quest}); }
bool LocalRealm::turnInQuest(uint32_t quest, uint64_t guid) { return command({LocalAction::TurnInQuest, guid, quest}); }
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
    return impl_->gameplay.travel().destinationsFrom(master->taxiNodeId, known);
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

void LocalRealm::setAuctionPriceMultiplier(uint32_t multiplier) {
    if (!impl_ || state() == LocalRealmState::Connected || state() == LocalRealmState::Connecting) return;
    impl_->botDirector.setAuctionPriceMultiplier(multiplier);
    LOG_INFO("[LOCAL_AUCTION] market enabled independently of playerbots; base multiplier=",
        impl_->botDirector.auctionPriceMultiplier(), " moneyCap=", LocalAuctionPricing::MoneyCap);
}

uint32_t LocalRealm::auctionPriceMultiplier() const {
    return impl_ ? impl_->botDirector.auctionPriceMultiplier() : LocalAuctionPricing::DefaultMultiplier;
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
    return ready() ? impl_->gameplay.vendorStock(impl_->self, npcGuid) : std::vector<uint32_t>{};
}
int32_t LocalRealm::vendorRemaining(uint32_t itemId, uint64_t npcGuid) const {
    return ready() ? impl_->gameplay.vendorRemaining(impl_->self, itemId, npcGuid) : 0;
}
uint32_t LocalRealm::vendorBuyPrice(uint32_t itemId, uint16_t count) const {
    const auto* item = content().item(itemId);
    return item ? localVendorBuyPrice(*item, count) : 0;
}
uint32_t LocalRealm::vendorSellPrice(uint32_t itemId, uint16_t count) const {
    const auto* item = content().item(itemId);
    return item ? localVendorSellPrice(*item, count) : 0;
}
std::vector<uint32_t> LocalRealm::trainableSpells() const {
    return ready() ? impl_->gameplay.trainableSpells(impl_->self) : std::vector<uint32_t>{};
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
bool LocalRealm::learnSpell(uint32_t spellId) { return command({LocalAction::LearnSpell, 0, spellId}); }
bool LocalRealm::learnProfession(uint32_t skillId) { return command({LocalAction::LearnProfession, 0, skillId}); }
bool LocalRealm::trainProfessionRank(uint32_t skillId) {
    return command({LocalAction::TrainProfessionRank, 0, skillId});
}
bool LocalRealm::setHome() { return command({LocalAction::SetHome, 0, 0}); }
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
    p.pendingCommands.clear();p.pendingProgress.reset();p.lobbyPending=false;p.worldLoading=false;
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
} // namespace wowee::game
