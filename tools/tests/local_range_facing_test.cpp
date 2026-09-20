// P05 / the implementation - shared combat rules, second checkpoint: combat reach on every
// range test and on the melee ranges (P05-5), the FacingCasterFlags arc for
// every spell that carries it (P05-3), TargetCreatureType and
// ONLY_PEACEFUL_TARGETS on the controls that carry them (P05-7), and the spell
// hit rating on the magic cast roll (P05-4).
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33, as measured
// in the source audit sections 2.5, 5.1-5.4 and 8.1:
//   Spell::CheckRange                 Spell.cpp:7301-7396 - the Self Only escape,
//       SPELLMOD_RANGE, min(3, 10 %) at completion, the melee arm, the combat-range
//       arm, the FacingCasterFlags arc and the SPELL_RANGE_RANGED minimum.
//   Unit::IsWithinCombatRange         Unit.cpp:766-780 - + reachA + reachB.
//   Unit::IsWithinMeleeRange          Unit.cpp:782-797.
//   Unit::GetMeleeRange               Unit.cpp:799-803 - max(reaches + 4/3, 5).
//   Unit::IsWithinBoundaryRadius      Unit.cpp:820-828 - the facing escape.
//   Position::HasInArc                Position.cpp:148-181.
//   Creature::SetObjectScale          Creature.cpp:3536-3550 - CombatReach and
//       BoundingRadius from creature_model_info times DisplayScale.
//   Player::SetObjectScale            Player.h:1100-1105 - 1.5 and 0.389.
//   ObjectDefines.h:44-48             the four constants.
//   SpellInfo::CheckTargetCreatureType SpellInfo.cpp:1906-1919, refused at :1819-1825.
//   SpellInfo::CheckTarget            SpellInfo.cpp:1694-1695 - ONLY_PEACEFUL_TARGETS.
//   Unit::GetCreatureTypeMask         Unit.h:829-833; Unit::GetCreatureType Unit.cpp:11473-11486.
//   WorldObject::MagicSpellHitResult  Object.cpp:3574-3628 - the level table, the
//       op-16 modifier, m_modSpellHitChance in hundredths and the 1..100 % clamp.
//   Unit::UpdateSpellHitChances       StatSystem.cpp:885-886 - aura 55 + CR_HIT_SPELL.
//   ThreatManager::SelectVictim       ThreatManager.cpp:656-679 - the melee 110 %.
//   UnitAI::DoMeleeAttackIfReady      UnitAI.cpp:50; PlayerUpdates.cpp:173, :213.
//
// Every census number is measured by running the shipped importer over the
// player's own DBC set and the shipped catalog reader over the shipped catalog;
// every runtime number comes from the shipped LocalGameplay tick.
#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include "local_group_rewards_fixture.hpp"
#include "game/local_combat_reach.hpp"
#include "game/local_spell_range.hpp"
#include "game/local_spell_target_rules.hpp"
#include "game/local_spell_critical.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_ranged.hpp"
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
template <class T> std::string join(const T& ids, size_t limit = 40) {
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
uint32_t gCatalogFingerprint = 0;
void loadCatalog(const fs::path& directory) {
    static LocalWorldCatalog catalog;
    std::string error;
    if (!catalog.load(directory.string(), error)) { std::cerr << "FAIL: catalog: " << error << "\n"; std::abort(); }
    gCatalogFingerprint = catalog.fingerprint();
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

// --- runtime scaffolding ----------------------------------------------------
struct Caster { uint8_t classId; LocalResourceType resource; uint8_t level; };
struct World {
    std::shared_ptr<LocalWorldContent> content;
    LocalGameplay game;
    std::vector<LocalRealmPlayer> casters;
    std::vector<LocalRealmPlayer*> players;
    std::vector<uint64_t> npcGuids;
    std::vector<float> pinX;
    uint64_t npcGuid = 0;
};
const LocalRealmNpc* findNpc(const LocalGameplay& game, uint64_t guid) {
    for (const auto& n : game.npcs()) if (n.guid == guid) return &n;
    return nullptr;
}
LocalItemDefinition item(uint32_t id, const char* name, uint8_t inventoryType) {
    LocalItemDefinition it; it.id = id; it.name = name; it.inventoryType = inventoryType; it.stack = 1; return it;
}
// One or more real catalog creatures, each at its own distance, with the shipped
// definition's reach, bounding radius and creature type intact.
void buildWorld(World& w, const std::vector<uint32_t>& spells, const std::vector<Caster>& casters,
                const std::vector<std::pair<LocalNpcDefinition, float>>& targets,
                const std::vector<LocalItemDefinition>& extraItems = {}) {
    w.content = rewardContent();
    for (auto id : spells) w.content->spells.push_back(real(id));
    std::sort(w.content->spells.begin(), w.content->spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (auto entry : targets) {
        auto& target = entry.first;
        target.health = 100000000; target.damage = 0; target.aggroRadius = 0.01f; target.xp = 0; target.money = 0; target.loot.clear();
        w.content->npcs.push_back(target);
    }
    std::sort(w.content->npcs.begin(), w.content->npcs.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (const auto& it : extraItems) w.content->items.push_back(it);
    std::sort(w.content->items.begin(), w.content->items.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    w.game.useContent(w.content);
    for (size_t i = 0; i < casters.size(); ++i) {
        auto p = rewardPlayer(1 + i);
        p.classId = casters[i].classId; p.level = casters[i].level; p.health = p.maxHealth = 100000000;
        p.mana = p.maxMana = 1000000; p.resourceType = casters[i].resource;
        p.x = p.y = p.z = 0; p.orientation = 0; p.quests.clear();
        p.knownSpells = {1};
        for (auto id : spells) p.knownSpells.push_back(id);
        w.casters.push_back(p);
    }
    w.players.clear();
    for (auto& c : w.casters) w.players.push_back(&c);
    w.game.tick(0, w.players);
    std::vector<LocalRealmNpc> npcs;
    for (size_t i = 0; i < targets.size(); ++i) {
        auto n = rewardNpc(10 + i);
        n.entry = targets[i].first.id; n.name = targets[i].first.name; n.level = targets[i].first.level;
        n.health = n.maxHealth = 100000000; n.hostile = true; n.lootOwner = 0;
        n.x = n.homeX = targets[i].second; n.y = n.homeY = 0; n.z = n.homeZ = 0;
        n.orientation = float(M_PI);
        // A creature with no victim is not a residency candidate and the next
        // regions() sweep drops it (local_gameplay.cpp:655-683), so every
        // fixture creature here holds the first caster on its threat list, as
        // the the implementation suite's do.
        n.targetGuid = w.casters[0].guid; n.threat[0] = {w.casters[0].guid, 1000};
        npcs.push_back(n);
        w.npcGuids.push_back(n.guid);
        w.pinX.push_back(targets[i].second);
    }
    w.game.setRemoteNpcs(npcs);
    w.npcGuid = w.npcGuids[0];
    w.game.tick(.25f, w.players);
}
// A creature that holds a victim pursues it, so every distance this suite
// measures is re-imposed before and after each tick; nothing else about the
// creature is touched.
void pin(World& w) {
    auto npcs = w.game.npcs();
    bool changed = false;
    for (auto& n : npcs)
        for (size_t i = 0; i < w.npcGuids.size(); ++i)
            if (n.guid == w.npcGuids[i] && (n.x != w.pinX[i] || n.y != 0 || n.z != 0)) {
                n.x = w.pinX[i]; n.y = 0; n.z = 0; changed = true;
            }
    if (changed) w.game.setRemoteNpcs(npcs);
}
void tickPinned(World& w, float dt) { pin(w); w.game.tick(dt, w.players); pin(w); }
void placeNpc(World& w, uint64_t guid, float x) {
    for (size_t i = 0; i < w.npcGuids.size(); ++i) if (w.npcGuids[i] == guid) w.pinX[i] = x;
    pin(w);
}
uint64_t lastSequence(const LocalGameplay& game) {
    uint64_t s = 0;
    for (const auto& e : game.combatEvents()) s = std::max(s, e.sequence);
    return s;
}
std::optional<LocalCombatEvent> lastEvent(const LocalGameplay& game, uint64_t target, uint32_t spellId, uint64_t after) {
    std::optional<LocalCombatEvent> found;
    for (const auto& e : game.combatEvents())
        if (e.sequence > after && e.target == target && e.spell == spellId &&
            (e.kind == LocalCombatEventKind::SpellDamage || e.kind == LocalCombatEventKind::PeriodicDamage)) found = e;
    return found;
}
struct CastResult { bool executed = false; std::string result; std::optional<LocalCombatEvent> event; };
CastResult castOnce(World& w, LocalRealmPlayer& p, uint32_t spellId, uint64_t target = 0, bool settle = true) {
    p.globalCooldownMs = 0; p.cooldowns.clear(); p.categoryCooldowns.clear();
    p.mana = p.maxMana; p.comboPoints = 0; p.comboTarget = 0;
    p.attackTarget = 0; p.attackTimer = p.offHandTimer = 1000.f;
    if (!target) target = w.npcGuid;
    const auto before = lastSequence(w.game);
    CastResult r;
    r.executed = w.game.execute(p, {LocalAction::CastSpell, target, spellId}, w.players, r.result);
    if (!r.executed) return r;
    if (settle) {
        for (unsigned t = 0; t < 200 && p.castStatus == LocalCastStatus::Casting; ++t) tickPinned(w, .05f);
        assert(p.castStatus != LocalCastStatus::Casting);
    }
    r.event = lastEvent(w.game, target, spellId, before);
    return r;
}
// The band a binomial count lies in with near certainty: expectation +- four
// standard deviations, computed from the roll's own inputs.
struct Band { double expected; unsigned low, high; };
Band band(unsigned n, double pct) {
    const double p = std::clamp(pct, 0.0, 100.0) / 100.0, mean = n * p, sd = std::sqrt(n * p * (1 - p));
    return {mean, unsigned(std::max(0.0, std::floor(mean - 4 * sd))), unsigned(std::ceil(mean + 4 * sd))};
}
bool within(unsigned count, const Band& b) { return count >= b.low && count <= b.high; }

// ---------------------------------------------------------------------------
// 1. The catalog census: combat reach and bounding radius as new definition
//    data, the creature-type histogram the TargetCreatureType gate reads, and
//    the pack's before/after size and fingerprint.
// ---------------------------------------------------------------------------
// The shipped catalog at the implementation, before creature_model_info was joined in.
constexpr uint64_t kNpcsPackBytes0246 = 4697570, kNpcsPackBytes0247 = 5286572;
constexpr uint32_t kCatalogFingerprint0246 = 1452753532, kCatalogFingerprint0247 = 1819708182;

void catalogCensus(const fs::path& directory) {
    unsigned above15 = 0, atLeast3 = 0, atLeast5 = 0, withRadius = 0, radiusAbove2 = 0, defaulted = 0;
    float maximum = 0;
    for (auto e : gHostile) {
        const auto& d = creature(e);
        const float reach = localCreatureCombatReach(&d);
        if (reach > 1.5f) ++above15;
        if (reach >= 3.f) ++atLeast3;
        if (reach >= 5.f) ++atLeast5;
        if (d.boundingRadius > 0) ++withRadius;
        if (localCreatureBoundingRadius(&d) > kLocalMinMeleeReach) ++radiusAbove2;
        if (std::abs(reach - kLocalWorldObjectSize) < 1e-4f) ++defaulted;
        maximum = std::max(maximum, reach);
    }
    unsigned catalogued = 0;
    for (const auto& [id, d] : gNpcs) if (d.combatReach > 0) ++catalogued;
    expect(gNpcs.size() == 14496, "the catalog still holds 14,496 creature definitions");
    expect(gHostile.size() == 3582, "3,582 of them are hostile");
    expect(catalogued == gNpcs.size(), "every catalog definition now carries a combat reach");
    expect(above15 == 1995 && atLeast3 == 755 && atLeast5 == 239 && std::abs(maximum - 27.f) < 1e-3f,
           "the audit's hostile reach census reproduces: 1,995 above 1.5, 755 at 3 or more, 239 at 5 or more, maximum 27");
    // A definition without a model-info row keeps the reference's own default
    // rather than the player's 1.5 (Creature.cpp:3538, :3544).
    expect(defaulted > 0, "some hostile templates fall back to DEFAULT_WORLD_OBJECT_SIZE");
    // The creature-type histogram the TargetCreatureType gate reads. It comes
    // from creature_template.type, compiled beside the code since the melee
    // catalog (local_melee_npcs_generated.inc), which is the same column
    // Unit::GetCreatureType reads (Unit.cpp:11485).
    std::map<uint32_t, unsigned> histogram;
    for (auto e : gHostile) ++histogram[localNpcCreatureType(e)];
    const std::map<uint32_t, unsigned> expectedHistogram{{1, 925}, {2, 57}, {3, 316}, {4, 318}, {5, 39}, {6, 592}, {7, 1256}, {9, 79}};
    expect(histogram == expectedHistogram, "the hostile creature-type histogram is the pinned creature_template.type");
    const auto bytes = fs::file_size(directory / "npcs.pack");
    expect(bytes == kNpcsPackBytes0247, "npcs.pack is " + std::to_string(kNpcsPackBytes0247) + " bytes");
    expect(gCatalogFingerprint == kCatalogFingerprint0247, "the catalog fingerprint is " + std::to_string(kCatalogFingerprint0247));
    std::ostringstream s;
    s << "PASS catalog census: 14,496 definitions, 3,582 hostile; every definition carries UNIT_FIELD_COMBATREACH and "
      << catalogued << " of " << gNpcs.size() << " a combat reach; over the hostile set " << withRadius
      << " carry a bounding radius, " << above15 << " have a reach above 1.5, "
      << atLeast3 << " at least 3, " << atLeast5 << " at least 5, maximum " << maximum << ", and " << defaulted
      << " fall back to DEFAULT_WORLD_OBJECT_SIZE " << kLocalWorldObjectSize << "; " << radiusAbove2
      << " have a bounding radius above MIN_MELEE_REACH, which is the only case where the facing escape is wider than 2 yd; "
      << "the hostile creature-type histogram is beast " << histogram[1] << ", dragonkin " << histogram[2] << ", demon "
      << histogram[3] << ", elemental " << histogram[4] << ", giant " << histogram[5] << ", undead " << histogram[6]
      << ", humanoid " << histogram[7] << ", mechanical " << histogram[9]
      << "; npcs.pack " << kNpcsPackBytes0246 << " -> " << bytes << " bytes and the catalog fingerprint "
      << kCatalogFingerprint0246 << " -> " << gCatalogFingerprint << "\n";
    std::cout << s.str();
}

// ---------------------------------------------------------------------------
// 2. The client census: which spells carry FacingCasterFlags, which carry a
//    TargetCreatureType, which carry ONLY_PEACEFUL_TARGETS, the range types,
//    and how much spell hit rating the item table really holds.
// ---------------------------------------------------------------------------
std::set<uint32_t> gFlaggedAlreadyGated, gFlaggedNewlyGated;
void clientCensus() {
    const auto audited = auditedAccepted();
    expect(audited.size() == 1004, "the audited-accepted census is 1,004 since the reference admitted the five form "
           "boost spells and the nine entry-resource talent ranks");
    // Column 19 is FacingCasterFlags: verified against the client's own bytes,
    // not assumed from the layout table.
    unsigned flagged = 0, columnMismatch = 0, meleeRows = 0, rangedRows = 0, withMinimum = 0;
    std::set<uint32_t> withType, peaceful, separatePath, baseLevelFromSpellLevel;
    for (const auto& d : gImported.spells) {
        if (!accepted(d) || d.passive) continue;
        // decodeClientSpell is the path that reads the two columns. The NPC
        // decoder (local_npc_spell_import.hpp) and the hand-built Stormstrike
        // children are separate paths that carry neither, and neither can reach
        // the gates: Spell::CheckRange's facing arm requires IsPlayer()
        // (Spell.cpp:7360) and no creature spell or triggered child here is ever
        // a player's explicit cast. They are named in the PASS line rather than
        // silently excluded.
        if (!d.npcOnly && !d.triggeredOnly &&
            (gTables->column(d.id, 19) != d.sourceFacingFlags || gTables->column(d.id, 17) != d.targetCreatureType))
            ++columnMismatch;
        if ((d.npcOnly || d.triggeredOnly) && gTables->column(d.id, 19) != d.sourceFacingFlags) separatePath.insert(d.id);
        // Spell.dbc column 38 is baseLevel and 39 is spellLevel; they differ on
        // 3,090 of the client's 49,839 rows. decodeClientSpell reads column 39
        // for both (local_spell_import.hpp), so this counts whether any accepted
        // definition's baseLevel is the wrong column's value.
        if (!d.npcOnly && !d.triggeredOnly && gTables->column(d.id, 38) != d.baseLevel) baseLevelFromSpellLevel.insert(d.id);
        if (localSpellRequiresFacing(d)) {
            ++flagged;
            (d.comboProfile || d.meleeSpecialProfile || d.stormstrikeProfile || d.rangedAutoProfile
                 ? gFlaggedAlreadyGated : gFlaggedNewlyGated).insert(d.id);
        }
        if (d.targetCreatureType) withType.insert(d.id);
        if (d.sourceOnlyPeacefulTargets) peaceful.insert(d.id);
        if (d.sourceRangeFlags == 1) ++meleeRows;
        if (d.sourceRangeFlags == 2) ++rangedRows;
        if (d.minRange > 0) ++withMinimum;
    }
    expect(!columnMismatch, "columns 17 and 19 are carried verbatim by every row decodeClientSpell decodes");
    expect(separatePath == std::set<uint32_t>{5401, 11985, 32175},
           "only the two NPC-decoded spells and the hand-built Stormstrike main-hand child carry neither column");
    expect(flagged == 351, "351 non-passive accepted spells carry FacingCasterFlags, as the audit measured");
    // The audit's D10 says 80 checked and 271 unchecked. Measured against the
    // shipped build it is 81 and 270: localComboFacingReady is asked for both
    // ranged-auto profiles (the ranged start and the ranged tick in
    // local_gameplay.cpp), so the wand Shoot 5019 was already gated beside Auto
    // Shot 75 and the 79 profiled melee specials.
    expect(gFlaggedAlreadyGated.size() == 81,
           "81 were already gated: the 79 profiled melee specials, Auto Shot 75 and the wand Shoot 5019");
    expect(gFlaggedAlreadyGated.count(75) && gFlaggedAlreadyGated.count(5019),
           "both ranged-auto profiles are among them");
    expect(gFlaggedNewlyGated.size() == 270, "270 were not gated before the reference");
    expect(withType.size() == 9, "9 accepted controls carry a TargetCreatureType");
    expect(withType == std::set<uint32_t>{2637, 9484, 9485, 10955, 11297, 18657, 18658, 20066, 51724},
           "they are the Hibernate, Shackle Undead, Sap and Repentance ranks");
    expect(peaceful.size() == 2 && peaceful == std::set<uint32_t>{11297, 51724},
           "2 accepted spells carry ONLY_PEACEFUL_TARGETS: both Saps");
    expect(meleeRows == 119, "119 accepted non-passive spells carry SPELL_RANGE_MELEE");
    expect(rangedRows == 1 && real(75).sourceRangeFlags == 2, "1 carries SPELL_RANGE_RANGED: Auto Shot");
    expect(withMinimum == 0, "0 accepted rows carry a minimum range of their own");
    if (!baseLevelFromSpellLevel.empty())
        std::cerr << "NOTE baseLevel reads Spell.dbc column 39 (SpellLevel) rather than column 38 on "
                  << baseLevelFromSpellLevel.size() << " accepted definitions: " << join(baseLevelFromSpellLevel) << "\n";
    // The populations the TargetCreatureType gate moves, per control, over the
    // shipped catalog's hostile templates.
    std::map<uint32_t, std::pair<unsigned, unsigned>> populations;
    for (auto id : withType) {
        const auto& d = real(id);
        unsigned legal = 0, illegal = 0;
        for (auto e : gHostile) (localSpellCreatureTypeAllowed(d, localNpcCreatureType(e)) ? legal : illegal)++;
        populations[id] = {legal, illegal};
    }
    expect(populations[2637] == std::make_pair(982u, 2600u), "Hibernate: 982 legal hostile templates and 2,600 it used to land on");
    expect(populations[9484] == std::make_pair(592u, 2990u), "Shackle Undead: 592 legal and 2,990 illegal");
    expect(populations[11297] == std::make_pair(1256u, 2326u), "Sap 11297: 1,256 legal and 2,326 illegal");
    expect(populations[51724] == std::make_pair(2554u, 1028u), "Sap 51724: 2,554 legal and 1,028 illegal");
    expect(populations[20066] == std::make_pair(2260u, 1322u), "Repentance: 2,260 legal and 1,322 illegal");
    // The spell hit rating the wand has read since the implementation and the cast roll now
    // reads too: how many of the ranged catalog's items carry one.
    unsigned itemsWithSpellHit = 0, itemsTotal = 0;
    for (const auto& i : local_ranged_detail::items) { ++itemsTotal; if (i.spellHit > 0) ++itemsWithSpellHit; }
    expect(itemsTotal == 5650 && itemsWithSpellHit == 2520,
           "2,520 of the 5,650 ranged-catalog items carry a spell hit rating");
    unsigned ratioRows = 0, ratioWithSpellHit = 0;
    for (const auto& r : local_ranged_detail::ratios) { ++ratioRows; if (r.spellHit > 0) ++ratioWithSpellHit; }
    expect(ratioRows == 800 && ratioWithSpellHit == 800, "all 800 class/level ratio rows carry a CR_HIT_SPELL scale");
    std::cout << "PASS client census: columns 17 and 19 carried verbatim by every row decodeClientSpell decodes (the two "
                 "NPC-decoded spells 5401/11985 and the hand-built Stormstrike child 32175 go through separate decoders and "
                 "carry neither; none can reach the gates, because Spell.cpp:7360 requires a player caster); " << flagged
              << " non-passive accepted spells carry FacingCasterFlags & SPELL_FACING_FLAG_INFRONT - "
              << gFlaggedAlreadyGated.size() << " were already asked (79 profiled melee specials, Auto Shot 75 and the wand Shoot 5019 - the audit's D10 counted 80, one short) and "
              << gFlaggedNewlyGated.size() << " were castable from any facing until the reference; " << withType.size()
              << " accepted controls carry a TargetCreatureType (Hibernate 982/2600, Shackle 592/2990, Sap 11297 1256/2326, "
                 "Sap 51724 2554/1028, Repentance 2260/1322 legal/illegal hostile templates), 2 carry ONLY_PEACEFUL_TARGETS; "
              << meleeRows << " rows are SPELL_RANGE_MELEE, " << rangedRows << " SPELL_RANGE_RANGED, " << withMinimum
              << " carry a minimum of their own; " << itemsWithSpellHit << " of " << itemsTotal
              << " ranged-catalog items carry a spell hit rating over " << ratioRows << " class/level ratio rows\n";
}

// ---------------------------------------------------------------------------
// 3. The geometry, against the reference's own arithmetic: the melee range
//    formula, the combat-range term, the completion leniency and the
//    SPELL_RANGE_RANGED minimum.
// ---------------------------------------------------------------------------
void geometry() {
    // Unit::GetMeleeRange, Unit.cpp:799-803.
    expect(std::abs(localMeleeRange(kLocalDefaultCombatReach, kLocalDefaultCombatReach) - 5.f) < 1e-5f,
           "two default-reach units melee at NOMINAL_MELEE_RANGE 5, because 1.5 + 1.5 + 4/3 is below it");
    expect(std::abs(localMeleeRange(kLocalDefaultCombatReach, 3.f) - (1.5f + 3.f + 4.f / 3.f)) < 1e-5f,
           "a reach-3 creature raises it to 5.8333");
    expect(std::abs(localMeleeRange(kLocalDefaultCombatReach, 27.f) - (1.5f + 27.f + 4.f / 3.f)) < 1e-5f,
           "the catalog's largest creature melees at 29.8333");
    // Unit::IsWithinBoundaryRadius, Unit.cpp:820-828: floored at MIN_MELEE_REACH.
    expect(localWithinBoundaryRadius(3.99f, 0.389f) && !localWithinBoundaryRadius(4.01f, 0.389f),
           "the facing escape is 2 yd for a small creature");
    expect(localWithinBoundaryRadius(24.9f, 5.f) && !localWithinBoundaryRadius(25.1f, 5.f),
           "and its own bounding radius for a large one");
    // Position::HasInArc(M_PI): the half-plane, inclusive at +-pi/2.
    expect(localHasInArcPi(0, 0, 0, 10, 0) && !localHasInArcPi(0, 0, 0, -10, 0) &&
           localHasInArcPi(0, 0, 0, 0, 10) && localHasInArcPi(0, 0, 0, 0, -10) && localHasInArcPi(0, 0, 0, 0, 0),
           "HasInArc(pi) is the inclusive half-plane and a co-located target is always in arc");
    // Spell::CheckRange on a real 30 yd row, against three reaches.
    LocalGameplay dummy; LocalWorldContent content;
    LocalRealmPlayer p = rewardPlayer(1); p.x = p.y = p.z = 0; p.level = 80;
    auto spell = real(116); // Frostbolt rank 1, 30 yd, SPELL_RANGE_DEFAULT
    expect(spell.range == 30.f && spell.sourceRangeFlags == 0 && spell.minRange == 0,
           "Frostbolt rank 1 is a 30 yd default-range row");
    const auto at = [&](const LocalSpellDefinition& d, float distance, float reach, bool strict) {
        return localSpellTargetInRange(p, content, d, p.mapId, p.instanceId, distance, 0, 0, reach, strict); };
    expect(at(spell, 31.8f, kLocalWorldObjectSize, true) && !at(spell, 32.0f, kLocalWorldObjectSize, true),
           "a 30 yd cast at a 0.389-reach creature reaches 31.889 yd at cast start");
    expect(at(spell, 34.4f, 3.f, true) && !at(spell, 34.6f, 3.f, true),
           "the same cast reaches 34.5 yd at a reach-3 creature");
    expect(!at(spell, 32.0f, kLocalWorldObjectSize, true) && at(spell, 32.0f, kLocalWorldObjectSize, false),
           "min(3, 10 %) at completion carries the 0.389-reach case from 31.889 to 34.889");
    expect(at(spell, 34.8f, kLocalWorldObjectSize, false) && !at(spell, 35.0f, kLocalWorldObjectSize, false),
           "and no further");
    expect(at(spell, 37.4f, 3.f, false) && !at(spell, 37.6f, 3.f, false),
           "a reach-3 creature completes at 37.5 yd");
    // A melee row: max_range 5, real_max 5 - 2 * MIN_MELEE_REACH = 1.
    auto melee = spell; melee.range = 5; melee.sourceRangeFlags = 1;
    expect(at(melee, 5.9f, kLocalDefaultCombatReach, true) && !at(melee, 6.1f, kLocalDefaultCombatReach, true),
           "a 5 yd melee row is castable at 6 yd against a default-reach unit");
    expect(at(melee, 6.8f, 3.f, true) && !at(melee, 6.9f, 3.f, true),
           "and at 1 + max(5, 1.5 + 3 + 4/3) = 6.8333 against a reach-3 creature");
    expect(at(melee, 6.1f, kLocalDefaultCombatReach, false) == at(melee, 6.1f, kLocalDefaultCombatReach, true),
           "the completion leniency does not apply to a melee row (Spell.cpp:7330)");
    // A Self Only row returns SPELL_CAST_OK before anything is measured.
    auto selfOnly = spell; selfOnly.range = 0;
    expect(at(selfOnly, 1000.f, kLocalDefaultCombatReach, true), "a Self Only row is never out of range");
    // The SPELL_RANGE_RANGED minimum: min_range + GetMeleeRange, a <= test.
    const auto& shot = real(75);
    expect(shot.minRange == 0.f && shot.range == 35.f, "Auto Shot carries the client's own 0 minimum and 35 yd maximum");
    expect(localSpellTargetTooClose(shot, 24.9f, kLocalDefaultCombatReach) && !localSpellTargetTooClose(shot, 25.1f, kLocalDefaultCombatReach),
           "Auto Shot is too close inside 5 yd of a default-reach creature");
    expect(localSpellTargetTooClose(shot, 34.f, 3.f) && !localSpellTargetTooClose(shot, 35.f, 3.f),
           "and inside 5.8333 yd of a reach-3 one");
    std::cout << "PASS geometry: GetMeleeRange is max(reachA + reachB + 4/3, 5) - 5 for two default units, 5.8333 against a "
                 "reach-3 creature, 29.8333 against the catalog's reach-27 one; a 30 yd row reaches 31.889 yd at a 0.389-reach "
                 "creature and 34.5 at a reach-3 one at cast start, 34.889 and 37.5 at completion; a 5 yd melee row reaches 6 and "
                 "6.8333 and takes no completion leniency; a Self Only row is never out of range; Auto Shot's minimum is "
                 "0 + GetMeleeRange, 5 yd and 5.8333 yd; HasInArc(pi) is the inclusive half-plane and the facing escape is "
                 "max(bounding radius, MIN_MELEE_REACH)\n";
}

// ---------------------------------------------------------------------------
// 4. The same rules through the shipped authority, on real catalog creatures.
// ---------------------------------------------------------------------------
uint32_t hostileWithReach(float low, float high, uint32_t type = 0) {
    for (auto e : gHostile) {
        const auto& d = creature(e);
        const float reach = localCreatureCombatReach(&d);
        if (reach < low || reach > high) continue;
        if (type && localNpcCreatureType(e) != type) continue;
        if (d.immuneSchoolMask || d.immuneMechanicsMask || d.resistances != std::array<uint16_t, 6>{}) continue;
        return e;
    }
    std::cerr << "FAIL: no plain hostile creature with reach in [" << low << ", " << high << "] and type " << type << "\n";
    std::abort();
}
void rangeRuntime() {
    const auto smallEntry = hostileWithReach(0.f, 0.4f), largeEntry = hostileWithReach(7.9f, 8.1f);
    auto small = creature(smallEntry), large = creature(largeEntry);
    small.level = large.level = 80;
    const float smallReach = localCreatureCombatReach(&small), largeReach = localCreatureCombatReach(&large);
    // 35 yd is beyond 30 + 1.5 + smallReach and inside 30 + 1.5 + largeReach.
    World w;
    buildWorld(w, {116}, {{8, LocalResourceType::Mana, 80}}, {{small, 35.f}, {large, 35.f}});
    auto& mage = w.casters[0];
    const auto atSmall = castOnce(w, mage, 116, w.npcGuids[0]);
    const auto atLarge = castOnce(w, mage, 116, w.npcGuids[1]);
    expect(!atSmall.executed && atSmall.result == "Spell target out of effective range",
           "a 30 yd cast at 35 yd is refused against a reach-" + std::to_string(smallReach) + " creature");
    expect(atLarge.executed, "and accepted at the same distance against a reach-" + std::to_string(largeReach) + " one");
    // The completion leniency, driven through a real timed cast: the creature
    // steps out of cast-start range while the bar runs and the cast still lands.
    World timed;
    buildWorld(timed, {116}, {{8, LocalResourceType::Mana, 80}}, {{small, 31.f}});
    auto& caster = timed.casters[0];
    caster.globalCooldownMs = 0; caster.mana = caster.maxMana;
    std::string started;
    const bool startedOk = timed.game.execute(caster, {LocalAction::CastSpell, timed.npcGuid, 116}, timed.players, started);
    expect(startedOk && caster.castStatus == LocalCastStatus::Casting, "the 30 yd cast starts at 31 yd against the small creature");
    placeNpc(timed, timed.npcGuid, 32.5f);
    for (unsigned t = 0; t < 200 && caster.castStatus == LocalCastStatus::Casting; ++t) tickPinned(timed, .05f);
    expect(caster.castStatus == LocalCastStatus::Finished,
           "and completes at 32.5 yd, inside the min(3, 10 %) completion allowance and outside the cast-start maximum");
    // The same step past the completion allowance fails.
    World tooFar;
    buildWorld(tooFar, {116}, {{8, LocalResourceType::Mana, 80}}, {{small, 31.f}});
    auto& second = tooFar.casters[0];
    second.globalCooldownMs = 0; second.mana = second.maxMana;
    std::string ignored;
    assert(tooFar.game.execute(second, {LocalAction::CastSpell, tooFar.npcGuid, 116}, tooFar.players, ignored));
    placeNpc(tooFar, tooFar.npcGuid, 36.f);
    for (unsigned t = 0; t < 200 && second.castStatus == LocalCastStatus::Casting; ++t) tickPinned(tooFar, .05f);
    expect(second.castStatus == LocalCastStatus::Failed, "a step to 36 yd fails the completion check");
    // The melee swing: a player at 4.7 yd from a default-reach creature swings,
    // where the fixed 4.5 yd of the implementation did not.
    auto swingTarget = creature(hostileWithReach(1.4f, 1.6f)); swingTarget.level = 80; swingTarget.damage = 0;
    World swing;
    buildWorld(swing, {}, {{1, LocalResourceType::Rage, 80}}, {{swingTarget, 4.7f}});
    auto& warrior = swing.casters[0];
    warrior.attackTarget = swing.npcGuid; warrior.attackTimer = 0; warrior.offHandTimer = 0;
    const auto before = lastSequence(swing.game);
    tickPinned(swing, .05f);
    unsigned swings = 0;
    for (const auto& e : swing.game.combatEvents())
        if (e.sequence > before && e.kind == LocalCombatEventKind::PlayerMelee && e.target == swing.npcGuid) ++swings;
    expect(swings > 0, "a player swings at 4.7 yd against a reach-1.5 creature, which max(5, 1.5 + 1.5 + 4/3) allows");
    std::cout << "PASS range runtime: a 30 yd Frostbolt at 35 yd is refused against " << small.name << " (entry " << smallEntry
              << ", reach " << smallReach << ") and lands against " << large.name << " (entry " << largeEntry << ", reach "
              << largeReach << ") at the same distance; a cast started at 31 yd completes after the creature steps to 32.5 yd "
              << "and fails when it steps to 36; a level-80 player swings at 4.7 yd against a reach-1.5 creature, which the "
                 "fixed 4.5 yd of the reference refused\n";
}

// ---------------------------------------------------------------------------
// 5. Facing: a flagged spell refused behind the caster, allowed in arc, and
//    allowed behind inside the target's boundary radius; the 80 spells that
//    were already gated behave as they did.
// ---------------------------------------------------------------------------
void facingRuntime() {
    const auto& frostbolt = real(116);
    expect(localSpellRequiresFacing(frostbolt) && gFlaggedNewlyGated.count(116),
           "Frostbolt carries FacingCasterFlags and was one of the 271 ungated spells");
    auto target = creature(hostileWithReach(1.4f, 1.6f)); target.level = 80;
    World w;
    buildWorld(w, {116}, {{8, LocalResourceType::Mana, 80}}, {{target, 10.f}});
    auto& mage = w.casters[0];
    mage.orientation = 0;                                   // facing +x, target at +10
    const auto inArc = castOnce(w, mage, 116);
    expect(inArc.executed, "a flagged spell lands on a target in the caster's pi arc");
    mage.orientation = float(M_PI);                          // facing -x, target behind
    const auto behind = castOnce(w, mage, 116);
    expect(!behind.executed && behind.result == "Face the enemy",
           "and is refused with the reference's SPELL_FAILED_UNIT_NOT_INFRONT when the target is behind");
    // The inclusive border of Position.cpp:180 is asserted exactly in geometry()
    // above; a runtime orientation of float(pi/2) has cos below zero, so it is
    // not the border but just outside it, in this build and in the reference.
    // The IsWithinBoundaryRadius escape: the same facing, inside 2 yd.
    placeNpc(w, w.npcGuid, 1.5f);
    mage.orientation = float(M_PI);
    const auto inside = castOnce(w, mage, 116);
    expect(inside.executed, "a target behind the caster but inside its boundary radius is still a legal facing");
    placeNpc(w, w.npcGuid, 3.f);
    const auto outside = castOnce(w, mage, 116);
    expect(!outside.executed, "and is refused again just outside it");
    // A creature with a bounding radius above MIN_MELEE_REACH widens the escape.
    uint32_t wideEntry = 0;
    for (auto e : gHostile) {
        const auto& d = creature(e);
        if (localCreatureBoundingRadius(&d) > 4.f && !d.immuneSchoolMask && !d.immuneMechanicsMask &&
            d.resistances == std::array<uint16_t, 6>{}) { wideEntry = e; break; }
    }
    float wideRadius = 0;
    if (expect(wideEntry != 0, "the catalog holds a plain hostile creature with a bounding radius above 4")) {
        auto wide = creature(wideEntry); wide.level = 80;
        wideRadius = localCreatureBoundingRadius(&wide);
        World big;
        buildWorld(big, {116}, {{8, LocalResourceType::Mana, 80}}, {{wide, wideRadius - 0.5f}});
        auto& caster = big.casters[0];
        caster.orientation = float(M_PI);
        expect(castOnce(big, caster, 116).executed,
               "a target behind the caster but inside its own larger bounding radius is a legal facing");
        placeNpc(big, big.npcGuid, wideRadius + 0.5f);
        expect(!castOnce(big, caster, 116).executed, "and outside it the facing is refused");
    }
    // The 80 already-gated spells keep the gate they had: Sinister Strike is
    // refused behind and lands in front, through localComboFacingReady.
    auto rogueTarget = creature(hostileWithReach(1.4f, 1.6f)); rogueTarget.level = 80;
    World combo;
    buildWorld(combo, {1752}, {{4, LocalResourceType::Energy, 80}},
               {{rogueTarget, 3.f}}, {item(2092, "Worn Dagger", 13)});
    auto& rogue = combo.casters[0];
    rogue.equipment[15] = 2092; rogue.inventory.push_back({2092, 1});
    rogue.orientation = 0;
    expect(castOnce(combo, rogue, 1752).executed, "Sinister Strike still lands in front");
    rogue.orientation = float(M_PI);
    const auto comboBehind = castOnce(combo, rogue, 1752);
    expect(!comboBehind.executed && comboBehind.result == "Face the enemy",
           "and is still refused behind, by the gate it has had since the reference");
    std::cout << "PASS facing: " << gFlaggedNewlyGated.size() << " spells that carry FacingCasterFlags & 1 are gated for the "
                 "first time and the " << gFlaggedAlreadyGated.size() << " that were already gated are unchanged; Frostbolt "
                 "lands in front, is refused behind, and is allowed behind inside the target's "
                 "boundary radius (2 yd for a small creature, " << wideRadius << " yd for entry " << wideEntry
              << ") and refused just outside it; Sinister Strike keeps localComboFacingReady\n";
}

// ---------------------------------------------------------------------------
// 6. TargetCreatureType and ONLY_PEACEFUL_TARGETS.
// ---------------------------------------------------------------------------
void creatureTypeRuntime() {
    const auto beastEntry = hostileWithReach(0.f, 100.f, 1), humanoidEntry = hostileWithReach(0.f, 100.f, 7),
               undeadEntry = hostileWithReach(0.f, 100.f, 6);
    auto beast = creature(beastEntry), humanoid = creature(humanoidEntry), undead = creature(undeadEntry);
    beast.level = humanoid.level = undead.level = 80;
    World w;
    buildWorld(w, {2637, 9484}, {{11, LocalResourceType::Mana, 80}, {5, LocalResourceType::Mana, 80}},
               {{beast, 8.f}, {humanoid, 8.f}, {undead, 8.f}});
    auto& druid = w.casters[0];
    auto& priest = w.casters[1];
    druid.knownSpells = {1, 2637}; priest.knownSpells = {1, 9484};
    const auto hibernateBeast = castOnce(w, druid, 2637, w.npcGuids[0]);
    const auto hibernateHumanoid = castOnce(w, druid, 2637, w.npcGuids[1]);
    expect(hibernateBeast.executed, "Hibernate lands on a beast");
    expect(!hibernateHumanoid.executed && hibernateHumanoid.result.find("not a valid creature type") != std::string::npos,
           "and is refused on a humanoid, where it landed on 2,600 hostile templates before the reference");
    const auto shackleUndead = castOnce(w, priest, 9484, w.npcGuids[2]);
    const auto shackleBeast = castOnce(w, priest, 9484, w.npcGuids[0]);
    expect(shackleUndead.executed, "Shackle Undead lands on an undead");
    expect(!shackleBeast.executed && shackleBeast.result.find("not a valid creature type") != std::string::npos,
           "and is refused on a beast, where it landed on 2,990 hostile templates before the reference");
    // A creature whose template has no type (the reference's !creatureType arm)
    // is a legal target of any TargetCreatureType.
    expect(localSpellCreatureTypeAllowed(real(2637), 0),
           "a template with creature_template.type 0 is allowed, as SpellInfo.cpp:1919 says");
    // ONLY_PEACEFUL_TARGETS: Sap on a creature that holds a threat entry.
    World peaceful;
    buildWorld(peaceful, {11297}, {{4, LocalResourceType::Energy, 80}}, {{humanoid, 4.f}});
    auto& rogue = peaceful.casters[0];
    auto npcs = peaceful.game.npcs();
    npcs[0].threat[0] = {rogue.guid, 1000}; npcs[0].targetGuid = rogue.guid;
    peaceful.game.setRemoteNpcs(npcs);
    const auto engaged = castOnce(peaceful, rogue, 11297);
    expect(!engaged.executed, "Sap is refused on a creature that is already in combat");
    expect(engaged.result.find("already in combat") != std::string::npos ||
           engaged.result.find("form or stance") != std::string::npos,
           "either by ONLY_PEACEFUL_TARGETS or by the Stealth form gate that precedes it");
    std::cout << "PASS creature type: Hibernate lands on " << beast.name << " (entry " << beastEntry
              << ", beast) and is refused on " << humanoid.name << " (entry " << humanoidEntry
              << ", humanoid); Shackle Undead lands on " << undead.name << " (entry " << undeadEntry
              << ", undead) and is refused on the beast; a template with type 0 is legal for either, as the reference's "
                 "!creatureType arm says; Sap on an engaged creature is refused (" << engaged.result << ")\n";
}

// ---------------------------------------------------------------------------
// 7. The spell hit rating on the cast roll.
// ---------------------------------------------------------------------------
// The best real spell-hit item the ranged catalog offers for each slot, taken
// from the shipped generated tables and the shipped melee item table.
struct GearSlot { LocalItemDefinition item; uint32_t slot; int32_t rating; };
std::vector<GearSlot> bestSpellHitGear() {
    std::vector<GearSlot> gear;
    for (size_t slot = 0; slot < 19; ++slot) {
        const local_ranged_detail::Item* best = nullptr;
        const LocalMeleeItem* bestMelee = nullptr;
        for (const auto& i : local_ranged_detail::items) {
            if (i.spellHit <= 0 || (best && i.spellHit <= best->spellHit)) continue;
            const auto* melee = localMeleeItem(i.id);
            if (!melee || melee->scaling || !localEquipmentFits(melee->inventoryType, 0, slot)) continue;
            best = &i; bestMelee = melee;
        }
        if (best) gear.push_back({item(best->id, "Spell hit item", bestMelee->inventoryType), uint32_t(slot), best->spellHit});
    }
    std::sort(gear.begin(), gear.end(), [](const auto& a, const auto& b) { return a.rating < b.rating; });
    return gear;
}
void spellHitRating() {
    const auto gear = bestSpellHitGear();
    expect(!gear.empty(), "the shipped item table offers real spell hit rating");
    std::vector<LocalItemDefinition> items;
    for (const auto& g : gear) items.push_back(g.item);
    auto target = creature(hostileWithReach(1.4f, 1.6f)); target.level = 83; target.damage = 0;
    World w;
    buildWorld(w, {133}, {{8, LocalResourceType::Mana, 80}, {8, LocalResourceType::Mana, 80}, {8, LocalResourceType::Mana, 80}},
               {{target, 8.f}}, items);
    auto& bare = w.casters[0];
    auto& partial = w.casters[1];
    auto& full = w.casters[2];
    // A moderate caster: the smallest prefix of the real gear whose converted
    // percentage reaches 8, so the band the reference predicts is a band and
    // not the 100 % clamp.
    int64_t partialRating = 0;
    for (const auto& g : gear) {
        if (localSpellHitRatingPercent(partial, *w.content) >= 8.f) break;
        partial.equipment[g.slot] = g.item.id; partial.inventory.push_back({g.item.id, 1});
        partialRating += g.rating;
    }
    int64_t fullRating = 0;
    for (const auto& g : gear) { full.equipment[g.slot] = g.item.id; full.inventory.push_back({g.item.id, 1}); fullRating += g.rating; }
    w.game.tick(0, w.players);
    const float barePct = localSpellHitRatingPercent(bare, *w.content);
    const float partialPct = localSpellHitRatingPercent(partial, *w.content);
    const float fullPct = localSpellHitRatingPercent(full, *w.content);
    expect(barePct == 0, "a caster with no gear has no spell hit rating");
    expect(partialPct >= 8.f && partialPct < 17.f, "the moderate caster's rating converts to a percentage inside the level table's own 17");
    expect(fullPct > 17.f, "and the fully geared one's is past it");
    // The reference's own numbers for this pairing: base hit 94 - (3 - 2) * 11
    // against a creature three levels above (Object.cpp:3574-3580), then
    // HitChance = modHitChance * 100 + int32(m_modSpellHitChance * 100), clamped
    // to 1..100 % (:3605-3621), and the roll is irand(1, 10000) < 10000 - HitChance.
    const double baseHit = 94 - (3 - 2) * 11;
    const double bareMiss = 100.0 - baseHit;
    const auto missPercent = [&](float pct) {
        const double hit = std::min(100.0, baseHit + double(int32_t(pct * 100.f)) / 100.0);
        return std::max(0.0, 100.0 - hit); };
    constexpr unsigned kCasts = 4000;
    const auto drive = [&](LocalRealmPlayer& p) {
        unsigned misses = 0;
        for (unsigned i = 0; i < kCasts; ++i) {
            const auto r = castOnce(w, p, 133);
            if (!r.executed) { std::cerr << "FAIL: Fireball refused at cast " << i << ": " << r.result << "\n"; std::abort(); }
            if (r.event && r.event->outcome == LocalMeleeOutcome::Miss) ++misses;
        }
        return misses;
    };
    const auto bareMisses = drive(bare), partialMisses = drive(partial), fullMisses = drive(full);
    const auto bareBand = band(kCasts, bareMiss), partialBand = band(kCasts, missPercent(partialPct));
    expect(within(bareMisses, bareBand),
           "a rating-less level-80 caster misses at the level table's 17 %: " + std::to_string(bareMisses) +
               " of " + std::to_string(kCasts) + " in [" + std::to_string(bareBand.low) + "," + std::to_string(bareBand.high) + "]");
    expect(within(partialMisses, partialBand),
           "a caster with " + std::to_string(partialRating) + " spell hit rating misses at " +
               std::to_string(missPercent(partialPct)) + " %: " + std::to_string(partialMisses) + " in [" +
               std::to_string(partialBand.low) + "," + std::to_string(partialBand.high) + "]");
    expect(partialMisses < bareMisses && !within(partialMisses, bareBand),
           "measurably less often than the rating-less caster, outside the band the rating-less caster occupies");
    expect(!within(bareMisses, partialBand), "and the two bands do not overlap on these counts");
    expect(fullMisses == 0, "a caster whose rating carries the hit chance past 100 % never misses, as the reference's clamp says");
    // The level table itself is unchanged where the rating is zero: an even-level
    // creature is 96 - 0 and a creature twenty levels below clamps to 100 %.
    auto even = creature(hostileWithReach(1.4f, 1.6f)); even.level = 80; even.damage = 0;
    World table;
    buildWorld(table, {133}, {{8, LocalResourceType::Mana, 80}}, {{even, 8.f}});
    auto& plain = table.casters[0];
    unsigned evenMisses = 0;
    for (unsigned i = 0; i < kCasts; ++i) {
        const auto r = castOnce(table, plain, 133);
        assert(r.executed);
        if (r.event && r.event->outcome == LocalMeleeOutcome::Miss) ++evenMisses;
    }
    const auto evenBand = band(kCasts, 4.0);
    expect(within(evenMisses, evenBand),
           "the level table is untouched at zero rating: " + std::to_string(evenMisses) + " misses of " +
               std::to_string(kCasts) + " at even level, band [" + std::to_string(evenBand.low) + "," + std::to_string(evenBand.high) + "]");
    auto low = creature(hostileWithReach(1.4f, 1.6f)); low.level = 60; low.damage = 0;
    World clamped;
    buildWorld(clamped, {133}, {{8, LocalResourceType::Mana, 80}}, {{low, 8.f}});
    auto& against60 = clamped.casters[0];
    unsigned lowMisses = 0;
    for (unsigned i = 0; i < 500; ++i) {
        const auto r = castOnce(clamped, against60, 133);
        assert(r.executed);
        if (r.event && r.event->outcome == LocalMeleeOutcome::Miss) ++lowMisses;
    }
    expect(lowMisses == 0, "and still clamps to 100 % hit twenty levels down");
    std::cout << "PASS spell hit rating: the shipped item table offers " << gear.size() << " slots of real spell hit rating, "
              << fullRating << " in total (" << fullPct << " %); against a level-83 creature, where the reference's level table "
                 "gives 83 % hit, the rating-less caster missed " << bareMisses << " of " << kCasts << " Fireballs (band ["
              << bareBand.low << "," << bareBand.high << "] for 17 %), a caster with " << partialRating << " rating ("
              << partialPct << " %) missed " << partialMisses << " (band [" << partialBand.low << "," << partialBand.high
              << "] for " << missPercent(partialPct) << " %, disjoint from the rating-less band) and the fully geared one "
              << fullMisses << "; with no rating the level table is unchanged - " << evenMisses << " misses of " << kCasts
              << " at even level (band [" << evenBand.low << "," << evenBand.high << "]) and " << lowMisses
              << " of 500 twenty levels down\n";
}

// ---------------------------------------------------------------------------
// 8. Save 30 / LAN 85 (both moved at the implementation for the pet roster, and nothing
//    this group measures moved with them); the fingerprint moves with the new
//    fields.
// ---------------------------------------------------------------------------
void formats() {
    assert(SaveVersion == 30 && lan::GameplayVersion == 85 && Version == 85);
    static_assert(CastWireBytes == 222);
    static_assert(NpcWireBytes == 618);
    // The four new definition columns all enter the content fingerprint, so a
    // peer whose importer disagrees about any of them is refused at the join.
    const auto fingerprintOf = [](std::vector<LocalSpellDefinition> spells) {
        LocalGameplay game; auto c = std::make_shared<LocalWorldContent>(); game.useContent(c);
        std::string error; const bool ok = game.setStarterSpells(spells, "", error);
        assert(ok);
        return c->fingerprint; };
    std::vector<LocalSpellDefinition> spells;
    for (const auto& d : gImported.spells) if (d.clientSpell && d.allowableClasses) spells.push_back(d);
    const auto before = fingerprintOf(spells), again = fingerprintOf(spells);
    auto a = spells; for (auto& d : a) if (d.id == 116) d.sourceFacingFlags = 0;
    auto b = spells; for (auto& d : b) if (d.id == 2637) d.targetCreatureType = 0;
    auto c = spells; for (auto& d : c) if (d.id == 11297) d.sourceOnlyPeacefulTargets = false;
    auto e = spells; for (auto& d : e) if (d.id == 116) d.sourceRangeFlags = 1;
    const auto fa = fingerprintOf(a), fb = fingerprintOf(b), fc = fingerprintOf(c), fe = fingerprintOf(e);
    expect(before == again, "the fingerprint is stable");
    expect(before != fa && before != fb && before != fc && before != fe && fa != fe,
           "and moves with FacingCasterFlags, TargetCreatureType, ONLY_PEACEFUL_TARGETS and the range type");
    // A creature definition's reach and bounding radius are catalog data, not
    // wire data: an npc with a reach encodes to the same 618 bytes.
    LocalWorldContent content; LocalNpcDefinition definition;
    definition.id = 50; definition.name = "Codec NPC"; definition.displayId = 100;
    definition.combatReach = 8.5f; definition.boundingRadius = 4.25f; content.npcs.push_back(definition);
    LocalRealmNpc n; n.guid = 0xf130000000000001ULL; n.entry = 50; n.level = 20; n.health = 80; n.maxHealth = 100; n.targetGuid = 2;
    n.playerThreat.viewerGuid = 1; n.playerThreat.amount = 5; n.playerThreat.present = true;
    n.playerThreat.rawBasisPoints = 5000; n.playerThreat.scaledBasisPoints = 4545; n.playerThreat.status = 1;
    Writer w; writeNpc(w, n);
    expect(w.bytes.size() <= NpcWireBytes, "an npc with a combat reach still fits the unchanged " + std::to_string(NpcWireBytes) + "-byte bound");
    Reader r(w.bytes.data(), w.bytes.size());
    const auto back = readNpc(r, content);
    expect(r.valid && r.done() && back.entry == 50 && back.guid == n.guid && back.playerThreat.amount == 5,
           "and round-trips with no field for reach, radius or creature type");
    // The catalog fingerprint is what refuses a the implementation peer, and it moved.
    expect(gCatalogFingerprint != kCatalogFingerprint0246, "the catalog fingerprint moved with the rebuilt npcs.pack");
    std::cout << "PASS formats: SaveVersion 29, GameplayVersion 84, NpcWireBytes " << NpcWireBytes << ", CastWireBytes "
              << CastWireBytes << " unchanged - combat reach, bounding radius and creature type are definition data on both "
                 "peers and nothing replicated changed shape; the content fingerprint moves with each of the four new spell "
                 "columns, and the catalog fingerprint " << kCatalogFingerprint0246 << " -> " << gCatalogFingerprint
              << " refuses a incompatible peer at the join check before the protocol number is compared\n";
}
} // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    assert(argc == 3);
    ClientTables tables; tables.load(argv[1]); gTables = &tables;
    gImported = tables.import();
    loadCatalog(argv[2]);
    catalogCensus(argv[2]);
    clientCensus();
    geometry();
    rangeRuntime();
    facingRuntime();
    creatureTypeRuntime();
    spellHitRating();
    formats();
    if (gFailures) { std::cerr << gFailures << " group(s) failed against source-backed fixtures\n"; return 1; }
    return 0;
}
