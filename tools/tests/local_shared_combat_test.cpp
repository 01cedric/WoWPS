// P05 / the implementation - shared combat rules, first checkpoint: the melee-class hit and
// critical roll for the 39 unprofiled DmgClass 2 spells (P05-1), the partial
// block on every physical melee-class spell and one creature block value
// (P05-2), the initial SpellLevel / spell_threat threat, the paladin heal
// halving and the assisting split (P05-6), the Lightning Overload proc copies
// retired from the trainable set (the importer defect of audit section 7 item
// 6), the LAN repeat / reconnect invariants (P05-8), and a save and a LAN
// protocol that carry nothing of P05's.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33, as measured
// in the source audit sections 1.6, 2.2-2.7, 3 and 8.1:
//   WorldObject::SpellHitResult       Object.cpp:3682-3741 - ALWAYS_HIT, immune,
//       positive, self, then MELEE / RANGED -> MeleeSpellHitResult (:3719-3723).
//   Unit::MeleeSpellHitResult         Unit.cpp:3317-3503 - miss, NO_ACTIVE_DEFENSE
//       exit, dodge, parry, full block for COMPLETELY_BLOCKED && !CU_DIRECT_DAMAGE.
//   Unit::isSpellBlocked              Unit.cpp:3263-3287 - partial block for a
//       physical MELEE / RANGED class spell, from the front, not NO_BLOCK.
//   Unit::CalculateSpellDamageTaken   Unit.cpp:1492-1633 - armour, block, 2x crit.
//   Unit::SpellCriticalDamageBonus    Unit.cpp:9117-9154 - +100 % MELEE / RANGED,
//       +50 % else; the periodic tick uses it too (SpellAuraEffects.cpp:6368).
//   Creature::GetShieldBlockValue     Creature.h:158-161 - level / 2 + str / 20,
//       and a plain creature's strength is never set (StatSystem.cpp:1034-1037).
//   Spell::TakePower                  Spell.cpp:5473-5531 - the avoided-cast refund
//       is rage / energy / rune / runic power only.
//   Spell::HandleThreatSpells         Spell.cpp:5759-5811 - flat + AP row or
//       SpellLevel, split over targets, 0 to a missed one, forwarded for a
//       positive spell.
//   SpellMgr::GetSpellThreatEntry     SpellMgr.cpp:963-976 - first-rank fallback.
//   ThreatManager::CalculateModifiedThreat ThreatManager.cpp:708-716 - pctMod.
//   ThreatManager::ForwardThreatForAssistingMe ThreatManager.cpp:764-790.
//   Spell::EffectHeal / periodic heal Spell.cpp:2862-2866, SpellAuraEffects.cpp:6676-6680.
//   spell_ranks.sql:2903-2906, spell_shaman.cpp:106-107 / :1439, trainer_spell.sql.
//
// Every census number is measured by running the shipped importer over the
// player's own DBC set; every runtime number comes from the shipped
// LocalGameplay tick driving real imported definitions against a real catalog
// creature; the LAN group drives the shipped LocalRealm host through its own
// datagram handlers.
#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include "local_group_rewards_fixture.hpp"
#include "game/local_spell_critical.hpp"
#include "game/local_spell_threat.hpp"
#include "game/local_spell_ranks.hpp"
#include "game/local_trainer_spells.hpp"
#include "game/local_threat_talents.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_world_catalog.hpp"
#include "game/local_services.hpp"
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
#include <sstream>
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
template <class T> std::string join(const T& ids, size_t limit = 400) {
    std::ostringstream s; size_t n = 0;
    for (auto id : ids) { if (n++) s << ","; if (n > limit) { s << "..."; break; } s << id; }
    return s.str();
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
    uint32_t column(uint32_t spellId, uint32_t col) {
        const auto* spells = get("Spell");
        for (uint32_t row = 0; row < spells->getRecordCount(); ++row)
            if (spells->getUInt32(row, 0) == spellId) return spells->getUInt32(row, col);
        return 0;
    }
};
LocalSpellImport gImported;
ClientTables* gTables = nullptr;

const LocalSpellDefinition& real(uint32_t id) {
    for (const auto& d : gImported.spells) if (d.id == id) return d;
    std::cerr << "FAIL: spell " << id << " is absent from the client import\n";
    std::abort();
}
bool accepted(const LocalSpellDefinition& d) { return d.clientSpell && d.unsupportedReason.empty(); }
bool castable(const LocalSpellDefinition& d) { return accepted(d) && !d.passive && !d.triggeredOnly && !d.npcOnly; }
std::set<uint32_t> auditedAccepted() {
    std::set<uint32_t> ids;
    for (const auto& r : gImported.audit) if (r.status == "Supported decoder; imported") ids.insert(r.id);
    return ids;
}

// --- the shipped catalog ----------------------------------------------------
std::map<uint32_t, LocalNpcDefinition> gNpcs;
std::vector<uint32_t> gHostile;
void loadCatalog(const fs::path& directory) {
    static LocalWorldCatalog catalog;
    std::string error;
    if (!catalog.load(directory.string(), error)) { std::cerr << "FAIL: catalog: " << error << "\n"; std::abort(); }
    std::ifstream f(directory / "npcs.pack", std::ios::binary);
    std::vector<unsigned char> pack{std::istreambuf_iterator<char>(f), {}};
    assert(pack.size() > 16 && std::memcmp(pack.data(), "WPCAT01\0", 8) == 0);
    uint32_t count = 0; std::memcpy(&count, pack.data() + 8, 4);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key = 0; std::memcpy(&key, pack.data() + 16 + 16 * size_t(i), 4);
        LocalNpcDefinition d;
        if (!catalog.npc(key, d, error)) { std::cerr << "FAIL: npc " << key << ": " << error << "\n"; std::abort(); }
        if (d.hostile) gHostile.push_back(key);
        gNpcs.emplace(key, std::move(d));
    }
}
const LocalNpcDefinition& creature(uint32_t entry) {
    const auto it = gNpcs.find(entry);
    if (it == gNpcs.end()) { std::cerr << "FAIL: creature " << entry << " is not in the catalog\n"; std::abort(); }
    return it->second;
}
// A plain hostile creature of the level: no immunity set, no resistance row,
// and - because parry needs it - a humanoid (type 7) without the no-parry /
// no-block / no-dodge extra flags and not a world boss (5.85 % dodge, 13.4 %
// parry), so every avoidance arm of the roll is live at its base 5 %. The
// rank is probed through the roll itself: at even level with no rating a
// roll of 999 is the last dodge and 1000 the first parry only when both are
// exactly 5 %.
uint32_t plainHumanoid(uint8_t level) {
    for (auto e : gHostile) {
        const auto& d = creature(e);
        if (!(d.level == level && !d.immuneSchoolMask && !d.immuneMechanicsMask && d.resistances == std::array<uint16_t, 6>{} &&
              localNpcCreatureType(e) == 7 && !(localNpcMeleeFlags(e) & (4u | 16u | 0x800000u)))) continue;
        LocalRealmNpc n = rewardNpc(); n.entry = e; n.level = level; n.x = 3; n.y = 0; n.orientation = float(M_PI);
        LocalRealmPlayer p = rewardPlayer(1); p.level = level; p.x = p.y = 0;
        LocalMeleeStats s{};
        if (localRollPlayerMelee(p, n, s, true, 999, 9999) == LocalMeleeOutcome::Dodge && localRollPlayerMelee(p, n, s, true, 1000, 9999) == LocalMeleeOutcome::Parry &&
            localRollPlayerMelee(p, n, s, true, 1500, 9999) == LocalMeleeOutcome::Hit) return e;
    }
    std::cerr << "FAIL: no plain hostile humanoid at level " << unsigned(level) << "\n"; std::abort();
}

// --- runtime scaffolding ----------------------------------------------------
struct Caster { uint8_t classId; LocalResourceType resource; uint8_t level; };
struct World {
    std::shared_ptr<LocalWorldContent> content;
    LocalGameplay game;
    std::vector<LocalRealmPlayer> casters;
    std::vector<LocalRealmPlayer*> players;
    std::vector<uint64_t> npcGuids;
    uint64_t npcGuid = 0;
};
const LocalRealmNpc* findNpc(const LocalGameplay& game, uint64_t guid) {
    for (const auto& n : game.npcs()) if (n.guid == guid) return &n;
    return nullptr;
}
LocalItemDefinition item(uint32_t id, const char* name, uint8_t inventoryType) {
    LocalItemDefinition it; it.id = id; it.name = name; it.inventoryType = inventoryType; it.stack = 1; return it;
}
void buildWorld(World& w, std::vector<uint32_t> spells, std::vector<Caster> casters, LocalNpcDefinition target,
                float npcX = 3, unsigned creatures = 1) {
    w.content = rewardContent();
    for (auto id : spells) w.content->spells.push_back(real(id));
    std::sort(w.content->spells.begin(), w.content->spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    target.health = 100000000; target.damage = 0; target.aggroRadius = 0.01f; target.xp = 0; target.money = 0; target.loot.clear();
    w.content->npcs.push_back(target);
    std::sort(w.content->npcs.begin(), w.content->npcs.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    // A shield (class 4 / subclass 6 in the melee and auction tables), a dagger
    // and a thrown weapon, so Shield Slam's equipment gate, the block-value
    // term, the weapon-speed reads and Heroic Throw's thrown gate read real items.
    w.content->items.push_back(item(2443, "Worn Large Shield", 14));
    w.content->items.push_back(item(2092, "Worn Dagger", 13));
    w.content->items.push_back(item(5856, "Wicked Throwing Dagger", 25));
    std::sort(w.content->items.begin(), w.content->items.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    w.game.useContent(w.content);
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
    std::vector<LocalRealmNpc> npcs;
    for (unsigned i = 0; i < creatures; ++i) {
        auto n = rewardNpc(10 + i);
        n.entry = target.id; n.name = target.name; n.level = target.level;
        n.health = n.maxHealth = target.health; n.hostile = true; n.lootOwner = 0;
        n.x = n.homeX = npcX + float(i) * 0.5f; n.y = n.homeY = 0; n.z = n.homeZ = 0;
        n.targetGuid = w.casters[0].guid; n.threat[0] = {w.casters[0].guid, 1000};
        npcs.push_back(n);
        w.npcGuids.push_back(n.guid);
    }
    w.game.setRemoteNpcs(npcs);
    w.npcGuid = w.npcGuids[0];
    w.game.tick(.25f, w.players);
}
uint64_t lastSequence(const LocalGameplay& game) {
    uint64_t s = 0;
    for (const auto& e : game.combatEvents()) s = std::max(s, e.sequence);
    return s;
}
std::optional<LocalCombatEvent> lastEvent(const LocalGameplay& game, uint64_t target, uint32_t spellId, uint64_t after, bool heal = false) {
    std::optional<LocalCombatEvent> found;
    for (const auto& e : game.combatEvents())
        if (e.sequence > after && e.target == target && e.spell == spellId &&
            (heal ? e.kind == LocalCombatEventKind::DirectHeal
                  : (e.kind == LocalCombatEventKind::SpellDamage || e.kind == LocalCombatEventKind::PeriodicDamage))) found = e;
    return found;
}
// Every damage event this suite drives must satisfy the conservation
// identity. The authority keeps a bounded history, so each observation is
// checked as its cast returns it rather than from the window at the end.
uint64_t gEventsChecked = 0, gBlockedEvents = 0;
void checkConservation(const LocalCombatEvent& e) {
    if (e.kind != LocalCombatEventKind::SpellDamage && e.kind != LocalCombatEventKind::PeriodicDamage &&
        e.kind != LocalCombatEventKind::PlayerMelee && e.kind != LocalCombatEventKind::ProcDamage &&
        e.kind != LocalCombatEventKind::PlayerRanged) return;
    ++gEventsChecked;
    if (e.blocked) ++gBlockedEvents;
    const uint64_t accounted = uint64_t(e.resisted) + e.effective + e.absorbed + e.blocked;
    if (e.killed) assert(accounted <= e.attempted);
    else assert(accounted == e.attempted);
    if (localOutcomeNullifiesDamage(e.outcome)) assert(!e.effective && !e.blocked);
}
void checkConservation(const LocalGameplay& game) { for (const auto& e : game.combatEvents()) checkConservation(e); }
struct CastResult { bool executed = false; std::string result; std::optional<LocalCombatEvent> event; uint32_t paid = 0; };
CastResult castOnce(World& w, LocalRealmPlayer& p, uint32_t spellId, uint64_t target = 0, bool heal = false) {
    p.globalCooldownMs = 0; p.cooldowns.clear(); p.categoryCooldowns.clear();
    p.mana = p.maxMana; p.comboPoints = 0; p.comboTarget = 0;
    // An avoided or missed cast leaves the caster auto-attacking its target
    // (attackTarget) and the tick that resolves the cast would swing at once;
    // the next cast is the same cast, not a cast plus swings.
    p.attackTarget = 0; p.attackTimer = p.offHandTimer = 1000.f;
    if (!target) target = w.npcGuid;
    const auto before = lastSequence(w.game);
    CastResult r;
    r.executed = w.game.execute(p, {LocalAction::CastSpell, target, spellId}, w.players, r.result);
    if (!r.executed) return r;
    for (unsigned t = 0; t < 120 && p.castStatus == LocalCastStatus::Casting; ++t) w.game.tick(.05f, w.players);
    assert(p.castStatus != LocalCastStatus::Casting);
    r.paid = p.maxMana - p.mana;
    r.event = lastEvent(w.game, target, spellId, before, heal);
    if (r.event) checkConservation(*r.event);
    return r;
}
uint64_t threatOf(const World& w, uint64_t npcGuid, uint64_t player) {
    const auto* n = findNpc(w.game, npcGuid); assert(n);
    for (const auto& e : n->threat) if (e.guid == player) return e.amount;
    return 0;
}

// The audit's population: hostile-targeted DmgClass 2 with no profile.
bool unprofiledMeleeClass(const LocalSpellDefinition& d) { return castable(d) && localMeleeClassSpellRoll(d); }
struct Tally { unsigned hit = 0, crit = 0, miss = 0, dodge = 0, parry = 0, block = 0, other = 0, partialBlocks = 0, blockedCriticals = 0; double hitAmt = 0, critAmt = 0; std::vector<uint32_t> blockedAmounts; };
Tally drive(World& w, LocalRealmPlayer& p, uint32_t spellId, unsigned n) {
    Tally t;
    for (unsigned i = 0; i < n; ++i) {
        auto r = castOnce(w, p, spellId);
        if (!r.executed) { std::cerr << "FAIL: cast " << spellId << " rejected: " << r.result << "\n"; std::abort(); }
        if (!r.event) { ++t.other; continue; }
        if (r.event->blocked) { ++t.partialBlocks; t.blockedAmounts.push_back(r.event->blocked); if (r.event->outcome == LocalMeleeOutcome::Critical) ++t.blockedCriticals; }
        switch (r.event->outcome) {
            case LocalMeleeOutcome::Hit: ++t.hit; t.hitAmt += r.event->attempted; break;
            case LocalMeleeOutcome::Critical: ++t.crit; t.critAmt += r.event->attempted; break;
            case LocalMeleeOutcome::Miss: ++t.miss; break;
            case LocalMeleeOutcome::Dodge: ++t.dodge; break;
            case LocalMeleeOutcome::Parry: ++t.parry; break;
            case LocalMeleeOutcome::Block: ++t.block; break;
            default: ++t.other; break;
        }
    }
    return t;
}
std::string describe(const Tally& t) {
    std::ostringstream s;
    s << "hit=" << t.hit << " crit=" << t.crit << " miss=" << t.miss << " dodge=" << t.dodge << " parry=" << t.parry
      << " block=" << t.block << " partial-blocks=" << t.partialBlocks << " meanHit=" << (t.hit ? t.hitAmt / t.hit : 0)
      << " meanCrit=" << (t.crit ? t.critAmt / t.crit : 0) << " ratio=" << ((t.hit && t.crit) ? (t.critAmt / t.crit) / (t.hitAmt / t.hit) : 0);
    return s.str();
}
// The band a binomial count lies in with near certainty: expectation +- four
// standard deviations, computed from the roll's own inputs.
struct Band { double expected; unsigned low, high; };
Band band(unsigned n, float pct) {
    const double p = std::clamp(double(pct), 0.0, 100.0) / 100.0, mean = n * p, sd = std::sqrt(n * p * (1 - p));
    return {mean, unsigned(std::max(0.0, std::floor(mean - 4 * sd))), unsigned(std::ceil(mean + 4 * sd))};
}
bool within(unsigned count, const Band& b) { return count >= b.low && count <= b.high; }

// ---------------------------------------------------------------------------
// 1. The census: the 39 / 22 / 31 melee-class population, the 87 physical
//    melee-class direct-damage spells, the block predicates, the threat
//    producers and the spell_threat rows, all measured over the shipped importer.
// ---------------------------------------------------------------------------
void census() {
    const auto acceptedIds = auditedAccepted();
    assert(acceptedIds.size() == 1004); // 990 at the implementation + the implementation's 14 form passives
    std::map<std::string, unsigned> byName;
    unsigned unprofiled = 0, direct = 0, periodic = 0, alwaysHit = 0, noActiveDefense = 0, profiled = 0, inert = 0;
    unsigned suppressCount = 0, noInitialCount = 0, notCastable = 0, fallbackRows = 0; std::map<std::string, unsigned> inertNames;
    unsigned physicalMeleeDirect = 0, partialBlockEligible = 0, fullyBlockable = 0, completelyBlocked = 0;
    unsigned spellLevelThreat = 0, spellLevelHeals = 0, tableDirect = 0, tableViaFirstRank = 0, noThreat = 0, noThreatDamaging = 0;
    unsigned noInitialThreat = 0, suppress = 0, paladinHeals = 0;
    std::map<std::string, unsigned> tableNames;
    for (const auto& d : gImported.spells) {
        if (!acceptedIds.count(d.id)) continue;
        if (d.clientSpell && d.sourceDamageClass == 2 && (d.comboProfile || d.meleeSpecialProfile || d.stormstrikeProfile == 1) && !d.passive) ++profiled;
        if (unprofiledMeleeClass(d)) {
            ++unprofiled; ++byName[d.name];
            if (d.damage) ++direct;
            if (d.periodicDamage) ++periodic;
            if (d.sourceAlwaysHit) ++alwaysHit;
            if (d.sourceNoActiveDefense) ++noActiveDefense;
        } else if (castable(d) && d.clientSpell && d.sourceDamageClass == 2 && !d.comboProfile && !d.meleeSpecialProfile && !d.stormstrikeProfile &&
                   !d.formId && !d.heal && !d.periodicHeal && !d.damage && !d.periodicDamage && !d.snarePercent && !d.controlProfile) {
            ++inert; ++inertNames[d.name];
        }
        // The SpellLevel producer bookkeeping the audit did not have.
        if (castable(d) && !d.sourceNoThreat && d.spellLevel && !localSpellThreatRow(d.id, 0)) {
            if (localSpellThreatRow(d)) ++fallbackRows;
            else if (d.sourceSuppressTargetProcs) ++suppressCount;
            else if (d.sourceNoInitialThreat) ++noInitialCount;
        }
        if (accepted(d) && !castable(d) && !d.passive && !d.sourceNoThreat && d.spellLevel && !localSpellThreatRow(d)) ++notCastable;
        if (castable(d) && d.sourceDamageClass == 2 && (d.schoolMask & 1) && d.damage) {
            ++physicalMeleeDirect;
            if (localSpellPartialBlockApplies(d)) ++partialBlockEligible;
        }
        if (castable(d) && d.sourceCompletelyBlocked) { ++completelyBlocked; if (localSpellFullyBlockable(d)) ++fullyBlockable; }
        if (castable(d) && !d.sourceNoThreat && !d.sourceSuppressTargetProcs && !localSpellThreatRow(d) && !d.sourceNoInitialThreat && d.spellLevel) {
            ++spellLevelThreat;
            if (d.heal || d.periodicHeal) ++spellLevelHeals;
        }
        if (castable(d) && localSpellThreatRow(d)) {
            if (localSpellThreatRow(d.id, 0)) ++tableDirect; else ++tableViaFirstRank;
            ++tableNames[d.name];
        }
        if (d.sourceNoThreat) { ++noThreat; if (castable(d) && (d.damage || d.periodicDamage)) ++noThreatDamaging; }
        if (castable(d) && d.sourceNoInitialThreat) ++noInitialThreat;
        if (castable(d) && d.sourceSuppressTargetProcs) ++suppress;
        if (castable(d) && (d.heal || d.periodicHeal) && d.allowableClasses == 2) ++paladinHeals;
    }
    std::string names;
    for (const auto& [name, count] : byName) names += (names.empty() ? "" : ", ") + name + " x" + std::to_string(count);
    std::string tableList;
    for (const auto& [name, count] : tableNames) tableList += (tableList.empty() ? "" : ", ") + name + " x" + std::to_string(count);
    // The audit's population of 39: 37 gain the roll - 22 with direct damage,
    // 13 with a periodic amount (Rend x10, Lacerate x3), 2 ALWAYS_HIT (the
    // Judgements), 5 NO_ACTIVE_DEFENSE (Heroic Throw, both Saps and both
    // Judgements, which the audit's "3" left out because ALWAYS_HIT precedes
    // it) - and 2 are inert here: Holy Vengeance 31803 and Blood Corruption
    // 53742 carry a periodic-damage effect with 0 base points (the amount is
    // Seal of Vengeance's script), so this build applies nothing for them and
    // they never reach the hostile target gate, let alone a roll.
    std::string inertList;
    for (const auto& [name, count] : inertNames) inertList += (inertList.empty() ? "" : ", ") + name + " x" + std::to_string(count);
    const bool population = expect(unprofiled == 37 && inert == 2 && direct == 22 && periodic == 13 && alwaysHit == 2 && noActiveDefense == 5 && profiled == 79,
        "melee-class population: " + std::to_string(unprofiled) + " unprofiled (" + std::to_string(direct) + " direct, " + std::to_string(periodic) +
        " periodic, " + std::to_string(alwaysHit) + " ALWAYS_HIT, " + std::to_string(noActiveDefense) + " NO_ACTIVE_DEFENSE) + " + std::to_string(inert) +
        " inert, " + std::to_string(profiled) + " profiled, against 37 + 2 / 22 / 13 / 2 / 5 / 79: " + names + "; inert: " + inertList);
    // 87 physical melee-class direct-damage spells; Heroic Throw's
    // NO_ACTIVE_DEFENSE exits isSpellBlocked, so 86 roll the partial block
    // (the audit's 86 counted "all but Bloodthirst"; the pinned source's
    // exception is Heroic Throw and Bloodthirst is in).
    const bool blocks = expect(physicalMeleeDirect == 87 && partialBlockEligible == 86 && completelyBlocked == 5 && fullyBlockable == 0,
        "partial block: " + std::to_string(physicalMeleeDirect) + " physical melee-class direct-damage spells, " + std::to_string(partialBlockEligible) +
        " roll isSpellBlocked, " + std::to_string(completelyBlocked) + " COMPLETELY_BLOCKED of which " + std::to_string(fullyBlockable) +
        " can be fully blocked, against 87 / 86 / 5 / 0");
    for (auto rake : {1822u, 9904u, 27003u, 48573u, 48574u}) {
        const auto& d = real(rake);
        assert(d.sourceCompletelyBlocked && d.sourceDirectDamage && !localSpellFullyBlockable(d) && d.comboProfile);
    }
    // Threat: the audit counted 605 non-passive accepted spells with a
    // SpellLevel, no NO_THREAT and no spell_threat row of their own. The pinned
    // HandleThreatSpells also skips SUPPRESS_TARGET_PROCS and the computed
    // CU_NO_INITIAL_THREAT, GetSpellThreatEntry reaches 21 more rows through
    // the first rank, and a triggeredOnly row is never cast here; the producer
    // count over the castable set is measured and pinned below, with the
    // accounting back to 605 printed. Heals count direct and periodic ones.
    const bool threat = expect(spellLevelThreat == 579 && spellLevelHeals == 146 && tableDirect == 19 && tableViaFirstRank == 21 && paladinHeals == 31 &&
                               spellLevelThreat + fallbackRows + suppressCount + noInitialCount + notCastable == 605,
        "threat producers: " + std::to_string(spellLevelThreat) + " SpellLevel (" + std::to_string(spellLevelHeals) + " heals), " +
        std::to_string(tableDirect) + " direct spell_threat rows + " + std::to_string(tableViaFirstRank) + " through the first rank, " +
        std::to_string(paladinHeals) + " paladin heals, " + std::to_string(fallbackRows) + " fallback rows + " + std::to_string(suppressCount) +
        " SUPPRESS_TARGET_PROCS + " + std::to_string(noInitialCount) + " CU_NO_INITIAL_THREAT + " + std::to_string(notCastable) +
        " not castable, against 579 / 146 / 19 / 21 / 31 and 605 in total: " + tableList);
    // NO_THREAT: 31 accepted carried it at the implementation; 32 since the implementation admitted Cat
    // Form (Passive) 3025, whose AttributesEx is exactly SPELL_ATTR1_NO_THREAT.
    // It is a passive the server casts, so the castable-and-damaging count the
    // retirement drove to zero is untouched.
    const bool noThreatOk = expect(noThreat == 32 && noThreatDamaging == 0,
        "NO_THREAT: " + std::to_string(noThreat) + " accepted, " + std::to_string(noThreatDamaging) + " castable and damaging, against 32 / 0");
    // The table itself: 106 rows, the 19 the audit named with their values.
    constexpr size_t rows = sizeof(kLocalSpellThreat) / sizeof(kLocalSpellThreat[0]);
    assert(rows == 106);
    for (size_t i = 1; i < rows; ++i) assert(kLocalSpellThreat[i - 1].entry < kLocalSpellThreat[i].entry);
    const std::map<uint32_t, std::tuple<int32_t, float, float>> named{
        {23922, {228, 1.f, 0.f}}, {23923, {268, 1.f, 0.f}}, {23924, {307, 1.f, 0.f}}, {23925, {347, 1.f, 0.f}}, {25258, {387, 1.f, 0.f}},
        {30356, {426, 1.f, 0.f}}, {47487, {650, 1.f, 0.f}}, {47488, {770, 1.f, 0.f}}, {5211, {53, 1.f, 0.f}}, {6798, {105, 1.f, 0.f}},
        {8983, {158, 1.f, 0.f}}, {33745, {182, .5f, 0.f}}, {48567, {409, .5f, 0.f}}, {48568, {515, .5f, 0.f}}, {5676, {0, 2.f, 0.f}},
        {8056, {0, 2.f, 0.f}}, {7294, {0, 2.f, 0.f}}, {57755, {0, 1.5f, 0.f}}, {34299, {0, 0.f, 0.f}}};
    for (const auto& [id, values] : named) {
        const auto* row = localSpellThreatRow(id, 0); assert(row);
        assert(row->flatMod == std::get<0>(values) && row->pctMod == std::get<1>(values) && row->apPctMod == std::get<2>(values));
    }
    // The fallback: Frost Shock r2 (8058) and Searing Pain r2 (17919) have no
    // row of their own and read rank 1's.
    assert(!localSpellThreatRow(8058, 0) && localSpellThreatRow(real(8058)) == localSpellThreatRow(8056, 0));
    assert(!localSpellThreatRow(17919, 0) && localSpellThreatRow(real(17919)) == localSpellThreatRow(5676, 0));
    // The raw columns are the DBC's: SpellLevel of Frostbolt r1 and Holy Light r1.
    assert(real(116).spellLevel == gTables->column(116, 39) && real(635).spellLevel == gTables->column(635, 39));
    assert(real(31804).sourceAlwaysHit && real(53733).sourceAlwaysHit && real(57755).sourceNoActiveDefense && real(11297).sourceNoActiveDefense && real(51724).sourceNoActiveDefense);
    assert(real(23922).sourceDirectDamage && real(635).sourceDirectDamage && !real(772).sourceDirectDamage);
    if (population && blocks && threat && noThreatOk)
        std::cout << "PASS census: " << unprofiled << " hostile-targeted DmgClass 2 spells with no profile gain the melee-class roll (" << direct
                  << " with direct damage gain the 2x critical, " << periodic << " carry a periodic amount, " << alwaysHit << " are ALWAYS_HIT, "
                  << noActiveDefense << " NO_ACTIVE_DEFENSE): " << names << "; the audit's other " << inert << " (" << inertList
                  << ") carry a 0-base-point periodic effect whose amount is a seal script and apply nothing here, so they have nothing to roll for; "
                  << profiled << " profiled melee-class spells already rolled; "
                  << physicalMeleeDirect << " physical melee-class direct-damage spells of which " << partialBlockEligible
                  << " roll the partial block (Heroic Throw's NO_ACTIVE_DEFENSE exits isSpellBlocked); " << completelyBlocked
                  << " COMPLETELY_BLOCKED rows (Rake) and " << fullyBlockable << " fully blockable, because every one carries a school damage effect "
                     "(SPELL_ATTR0_CU_DIRECT_DAMAGE, Unit.cpp:3351); " << spellLevelThreat << " castable spells owe SpellLevel threat (" << spellLevelHeals
                  << " direct or periodic heals; the audit's 605 = these + " << fallbackRows << " first-rank spell_threat rows + " << suppressCount
                  << " SUPPRESS_TARGET_PROCS + " << noInitialCount << " CU_NO_INITIAL_THREAT + " << notCastable << " accepted but not castable), " << tableDirect << " read their own spell_threat row and " << tableViaFirstRank
                  << " their first rank's (SpellMgr.cpp:963-976): " << tableList << "; " << noThreat << " accepted NO_THREAT rows, " << noThreatDamaging
                  << " of them castable and damaging; " << noInitialThreat << " castable CU_NO_INITIAL_THREAT, " << suppress << " SUPPRESS_TARGET_PROCS; "
                  << paladinHeals << " paladin heals\n";
}

// ---------------------------------------------------------------------------
// 2. The melee-class roll, measured: 2,000 Shield Slams at even level land in
//    the bands the roll's own inputs predict, crit at 2x; Fireball still 1.5x;
//    the Judgements are never avoided; Sap misses about 5 % and is never dodged
//    or parried; Heroic Throw likewise; Rake keeps its combo roll and cannot be
//    fully blocked.
// ---------------------------------------------------------------------------
void shieldSlamAndFriends() {
    const auto& target = creature(plainHumanoid(80));
    World w; buildWorld(w, {23922, 133, 31804, 11297, 57755, 1495}, {{1, LocalResourceType::Rage, 80}, {8, LocalResourceType::Mana, 80}, {2, LocalResourceType::Mana, 80},
                                                                    {4, LocalResourceType::Energy, 80}, {3, LocalResourceType::Mana, 80}}, target, 3);
    auto& warrior = w.casters[0]; auto& mage = w.casters[1]; auto& paladin = w.casters[2]; auto& rogue = w.casters[3]; auto& hunter = w.casters[4];
    for (auto* p : {&warrior, &paladin, &rogue, &hunter}) { p->equipment[15] = 2092; p->inventory.push_back({2092, 1}); }
    warrior.equipment[16] = 2443; warrior.inventory.push_back({2443, 1});
    warrior.equipment[17] = 5856; warrior.inventory.push_back({5856, 1});
    // The bands from the roll's own inputs (local_melee.cpp, localRollPlayerMelee
    // with special = true): miss = clamp(5 + diff x 0.1 - hit, 0, 60) with
    // diff = (creature level - caster level) x 5 = 0; dodge = 5 + diff x 0.04 -
    // expertise; parry = 5 + diff x 0.04 - expertise for a humanoid faced from
    // the front; each drawn in sequence from one roll, so the later arms are
    // conditioned on the earlier ones missing.
    const auto stats = localMeleeStats(warrior, *w.content);
    const float missPct = std::clamp(5.f - stats.hit, 0.f, 60.f);
    const float dodgePct = std::max(0.f, 5.f - stats.expertise), parryPct = std::max(0.f, 5.f - stats.expertise);
    constexpr unsigned N = 2000;
    const auto missBand = band(N, missPct);
    const auto dodgeBand = band(N, dodgePct * (100 - missPct) / 100);
    const auto parryBand = band(N, parryPct * (100 - missPct) / 100 * (100 - dodgePct) / 100);
    const auto critPct = std::max(0.f, stats.crit);
    const auto slam = drive(w, warrior, 23922, N);
    const bool slamBands = expect(within(slam.miss, missBand) && within(slam.dodge, dodgeBand) && within(slam.parry, parryBand) && slam.block == 0 && slam.other == 0,
        "Shield Slam avoidance " + describe(slam) + " outside miss [" + std::to_string(missBand.low) + "," + std::to_string(missBand.high) + "] dodge [" +
        std::to_string(dodgeBand.low) + "," + std::to_string(dodgeBand.high) + "] parry [" + std::to_string(parryBand.low) + "," + std::to_string(parryBand.high) + "]");
    const double ratio = (slam.hit && slam.crit) ? (slam.critAmt / slam.crit) / (slam.hitAmt / slam.hit) : 0;
    const bool slamCrit = expect(slam.crit > 0 && ratio >= 1.95 && ratio <= 2.05 && within(slam.crit, band(N - slam.miss - slam.dodge - slam.parry, critPct)),
        "Shield Slam critical " + describe(slam) + " (crit chance " + std::to_string(critPct) + " %)");
    // The partial block (P05-2): about 5 % of the landed casts, each blocked
    // by exactly the creature's block value, level / 2 = 40, and a critical
    // can be blocked too. From behind the block chance is nil.
    unsigned blockedExact = 0; const unsigned blockedCritical = slam.blockedCriticals;
    for (auto b : slam.blockedAmounts) if (b == 40) ++blockedExact;
    const auto blockBand = band(slam.hit + slam.crit, 5.f);
    const bool slamBlock = expect(slam.partialBlocks > 0 && within(slam.partialBlocks, blockBand) && blockedExact == slam.partialBlocks,
        "Shield Slam partial block: " + std::to_string(slam.partialBlocks) + " of " + std::to_string(slam.hit + slam.crit) + " landed casts, " +
        std::to_string(blockedExact) + " by exactly 40, " + std::to_string(blockedCritical) + " on a critical, band [" + std::to_string(blockBand.low) + "," + std::to_string(blockBand.high) + "]");
    {
        // isSpellBlocked's front arc: a creature turned away never blocks.
        auto n = *findNpc(w.game, w.npcGuid);
        unsigned front = 0, behind = 0;
        n.orientation = float(M_PI); // faces -x, the warrior at x ~ 0 is in front
        for (uint32_t roll = 0; roll < 10000; ++roll) if (localRollMeleeSpecialBlock(warrior, n, roll)) ++front;
        n.orientation = 0.f;         // faces +x, the warrior is behind
        for (uint32_t roll = 0; roll < 10000; ++roll) if (localRollMeleeSpecialBlock(warrior, n, roll)) ++behind;
        assert(front == 500 && behind == 0);
        assert(localCreatureBlockValue(n) == 40);
    }
    // A dodged Shield Slam refunds to irand(0, cost / 4) (rage, Spell.cpp:5528-5531).
    {
        unsigned refunded = 0, full = 0; const auto cost = localSpellBaseResourceCost(warrior, *w.content, real(23922));
        for (unsigned i = 0; i < 400; ++i) {
            auto r = castOnce(w, warrior, 23922); assert(r.executed && r.event);
            if (localMeleeAvoided(r.event->outcome)) { assert(r.paid <= cost / 4); ++refunded; } else { assert(r.paid == cost); ++full; }
        }
        assert(refunded > 0 && full > 0);
    }
    // Fireball: the magic roll and the 1.5x critical are untouched.
    const auto fireball = drive(w, mage, 133, N);
    const double fireRatio = (fireball.hit && fireball.crit) ? (fireball.critAmt / fireball.crit) / (fireball.hitAmt / fireball.hit) : 0;
    const bool fire = expect(fireball.crit > 0 && fireRatio >= 1.49 && fireRatio <= 1.51 && fireball.dodge == 0 && fireball.parry == 0 && fireball.partialBlocks == 0,
        "Fireball " + describe(fireball));
    // Judgement of Vengeance: ALWAYS_HIT - 0 avoided of 2,000, and the melee
    // critical still rolls (holy school, DmgClass 2: the 2x branch).
    const auto judgement = drive(w, paladin, 31804, N);
    const double judgeRatio = (judgement.hit && judgement.crit) ? (judgement.critAmt / judgement.crit) / (judgement.hitAmt / judgement.hit) : 0;
    const bool judge = expect(judgement.miss == 0 && judgement.dodge == 0 && judgement.parry == 0 && judgement.block == 0 && judgement.partialBlocks == 0 &&
                              judgement.crit > 0 && judgeRatio >= 1.95 && judgeRatio <= 2.05 && judgement.other == 0,
        "Judgement of Vengeance " + describe(judgement));
    // A missed Judgement is impossible; a missed Mongoose Bite (mana) pays in full.
    {
        unsigned avoided = 0; const auto cost = localSpellBaseResourceCost(hunter, *w.content, real(1495));
        for (unsigned i = 0; i < 400; ++i) {
            auto r = castOnce(w, hunter, 1495); assert(r.executed && r.event);
            if (localMeleeAvoided(r.event->outcome)) ++avoided;
            assert(r.paid == cost);
        }
        assert(avoided > 0 && cost > 0);
    }
    // Sap: NO_ACTIVE_DEFENSE - a miss roll only. Sap requires Stealth
    // (requiredForms), which is not a form this realm can enter, so the cast
    // is refused at the form gate before any roll ("This spell cannot be used
    // in the current form or stance") - the rule is measured on the roll the
    // cast site would make, over every roll value, with Sap's own rules: 5 %
    // misses at even level, no dodge, no parry, no block, then the critical.
    {
        std::string result;
        assert(!w.game.execute(rogue, {LocalAction::CastSpell, w.npcGuid, 11297}, w.players, result) && result == "This spell cannot be used in the current form or stance");
    }
    unsigned sapMiss = 0, sapLanded = 0, sapOther = 0;
    {
        const auto n = *findNpc(w.game, w.npcGuid);
        const auto sapStats = localMeleeStats(rogue, *w.content);
        const LocalMeleeSpellRules sapRules{real(11297).sourceAlwaysHit, real(11297).sourceNoActiveDefense, localSpellFullyBlockable(real(11297))};
        assert(!sapRules.alwaysHit && sapRules.noActiveDefense && !sapRules.fullBlock);
        for (uint32_t roll = 0; roll < 10000; ++roll) {
            const auto outcome = localRollPlayerMelee(rogue, n, sapStats, true, roll, 9999, false, sapRules);
            if (outcome == LocalMeleeOutcome::Miss) ++sapMiss; else if (outcome == LocalMeleeOutcome::Hit) ++sapLanded; else ++sapOther;
        }
        // Without the NO_ACTIVE_DEFENSE rule the same rolls dodge and parry.
        unsigned plainAvoided = 0;
        for (uint32_t roll = 0; roll < 10000; ++roll) {
            const auto outcome = localRollPlayerMelee(rogue, n, sapStats, true, roll, 9999);
            if (outcome == LocalMeleeOutcome::Dodge || outcome == LocalMeleeOutcome::Parry) ++plainAvoided;
        }
        assert(plainAvoided == 1000);
    }
    const bool sap = expect(sapMiss == 500 && sapLanded == 9500 && sapOther == 0,
        "Sap roll: " + std::to_string(sapMiss) + " misses, " + std::to_string(sapLanded) + " landed, " + std::to_string(sapOther) + " other of 10,000 rolls");
    // Heroic Throw: NO_ACTIVE_DEFENSE - misses, never dodged / parried / blocked, crits at 2x.
    const auto throwT = drive(w, warrior, 57755, N);
    const auto throwStats = localMeleeStats(warrior, *w.content);
    const auto throwBand = band(N, std::clamp(5.f - throwStats.hit, 0.f, 60.f));
    const double throwRatio = (throwT.hit && throwT.crit) ? (throwT.critAmt / throwT.crit) / (throwT.hitAmt / throwT.hit) : 0;
    const bool heroic = expect(within(throwT.miss, throwBand) && throwT.dodge == 0 && throwT.parry == 0 && throwT.block == 0 && throwT.partialBlocks == 0 &&
                               throwT.crit > 0 && throwRatio >= 1.95 && throwRatio <= 2.05,
        "Heroic Throw " + describe(throwT) + " band [" + std::to_string(throwBand.low) + "," + std::to_string(throwBand.high) + "]");
    if (slamBands && slamCrit && slamBlock && fire && judge && sap && heroic)
        std::cout << "PASS melee-class roll: 2,000 Shield Slams by a level-80 warrior at " << target.name << " (level 80, humanoid, no set): "
                  << describe(slam) << " - miss in [" << missBand.low << "," << missBand.high << "] (" << missPct << " %), dodge in [" << dodgeBand.low << ","
                  << dodgeBand.high << "], parry in [" << parryBand.low << "," << parryBand.high << "] from the roll's own inputs, critical/hit mean ratio "
                  << ratio << " in [1.95, 2.05] (" << critPct << " % chance); " << slam.partialBlocks << " partial blocks of exactly 40 = level / 2 ("
                  << blockedCritical << " on a critical, 0 from behind, 5 % from the front); a dodged Shield Slam refunds rage, a missed Mongoose Bite pays full mana; "
                     "Fireball " << describe(fireball) << " ratio " << fireRatio << "; Judgement of Vengeance (ALWAYS_HIT) " << describe(judgement) << " ratio "
                  << judgeRatio << "; Sap (NO_ACTIVE_DEFENSE; the cast itself is refused at the Stealth form gate) " << sapMiss << " misses of 10,000 rolls, "
                  << sapLanded << " landed, 0 dodged or parried where the plain roll avoids 1,000; "
                     "Heroic Throw (NO_ACTIVE_DEFENSE) " << describe(throwT) << " ratio " << throwRatio << "\n";
}

// ---------------------------------------------------------------------------
// 3. The periodic multiplier by class (shape only - no reachable grant) and the
//    critical predicates.
// ---------------------------------------------------------------------------
void criticalShape() {
    assert(localMagicCriticalAmount(100) == 150 && localMeleeCriticalAmount(100) == 200 && localMeleeCriticalAmount(1000000) == 1000000);
    assert(localSpellCriticalAmount(&real(772), 100) == 200);   // Rend, DmgClass 2
    assert(localSpellCriticalAmount(&real(133), 100) == 150);   // Fireball, DmgClass 1
    assert(localSpellCriticalAmount(&real(75), 100) == 200);    // Auto Shot, DmgClass 3
    assert(localSpellCriticalAmount(nullptr, 100) == 150);
    assert(!localDirectMagicCritEligible(real(23922)) && localDirectSpellCritEligible(real(23922)));
    assert(localDirectMagicCritEligible(real(133)) && localDirectSpellCritEligible(real(133)));
    assert(!localDirectMagicCritEligible(real(31804)) && localDirectSpellCritEligible(real(31804))); // holy, DmgClass 2
    unsigned meleeDots = 0, meleeDotsWithAmount = 0;
    for (const auto& d : gImported.spells) {
        if (!castable(d) || d.sourceDamageClass != 2) continue;
        bool periodicEffect = false;
        for (unsigned k = 0; k < 3; ++k) if (d.sourceEffect[k] == 6 && d.effectAura[k] == 3) periodicEffect = true;
        if (periodicEffect) ++meleeDots;
        if (d.periodicDamage) ++meleeDotsWithAmount;
    }
    assert(meleeDots == 31 && meleeDotsWithAmount == 29);
    std::cout << "PASS critical multiplier by class: localSpellCriticalAmount doubles a DmgClass 2 or 3 amount and adds half to any other, "
                 "the periodic tick in damageNpc reads it (SpellAuraEffects.cpp:6368 -> SpellCriticalDamageBonus), so the " << meleeDots
              << " accepted melee-class periodic-damage rows (" << meleeDotsWithAmount << " with a non-zero amount) would tick at 2x under a critical grant - none is importable (the reference), so this is shape only; "
                 "the magic-class eligibility keeps its DmgClass 1 test at the caller\n";
}

// ---------------------------------------------------------------------------
// 4. Threat: SpellLevel on a cast and a heal, the table rows, the first-rank
//    fallback, the paladin halving, the assisting split.
// ---------------------------------------------------------------------------
void threat() {
    const auto& target = creature(plainHumanoid(80));
    World w; buildWorld(w, {116, 47488, 5676, 17919, 57755, 8058, 635, 2050, 33745, 5487}, {{8, LocalResourceType::Mana, 80}, {1, LocalResourceType::Rage, 80},
                        {9, LocalResourceType::Mana, 80}, {2, LocalResourceType::Mana, 80}, {5, LocalResourceType::Mana, 80}, {11, LocalResourceType::Mana, 80},
                        {5, LocalResourceType::Mana, 80}}, target, 3, 2);
    auto& mage = w.casters[0]; auto& warrior = w.casters[1]; auto& warlock = w.casters[2]; auto& paladin = w.casters[3]; auto& priest = w.casters[4]; auto& druid = w.casters[5];
    auto& bystander = w.casters[6];
    warrior.equipment[15] = 2092; warrior.inventory.push_back({2092, 1});
    warrior.equipment[16] = 2443; warrior.inventory.push_back({2443, 1});
    warrior.equipment[17] = 5856; warrior.inventory.push_back({5856, 1});
    const auto npc1 = w.npcGuids[0], npc2 = w.npcGuids[1];
    // A landed Frostbolt: damage x 1000 + SpellLevel x 1000 (Spell.cpp:5779-5780);
    // a missed one adds the pre-existing thousandth only.
    const auto& frostbolt = real(116);
    unsigned landed = 0, missed = 0;
    for (unsigned i = 0; i < 60 && (!landed || !missed); ++i) {
        const auto before = threatOf(w, npc1, mage.guid);
        auto r = castOnce(w, mage, 116); assert(r.executed && r.event);
        const auto after = threatOf(w, npc1, mage.guid);
        if (r.event->outcome == LocalMeleeOutcome::Miss) { assert(after == before + 1); ++missed; }
        else { assert(after == before + uint64_t(r.event->effective) * 1000 + uint64_t(frostbolt.spellLevel) * 1000); ++landed; }
        if (r.event->outcome == LocalMeleeOutcome::Miss) { auto n = *findNpc(w.game, npc1); w.game.setRemoteNpcs({n, *findNpc(w.game, npc2)}); }
    }
    assert(landed > 0 && frostbolt.spellLevel > 0);
    // Shield Slam r8: its own row, flat 770 in place of SpellLevel, damage x 1.
    uint64_t slamFlat = 0; unsigned slamLanded = 0;
    for (unsigned i = 0; i < 60 && !slamLanded; ++i) {
        const auto before = threatOf(w, npc1, warrior.guid);
        auto r = castOnce(w, warrior, 47488); assert(r.executed && r.event);
        if (localMeleeAvoided(r.event->outcome)) { assert(threatOf(w, npc1, warrior.guid) == before + 1); continue; }
        slamFlat = threatOf(w, npc1, warrior.guid) - before - uint64_t(r.event->effective) * 1000; ++slamLanded;
    }
    assert(slamLanded && slamFlat == 770000 && real(47488).spellLevel);
    // Heroic Throw: x 1.5 on the damage, flat 0 and therefore no SpellLevel.
    uint64_t throwDelta = 0; uint32_t throwEffective = 0;
    for (unsigned i = 0; i < 60 && !throwEffective; ++i) {
        const auto before = threatOf(w, npc1, warrior.guid);
        auto r = castOnce(w, warrior, 57755); assert(r.executed && r.event);
        if (localMeleeAvoided(r.event->outcome)) continue;
        throwDelta = threatOf(w, npc1, warrior.guid) - before; throwEffective = r.event->effective;
    }
    assert(throwEffective && throwDelta == uint64_t(double(throwEffective) * 1000 * 1.5) && real(57755).spellLevel);
    // Searing Pain r1 and r2 (rank 2 has no row: first-rank fallback): x 2, no SpellLevel.
    for (auto id : {5676u, 17919u}) {
        uint64_t delta = 0; uint32_t effective = 0;
        for (unsigned i = 0; i < 60 && !effective; ++i) {
            const auto before = threatOf(w, npc1, warlock.guid);
            auto r = castOnce(w, warlock, id); assert(r.executed && r.event);
            if (r.event->outcome == LocalMeleeOutcome::Miss) continue;
            delta = threatOf(w, npc1, warlock.guid) - before; effective = r.event->effective;
        }
        assert(effective && delta == uint64_t(effective) * 2000 && real(id).spellLevel);
    }
    // Frost Shock r2 (8058) through the fallback: x 2.
    {
        uint64_t delta = 0; uint32_t effective = 0;
        auto shaman = mage; shaman.classId = 7; shaman.guid = 77; shaman.knownSpells.push_back(8058);
        w.players.push_back(&shaman);
        for (unsigned i = 0; i < 60 && !effective; ++i) {
            const auto before = threatOf(w, npc1, shaman.guid);
            auto r = castOnce(w, shaman, 8058); assert(r.executed && r.event);
            if (r.event->outcome == LocalMeleeOutcome::Miss) continue;
            delta = threatOf(w, npc1, shaman.guid) - before; effective = r.event->effective;
        }
        assert(effective && delta == uint64_t(effective) * 2000);
        w.players.pop_back();
    }
    // Lacerate: flat 182 and x 0.5 on the direct hit - bear form, rage.
    {
        druid.formSpellId = 5487; druid.resourceType = LocalResourceType::Rage; druid.mana = druid.maxMana = 1000000;
        druid.equipment[15] = 2092; druid.inventory.push_back({2092, 1});
        uint64_t delta = 0; uint32_t effective = 0;
        for (unsigned i = 0; i < 60 && !effective; ++i) {
            const auto before = threatOf(w, npc1, druid.guid);
            auto r = castOnce(w, druid, 33745);
            if (!r.executed) { std::cerr << "FAIL: Lacerate rejected: " << r.result << "\n"; std::abort(); }
            assert(r.event);
            if (localMeleeAvoided(r.event->outcome)) continue;
            delta = threatOf(w, npc1, druid.guid) - before; effective = r.event->effective;
        }
        // The flat term is added with ignoreModifiers = true (Spell.cpp:5806);
        // the damage term carries the x 0.5 and, through CalculateModifiedThreat,
        // Bear Form's own threat multiplier (the form's threatPercent).
        assert(localSpellThreatPct(&real(33745), 1000) == 500);
        const auto damageTerm = localTalentThreat(druid, *w.content, &real(33745), uint64_t(effective) * 1000);
        assert(effective && damageTerm == uint64_t(effective) * 500 * localActiveForm(druid)->threatPercent / 100 && delta == 182000 + damageTerm);
        druid.formSpellId = 0; druid.resourceType = LocalResourceType::Mana;
    }
    // Heals: the priest's Lesser Heal r1 (2050) on the mage, engaged by both
    // creatures - each gains (effective x 500 + SpellLevel x 1000) / 2, the
    // remainder to the lower guid; the paladin's Holy Light r1 (635) halves
    // the heal term: (effective x 250 + SpellLevel x 1000) / 2.
    {
        auto n1 = *findNpc(w.game, npc1); auto n2 = *findNpc(w.game, npc2);
        for (auto* n : {&n1, &n2}) { n->threat = {}; n->threat[0] = {mage.guid, 1000}; n->targetGuid = mage.guid; }
        w.game.setRemoteNpcs({n1, n2});
        for (auto [healer, spell, per] : {std::tuple<LocalRealmPlayer*, uint32_t, uint64_t>{&priest, 2050u, 500u}, {&paladin, 635u, 250u}}) {
            mage.health = mage.maxHealth / 2;
            const auto b1 = threatOf(w, npc1, healer->guid), b2 = threatOf(w, npc2, healer->guid);
            auto r = castOnce(w, *healer, spell, mage.guid, true);
            if (!r.executed) { std::cerr << "FAIL: heal " << spell << " rejected: " << r.result << "\n"; std::abort(); }
            assert(r.event && r.event->effective > 0);
            const uint64_t budget = uint64_t(r.event->effective) * per + uint64_t(real(spell).spellLevel) * 1000;
            const auto d1 = threatOf(w, npc1, healer->guid) - b1, d2 = threatOf(w, npc2, healer->guid) - b2;
            // The heal term and the initial term are each split with their own
            // remainder to the lower guid.
            const uint64_t healTerm = uint64_t(r.event->effective) * per, initial = uint64_t(real(spell).spellLevel) * 1000;
            assert(d1 == healTerm / 2 + healTerm % 2 + initial / 2 + initial % 2 && d2 == healTerm / 2 + initial / 2 && d1 + d2 == budget);
            assert(real(spell).spellLevel > 0);
        }
        // The same heal by a priest nobody is engaged with, on herself, adds
        // nothing anywhere (ForwardThreatForAssistingMe: _threatenedByMe empty).
        bystander.health = bystander.maxHealth / 2;
        auto r = castOnce(w, bystander, 2050, bystander.guid, true); assert(r.executed && r.event && r.event->effective);
        assert(threatOf(w, npc1, bystander.guid) == 0 && threatOf(w, npc2, bystander.guid) == 0);
        // A stunned creature is UNIT_STATE_CONTROLLED and gets no assist threat.
        n1 = *findNpc(w.game, npc1); n2 = *findNpc(w.game, npc2);
        n2.controls.push_back({5211, 4000, warrior.guid, 0, uint8_t(LocalNpcControlKind::Stun)});
        w.game.setRemoteNpcs({n1, n2});
        mage.health = mage.maxHealth / 2;
        const auto c1 = threatOf(w, npc1, priest.guid), c2 = threatOf(w, npc2, priest.guid);
        r = castOnce(w, priest, 2050, mage.guid, true); assert(r.executed && r.event && r.event->effective);
        assert(threatOf(w, npc2, priest.guid) == c2 && threatOf(w, npc1, priest.guid) == c1 + uint64_t(r.event->effective) * 500 + uint64_t(real(2050).spellLevel) * 1000);
    }
    std::cout << "PASS threat: a landed Frostbolt adds damage x 1000 + SpellLevel " << frostbolt.spellLevel << " x 1000 thousandths and a missed one the "
                 "pre-existing thousandth; Shield Slam r8 adds its row's flat 770 in place of SpellLevel " << real(47488).spellLevel << "; Heroic Throw multiplies "
                 "its damage threat by 1.5 and adds no SpellLevel (a row with flat 0); Searing Pain r1 and r2 (rank 2 through GetSpellThreatEntry's first-rank "
                 "fallback) and Frost Shock r2 multiply by 2; Lacerate adds flat 182 and halves its damage threat; the priest's Lesser Heal splits "
                 "effective x 500 + SpellLevel x 1000 over the two creatures engaged with the recipient, the paladin's Holy Light effective x 250 + SpellLevel "
                 "x 1000; a heal on an unengaged player adds nothing; a stunned creature gets no assist threat\n";
}

// ---------------------------------------------------------------------------
// 5. The importer defect: the Lightning Overload proc copies retired.
// ---------------------------------------------------------------------------
void lightningOverloadCopies() {
    const auto acceptedIds = auditedAccepted();
    assert(acceptedIds.size() == 1004); // 990 at the implementation + the implementation's 14 form passives
    const std::set<uint32_t> copies{49239, 49240, 49268, 49269};
    const std::set<uint32_t> shieldLeaves{26372, 49278, 49279};
    std::set<uint32_t> retired;
    for (const auto& d : gImported.spells) if (d.triggeredOnly && acceptedIds.count(d.id) && !d.talentId && localSpellRankFirst(d.id) &&
        localSpellRankFirst(d.id) != d.id && !localSpellRankChainOfferedByTrainer(localSpellRankFirst(d.id))) retired.insert(d.id);
    std::set<uint32_t> expected = copies; expected.insert(shieldLeaves.begin(), shieldLeaves.end());
    const bool exact = expect(retired == expected && gImported.untrainedChainRanksRetired == 7 && gImported.procChildrenRetired == 2,
        "untrained-chain retirement flipped " + join(retired) + " (" + std::to_string(gImported.untrainedChainRanksRetired) + "), expected " + join(expected));
    for (auto id : expected) { const auto& d = real(id); assert(accepted(d) && d.triggeredOnly && !castable(d)); }
    // The link at the pin: spell_ranks chains rooted at 45284 / 45297, ranks 13-14 and 7-8.
    assert(localSpellRankFirst(49239) == 45284 && localSpellRankFirst(49240) == 45284 && localSpellRankFirst(49268) == 45297 && localSpellRankFirst(49269) == 45297);
    assert(localSpellRankRow(49239)->rank == 13 && localSpellRankRow(49240)->rank == 14 && localSpellRankRow(49268)->rank == 7 && localSpellRankRow(49269)->rank == 8);
    assert(!localSpellRankChainOfferedByTrainer(45284) && !localSpellRankChainOfferedByTrainer(45297) && !localSpellRankChainOfferedByTrainer(26364));
    assert(localSpellRankChainOfferedByTrainer(403) && localSpellRankChainOfferedByTrainer(421) && localTrainerOffers(49238) && localTrainerOffers(49270) && !localTrainerOffers(49239));
    // The client's own data: the chain roots carry no class mask; the copies do.
    assert(real(49239).sourceNoThreat && real(49269).sourceNoThreat && !real(49238).sourceNoThreat);
    constexpr size_t trainerRows = sizeof(kLocalTrainerSpells) / sizeof(kLocalTrainerSpells[0]);
    assert(trainerRows == 3592);
    for (size_t i = 1; i < trainerRows; ++i) assert(kLocalTrainerSpells[i - 1] < kLocalTrainerSpells[i]);
    // The stand-ins the rule leaves alone (their own rank 1 is class-masked):
    // Holy Shock x14, Immolation Trap x8, Pounce Bleed x5, Blessed Recovery x3.
    std::map<std::string, unsigned> standIns;
    for (const auto& d : gImported.spells) {
        if (!castable(d) || d.talentId || !localSpellRankFirst(d.id)) continue;
        if (!localSpellRankChainOfferedByTrainer(localSpellRankFirst(d.id))) ++standIns[d.name];
    }
    unsigned standInTotal = 0; std::string standInList;
    for (const auto& [name, count] : standIns) { standInTotal += count; standInList += (standInList.empty() ? "" : ", ") + name + " x" + std::to_string(count); }
    const bool remainder = expect(standInTotal == 30 && standIns.size() == 4, "untrained-chain stand-ins: " + standInList);
    // The castable set loses exactly the seven; a fresh level-80 shaman
    // spellbook and the class trainer lack them.
    std::set<uint32_t> castableIds;
    for (const auto& d : gImported.spells) if (castable(d)) castableIds.insert(d.id);
    for (auto id : expected) assert(!castableIds.count(id));
    // The the implementation trainer harness: the whole import, the client starter set, a trainer.
    auto content = std::make_shared<LocalWorldContent>(); content->spells = gImported.spells; content->clientStarterSpells = true;
    std::sort(content->spells.begin(), content->spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    LocalNpcDefinition trainerDefinition; trainerDefinition.id = 5497; trainerDefinition.name = "Trainer"; trainerDefinition.health = 100; trainerDefinition.hostile = false;
    content->npcs.push_back(trainerDefinition);
    LocalGameplay game; game.useContent(content);
    auto shaman = rewardPlayer(1); shaman.classId = 7; shaman.level = 80; shaman.money = 1000000000; shaman.knownSpells.clear(); shaman.quests.clear();
    shaman.resourceType = LocalResourceType::Mana;
    std::vector<LocalRealmPlayer*> players{&shaman};
    game.tick(0, players);
    auto trainer = rewardNpc(0xf13000000000000aULL); trainer.entry = 5497; trainer.hostile = false; trainer.x = 1;
    trainer.classTrainer = true; trainer.trainerClass = 7; trainer.targetGuid = 0; trainer.threat = {};
    game.setRemoteNpcs({trainer});
    auto offers = game.trainableSpells(shaman, trainer.guid);
    auto fresh = shaman; fresh.gameplayInitialized = false;
    game.initializePlayer(fresh, true);
    for (auto id : expected) {
        assert(std::find(offers.begin(), offers.end(), id) == offers.end());
        assert(std::find(fresh.knownSpells.begin(), fresh.knownSpells.end(), id) == fresh.knownSpells.end());
    }
    assert(std::find(fresh.knownSpells.begin(), fresh.knownSpells.end(), 49238u) != fresh.knownSpells.end()); // Lightning Bolt r14, the real one
    // A save that still knows a copy loses it on load, in memory.
    auto stale = fresh; stale.knownSpells.push_back(49240); stale.knownSpells.push_back(49279);
    game.initializePlayer(stale, false);
    assert(std::find(stale.knownSpells.begin(), stale.knownSpells.end(), 49240u) == stale.knownSpells.end() &&
           std::find(stale.knownSpells.begin(), stale.knownSpells.end(), 49279u) == stale.knownSpells.end());
    // Casting one directly is refused.
    std::string result;
    stale.knownSpells.push_back(49240);
    assert(!game.execute(stale, {LocalAction::CastSpell, trainer.guid, 49240}, players, result) && result == "Triggered spell cannot be cast directly");
    if (exact && remainder)
        std::cout << "PASS Lightning Overload copies: the accepted census is 1,004 and the untrained-chain rule retires " << gImported.untrainedChainRanksRetired
                  << " decoded rows to triggeredOnly - the four copies " << join(copies) << " (spell_ranks chains 45284 / 45297, ranks 13-14 and 7-8, cast by "
                     "spell_sha_lightning_overload, offered by no trainer) and Lightning Shield's own damage leaves " << join(shieldLeaves)
                  << " (chain 26364, the reactive-shield profile's children, the same stray class mask on ranks 9-11); the aura-42 rule still retires 2; "
                     "the castable set, the level-80 class trainer and the fresh level-80 shaman spellbook (" << fresh.knownSpells.size()
                  << " spells, Lightning Bolt r14 49238 present) lack all seven; a loaded save drops them; the direct cast is refused; the "
                  << standInTotal << " stand-ins in 4 untrained chains whose own rank 1 the client marks learnable stay: " << standInList << "\n";
}

// ---------------------------------------------------------------------------
// 6. P05-8: the LAN repeat / reconnect invariants, driven through the shipped
//    host's own datagram handlers.
// ---------------------------------------------------------------------------
sockaddr_in loopback(uint16_t port) { sockaddr_in a{}; initAddress(a); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(port); return a; }
void repeatAndReconnect() {
    char temp[] = "/tmp/wowps-shared-combat-0246-XXXXXX"; auto* dir = mkdtemp(temp); assert(dir);
    LocalRealm host; auto& h = *host.impl_;
    auto content = rewardContent(); content->quests.clear();
    // Fireball r1 (133): a 1.5 s cast, so the guest is mid-cast for many pumps.
    const auto& fireball = real(133);
    content->spells.push_back(fireball);
    std::sort(content->spells.begin(), content->spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    auto target = creature(plainHumanoid(80)); target.health = 100000000; target.damage = 0; target.aggroRadius = 0.01f; target.xp = 0; target.money = 0; target.loot.clear();
    content->npcs.push_back(target);
    std::sort(content->npcs.begin(), content->npcs.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    h.gameplay.useContent(content);
    h.state = LocalRealmState::Hosting; h.realmId = 123; h.directory = dir;
    h.self = rewardPlayer(1); h.self.quests.clear();
    auto guest = rewardPlayer(2); guest.quests.clear(); guest.classId = 8; guest.level = 80; guest.resourceType = LocalResourceType::Mana;
    guest.mana = guest.maxMana = 100000; guest.health = guest.maxHealth = 100000; guest.knownSpells = {1, 133}; guest.x = guest.y = guest.z = 0;
    h.saved = {{{1, 11}, h.self}, {{2, 22}, guest}};
    assert(h.openSocket(0));
    LocalRealm::Impl::Peer peer; peer.guid = 2; peer.identity = {2, 22}; peer.address = loopback(40000); peer.session = 987; peer.loading = false; peer.lastSeen = h.now;
    h.peers.push_back(peer);
    auto npc = rewardNpc(10); npc.entry = target.id; npc.name = target.name; npc.level = target.level; npc.health = npc.maxHealth = target.health;
    npc.hostile = true; npc.lootOwner = 0; npc.x = npc.homeX = 8; npc.y = npc.homeY = 0; npc.z = npc.homeZ = 0; npc.targetGuid = 2; npc.threat[0] = {2, 1000};
    h.gameplay.setRemoteNpcs({npc});
    h.refreshPlayers();
    host.update(.05f);
    auto& record = h.saved[1].player;
    const auto manaBefore = record.mana;
    const auto command = [&](uint32_t id) {
        Writer w; w.u32(id); w.u8(uint8_t(LocalAction::CastSpell)); w.u64(npc.guid); w.u32(133); w.u32(0); w.u32(0); w.u32(0); w.u64(0);
        Reader r(w.bytes.data(), w.bytes.size());
        h.receiveCommand(h.peers[0], r);
    };
    // (i) id 1 starts the cast: one reservation, nothing spent, one cast sequence.
    command(1);
    assert(h.peers[0].lastCommand == 1 && h.peers[0].lastCommandSuccess);
    assert(record.castingSpellId == 133 && record.castStatus == LocalCastStatus::Casting && record.castCostPrepared && record.castPreparedCost > 0);
    assert(record.mana == manaBefore && record.castRemainingMs == record.castTotalMs && record.castTotalMs >= 1500);
    const auto sequence = record.castSequence, remaining = record.castRemainingMs;
    host.update(.5f);
    assert(record.castingSpellId == 133 && record.castRemainingMs < remaining);
    const auto afterHalf = record.castRemainingMs;
    // The acknowledged id again: re-acknowledged, not re-executed - the cast
    // keeps its sequence and its progress, no second reservation.
    command(1);
    assert(h.peers[0].lastCommand == 1 && record.castingSpellId == 133 && record.castSequence == sequence && record.castRemainingMs == afterHalf &&
           record.castCostPrepared && record.mana == manaBefore);
    // (iii) id 3 - a gap - is dropped without execution.
    command(3);
    assert(h.peers[0].lastCommand == 1 && record.castingSpellId == 133 && record.castSequence == sequence && record.castRemainingMs == afterHalf);
    // id 2 while the cast runs is the reference's SPELL_FAILED_SPELL_IN_PROGRESS: refused, not a second cast.
    command(2);
    assert(h.peers[0].lastCommand == 2 && !h.peers[0].lastCommandSuccess && record.castingSpellId == 133 && record.castSequence == sequence);
    unsigned castEvents = 0;
    for (const auto& e : h.gameplay.combatEvents()) if (e.kind == LocalCombatEventKind::SpellCast && e.source == 2) ++castEvents;
    assert(castEvents == 0); // the cast has not completed: no SpellCast event yet
    // (ii) the session goes silent past ReconnectSilence and the same identity
    // re-HELLOs: the cast is gone, nothing was spent, the GCD is clear, the
    // creature's threat entry survives, the new peer's command counter is fresh.
    h.peers[0].lastSeen = h.now - ReconnectSilence - 1;
    {
        Writer w; w.u64(2); w.u64(22); w.name(record.name); w.u8(record.race); w.u8(record.classId); w.u8(0); w.u32(h.gameplay.content().fingerprint);
        LocalRealmPlayer appearance; writeAppearance(w, appearance); w.u64(2);
        Reader r(w.bytes.data(), w.bytes.size());
        h.helloBudget = 4;
        h.handleHello(r, loopback(40001), 0x5151);
    }
    assert(h.peers.size() == 1 && h.peers[0].joinNonce == 0x5151 && h.peers[0].lastCommand == 0 && h.peers[0].guid == 2);
    auto& reconnected = h.findSaved(2)->player;
    assert(reconnected.castingSpellId == 0 && reconnected.castStatus == LocalCastStatus::None && !reconnected.castCostPrepared && reconnected.castPreparedCost == 0 &&
           reconnected.castRemainingMs == 0 && reconnected.castTotalMs == 0 && reconnected.globalCooldownMs == 0 && reconnected.mana == manaBefore &&
           reconnected.castSequence == 0 && reconnected.castPushbackMs == 0 && reconnected.castPushbackCount == 0);
    bool threatSurvives = false;
    for (const auto& n : h.gameplay.npcs()) for (const auto& e : n.threat) if (e.guid == 2 && e.amount == 1000) threatSurvives = true;
    assert(threatSurvives);
    castEvents = 0;
    for (const auto& e : h.gameplay.combatEvents()) if (e.kind == LocalCombatEventKind::SpellCast && e.source == 2) ++castEvents;
    assert(castEvents == 0);
    // The reconnected session's id 1 is a fresh command and starts a fresh cast.
    h.peers[0].loading = false;
    command(1);
    assert(h.peers[0].lastCommand == 1 && h.peers[0].lastCommandSuccess && reconnected.castingSpellId == 133 && reconnected.castSequence == 1 && reconnected.mana == manaBefore);
    // A second live HELLO from the same identity inside ReconnectSilence cannot take the session.
    {
        Writer w; w.u64(2); w.u64(22); w.name(record.name); w.u8(record.race); w.u8(record.classId); w.u8(0); w.u32(h.gameplay.content().fingerprint);
        LocalRealmPlayer appearance; writeAppearance(w, appearance); w.u64(2);
        Reader r(w.bytes.data(), w.bytes.size());
        h.handleHello(r, loopback(40002), 0x5252);
    }
    assert(h.peers.size() == 1 && h.peers[0].joinNonce == 0x5151 && reconnected.castingSpellId == 133);
    host.stop();
    fs::remove_all(dir);
    std::cout << "PASS repeat / reconnect: a guest's CastSpell id 1 starts a " << record.castTotalMs << " ms Fireball with one cost reservation and nothing "
                 "spent; re-sending id 1 is re-acknowledged and not re-executed (same cast sequence, same progress, one reservation); id 3 is dropped; id 2 "
                 "during the cast is refused as a cast in progress; a session silent past ReconnectSilence re-HELLOed by the same identity comes back "
                 "with no cast, nothing spent, GCD 0, pushback state 0 and the creature's threat entry intact, its command counter fresh; a live "
                 "session cannot be taken over\n";
}

// ---------------------------------------------------------------------------
// 7. The save and the LAN protocol carry nothing of P05's (both moved at the implementation
//    for the pet roster); the definition fingerprint moves with the new
//    fields; the older fixture's assumption that a Shield Slam critical is 1.5x
//    is gone.
// ---------------------------------------------------------------------------
void formats() {
    // the implementation moved both, for the pet roster: writePet gained the pet's
    // command state, react state and stay point, and that one layout is
    // shared by the character save and the LAN pet deck. Nothing this
    // group measures moved with it.
    assert(SaveVersion == 30 && lan::GameplayVersion == 85 && Version == 85);
    // The fingerprint moves with SpellLevel and the new attribute bits: two
    // contents that differ only in one such column do not join.
    const auto fingerprintOf = [](std::vector<LocalSpellDefinition> spells) {
        LocalGameplay game; auto c = std::make_shared<LocalWorldContent>(); game.useContent(c);
        std::string error; const bool ok = game.setStarterSpells(spells, "", error);
        assert(ok);
        return c->fingerprint; };
    std::vector<LocalSpellDefinition> spells;
    for (const auto& d : gImported.spells) if (d.clientSpell && d.allowableClasses) spells.push_back(d);
    const auto before = fingerprintOf(spells), again = fingerprintOf(spells);
    auto altered = spells; for (auto& d : altered) if (d.id == 23922) d.spellLevel = 1;
    auto altered2 = spells; for (auto& d : altered2) if (d.id == 23922) d.sourceNoActiveDefense = true;
    auto altered3 = spells; for (auto& d : altered3) if (d.id == 1822) d.sourceDirectDamage = false;
    const auto moved = fingerprintOf(altered), moved2 = fingerprintOf(altered2), moved3 = fingerprintOf(altered3);
    assert(before == again && before != moved && before != moved2 && before != moved3 && moved != moved2);
    // The threat view codec: the same bytes as the implementation (the values move, the
    // format does not) - the replicated LocalNpcThreatView is viewerGuid,
    // amount, raw and scaled basis points and a status byte (local_realm.cpp,
    // writeNpc / readNpc), all of which now carry the initial and table
    // threat in their values.
    static_assert(CastWireBytes == 222);
    static_assert(NpcWireBytes == 618);
    LocalWorldContent content; LocalNpcDefinition definition; definition.id = 50; definition.name = "Codec NPC"; definition.displayId = 100; content.npcs.push_back(definition);
    LocalRealmNpc n; n.guid = 0xf130000000000001ULL; n.entry = 50; n.level = 20; n.health = 80; n.maxHealth = 100; n.targetGuid = 2;
    n.playerThreat.viewerGuid = 1; n.playerThreat.amount = 5; n.playerThreat.present = true; n.playerThreat.rawBasisPoints = 5000; n.playerThreat.scaledBasisPoints = 4545; n.playerThreat.status = 1;
    Writer w; writeNpc(w, n);
    Reader r(w.bytes.data(), w.bytes.size());
    const auto back = readNpc(r, content);
    assert(r.valid && r.done() && back.playerThreat.viewerGuid == 1 && back.playerThreat.amount == 5 && back.playerThreat.rawBasisPoints == 5000 &&
           back.playerThreat.scaledBasisPoints == 4545 && back.playerThreat.status == 1 && back.playerThreat.present);
    std::cout << "PASS formats: SaveVersion 29, GameplayVersion 84, NpcWireBytes " << NpcWireBytes << ", CastWireBytes " << CastWireBytes
              << " unchanged; the content fingerprint moves with spellLevel and the new attribute bits\n";
}
} // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    assert(argc == 3);
    ClientTables tables; tables.load(argv[1]); gTables = &tables;
    gImported = tables.import();
    loadCatalog(argv[2]);
    census();
    shieldSlamAndFriends();
    criticalShape();
    threat();
    lightningOverloadCopies();
    repeatAndReconnect();
    formats();
    assert(gEventsChecked > 0 && gBlockedEvents > 0);
    std::cout << "PASS conservation identity: resisted + effective + absorbed + blocked == attempted held on every one of " << gEventsChecked
              << " damage observations this suite retained, " << gBlockedEvents << " of them carrying a partial block\n";
    if (gFailures) { std::cerr << gFailures << " group(s) failed against source-backed fixtures\n"; return 1; }
    return 0;
}
