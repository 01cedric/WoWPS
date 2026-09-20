// P04 / the implementation - creature template immunity and partial spell resistance: the
// catalog fields, the predicates, their placement ahead of the hit roll and the
// diminishing read, effect stripping, the dispel effect beside damage, the
// partial-resist bucket roll, the level term, the conservation identity, the
// full-resist shape, the LAN codec and a save that carries nothing of theirs.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33, as measured
// in the source audit sections 3, 6 and 10 and
// the source audit sections 1-3 and 10:
//   Creature::LoadTemplateImmunities   Creature.cpp:2246-2283 - a template's
//       creature_immunities set enters m_spellImmune with the placeholder id
//       UINT32_MAX; SchoolMask and MechanicsMask are the two columns any
//       admitted spell can meet.
//   Creature::IsImmunedToSpell         Creature.cpp:2286-2316 - the spell-level
//       mechanic first, then "immune to all effects", then Unit's predicate.
//   Creature::IsImmunedToSpellEffect   Creature.cpp:2318-2328 - the effect-level
//       mechanic, ahead of Unit's NO_IMMUNITIES read.
//   Unit::IsImmunedToSpell             Unit.cpp:9733-9799 - NO_IMMUNITIES, the
//       dispel and mechanic arms, all-effects, then HasSchoolImmunityForMask.
//   WorldObject::SpellHitResult        Object.cpp:3746-3770 - immunity is asked
//       BEFORE the hit roll; Spell::DoSpellHitOnUnit (Spell.cpp:3195) reads the
//       diminishing record only for a target that was hit.
//   Spell::AddUnitTarget               Spell.cpp:2413-2416 - immune effects are
//       stripped from what a landing cast applies.
//   AuraEffect::HandlePeriodicDamageAurasTick SpellAuraEffects.cpp:6287 - a
//       school-immune target takes no tick and keeps the aura.
//   Spell::CheckCast                   Spell.cpp:6264-6306 - a pure dispel with
//       nothing to dispel fails; a dispel beside another effect skips the gate.
//   Unit::GetDispellableAuraList       Unit.cpp:5612-5673 - an offensive magic
//       dispel removes helpful auras only.
//   Unit::GetEffectiveResistChance     Unit.cpp:2288-2323 - resistance, the
//       +5-per-level term, K(casterLevel), the 75 % cap.
//   Unit::CalcAbsorbResist             Unit.cpp:2341-2396 - the gate and the
//       eleven-bucket walk; bucket 10 is HITINFO_FULL_RESIST.
//   Unit::GetResistance(SpellSchoolMask) Unit.cpp:15821-15830 - the minimum.
//   SpellEffects.cpp:360-369 - Shield Slam adds the shield block value.
//
// Every census number is measured by running the shipped importer over the
// player's own Spell.dbc and the shipped catalog reader over the shipped
// npcs.pack, never asserted from a fixture. Every runtime number comes from the
// shipped LocalGameplay tick driving real imported definitions against real
// catalog creatures.
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
// Casts until the authority resolves something other than a miss: a missed
// spell never reaches Spell::DoSpellHitOnUnit and changes nothing, so retrying
// preserves state. Returns the outcome that was resolved (Hit for a landed
// control with no damage observation).
LocalMeleeOutcome castUntilResolved(World& w, LocalRealmPlayer& p, uint32_t spellId, CastResult* out = nullptr,
                                    unsigned attempts = 60) {
    for (unsigned i = 0; i < attempts; ++i) {
        auto r = castOnce(w, p, spellId);
        if (!r.executed) { std::cerr << "FAIL: cast " << spellId << " rejected: " << r.result << "\n"; std::abort(); }
        if (out) *out = r;
        if (!r.event) {
            const auto* n = findNpc(w.game, w.npcGuid);
            bool landed = false;
            if (n) {
                for (const auto& a : n->controls) if (a.spellId == spellId && a.casterGuid == p.guid) landed = true;
                for (const auto& a : n->snares) if (a.spellId == spellId && a.casterGuid == p.guid) landed = true;
                for (const auto& a : n->damageAuras) if (a.spellId == spellId && a.casterGuid == p.guid) landed = true;
            }
            if (landed) return LocalMeleeOutcome::Hit;
            continue;
        }
        if (r.event->outcome == LocalMeleeOutcome::Miss) continue;
        if (r.event->outcome == LocalMeleeOutcome::Immune) return LocalMeleeOutcome::Immune;
        return r.event->outcome;
    }
    std::cerr << "FAIL: cast " << spellId << " never resolved in " << attempts << " attempts\n";
    std::abort();
}
uint32_t npcHealth(const World& w) { const auto* n = findNpc(w.game, w.npcGuid); assert(n); return n->health; }

// Every damage event this suite drives must satisfy the conservation identity.
uint64_t gEventsChecked = 0, gEventsWithResist = 0, gFullResists = 0;
void checkConservation(const LocalGameplay& game) {
    for (const auto& e : game.combatEvents()) {
        if (e.kind != LocalCombatEventKind::SpellDamage && e.kind != LocalCombatEventKind::PeriodicDamage &&
            e.kind != LocalCombatEventKind::PlayerMelee && e.kind != LocalCombatEventKind::ProcDamage &&
            e.kind != LocalCombatEventKind::PlayerRanged) continue;
        ++gEventsChecked;
        if (e.resisted) ++gEventsWithResist;
        const uint64_t accounted = uint64_t(e.resisted) + e.effective + e.absorbed + e.blocked;
        if (e.killed) assert(accounted <= e.attempted);
        else assert(accounted == e.attempted);
        if (localOutcomeNullifiesDamage(e.outcome)) assert(!e.effective && !e.blocked);
        if (e.outcome == LocalMeleeOutcome::Resist) { ++gFullResists; assert(e.resisted && e.resisted == e.attempted); }
        if (e.resisted) assert(e.outcome == LocalMeleeOutcome::Hit || e.outcome == LocalMeleeOutcome::Critical ||
                               e.outcome == LocalMeleeOutcome::Resist);
    }
}

// The audit's seventeen runtime-castable controls and their spell-level mechanic.
constexpr uint32_t kControls[] = {853, 5588, 5589, 10308, 5211, 6798, 8983, 2637, 18657, 18658,
                                  20066, 11297, 51724, 9484, 9485, 10955, 15487};
const char* schoolName(uint32_t mask) {
    switch (mask) { case 1: return "physical"; case 2: return "holy"; case 4: return "fire"; case 8: return "nature";
        case 16: return "frost"; case 32: return "shadow"; case 64: return "arcane"; case 126: return "all magic";
        case 96: return "frost+shadow+arcane"; default: return "mixed"; }
}

// ---------------------------------------------------------------------------
// 1. The catalog census: what the shipped reader hands the runtime for every
//    one of the 14,496 spawnable definitions, against the pinned SQL.
// ---------------------------------------------------------------------------
void catalogCensus() {
    assert(gNpcs.size() == 14496);
    assert(gHostile.size() == 3582);
    unsigned withMask = 0, withSchool = 0, withMechanic = 0, withRes = 0;
    unsigned hostileMask = 0, hostileSchool = 0, hostileMechanic = 0, hostileRes = 0;
    std::map<unsigned, unsigned> hostileBits;
    std::map<uint32_t, unsigned> hostileSchools;
    std::array<unsigned, 6> hostileResSchools{};
    unsigned holyRows = 0;
    for (const auto& [id, d] : gNpcs) {
        const bool mask = d.immuneSchoolMask || d.immuneMechanicsMask;
        const bool res = std::any_of(d.resistances.begin(), d.resistances.end(), [](auto v) { return v != 0; });
        withMask += mask; withSchool += d.immuneSchoolMask != 0; withMechanic += d.immuneMechanicsMask != 0; withRes += res;
        if (d.resistances[0]) ++holyRows;
        if (!d.hostile) continue;
        hostileMask += mask; hostileSchool += d.immuneSchoolMask != 0; hostileMechanic += d.immuneMechanicsMask != 0; hostileRes += res;
        for (unsigned b : {9u, 10u, 11u, 12u, 14u, 15u, 20u, 30u}) if ((d.immuneMechanicsMask >> b) & 1u) ++hostileBits[b];
        if (d.immuneSchoolMask) ++hostileSchools[d.immuneSchoolMask];
        for (size_t s = 0; s < 6; ++s) if (d.resistances[s]) ++hostileResSchools[s];
    }
    // the source audit section 6.1: 2,106 spawnable
    // templates point at a set; 7 of them point at sets whose only content is
    // an Effects list or ImmuneAoE (measured to intersect nothing), so 2,099
    // carry a school or mechanic mask into the definition; 1,990 / 167 all,
    // 1,043 / 109 hostile; 1,125 hostile point at a set, 1,123 carry a mask.
    const bool immunityExact = expect(withMask == 2099 && withMechanic == 1990 && withSchool == 167 &&
                                      hostileMask == 1123 && hostileMechanic == 1043 && hostileSchool == 109,
        "immunity census: " + std::to_string(withMask) + " carried / " + std::to_string(withMechanic) + " mechanic / " +
        std::to_string(withSchool) + " school; hostile " + std::to_string(hostileMask) + " / " +
        std::to_string(hostileMechanic) + " / " + std::to_string(hostileSchool) +
        " against the audit's 2099 (2106 minus 7 empty-mask sets) / 1990 / 167; 1123 / 1043 / 109");
    // Section 6.1's per-mechanic table for the hostile set.
    const std::map<unsigned, unsigned> expectedBits{{9, 337}, {10, 470}, {11, 384}, {12, 366}, {14, 432}, {15, 6}, {20, 307}, {30, 370}};
    const bool bitsExact = expect(hostileBits == expectedBits,
        "per-mechanic table: silence " + std::to_string(hostileBits[9]) + ", sleep " + std::to_string(hostileBits[10]) +
        ", snare " + std::to_string(hostileBits[11]) + ", stun " + std::to_string(hostileBits[12]) + ", knockout " +
        std::to_string(hostileBits[14]) + ", bleed " + std::to_string(hostileBits[15]) + ", shackle " +
        std::to_string(hostileBits[20]) + ", sapped " + std::to_string(hostileBits[30]) +
        " against 337 / 470 / 384 / 366 / 432 / 6 / 307 / 370");
    // Section 6.4: nature 37, frost 31, fire 29, arcane 10, all magic 1, frost+shadow+arcane 1; none physical.
    const std::map<uint32_t, unsigned> expectedSchools{{8, 37}, {16, 31}, {4, 29}, {64, 10}, {126, 1}, {96, 1}};
    const bool schoolsExact = expect(hostileSchools == expectedSchools, "hostile school-immunity table differs from the audit's 37 / 31 / 29 / 10 / 1 / 1");
    for (const auto& [id, d] : gNpcs) assert(!(d.immuneSchoolMask & 1u)); // not one creature is physically immune
    // the source audit section 3.1: 639 spawnable
    // creatures have a non-zero row, 4 of them only a negative one (clamped to
    // zero at compile time, all four non-hostile Blackrock fire elementals), so
    // 635 carry a value; 288 hostile; per school 160 / 157 / 138 / 147 / 145; no holy row anywhere.
    const bool resExact = expect(withRes == 635 && hostileRes == 288 && holyRows == 0 &&
                                 hostileResSchools == std::array<unsigned, 6>{0, 160, 157, 138, 147, 145},
        "resistance census: " + std::to_string(withRes) + " carried (639 minus 4 negative-only), hostile " +
        std::to_string(hostileRes) + ", holy rows " + std::to_string(holyRows) + ", hostile per school fire " +
        std::to_string(hostileResSchools[1]) + " nature " + std::to_string(hostileResSchools[2]) + " frost " +
        std::to_string(hostileResSchools[3]) + " shadow " + std::to_string(hostileResSchools[4]) + " arcane " +
        std::to_string(hostileResSchools[5]));
    // Named rows, read from the shipped definitions.
    const auto& emberseer = creature(9816), &dessecus = creature(7104), &infernal = creature(6073),
               &rock = creature(92), &water = creature(691), &ravager = creature(11561);
    assert(emberseer.resistances[1] == 1000 && (emberseer.immuneSchoolMask == 4));
    assert(dessecus.resistances == (std::array<uint16_t, 6>{0, 560, 560, 560, 560, 560}) && !dessecus.immuneSchoolMask && !dessecus.immuneMechanicsMask);
    // The resistance audit's worked example, Searing Infernal, carries fire 300
    // AND a fire-school immunity set: at the pin a Fireball is IMMUNE before
    // any resist roll, which the bucket group below accounts for.
    assert(infernal.resistances[1] == 300 && infernal.immuneSchoolMask == 4 && infernal.level == 29);
    assert(rock.immuneSchoolMask == 8 && ((rock.immuneMechanicsMask >> kLocalMechanicStunned) & 1u) && rock.level == 39 && rock.hostile);
    assert(water.immuneSchoolMask == 16 && ((water.immuneMechanicsMask >> 11) & 1u) && water.level == 36 && water.hostile);
    assert(((ravager.immuneMechanicsMask >> kLocalMechanicBleed) & 1u) && !ravager.immuneSchoolMask && ravager.hostile);
    // A definition without either field is zero on every new member, so a
    // creature the SQL says nothing about behaves exactly as before the reference.
    LocalNpcDefinition blank;
    assert(!blank.immuneSchoolMask && !blank.immuneMechanicsMask && (blank.resistances == std::array<uint16_t, 6>{}));
    if (immunityExact && bitsExact && schoolsExact && resExact)
        std::cout << "PASS catalog census: the shipped reader decodes all 14496 definitions (3582 hostile); "
                  << withMask << " carry an immunity mask (" << withMechanic << " mechanic, " << withSchool
                  << " school; the 7 templates whose set holds only an Effects list or ImmuneAoE carry none), "
                  << hostileMask << " of them hostile with the per-mechanic table 337 silence / 470 sleep / 384 snare / "
                     "366 stun / 432 knockout / 6 bleed / 307 shackle / 370 sapped and the school table 37 nature / 31 frost / "
                     "29 fire / 10 arcane / 1 all-magic / 1 frost+shadow+arcane, none physical; " << withRes
                  << " carry a resistance row (639 with a non-zero SQL row minus the 4 negative-only ones clamped to zero), "
                  << hostileRes << " hostile, per school fire 160 / nature 157 / frost 138 / shadow 147 / arcane 145 and 0 holy; "
                     "Pyroguard Emberseer fire 1000, Dessecus 560 in all five, Searing Infernal fire 300 beside a fire "
                     "immunity, Rock Elemental nature+stun, Lesser Water Elemental frost+snare, Undead Ravager bleed\n";
}

// ---------------------------------------------------------------------------
// 2. The immunity cross-reference: the seventeen controls, the damaging
//    schools, the snares and the bleeds against every hostile set, through the
//    shipped predicates.
// ---------------------------------------------------------------------------
void immunityCensus() {
    std::vector<const LocalSpellDefinition*> controls, damaging, snares, bleeds;
    std::set<uint32_t> accepted;
    for (const auto& r : gImported.audit) if (r.status == "Supported decoder; imported") accepted.insert(r.id);
    for (const auto& d : gImported.spells) {
        if (!castable(d)) continue;
        if (d.controlProfile) controls.push_back(&d);
        if (d.damage || d.periodicDamage) damaging.push_back(&d);
        if (d.snarePercent) snares.push_back(&d);
        if (d.mechanic == kLocalMechanicBleed || std::any_of(d.effectMechanic.begin(), d.effectMechanic.end(),
                                                             [](auto m) { return m == kLocalMechanicBleed; }))
            bleeds.push_back(&d);
    }
    // 1,004 since the implementation added the five form boosts and nine entry-resource
    // ranks (P06); none of the fourteen carries a control, a snare or a bleed.
    assert(accepted.size() == 1004);
    assert(controls.size() == 17 && snares.size() == 23 && bleeds.size() == 34);
    // 398 damaging definitions at the implementation plus the eight Shield Slam ranks, less
    // the seven proc copies the implementation retired from the castable set (Lightning
    // Overload's 49239/49240/49268/49269 and Lightning Shield's damage leaves
    // 26372/49278/49279 - still accepted definitions, now triggeredOnly).
    assert(damaging.size() == 399);
    for (auto id : kControls) assert(std::any_of(controls.begin(), controls.end(), [&](auto* d) { return d->id == id; }));
    // Every control carries its mechanic at the SPELL level and none on an
    // effect, so Creature::IsImmunedToSpell's first test decides the whole cast.
    for (auto* d : controls) { assert(d->mechanic && (d->effectMechanic == std::array<uint8_t, 3>{})); assert(d->effectMask == 1); }

    // Section 6.2 counts the MECHANIC arm alone: 6,402 (spell, creature) pairs
    // over 504 creatures. The whole predicate also carries the school arm
    // (Unit.cpp:9792-9795 runs for every spell, a control included), so a
    // nature-immune creature is immune to Hibernate and a shadow-immune one to
    // Silence: 6,511 pairs over 538 creatures. Both are asserted; the audit's
    // table is the mechanic arm and is right about it.
    std::map<uint32_t, unsigned> perControl, perControlMechanic; unsigned controlPairs = 0, mechanicPairs = 0;
    std::set<uint32_t> controlUnion, mechanicUnion;
    for (auto* d : controls)
        for (auto e : gHostile) {
            const auto& def = creature(e);
            if ((def.immuneMechanicsMask >> d->mechanic) & 1u) { ++mechanicPairs; ++perControlMechanic[d->id]; mechanicUnion.insert(e); }
            if (localNpcImmuneToSpell(def, *d, false)) {
                ++controlPairs; ++perControl[d->id]; controlUnion.insert(e);
                assert(((def.immuneMechanicsMask >> d->mechanic) & 1u) || localNpcSchoolImmune(def, d->schoolMask, false));
            }
        }
    const bool controlsExact = expect(mechanicPairs == 6402 && mechanicUnion.size() == 504 &&
                                      perControlMechanic[853] == 366 && perControlMechanic[5211] == 366 && perControlMechanic[2637] == 470 &&
                                      perControlMechanic[20066] == 432 && perControlMechanic[11297] == 370 && perControlMechanic[9484] == 307 && perControlMechanic[15487] == 337 &&
                                      controlPairs == 6511 && controlUnion.size() == 538 &&
                                      perControl[853] == 367 && perControl[2637] == 503 && perControl[15487] == 339,
        "control immunity: mechanic arm " + std::to_string(mechanicPairs) + " pairs over " + std::to_string(mechanicUnion.size()) +
        " creatures against the audit's 6402 / 504 (HoJ " + std::to_string(perControlMechanic[853]) + ", Hibernate " +
        std::to_string(perControlMechanic[2637]) + ", Repentance " + std::to_string(perControlMechanic[20066]) + ", Sap " +
        std::to_string(perControlMechanic[11297]) + ", Shackle " + std::to_string(perControlMechanic[9484]) + ", Silence " +
        std::to_string(perControlMechanic[15487]) + "); whole predicate " + std::to_string(controlPairs) + " pairs over " +
        std::to_string(controlUnion.size()) + " creatures against 6511 / 538 (HoJ " + std::to_string(perControl[853]) +
        ", Hibernate " + std::to_string(perControl[2637]) + ", Silence " + std::to_string(perControl[15487]) + ")");
    // The friendly-caster clause: template school immunity is ignored for a
    // friendly caster (Unit.cpp:9571-9581); mechanic immunity is not.
    assert(localNpcImmuneToSpell(creature(92), real(10308), true));
    assert(!localNpcSchoolImmune(creature(92), 8, true) && localNpcSchoolImmune(creature(92), 8, false));
    // A stun-immune creature is not immune to a spell with no mechanic.
    assert(!localNpcImmuneToSpell(creature(92), real(133), false));

    // Section 6.4: 5,506 school-immune pairs over 109 creatures, by school, at
    // the implementation; 5,240 since the implementation retired seven nature-school proc copies (each
    // met the 38 nature-immune creatures).
    unsigned schoolPairs = 0; std::set<uint32_t> schoolUnion; std::map<uint32_t, unsigned> bySchool, spellsBySchool;
    for (auto* d : damaging) {
        ++spellsBySchool[d->schoolMask];
        for (auto e : gHostile)
            if (localNpcSchoolImmune(creature(e), d->schoolMask, false)) {
                ++schoolPairs; schoolUnion.insert(e); ++bySchool[d->schoolMask];
                assert(localNpcImmuneToSpell(creature(e), *d, false) || d->sourceNoImmunities || d->sourceNoSchoolImmunities);
                assert(localNpcImmuneToDamage(creature(e), *d, false) || d->sourceNoImmunities || d->sourceNoSchoolImmunities);
            }
    }
    const bool schoolExact = expect(schoolPairs == 5240 && schoolUnion.size() == 109,
        "school immunity: " + std::to_string(schoolPairs) + " pairs over " + std::to_string(schoolUnion.size()) +
        " creatures against 5240 / 109 (the audit's 5506 / 109 less the seven nature-school proc copies the reference retired)");
    // Physical damage is never school-immune; 112 physical definitions (104 + Shield Slam x8).
    assert(bySchool[1] == 0 && spellsBySchool[1] == 112 && spellsBySchool[4] == 82 && spellsBySchool[16] == 26);
    // Whole-spell immunity for a damaging spell is school immunity, or a
    // spell-level bleed, or every effect stripped; the difference is the bleed
    // pairs on the six bleed-immune creatures.
    unsigned whole = 0;
    for (auto* d : damaging) for (auto e : gHostile) if (localNpcImmuneToSpell(creature(e), *d, false)) ++whole;
    assert(whole >= schoolPairs);

    // Section 6.3: the snare rides effect 0 of all 23 ranks (mechanic 11, spell
    // mechanic 0); 384 hostile creatures carry the bit, 379 of them are not
    // also frost-immune and take the damage with the snare stripped.
    std::set<uint32_t> snareBit, snareStripped, snareImmuneWhole;
    for (auto* d : snares) {
        assert(d->effectMechanic[0] == 11 && !d->mechanic && d->effectMask == (d->snareZeroHealingMarker ? 7 : 3));
        for (auto e : gHostile) {
            const auto& def = creature(e);
            if ((def.immuneMechanicsMask >> 11) & 1u) snareBit.insert(e);
            if (localNpcImmuneToSpell(def, *d, false)) { snareImmuneWhole.insert(e); continue; }
            const auto stripped = localNpcStrippedEffects(def, *d);
            assert(stripped == 0 || stripped == 1);
            if (stripped & 1u) snareStripped.insert(e);
        }
    }
    const bool snaresExact = expect(snareBit.size() == 384 && snareStripped.size() == 379,
        "snare stripping: " + std::to_string(snareBit.size()) + " hostile creatures carry MECHANIC_SNARE, " +
        std::to_string(snareStripped.size()) + " strip it from a landing Frostbolt/Frost Shock (audit: 384; 379 not also frost-immune)");
    // The bleeds: Rend and Rip carry MECHANIC_BLEED at the spell level and are
    // immune whole on the six; Rake carries it on effects 0 and 1 and keeps its
    // combo-point effect 2, so it lands with both amounts stripped.
    std::set<uint32_t> bleedCreatures; unsigned bleedWhole = 0, bleedStripped = 0;
    for (auto* d : bleeds)
        for (auto e : gHostile) {
            const auto& def = creature(e);
            if (!((def.immuneMechanicsMask >> kLocalMechanicBleed) & 1u)) continue;
            bleedCreatures.insert(e);
            if (localNpcImmuneToSpell(def, *d, false)) ++bleedWhole;
            else if (localNpcStrippedEffects(def, *d)) ++bleedStripped;
        }
    assert(bleedCreatures.size() == 6);
    assert(localNpcImmuneToSpell(creature(11561), real(772), false));   // Rend: spell mechanic 15
    assert(!localNpcImmuneToSpell(creature(11561), real(1822), false));  // Rake: effects 0 and 1 stripped, 2 kept
    assert(localNpcStrippedEffects(creature(11561), real(1822)) == 3 && real(1822).effectMask == 7);
    assert(real(1822).directEffectSlot == 0 && real(1822).periodicEffectSlot == 1);
    // Every effect mechanic an accepted definition carries sits on a slot the
    // runtime knows how to strip: the snare's slot 0, the direct slot or the
    // periodic slot, and never on an aggregated (255) direct amount.
    unsigned mechanicSlots = 0;
    for (const auto& d : gImported.spells) {
        if (!castable(&d == nullptr ? d : d)) continue;
        for (unsigned k = 0; k < 3; ++k) {
            if (!d.effectMechanic[k]) continue;
            ++mechanicSlots;
            const bool known = (d.snarePercent && k == 0) || d.directEffectSlot == k || d.periodicEffectSlot == k || d.controlEffectSlot == k;
            if (!known) std::cerr << "     " << d.id << " " << d.name << " carries mechanic " << unsigned(d.effectMechanic[k])
                                  << " on slot " << k << " which no runtime site strips\n";
            assert(known);
            assert(!(d.damage && d.directEffectSlot == 255 && d.effectMechanic[k]));
        }
    }
    if (controlsExact && schoolExact && snaresExact)
        std::cout << "PASS immunity census: over the 17 runtime-castable controls x 1123 hostile sets the mechanic arm "
                     "answers immune for exactly 6402 pairs on 504 creatures (366 / 470 / 432 / 370 / 307 / 337 per spell, "
                     "the audit's section 6.2 table) and the whole shipped localNpcImmuneToSpell, whose school arm the "
                     "reference applies to controls too, for 6511 pairs on 538 (a nature-immune creature is immune to "
                     "Hibernate, a shadow-immune one to Silence); over the 399 damaging definitions (112 physical, "
                     "none of them immune anywhere) 5240 pairs on 109 creatures are school-immune; 384 hostile creatures carry "
                     "MECHANIC_SNARE and 379 of them strip effect 0 from all 23 Frostbolt/Frost Shock ranks while the other 5 "
                     "are frost-immune whole; 34 bleeds meet 6 bleed-immune creatures, " << bleedWhole
                  << " pairs immune whole (spell-level mechanic) and " << bleedStripped
                  << " stripped (Rake keeps its combo point); all " << mechanicSlots
                  << " effect-mechanic slots on accepted definitions are slots the runtime strips; template school "
                     "immunity is ignored for a friendly caster and mechanic immunity is not\n";
}

// ---------------------------------------------------------------------------
// 3. A stun-immune creature resolves Immune ahead of everything: the cost is
//    paid, no control is applied, no diminishing record is seeded - and a
//    non-immune creature of the same level still climbs the the implementation ladder.
// ---------------------------------------------------------------------------
uint32_t plainHostile(uint8_t level) {
    for (auto e : gHostile) {
        const auto& d = creature(e);
        if (d.level == level && !d.immuneSchoolMask && !d.immuneMechanicsMask &&
            (d.resistances == std::array<uint16_t, 6>{})) return e;
    }
    std::cerr << "FAIL: no plain hostile creature at level " << unsigned(level) << "\n"; std::abort();
}
void stunImmuneBeforeDiminishing() {
    World w; buildWorld(w, {10308}, {{2, LocalResourceType::Mana, 80}, {2, LocalResourceType::Mana, 80}}, creature(92));
    auto& paladin = w.casters[0];
    CastResult r;
    const auto outcome = castUntilResolved(w, paladin, 10308, &r);
    assert(outcome == LocalMeleeOutcome::Immune);
    assert(r.result == "Immune");
    const auto immunePaid = r.manaPaid;
    assert(immunePaid > 0); // Object.cpp:3748 is in the hit roll, not CheckCast: the cost is paid
    const auto* n = findNpc(w.game, w.npcGuid);
    assert(n && n->controls.empty() && n->diminishing.empty() && !localNpcStunned(*n));
    assert(r.event && r.event->outcome == LocalMeleeOutcome::Immune && r.event->kind == LocalCombatEventKind::SpellDamage);
    assert(!r.event->attempted && !r.event->effective && !r.event->resisted && !r.event->blocked);
    assert(localProcEventHitMask(*r.event) == LocalProcHitImmune);
    // Four more casts from two paladins: still Immune every time, the record
    // still absent - the ladder is never entered.
    for (unsigned i = 0; i < 4; ++i) assert(castUntilResolved(w, w.casters[i % 2], 10308) == LocalMeleeOutcome::Immune);
    n = findNpc(w.game, w.npcGuid);
    assert(n->diminishing.empty() && n->controls.empty());
    // The same two paladins against a plain creature of the same level: the
    // the implementation ladder, 6000 / 3000 / 1500 and then Immune from diminishing.
    World plain; buildWorld(plain, {10308}, {{2, LocalResourceType::Mana, 80}, {2, LocalResourceType::Mana, 80}}, creature(plainHostile(39)));
    std::vector<uint32_t> durations;
    for (unsigned cast = 0; cast < 3; ++cast) {
        CastResult landed;
        assert(castUntilResolved(plain, plain.casters[cast % 2], 10308, &landed) == LocalMeleeOutcome::Hit);
        assert(landed.manaPaid == immunePaid); // the immune cast paid exactly what a landed one pays
        const auto* pn = findNpc(plain.game, plain.npcGuid);
        uint32_t remaining = 0;
        for (const auto& a : pn->controls) if (a.casterGuid == plain.casters[cast % 2].guid) remaining = a.remainingMs;
        durations.push_back(remaining);
    }
    assert(castUntilResolved(plain, plain.casters[1], 10308) == LocalMeleeOutcome::Immune);
    assert((durations == std::vector<uint32_t>{6000, 3000, 1500}));
    const auto* pn = findNpc(plain.game, plain.npcGuid);
    assert(!pn->diminishing.empty());
    checkConservation(w.game); checkConservation(plain.game);
    std::cout << "PASS stun immunity before diminishing: Hammer of Justice rank 4 on Rock Elemental 92 (level 39, "
                 "stun-immune by its set) resolved Immune with result \"Immune\", paid its " << immunePaid
              << " mana, applied no control, seeded no diminishing record through five casts from two paladins, and "
                 "reached the observation as a zero SpellDamage event with hit mask 0x100; the same two paladins on "
              << creature(plainHostile(39)).name << " (level 39, no set) still applied 6000 / 3000 / 1500 ms and then Immune from the ladder\n";
}

// ---------------------------------------------------------------------------
// 4. School immunity at the cast, at the periodic tick, and the snare that
//    vanishes from a landing cast.
// ---------------------------------------------------------------------------
void schoolAndSnareImmunity() {
    // Lesser Water Elemental 691: frost-immune and snare-immune. Frostbolt is
    // frost with its snare on effect 0: immune whole, no snare, no damage;
    // Fireball lands and burns.
    World w; buildWorld(w, {116, 133}, {{8, LocalResourceType::Mana, 80}}, creature(691));
    auto& mage = w.casters[0];
    const auto before = npcHealth(w);
    CastResult r;
    assert(castUntilResolved(w, mage, 116, &r) == LocalMeleeOutcome::Immune);
    assert(r.manaPaid > 0 && r.result == "Casting " + real(116).name); // a cast-time spell finishes in tick; the cost is still paid
    const auto* n = findNpc(w.game, w.npcGuid);
    // The creature's health moves only by the mage's unarmed swings that a
    // failed cast starts (attackTarget is set on the nullified path exactly as
    // for a miss); no Frostbolt observation carries a point of damage.
    assert(n->snares.empty() && r.event && !r.event->attempted && !r.event->effective);
    for (const auto& e : w.game.combatEvents())
        if (e.spell == 116 && (e.kind == LocalCombatEventKind::SpellDamage || e.kind == LocalCombatEventKind::PeriodicDamage))
            assert(!e.effective && e.outcome == LocalMeleeOutcome::Immune);
    assert(castUntilResolved(w, mage, 133, &r) != LocalMeleeOutcome::Immune);
    assert(npcHealth(w) < before && r.event && r.event->effective > 0);
    // Fireball's burn (effect 1, periodic fire) ticks: a fire-immune definition
    // would refuse every tick and keep the aura. Edit the creature's shared
    // definition in place under the live aura - the one way to reach
    // SpellAuraEffects.cpp:6287 at runtime, since a DoT of an immune school can
    // never land in the first place - and watch the ticks turn Immune while
    // the aura survives; then restore it. The mage keeps swinging unarmed, so
    // the creature's health is not the measure: the burn's own observations are.
    auto* live = const_cast<LocalNpcDefinition*>(w.content->npc(691));
    assert(live && live->immuneSchoolMask == 16);
    live->immuneSchoolMask = 16 | 4;
    // Fireball rank 1's burn is the real definition's duration over its real
    // interval - 4,000 ms in 2,000 ms ticks, so exactly two ticks - and the
    // aura must still be on the creature after the first immune tick.
    const auto& fireball = real(133);
    assert(fireball.periodicDamage && fireball.periodicIntervalMs && fireball.durationMs >= fireball.periodicIntervalMs);
    const unsigned expectedTicks = fireball.durationMs / fireball.periodicIntervalMs;
    assert(expectedTicks == 2);
    const auto seq = lastSequence(w.game);
    const auto countTicks = [&](unsigned& immune, unsigned& damaging) {
        immune = damaging = 0;
        for (const auto& e : w.game.combatEvents())
            if (e.sequence > seq && e.spell == 133 && e.kind == LocalCombatEventKind::PeriodicDamage) {
                if (e.outcome == LocalMeleeOutcome::Immune) { ++immune; assert(!e.attempted && !e.effective && !e.resisted); }
                else ++damaging;
            }
    };
    unsigned immuneTicks = 0, damagingTicks = 0;
    for (unsigned t = 0; t < 8 && !immuneTicks; ++t) { w.game.tick(.25f, w.players); countTicks(immuneTicks, damagingTicks); }
    assert(immuneTicks == 1 && !damagingTicks);
    { const auto* kept = findNpc(w.game, w.npcGuid); // SendTickImmune, then return: the aura is kept
      assert(std::any_of(kept->damageAuras.begin(), kept->damageAuras.end(), [](const auto& a) { return a.spellId == 133 && a.remainingMs; })); }
    for (unsigned t = 0; t < 40; ++t) w.game.tick(.25f, w.players); // 10 s more, past the burn
    countTicks(immuneTicks, damagingTicks);
    assert(immuneTicks == expectedTicks && damagingTicks == 0);
    { const auto* gone = findNpc(w.game, w.npcGuid);
      assert(std::none_of(gone->damageAuras.begin(), gone->damageAuras.end(), [](const auto& a) { return a.spellId == 133; })); }
    live->immuneSchoolMask = 16;

    // Rock Elemental 92: nature-immune. Lightning Bolt (nature) is Immune; Fire
    // Blast (fire) lands; neither carries a mechanic. (Earth Shock is not
    // accepted by the importer - its interrupt effect - so it is not a fixture.)
    assert(real(403).schoolMask == 8 && !real(403).mechanic && real(2136).schoolMask == 4 && !real(2136).mechanic);
    World rock; buildWorld(rock, {403, 2136}, {{7, LocalResourceType::Mana, 80}, {8, LocalResourceType::Mana, 80}}, creature(92));
    assert(castUntilResolved(rock, rock.casters[0], 403, &r) == LocalMeleeOutcome::Immune && !r.event->attempted);
    assert(castUntilResolved(rock, rock.casters[1], 2136, &r) != LocalMeleeOutcome::Immune && r.event->effective > 0);

    // Lesser Rock Elemental 2735: snare-immune, not frost-immune. Frostbolt
    // lands its damage and its snare is stripped; no immune line, no snare.
    const auto& lesser = creature(2735);
    assert(((lesser.immuneMechanicsMask >> 11) & 1u) && !(lesser.immuneSchoolMask & 16));
    World snare; buildWorld(snare, {116}, {{8, LocalResourceType::Mana, 80}}, lesser);
    const auto snareBefore = npcHealth(snare);
    const auto outcome = castUntilResolved(snare, snare.casters[0], 116, &r);
    assert(outcome == LocalMeleeOutcome::Hit || outcome == LocalMeleeOutcome::Critical);
    assert(npcHealth(snare) < snareBefore && r.event && r.event->effective > 0);
    const auto* sn = findNpc(snare.game, snare.npcGuid);
    assert(sn->snares.empty());
    // Control: the same Frostbolt on a plain creature applies the snare.
    World plain; buildWorld(plain, {116}, {{8, LocalResourceType::Mana, 80}}, creature(plainHostile(lesser.level)));
    assert(castUntilResolved(plain, plain.casters[0], 116) != LocalMeleeOutcome::Immune);
    assert(!findNpc(plain.game, plain.npcGuid)->snares.empty());
    checkConservation(w.game); checkConservation(rock.game); checkConservation(snare.game); checkConservation(plain.game);
    std::cout << "PASS school and snare immunity: Frostbolt on Lesser Water Elemental 691 (frost-immune) resolved Immune, "
                 "paid its mana, applied no snare and no damage while Fireball landed; with the creature's definition "
                 "made fire-immune under the live burn, " << immuneTicks
              << " of " << expectedTicks << " ticks resolved Immune with nothing attempted and the aura survived the first of them; Lightning Bolt on Rock Elemental 92 "
                 "(nature-immune) is Immune while Fire Blast lands; Frostbolt on Lesser Rock Elemental 2735 (snare-immune, "
                 "not frost-immune) landed its damage with the snare stripped and no immune line, and the same Frostbolt "
                 "snares a plain creature\n";
}

// ---------------------------------------------------------------------------
// 5. Effect 38 beside damage: Shield Slam x8 admitted with the shield block
//    term, the dispel draws from an empty list, the fifteen pure dispels are
//    refused by name.
// ---------------------------------------------------------------------------
void shieldSlamAndPureDispels() {
    std::set<uint32_t> accepted;
    std::map<uint32_t, std::string> status;
    for (const auto& r : gImported.audit) { status[r.id] = r.status; if (r.status == "Supported decoder; imported") accepted.insert(r.id); }
    const uint32_t shieldSlams[] = {23922, 23923, 23924, 23925, 25258, 30356, 47487, 47488};
    for (auto id : shieldSlams) {
        assert(accepted.count(id));
        const auto& d = real(id);
        assert(d.dispelProfile == 1 && d.dispelAttempts == 1 && d.dispelType == 0 && d.effectMask == 3 && d.damage && d.schoolMask == 1);
        assert(d.requiredItemClass == 4 && d.requiredItemSubclasses == 64);
        assert(d.spellFamily == 4 && (d.spellFamilyFlags[1] & 0x200u) && d.cooldownCategory == 1209);
    }
    // The audit's twelve pure dispels the importer used to reject as
    // "Unsupported effect 38" are now refused by name; the three Absolution
    // ranks still die on their affected-spell gate; nothing else moved.
    const uint32_t pure[] = {527, 988, 370, 8012, 4987, 51886, 528, 8946, 1152, 475, 2782, 3137};
    for (auto id : pure) assert(status.at(id) == "A pure dispel is not implemented: nothing this realm holds can be dispelled");
    for (auto id : {33167u, 33171u, 33172u}) assert(status.at(id) == "Affected spells are not implemented for this modifier");
    assert(accepted.size() == 1004);
    for (const auto& d : gImported.spells) if (d.dispelProfile) assert(std::find(std::begin(shieldSlams), std::end(shieldSlams), d.id) != std::end(shieldSlams));
    // The Dispel column is carried on every definition: Frostbolt and Hammer of
    // Justice are magic (1), Curse of Agony a curse (2), Fireball none (0).
    assert(real(116).dispelType == 1 && real(853).dispelType == 1 && real(133).dispelType == 0 && real(980).dispelType == 2);
    assert(localDispelMask(7) == 0x1eu && localDispelMask(1) == 2u);

    // Runtime: a priest's Shadow Word: Pain and a mage's Frostbolt snare on a
    // plain creature, then a warrior's Shield Slam with a real shield.
    const auto& target = creature(plainHostile(60));
    World w; buildWorld(w, {589, 116, 23922}, {{5, LocalResourceType::Mana, 80}, {8, LocalResourceType::Mana, 80}, {1, LocalResourceType::Rage, 80}}, target);
    auto& warrior = w.casters[2];
    warrior.equipment[16] = 2443; warrior.inventory.push_back({2443, 1});
    assert(castUntilResolved(w, w.casters[0], 589) != LocalMeleeOutcome::Immune);
    assert(castUntilResolved(w, w.casters[1], 116) != LocalMeleeOutcome::Immune);
    const auto* n = findNpc(w.game, w.npcGuid);
    assert(!n->damageAuras.empty() && !n->snares.empty());
    // Both are harmful and player-cast, so an offensive magic dispel finds nothing.
    assert(localNpcDispellableAuraCount(*n, *w.content, localDispelMask(1), false, false) == 0);
    // A FRIENDLY magic dispel of the same creature would find its magic
    // debuffs (Shadow Word: Pain 589 is magic): the direction rule, transcribed.
    assert(localNpcDispellableAuraCount(*n, *w.content, localDispelMask(1), true, false) >= 1);
    const auto healthBefore = npcHealth(w);
    CastResult r;
    // Since the implementation (P05-1) Shield Slam rolls MeleeSpellHitResult: at twenty
    // levels above the creature it cannot miss but can still be dodged or
    // parried (1 % each) and blocked, so a landed cast is awaited rather than
    // assumed - an avoided cast applies nothing and retrying preserves state.
    // This fixture used to assume the unconditional Hit of the no-roll path.
    auto outcome = LocalMeleeOutcome::Miss;
    unsigned avoided = 0;
    for (unsigned attempt = 0; attempt < 60; ++attempt) {
        outcome = castUntilResolved(w, warrior, 23922, &r);
        if (!localMeleeAvoided(outcome)) break;
        ++avoided;
    }
    assert(outcome == LocalMeleeOutcome::Hit || outcome == LocalMeleeOutcome::Critical);
    assert(r.event && r.event->effective > 0 && npcHealth(w) < healthBefore);
    n = findNpc(w.game, w.npcGuid);
    assert(!n->damageAuras.empty() && !n->snares.empty()); // nothing was dispelled
    // The amount: effect 1's 294-308 (bp 293, die 15) plus the shield block
    // value, soft/hard capped (SpellEffects.cpp:360-369, Unit.h:1181-1194),
    // through the plain creature's armor; a critical is the doubled amount
    // through armor (Unit::SpellCriticalDamageBonus, +100 % for a MELEE damage
    // class - the implementation; this fixture used to accept the 1.5x magic multiplier,
    // which the audit found no Shield Slam ever reached). `attempted` carries
    // the partial block, so a blocked cast reads the same.
    const auto stats = localMeleeStats(warrior, *w.content);
    const auto term = localShieldSlamBlockValue(stats.shieldBlockValue, uint32_t(80 * 24.5f), uint32_t(80 * 34.5f));
    assert(stats.shieldBlockValue > 0 && term == stats.shieldBlockValue);
    assert(real(23922).damage == 294 && real(23922).damageMax == 308);
    bool amountExplained = false;
    for (uint32_t base = real(23922).damage; base <= real(23922).damageMax; ++base) {
        const auto expected = localArmorReducedDamage(outcome == LocalMeleeOutcome::Critical ? localMeleeCriticalAmount(base + term) : base + term, target.armor, 80);
        if (r.event->attempted == expected) amountExplained = true;
    }
    assert(amountExplained);
    assert(!r.event->blocked || r.event->blocked == localCreatureBlockValue(*findNpc(w.game, w.npcGuid)));
    // And the term is load-bearing: no roll without it reaches the observed amount.
    for (uint32_t base = real(23922).damage; base <= real(23922).damageMax; ++base) {
        const auto bare = localArmorReducedDamage(outcome == LocalMeleeOutcome::Critical ? localMeleeCriticalAmount(base) : base, target.armor, 80);
        assert(r.event->attempted != bare);
    }
    assert(!r.event->resisted); // physical never resists
    // Without a shield the equipment gate refuses the cast, as the client's
    // own EquippedItemClass/SubclassMask (columns 68-69) require.
    auto bare = warrior; bare.equipment[16] = 0;
    auto rr = castOnce(w, bare, 23922);
    assert(!rr.executed);
    checkConservation(w.game);
    std::cout << "PASS Shield Slam and the pure dispels: the eight Shield Slam ranks are accepted (1,004 audited-accepted "
                 "ids, 982 + 8 + 2.00's 14), each carrying dispelProfile 1 / one attempt / magic beside weapon damage; "
                 "the twelve pure dispels are refused as \"A pure dispel is not implemented\" and the three Absolution "
                 "ranks still stop at their modifier gate; against " << target.name
              << " holding Shadow Word: Pain and a Frostbolt snare the offensive magic dispel list is empty (a friendly "
                 "one would find the magic debuff), Shield Slam landed " << r.event->attempted << (outcome == LocalMeleeOutcome::Critical ? " as a 2x critical" : "")
              << " after " << avoided << " avoided cast(s) (294-308 plus the block term, through armor" << (r.event->blocked ? ", partially blocked" : "")
              << ") with a real shield (block value " << stats.shieldBlockValue << " under the level-80 caps "
              << uint32_t(80 * 24.5f) << " / " << uint32_t(80 * 34.5f) << "), removed nothing, and is refused without a shield\n";
}

// ---------------------------------------------------------------------------
// 6. The resistance census: 243 resistable magic spells x 288 hostile rows (250 at the implementation).
// ---------------------------------------------------------------------------
void resistanceCensus() {
    std::vector<const LocalSpellDefinition*> magic, holy, physical;
    for (const auto& d : gImported.spells) {
        if (!castable(d) || !(d.damage || d.periodicDamage)) continue;
        if (d.schoolMask & 1u) { physical.push_back(&d); assert(!localPartialResistApplies(d.schoolMask, true, d.sourceNoCastLog, false)); continue; }
        if (d.schoolMask == 2u) { holy.push_back(&d); continue; }
        assert(d.schoolMask && !(d.schoolMask & 3u));
        magic.push_back(&d);
    }
    // 250 resistable magic spells at the implementation; 243 since the implementation retired the seven
    // nature-school proc copies (Lightning Overload's 49239/49240/49268/49269,
    // Lightning Shield's leaves 26372/49278/49279) from the castable set.
    assert(magic.size() == 243 && holy.size() == 44 && physical.size() == 112);
    for (auto* d : magic) assert(!d->sourceNoCastLog); // no admitted magic spell carries NO_CAST_LOG
    // Every accepted mask is a single magic bit, so the minimum rule has no
    // local effect today; it is still the reference's reduction.
    for (auto* d : magic) assert((d->schoolMask & (d->schoolMask - 1)) == 0);
    unsigned pairs = 0; std::set<uint32_t> creatures, spells; std::map<uint32_t, unsigned> bySchool;
    double meanFraction = 0;
    for (auto* d : magic)
        for (auto e : gHostile) {
            const auto& def = creature(e);
            const auto r = localResistanceForMask(def.resistances, d->schoolMask);
            if (!r) continue;
            ++pairs; creatures.insert(e); spells.insert(d->id); ++bySchool[d->schoolMask];
            meanFraction += localAverageResist(r, def.level, def.level, false);
        }
    meanFraction /= pairs;
    // 37,910 pairs at the implementation; the seven retired nature spells x 157 nature rows
    // = 1,099 fewer, 36,811, over the same 288 creatures.
    const bool exact = expect(pairs == 36811 && creatures.size() == 288 && spells.size() == 243,
        "resistance pairs: " + std::to_string(pairs) + " over " + std::to_string(creatures.size()) + " creatures and " +
        std::to_string(spells.size()) + " spells against 36811 / 288 / 243 (the audit's 37910 / 288 / 250 less the seven nature-school proc copies the reference retired)");
    // Section 3.3 by school: fire 13,120, shadow 8,820, nature 6,437, arcane 5,945, frost 3,588.
    const bool schoolsExact = expect(bySchool[4] == 13120 && bySchool[32] == 8820 && bySchool[8] == 5338 && bySchool[64] == 5945 && bySchool[16] == 3588,
        "resistance pairs by school: fire " + std::to_string(bySchool[4]) + " shadow " + std::to_string(bySchool[32]) + " nature " +
        std::to_string(bySchool[8]) + " arcane " + std::to_string(bySchool[64]) + " frost " + std::to_string(bySchool[16]));
    // Holy: the gate admits it against a creature unless the spell carries
    // SPELL_ATTR4_NO_CAST_LOG ("Cannot be resisted", SharedDefines.h:518) -
    // only the Holy Shock damage rows do - and never against a player; and no
    // creature carries a holy row.
    unsigned holyNoCastLog = 0;
    for (auto* d : holy) {
        holyNoCastLog += d->sourceNoCastLog;
        assert(localPartialResistApplies(d->schoolMask, true, d->sourceNoCastLog, false) == !d->sourceNoCastLog);
        assert(!localPartialResistApplies(d->schoolMask, false, d->sourceNoCastLog, false));
    }
    assert(holyNoCastLog > 0);
    for (auto* d : holy) if (d->sourceNoCastLog) assert(d->spellFamily == 10 && d->name == "Holy Shock");
    for (auto e : gHostile) assert(localResistanceForMask(creature(e).resistances, 2) == 0);
    // The minimum rule and the empty-mask answer.
    assert(localResistanceForMask({0, 300, 0, 50, 0, 0}, 4 | 16) == 50 && localResistanceForMask({0, 300, 0, 50, 0, 0}, 4) == 300);
    assert(localResistanceForMask({7, 7, 7, 7, 7, 7}, 0) == 0 && localResistanceForMask({7, 7, 7, 7, 7, 7}, 1) == 0);
    if (exact && schoolsExact)
        std::cout << "PASS resistance census: of the 399 castable damaging definitions 243 are resistable magic (single "
                     "school bits, none NO_CAST_LOG), 44 holy (gated to creature victims, which every local target is, met by "
                     "0 rows, and " << holyNoCastLog << " of them - the Holy Shock damage rows - exempt by NO_CAST_LOG) and 112 physical "
                     "(never); the 243 meet the 288 hostile creatures with a row in exactly "
                     "36811 pairs - fire 13120, shadow 8820, nature 5338, arcane 5945, frost 3588 - whose even-level average "
                     "resist is " << meanFraction * 100 << " % (the audit's 32.7 % is over the 243 non-binary ones; the seven "
                     "Frost Shock ranks are binary at the pin and deferred, so here they partially resist like the rest)\n";
}

// ---------------------------------------------------------------------------
// 7. The bucket roll for real: Fire Blast on Dessecus (560 in every school) by
//    a level-56 mage, average 0.8 capped at 0.75, so buckets 6..9 at 12.5 /
//    37.5 / 37.5 / 12.5 %.
// ---------------------------------------------------------------------------
unsigned bucketOf(const LocalCombatEvent& e) {
    for (unsigned i = 0; i <= 10; ++i) if (localResistedAmount(e.attempted, i) == e.resisted) return i;
    std::cerr << "FAIL: resisted " << e.resisted << " of " << e.attempted << " is not a bucket\n"; std::abort();
}
void bucketDistribution() {
    const auto& dessecus = creature(7104);
    assert(dessecus.level == 56);
    const float average = localAverageResist(560, 56, 56, false);
    assert(std::fabs(average - 0.75f) < 1e-6f); // 560 / (560 + 140) = 0.8, capped
    // The function itself over 200,000 uniform rolls.
    std::array<unsigned, 11> counts{};
    for (unsigned i = 0; i < 200000; ++i) ++counts[localPartialResistBucket(average, (i + 0.5f) / 200000.f)];
    assert(!counts[0] && !counts[1] && !counts[2] && !counts[3] && !counts[4] && !counts[5] && !counts[10]);
    assert(std::abs(int(counts[6]) - 25000) < 300 && std::abs(int(counts[7]) - 75000) < 300 &&
           std::abs(int(counts[8]) - 75000) < 300 && std::abs(int(counts[9]) - 25000) < 300);
    // The runtime, 4000 instant Fire Blasts.
    World w; buildWorld(w, {2136}, {{8, LocalResourceType::Mana, 56}}, dessecus);
    std::array<unsigned, 11> observed{}; unsigned landed = 0, misses = 0; double fraction = 0, truncatedExpectation = 0;
    for (unsigned i = 0; i < 4000; ++i) {
        auto r = castOnce(w, w.casters[0], 2136);
        assert(r.executed && r.event);
        if (r.event->outcome == LocalMeleeOutcome::Miss) { ++misses; continue; }
        assert(r.event->outcome == LocalMeleeOutcome::Hit || r.event->outcome == LocalMeleeOutcome::Critical);
        ++landed; ++observed[bucketOf(*r.event)];
        fraction += double(r.event->resisted) / r.event->attempted;
        // Unit.cpp:2367 truncates `damage * i / 10` before the cast, so on a
        // small hit the realised fraction sits below the bucket's i / 10; the
        // expectation is taken through the same truncation, per hit.
        const double share[4] = {.125, .375, .375, .125};
        for (unsigned b = 6; b <= 9; ++b) truncatedExpectation += share[b - 6] * double(localResistedAmount(r.event->attempted, b)) / r.event->attempted;
        assert(r.event->attempted == r.event->resisted + r.event->effective);
    }
    fraction /= landed; truncatedExpectation /= landed;
    assert(landed > 3800);
    for (unsigned b : {0u, 1u, 2u, 3u, 4u, 5u, 10u}) assert(!observed[b]);
    const auto within = [&](unsigned b, double p) { const double sd = std::sqrt(landed * p * (1 - p)); return std::fabs(observed[b] - landed * p) < 4.5 * sd; };
    const bool exact = expect(within(6, .125) && within(7, .375) && within(8, .375) && within(9, .125),
        "bucket distribution: " + std::to_string(observed[6]) + " / " + std::to_string(observed[7]) + " / " +
        std::to_string(observed[8]) + " / " + std::to_string(observed[9]) + " of " + std::to_string(landed) +
        " against 12.5 / 37.5 / 37.5 / 12.5 %");
    assert(truncatedExpectation <= 0.75 && truncatedExpectation > 0.7);
    assert(std::fabs(fraction - truncatedExpectation) < 0.01);
    // The audit's own worked example, Searing Infernal 6073 (fire 300 at level
    // 29, "76.9 % capped at 75 %"): at the pin its set makes it FIRE-IMMUNE, so
    // a Fireball is Immune at the hit roll and the resist roll never runs.
    World infernal; buildWorld(infernal, {2136}, {{8, LocalResourceType::Mana, 29}}, creature(6073));
    CastResult r;
    assert(castUntilResolved(infernal, infernal.casters[0], 2136, &r) == LocalMeleeOutcome::Immune);
    assert(!r.event->resisted);
    checkConservation(w.game); checkConservation(infernal.game);
    if (exact)
        std::cout << "PASS bucket distribution: Dessecus 7104 (level 56, 560 in every magic school) against a level-56 "
                     "mage gives average 0.8 capped to 0.75; the bucket walk over 200000 uniform rolls and " << landed
                  << " real Fire Blasts (" << misses << " missed) both fall only in buckets 6-9 - measured "
                  << observed[6] << " / " << observed[7] << " / " << observed[8] << " / " << observed[9]
                  << " against 12.5 / 37.5 / 37.5 / 12.5 % - with the mean fraction resisted " << fraction * 100
                  << " % against " << truncatedExpectation * 100 << " % expected once Unit.cpp:2367's integer truncation of "
                     "damage * i / 10 is applied to each hit's own amount; the audit's Searing Infernal example is fire-IMMUNE by its set at the pin, so its Fireball "
                     "never reaches the roll\n";
}

// ---------------------------------------------------------------------------
// 8. The level term needs no data; physical never resists; holy resists a
//    creature only by the level term, because no creature carries a holy row.
// ---------------------------------------------------------------------------
void levelTermPhysicalHoly() {
    // Unit.cpp:2309-2323 by hand: +5 per level, K(20) = 50, K(56) = 140, K(80) = 400.
    assert(std::fabs(localAverageResist(0, 20, 23, false) - 15.f / 65.f) < 1e-6f);
    assert(std::fabs(localAverageResist(0, 80, 83, false) - 15.f / 415.f) < 1e-6f);
    assert(localAverageResist(0, 23, 23, false) == 0.f && localAverageResist(0, 80, 20, false) == 0.f);
    assert(localAverageResist(0, 20, 23, true) == 0.f);          // a binary spell skips the term
    assert(std::fabs(localAverageResist(100, 0, 40, false) - 100.f / 200.f) < 1e-6f); // no caster: K from the victim
    for (float a = 0; a <= 0.1f; a += 0.001f) {                    // the <= 0.1 special case sums to a
        std::array<float, 11> p{};
        for (unsigned i = 0; i < 11; ++i) p[i] = std::max(0.f, 0.5f - 2.5f * std::fabs(0.1f * i - a));
        p[0] = 1 - 7.5f * a; p[1] = 5 * a; p[2] = 2.5f * a;
        float mean = 0; for (unsigned i = 0; i < 11; ++i) mean += p[i] * i / 10.f;
        assert(std::fabs(mean - a) < 1e-5f);
    }
    // A level-20 mage's Fire Blast on a zero-resistance creature three levels up.
    const auto& up3 = creature(plainHostile(23));
    World w; buildWorld(w, {2136}, {{8, LocalResourceType::Mana, 20}}, up3);
    double fraction = 0, truncatedExpectation = 0; unsigned landed = 0, resistedHits = 0; std::array<unsigned, 11> observed{};
    // The eleven bucket probabilities at a = 15 / 65, exactly as Unit.cpp:2345-2358 forms them.
    const float a = localAverageResist(0, 20, 23, false);
    std::array<double, 11> share{};
    for (unsigned i = 0; i < 11; ++i) share[i] = std::max(0.f, 0.5f - 2.5f * std::fabs(0.1f * i - a));
    for (unsigned i = 0; i < 3000; ++i) {
        auto r = castOnce(w, w.casters[0], 2136);
        assert(r.executed && r.event);
        if (r.event->outcome == LocalMeleeOutcome::Miss) continue;
        ++landed; fraction += double(r.event->resisted) / r.event->attempted; resistedHits += r.event->resisted != 0;
        ++observed[bucketOf(*r.event)];
        for (unsigned b = 0; b <= 10; ++b) truncatedExpectation += share[b] * double(localResistedAmount(r.event->attempted, b)) / r.event->attempted;
    }
    fraction /= landed; truncatedExpectation /= landed;
    assert(truncatedExpectation < 15. / 65. && truncatedExpectation > 0.2); // truncation only ever lowers it
    const bool termExact = expect(std::fabs(fraction - truncatedExpectation) < 0.01,
        "level term: mean fraction resisted " + std::to_string(fraction) + " against 15 / 65 = 0.2308 through per-hit truncation, " +
        std::to_string(truncatedExpectation));
    for (unsigned b : {0u, 5u, 6u, 7u, 8u, 9u, 10u}) assert(!observed[b]); // buckets 1-4 only at 0.2308
    // The same mage at the creature's own level: the term is zero and nothing resists.
    World even; buildWorld(even, {2136}, {{8, LocalResourceType::Mana, 23}}, up3);
    for (unsigned i = 0; i < 300; ++i) { auto r = castOnce(even, even.casters[0], 2136); assert(r.executed && r.event && !r.event->resisted); }
    // Physical: Shield Slam on Dessecus (560 in every magic school, level 56)
    // from a level-50 warrior with a shield - six levels down AND 560 of
    // everything, and still no resist, because armor is physical's mitigation.
    World phys; buildWorld(phys, {23922}, {{1, LocalResourceType::Rage, 50}}, creature(7104), 100000000, 2); // melee range
    phys.casters[0].equipment[16] = 2443; phys.casters[0].inventory.push_back({2443, 1});
    unsigned physLanded = 0;
    for (unsigned i = 0; i < 200; ++i) {
        auto r = castOnce(phys, phys.casters[0], 23922);
        assert(r.executed && r.event && !r.event->resisted);
        physLanded += !localOutcomeNullifiesDamage(r.event->outcome) && r.event->effective > 0;
    }
    assert(physLanded > 100);
    // Holy: Smite on Dessecus at its own level never resists (0 holy rows); by
    // a level-51 priest it resists by the level term alone - CalcAbsorbResist
    // admits holy against a creature victim ("holy resistance exists for npcs").
    World holyEven; buildWorld(holyEven, {585}, {{5, LocalResourceType::Mana, 56}}, creature(7104));
    for (unsigned i = 0; i < 120; ++i) { auto r = castOnce(holyEven, holyEven.casters[0], 585); assert(r.executed && r.event && !r.event->resisted); }
    World holyUp; buildWorld(holyUp, {585}, {{5, LocalResourceType::Mana, 51}}, creature(7104));
    unsigned holyResisted = 0, holyLanded = 0;
    for (unsigned i = 0; i < 300; ++i) {
        auto r = castOnce(holyUp, holyUp.casters[0], 585);
        assert(r.executed && r.event);
        if (r.event->outcome == LocalMeleeOutcome::Miss) continue;
        ++holyLanded; holyResisted += r.event->resisted != 0;
    }
    assert(std::fabs(localAverageResist(0, 51, 56, false) - 25.f / 152.5f) < 1e-6f);
    assert(holyResisted > holyLanded / 4); // average 0.164: buckets 0-3, bucket 0 at 9 %, so most hits carry a resist
    checkConservation(w.game); checkConservation(even.game); checkConservation(phys.game); checkConservation(holyEven.game); checkConservation(holyUp.game);
    if (termExact)
        std::cout << "PASS level term, physical and holy: a level-20 mage's Fire Blast on " << up3.name
                  << " (level 23, no row) lost " << fraction * 100 << " % on average over " << landed
                  << " landed casts (15 / 65 = 23.08 %, which Unit.cpp:2367's truncation brings to " << truncatedExpectation * 100
                  << " % on these amounts; buckets 1-4 only, " << resistedHits
                  << " hits carrying a resist) and nothing at level 23; Shield Slam on Dessecus from six levels down "
                     "never resists (physical, " << physLanded << " of 200 landed); Smite on Dessecus at even level never resists (0 holy rows anywhere) and "
                     "from five levels down resists by the level term on " << holyResisted << " of " << holyLanded
                  << " hits (average 25 / 152.5 = 16.4 %) - holy against a creature is inside the reference's gate, "
                     "not outside it\n";
}

// ---------------------------------------------------------------------------
// 9. The full-resist shape: bucket 10 is Resist, PROC_HIT_FULL_RESIST alone,
//    and the codec carries it - and it has no producer, because the 75 % cap
//    keeps bucket 10 at zero probability.
// ---------------------------------------------------------------------------
void fullResistShape() {
    // p[10] = 0.5 - 2.5 * |1.0 - a| is positive only for a > 0.8, and
    // GetEffectiveResistChance caps a at 0.75: the bucket walk cannot reach 10.
    for (float a = 0; a <= 0.75f + 1e-6f; a += 0.0005f)
        for (float r : {0.f, 0.25f, 0.5f, 0.75f, 0.9f, 0.99f, 0.9999f}) assert(localPartialResistBucket(a, r) <= 9);
    assert(localPartialResistBucket(0.75f, 0.9999f) == 9 && localPartialResistBucket(0.9f, 0.9999f) == 10);
    for (uint32_t d : {1u, 15u, 100u, 1000000u}) assert(localResistedAmount(d, 10) == d && localResistedAmount(d, 0) == 0);
    assert(localResistedAmount(15, 1) == 1); // integer arithmetic before the cast
    // The shape, from a synthesized observation and view: what damageNpc would
    // emit for bucket 10 - outcome Resist, amount 0, resisted the whole hit.
    LocalCombatEvent e; e.kind = LocalCombatEventKind::SpellDamage; e.source = 1; e.target = 2; e.spell = 2136;
    e.attempted = 120; e.resisted = 120; e.effective = 0; e.outcome = LocalMeleeOutcome::Resist;
    assert(localProcEventHitMask(e) == LocalProcHitFullResist);
    assert(localOutcomeNullifiesDamage(e.outcome) && !localMeleeAvoided(e.outcome));
    LocalRealmPlayer p; p.guid = 1; p.classId = 8; p.positionRevision = 3; p.meleeViewPositionRevision = 3; p.meleeSerial = 1;
    p.meleeViews.back() = {1, 2136, 0, 0, 1, 2, LocalMeleeOutcome::Resist, false, false, 120};
    Writer wire; writeCast(wire, p);
    assert(wire.bytes.size() == CastWireBytes);
    LocalRealmPlayer copy; copy.guid = 1; copy.classId = 8; copy.positionRevision = 3;
    Reader r(wire.bytes.data(), wire.bytes.size());
    assert(readCast(r, copy) && r.done());
    assert(copy.meleeViews.back().outcome == LocalMeleeOutcome::Resist && copy.meleeViews.back().resisted == 120 && !copy.meleeViews.back().amount);
    // Zero runtime producers: no damage event in any world this suite drove
    // was a full resist (the counter is asserted at the end of main).
    std::cout << "PASS full-resist shape: bucket 10 has zero probability at every average up to the 75 % cap (p[10] "
                 "needs > 0.8), so LocalMeleeOutcome::Resist from the partial roll is a transcribed shape with no producer "
                 "until the binary hit roll of step 3; the shape is nonetheless complete - localResistedAmount(d, 10) == d, "
                 "a Resist observation masks to exactly LocalProcHitFullResist (0x8), and a Resist view with amount 0 and "
                 "resisted 120 round-trips through writeCast/readCast at " << CastWireBytes << " bytes\n";
}

// ---------------------------------------------------------------------------
// 10. LAN 85 and save 30: the view field, its validation, the NPC wire and the
//     player block. Both versions moved at the implementation for the pet roster; nothing
//     this group measures moved with them.
// ---------------------------------------------------------------------------
void formats() {
    // the implementation moved both, for the pet roster: writePet gained the pet's
    // command state, react state and stay point, and that one layout is
    // shared by the character save and the LAN pet deck. Nothing this
    // group measures moved with it.
    static_assert(SaveVersion == 30);
    static_assert(Version == 85 && Version == lan::GameplayVersion);
    static_assert(CastWireBytes == 62 + 4 * 39 + 4);
    static_assert(NpcWireBytes == 618); // definition data is not on the NPC wire
    LocalRealmPlayer p; p.guid = 7; p.classId = 8; p.positionRevision = 1; p.meleeViewPositionRevision = 1;
    auto view = [&](uint32_t serial, LocalMeleeOutcome o, uint32_t amount, uint32_t resisted, bool healing = false) {
        LocalMeleeView v; v.serial = serial; v.spell = 2136; v.source = 7; v.target = healing ? 7 : 9; v.outcome = o;
        v.amount = amount; v.resisted = resisted; v.healing = healing; return v; };
    p.meleeViews = {view(1, LocalMeleeOutcome::Hit, 80, 40), view(2, LocalMeleeOutcome::Critical, 100, 20),
                    view(3, LocalMeleeOutcome::Resist, 0, 150), view(4, LocalMeleeOutcome::Hit, 33, 0)};
    p.meleeSerial = 4;
    const auto encode = [](const LocalRealmPlayer& q) { Writer w; writeCast(w, q); return w.bytes; };
    const auto decode = [](const std::vector<uint8_t>& bytes, LocalRealmPlayer& into) { Reader r(bytes.data(), bytes.size()); return readCast(r, into) && r.done(); };
    auto loaded = p; loaded.meleeViews = {};
    assert(decode(encode(p), loaded));
    for (size_t i = 0; i < 4; ++i) {
        assert(loaded.meleeViews[i].resisted == p.meleeViews[i].resisted && loaded.meleeViews[i].amount == p.meleeViews[i].amount &&
               loaded.meleeViews[i].outcome == p.meleeViews[i].outcome);
    }
    // The LAN 83 prefix of every view is untouched: the 35 bytes before the
    // resisted word decode as they did, and the new word sits at offset 35.
    const auto bytes = encode(p);
    assert(bytes.size() == CastWireBytes);
    const auto wordAt = [&](size_t at) { // the codec is network order
        return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) | (uint32_t(bytes[at + 2]) << 8) | uint32_t(bytes[at + 3]); };
    assert(wordAt(66) == 1 && wordAt(66 + 4) == 2136 && wordAt(66 + 8) == 80); // the LAN 83 head of view 0
    assert(wordAt(66 + 35) == 40 && wordAt(66 + 39 * 2 + 35) == 150);
    // Refusals: a resist on a heal, on a miss, on an immune, on a deflect, on an
    // empty slot, and above the bound.
    unsigned rejected = 0;
    auto rejects = [&](LocalRealmPlayer q) { LocalRealmPlayer into = p; into.meleeViews = {}; if (!decode(encode(q), into)) ++rejected; else assert(false); };
    { auto q = p; q.meleeViews[3] = view(4, LocalMeleeOutcome::Hit, 33, 5, true); q.meleeViews[3].spell = 2136; rejects(q); }
    for (auto o : {LocalMeleeOutcome::Miss, LocalMeleeOutcome::Immune, LocalMeleeOutcome::Deflect, LocalMeleeOutcome::Dodge, LocalMeleeOutcome::Parry})
    { auto q = p; q.meleeViews[3] = view(4, o, 0, 5); rejects(q); }
    { auto q = p; q.meleeViews[3] = view(4, LocalMeleeOutcome::Hit, 33, 1000001); rejects(q); }
    { auto q = p; q.meleeViews = {}; q.meleeViews[0] = view(0, LocalMeleeOutcome::Hit, 0, 1); q.meleeSerial = 0; rejects(q); }
    { auto q = p; q.meleeViews[3] = view(4, LocalMeleeOutcome::Resist, 1, 5); rejects(q); } // nullifying outcomes carry no amount
    assert(rejected == 9);
    // A Resist view with resisted 0 is still admitted - the the implementation codec suite
    // pins that, and the the implementation reader does not narrow it.
    { auto q = p; q.meleeViews[3] = view(4, LocalMeleeOutcome::Resist, 0, 0); LocalRealmPlayer into = p; into.meleeViews = {}; assert(decode(encode(q), into)); }
    // Every truncation point is refused.
    for (size_t n = 0; n < bytes.size(); ++n) { std::vector<uint8_t> shortBytes(bytes.begin(), bytes.begin() + n); LocalRealmPlayer into = p; into.meleeViews = {}; assert(!decode(shortBytes, into)); }
    // The owner-progress budget still covers a full cast block.
    static_assert(MaxOwnerProgressBytes <= 8192);
    // Definitions are not saved and the player block did not move.
    LocalRealmPlayer saved; saved.guid = 1; saved.classId = 8; saved.level = 80; saved.money = 321; saved.knownSpells = {2136, 23922};
    Writer current; writeProgress(current, saved);
    LocalRealmPlayer restored; restored.guid = 1;
    Reader read(current.bytes.data(), current.bytes.size());
    assert(readProgress(read, restored) && read.done());
    assert(restored.money == saved.money && restored.knownSpells == saved.knownSpells);
    // The per-player block last grew at save 29 (the area emitters); save 30
    // grew the realm-level PET roster instead, so the migration is pinned at
    // its own boundary, 28 -> 29, and 30 is asserted to have left the player
    // block byte-identical.
    Writer previous; writeProgress(previous, saved, 28);
    Writer atTwentyNine; writeProgress(atTwentyNine, saved, 29);
    assert(previous.bytes.size() < atTwentyNine.bytes.size());
    assert(atTwentyNine.bytes == current.bytes);
    LocalRealmPlayer legacy; legacy.guid = 1;
    Reader old(previous.bytes.data(), previous.bytes.size());
    assert(readProgress(old, legacy, 28) && old.done());
    // An NPC with an immunity set and a resistance row encodes exactly as one
    // without: both are definition data on both peers.
    LocalWorldContent c; c.npcs.push_back(creature(92)); c.npcs.push_back(creature(plainHostile(39)));
    LocalRealmNpc a; a.guid = 0xf130000000000001ULL; a.entry = 92; a.level = 39; a.health = 80; a.maxHealth = 100;
    auto b = a; b.entry = plainHostile(39);
    Writer wa; writeNpc(wa, a); Writer wb; writeNpc(wb, b);
    assert(wa.bytes.size() == wb.bytes.size());
    std::cout << "PASS LAN" << int(Version) << " / save" << int(SaveVersion) << ": the cast block is " << CastWireBytes
              << " bytes (four 39-byte views, the resisted word at offset 35 of each, every LAN 83 offset kept), a window of "
                 "partial, critical-partial, full and unresisted views round-trips, " << rejected
              << " malformed shapes (a resist on a heal, a miss, an immune, a deflect, a dodge, a parry, an empty slot, above the "
                 "bound, and an amount beside a full resist) are refused while a zero-resist Resist view stays admitted as the the reference "
                 "suite pins it, all " << bytes.size() << " truncation points are refused, NpcWireBytes is still " << NpcWireBytes
              << " with an immune, resistant creature encoding to the same length as a plain one, and the save" << int(SaveVersion)
              << " player block still round-trips, with save28 a strictly shorter readable prefix of save29, which is byte-identical to the current one\n";
}
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    assert(argc == 3);
    ClientTables tables;
    tables.load(argv[1]);
    gTables = &tables;
    gImported = tables.import();
    loadCatalog(argv[2]);
    catalogCensus();
    immunityCensus();
    stunImmuneBeforeDiminishing();
    schoolAndSnareImmunity();
    shieldSlamAndPureDispels();
    resistanceCensus();
    bucketDistribution();
    levelTermPhysicalHoly();
    fullResistShape();
    formats();
    assert(gEventsChecked > 0 && gEventsWithResist > 0);
    assert(gFullResists == 0); // the 75 % cap keeps bucket 10 at zero probability: no producer, as group 9 proves
    std::cout << "PASS conservation identity: resisted + effective + absorbed + blocked == attempted held on every one of "
              << gEventsChecked << " damage observations this suite retained (" << gEventsWithResist
              << " of them carrying a resist), " << gFullResists << " observations were a full resist, and every resist rode a Hit, a Critical or a Resist\n";
    if (gFailures)
        std::cerr << gFailures << " group(s) failed against source-backed fixtures / "
                     "source-backed fixtures\n";
    return gFailures ? 1 : 0;
}
