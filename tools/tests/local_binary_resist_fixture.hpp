// Test scaffold extracted from the the implementation actual DBC/catalog/authority fixture.
#include "../../src/game/local_realm.cpp"
#include "local_group_rewards_fixture.hpp"
#include "game/local_diminishing.hpp"
#include "game/local_npc_auras.hpp"
#include "game/local_resistance.hpp"
#include "game/local_melee.hpp"
#include "game/local_armor.hpp"
#include "game/local_spell_critical.hpp"
#include "game/local_proc_rules.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_world_catalog.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace wowee;
using namespace wowee::game;
namespace fs = std::filesystem;

unsigned gFailures = 0;
bool expect(bool ok, const std::string& what) {
    if (!ok) { ++gFailures; std::cerr << "FAIL " << what << "\n"; }
    return ok;
}

// --- the client's own tables, imported once ---------------------------------
struct ClientTables {
    std::map<std::string, pipeline::DBCFile> files;
    std::map<std::string, std::vector<uint8_t>> bytes;
    const pipeline::DBCFile* get(const char* name) { return &files.at(name); }
    void load(const fs::path& dbc) {
        for (const auto* name : {"Spell", "SpellRange", "SpellCastTimes", "SpellDuration", "SpellIcon",
                                 "SpellRadius", "SpellRuneCost", "SkillLine", "SkillLineAbility",
                                 "Talent", "TalentTab"}) {
            std::ifstream f(dbc / (std::string(name) + ".dbc"), std::ios::binary);
            bytes[name] = {std::istreambuf_iterator<char>(f), {}};
            assert(files[name].load(bytes[name]));
        }
    }
    LocalSpellImport import() {
        auto out = importClientStarterSpells(get("Spell"), get("SpellRange"), get("SpellCastTimes"),
            get("SpellDuration"), get("SpellIcon"), get("SkillLineAbility"), get("SkillLine"),
            get("Talent"), get("SpellRuneCost"), get("SpellRadius"));
        detail::importClientTalents(out, get("Talent"), get("TalentTab"), get("Spell"), get("SpellRange"),
            get("SpellCastTimes"), get("SpellDuration"), get("SpellIcon"), get("SpellRuneCost"),
            get("SpellRadius"));
        return out;
    }
};
LocalSpellImport gImported;
ClientTables* gTables = nullptr;

const LocalSpellDefinition& real(uint32_t id) {
    for (const auto& d : gImported.spells) if (d.id == id) return d;
    std::cerr << "FAIL: spell " << id << " is absent from the client import\n";
    std::abort();
}
bool castable(const LocalSpellDefinition& d) {
    return d.clientSpell && d.unsupportedReason.empty() && !d.passive && !d.triggeredOnly && !d.npcOnly;
}

// --- the shipped catalog, every creature definition, read by the shipped reader
std::map<uint32_t, LocalNpcDefinition> gNpcs;
std::vector<uint32_t> gHostile;
LocalWorldCatalog* gCatalog = nullptr;

void loadCatalog(const fs::path& directory) {
    static LocalWorldCatalog catalog;
    std::string error;
    if (!catalog.load(directory.string(), error)) { std::cerr << "FAIL: catalog: " << error << "\n"; std::abort(); }
    gCatalog = &catalog;
    // Enumerate the record keys straight from npcs.pack (WPCAT01: an 8-byte
    // magic, a count, a header size, then one <key, offset, length> per record);
    // every definition is then decoded by LocalWorldCatalog::npc itself.
    std::ifstream f(directory / "npcs.pack", std::ios::binary);
    std::vector<unsigned char> pack{std::istreambuf_iterator<char>(f), {}};
    assert(pack.size() > 16 && std::memcmp(pack.data(), "WPCAT01\0", 8) == 0);
    uint32_t count = 0; std::memcpy(&count, pack.data() + 8, 4);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key = 0; std::memcpy(&key, pack.data() + 16 + 16 * size_t(i), 4);
        LocalNpcDefinition d;
        if (!catalog.npc(key, d, error)) { std::cerr << "FAIL: npc " << key << ": " << error << "\n"; std::abort(); }
        assert(d.id == key);
        if (d.hostile) gHostile.push_back(key);
        gNpcs.emplace(key, std::move(d));
    }
}
const LocalNpcDefinition& creature(uint32_t entry) {
    const auto it = gNpcs.find(entry);
    if (it == gNpcs.end()) { std::cerr << "FAIL: creature " << entry << " is not in the catalog\n"; std::abort(); }
    return it->second;
}

// ---------------------------------------------------------------------------
// Runtime scaffolding: the shipped authority driving real imported definitions
// against a real catalog creature (its own level, immunity set and resistance
// row), with only its health raised so hundreds of casts cannot kill it.
// ---------------------------------------------------------------------------
struct Caster { uint8_t classId; LocalResourceType resource; uint8_t level; };
struct World {
    std::shared_ptr<LocalWorldContent> content;
    LocalGameplay game;
    std::vector<LocalRealmPlayer> casters;
    std::vector<LocalRealmPlayer*> players;
    uint64_t npcGuid = 0;
    uint32_t entry = 0;
};
const LocalRealmNpc* findNpc(const LocalGameplay& game, uint64_t guid) {
    for (const auto& n : game.npcs()) if (n.guid == guid) return &n;
    return nullptr;
}
void buildWorld(World& w, std::vector<uint32_t> spells, std::vector<Caster> casters,
                LocalNpcDefinition target, uint32_t health = 100000000, float npcX = 8) {
    w.content = rewardContent();
    for (auto id : spells) w.content->spells.push_back(real(id));
    std::sort(w.content->spells.begin(), w.content->spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    target.health = health; target.damage = 0; target.aggroRadius = 0.01f; target.xp = 0; target.money = 0; target.loot.clear();
    w.content->npcs.push_back(target);
    std::sort(w.content->npcs.begin(), w.content->npcs.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    // A shield for a warrior: class 4 / subclass 6 in the melee and auction
    // tables, inventory type 14 here, so Shield Slam's equipment gate and the
    // block-value term both read a real item.
    LocalItemDefinition shield; shield.id = 2443; shield.name = "Worn Large Shield"; shield.inventoryType = 14; shield.stack = 1;
    w.content->items.push_back(shield);
    std::sort(w.content->items.begin(), w.content->items.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    w.game.useContent(w.content);
    w.entry = target.id;
    for (size_t i = 0; i < casters.size(); ++i) {
        auto p = rewardPlayer(1 + i);
        p.classId = casters[i].classId; p.level = casters[i].level; p.health = p.maxHealth = 100000000;
        p.mana = p.maxMana = 1000000; p.resourceType = casters[i].resource;
        p.x = float(i) * 0.05f; p.y = p.z = 0; p.quests.clear();
        p.knownSpells = {1};
        for (auto id : spells) p.knownSpells.push_back(id);
        w.casters.push_back(p);
    }
    w.players.clear();
    for (auto& c : w.casters) w.players.push_back(&c);
    w.game.tick(0, w.players);
    auto n = rewardNpc();
    n.entry = target.id; n.name = target.name; n.level = target.level;
    n.health = n.maxHealth = health; n.hostile = true; n.lootOwner = 0;
    n.x = n.homeX = npcX; n.y = n.homeY = 0; n.z = n.homeZ = 0;
    n.targetGuid = w.casters[0].guid; n.threat[0] = {w.casters[0].guid, 1000};
    w.game.setRemoteNpcs({n});
    w.npcGuid = n.guid;
    w.game.tick(.25f, w.players);
}
// The last damage observation this spell produced against the creature, if any.
std::optional<LocalCombatEvent> lastDamageEvent(const LocalGameplay& game, uint64_t target, uint32_t spellId, uint64_t after) {
    std::optional<LocalCombatEvent> found;
    for (const auto& e : game.combatEvents())
        if (e.sequence > after && e.target == target && e.spell == spellId &&
            (e.kind == LocalCombatEventKind::SpellDamage || e.kind == LocalCombatEventKind::PeriodicDamage)) found = e;
    return found;
}
uint64_t lastSequence(const LocalGameplay& game) {
    uint64_t s = 0;
    for (const auto& e : game.combatEvents()) s = std::max(s, e.sequence);
    return s;
}
struct CastResult { bool executed = false; std::string result; std::optional<LocalCombatEvent> event; uint32_t manaPaid = 0; };
// One cast, resolved through the cast bar when the spell has one. Cooldowns
// and resources are reset first so every attempt is the same attempt.
CastResult castOnce(World& w, LocalRealmPlayer& p, uint32_t spellId) {
    p.globalCooldownMs = 0; p.cooldowns.clear(); p.categoryCooldowns.clear();
    p.mana = p.maxMana; p.comboPoints = 0; p.comboTarget = 0;
    const auto before = lastSequence(w.game);
    CastResult r;
    r.executed = w.game.execute(p, {LocalAction::CastSpell, w.npcGuid, spellId}, w.players, r.result);
    if (!r.executed) return r;
    for (unsigned t = 0; t < 120 && p.castStatus == LocalCastStatus::Casting; ++t) w.game.tick(.05f, w.players);
    assert(p.castStatus != LocalCastStatus::Casting);
    r.manaPaid = p.maxMana - p.mana;
    r.event = lastDamageEvent(w.game, w.npcGuid, spellId, before);
    return r;
}

} // anonymous namespace

