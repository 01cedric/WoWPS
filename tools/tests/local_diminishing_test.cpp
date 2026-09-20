// P04 / C2 - diminishing returns: the group classifier, the standard ladder,
// the DRTYPE gate, the immune outcome, the per-target record, the stack and the
// fifteen-second window.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33, as measured
// in the source audit sections 1, 4, 5, 6 and 8 and
// transcribed in the source audit:
//   GetDiminishingReturnsGroupForSpell  SpellMgr.cpp:107-292 - the positive
//       gate, the taunt short-circuit, the explicit family branches and the
//       mechanic tail. MECHANIC_* numbering is SharedDefines.h:1313-1348.
//   GetDiminishingReturnsGroupType      SpellMgr.cpp:294-311 - TAUNT,
//       CONTROLLED_STUN, STUN, OPENING_STUN, CYCLONE and CHARGE are DRTYPE_ALL;
//       LIMITONLY and NONE are DRTYPE_NONE; everything else is DRTYPE_PLAYER.
//   Spell::DoSpellHitOnUnit             Spell.cpp:3195-3216 - :3207 tests the
//       TARGET, :3211 tests the CASTER. Transposing them inverts this build.
//   Unit::GetDiminishing                Unit.cpp:11292-11316 - the read, which
//       mutates on expiry, is gated on stack == 0 and compares strictly > 15000.
//   Unit::IncrDiminishing               Unit.cpp:11319-11331 - a NEW record
//       seeds at DIMINISHING_LEVEL_2, and the existing branch never touches
//       hitTime.
//   Unit::ApplyDiminishingToDuration    Unit.cpp:11333-11410 - two ladders,
//       1.0/0.5/0.25/0.0 for everything and 1.0/0.65/0.4225/0.274625/0.0 for
//       taunt, plus the PvP clamp neither this realm's targets nor its spell
//       durations can reach.
//   Unit::ApplyDiminishingAura          Unit.cpp:11412-11430 - apply raises the
//       stack, unapply lowers it and stamps hitTime at zero.
//   Spell.cpp:3275-3287 - a zero multiplier is SPELL_MISS_IMMUNE, not a shorter
//       control.
//
// Every census number is measured by running the shipped importer over the
// player's own Spell.dbc and classifying the result with the shipped
// classifier. Every runtime number is produced by the shipped LocalGameplay
// tick driving real imported definitions, so each duration, mechanic and
// interrupt flag is the client's own value and not a fixture's.
#include "../../src/game/local_realm.cpp"
#include "local_group_rewards_fixture.hpp"
#include "game/local_diminishing.hpp"
#include "game/local_npc_auras.hpp"
#include "game/local_forms.hpp"
#include "game/local_proc_rules.hpp"
#include "game/local_spell_import.hpp"
#include "game/protocol_constants.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace {
using namespace wowee;
using namespace wowee::game;
namespace fs = std::filesystem;

// Two groups below assert against the reference and are expected to hold; when
// one does not, the suite must still run the rest so the whole picture is
// reported, and must still exit non-zero. `expect` is exactly `assert` with the
// abort deferred to the end of main - nothing is relaxed by it.
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
                                 "Talent", "TalentTab", "SpellMechanic"}) {
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

// the source audit section 4: of the 982 ids the
// importer accepts, exactly 18 map to a non-DIMINISHING_NONE group, 8 of them
// DRTYPE_ALL and 10 DRTYPE_PLAYER.
constexpr uint32_t kControlledStun[] = {853, 5211, 5588, 5589, 6798, 8983, 10308, 12355};
constexpr uint32_t kSleep[]     = {2637, 18657, 18658};
constexpr uint32_t kDisorient[] = {9484, 9485, 10955, 11297, 20066, 51724};
constexpr uint32_t kSilence[]   = {15487};

const char* groupName(LocalDiminishingGroup g) {
    switch (g) {
        case LocalDiminishingGroup::None: return "NONE";
        case LocalDiminishingGroup::Banish: return "BANISH";
        case LocalDiminishingGroup::Charge: return "CHARGE";
        case LocalDiminishingGroup::OpeningStun: return "OPENING_STUN";
        case LocalDiminishingGroup::ControlledStun: return "CONTROLLED_STUN";
        case LocalDiminishingGroup::ControlledRoot: return "CONTROLLED_ROOT";
        case LocalDiminishingGroup::Cyclone: return "CYCLONE";
        case LocalDiminishingGroup::Disarm: return "DISARM";
        case LocalDiminishingGroup::Disorient: return "DISORIENT";
        case LocalDiminishingGroup::Entrapment: return "ENTRAPMENT";
        case LocalDiminishingGroup::Fear: return "FEAR";
        case LocalDiminishingGroup::Horror: return "HORROR";
        case LocalDiminishingGroup::MindControl: return "MIND_CONTROL";
        case LocalDiminishingGroup::Root: return "ROOT";
        case LocalDiminishingGroup::Stun: return "STUN";
        case LocalDiminishingGroup::ScatterShot: return "SCATTER_SHOT";
        case LocalDiminishingGroup::Silence: return "SILENCE";
        case LocalDiminishingGroup::Sleep: return "SLEEP";
        case LocalDiminishingGroup::Taunt: return "TAUNT";
        case LocalDiminishingGroup::LimitOnly: return "LIMITONLY";
        case LocalDiminishingGroup::DragonsBreath: return "DRAGONS_BREATH";
    }
    return "?";
}
const char* typeName(LocalDiminishingType t) {
    return t == LocalDiminishingType::All ? "DRTYPE_ALL"
         : t == LocalDiminishingType::Player ? "DRTYPE_PLAYER" : "DRTYPE_NONE";
}
bool listed(const uint32_t* first, const uint32_t* last, uint32_t id) {
    return std::find(first, last, id) != last;
}

// ---------------------------------------------------------------------------
// 1. The classification census, over the player's own Spell.dbc.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 1a. The mechanic tail is a pure table. Pin every id it tests directly against
//     SharedDefines.h:1313-1349, and pin the three rows a previous transcription
//     of this header actually named, so the same slip fails loudly rather than
//     quietly rerouting one spell into the wrong group.
// ---------------------------------------------------------------------------
void mechanicConstants() {
    struct Row { uint8_t value; const char* name; };
    // SharedDefines.h:1313-1349 at 9c416aaacb5537636abb13c80f55a88947838e33,
    // transcribed row by row: every constant the classifier's tail reads.
    const Row tested[] = {
        {kLocalMechanicCharm,    "MECHANIC_CHARM = 1"},
        {kLocalMechanicDisarm,   "MECHANIC_DISARM = 3"},
        {kLocalMechanicFear,     "MECHANIC_FEAR = 5"},
        {kLocalMechanicRoot,     "MECHANIC_ROOT = 7"},
        {kLocalMechanicSilenced, "MECHANIC_SILENCE = 9"},
        {kLocalMechanicSleep,    "MECHANIC_SLEEP = 10"},
        {kLocalMechanicStunned,  "MECHANIC_STUN = 12"},
        {kLocalMechanicKnockout, "MECHANIC_KNOCKOUT = 14"},
        {kLocalMechanicBleed,    "MECHANIC_BLEED = 15"},
        {kLocalMechanicPolymorph,"MECHANIC_POLYMORPH = 17"},
        {kLocalMechanicBanish,   "MECHANIC_BANISH = 18"},
        {kLocalMechanicShackle,  "MECHANIC_SHACKLE = 20"},
        {kLocalMechanicHorror,   "MECHANIC_HORROR = 24"},
        {kLocalMechanicSapped,   "MECHANIC_SAPPED = 30"},
    };
    const uint8_t expected[] = {1, 3, 5, 7, 9, 10, 12, 14, 15, 17, 18, 20, 24, 30};
    unsigned wrong = 0;
    std::string detail;
    for (size_t i = 0; i < std::size(tested); ++i)
        if (tested[i].value != expected[i]) {
            ++wrong;
            detail += std::string(detail.empty() ? "" : "; ") + tested[i].name + " is " +
                      std::to_string(unsigned(tested[i].value));
        }
    const bool exact = expect(wrong == 0,
        "diminishing mechanic constants: " + std::to_string(wrong) + " of the " +
        std::to_string(std::size(tested)) + " SpellMechanic ids the classifier's mechanic tail "
        "(include/game/local_diminishing.hpp, SpellMgr.cpp:265-291) tests do not match "
        "SharedDefines.h:1313-1349 - " + detail + ". A mistyped id here silently reroutes a spell "
        "into the wrong diminishing group; the knockout row alone decides whether 20066 Repentance "
        "reaches DIMINISHING_DISORIENT.");
    // The three rows an earlier draft of this header actually named. They are
    // real mechanics with real numbers, which is exactly why the slip was
    // invisible: naming them here makes the trap explicit.
    assert(kLocalMechanicFear != 4);      // 4 is MECHANIC_DISTRACT
    assert(kLocalMechanicKnockout != 16); // 16 is MECHANIC_BANDAGE
    assert(kLocalMechanicHorror != 29);   // 29 is MECHANIC_IMMUNE_SHIELD
    // And the numbering is a permutation of distinct rows, so a copy-paste that
    // duplicates one cannot pass the table above by accident.
    std::set<uint8_t> distinct;
    for (const auto& row : tested) distinct.insert(row.value);
    assert(distinct.size() == std::size(tested));
    if (exact)
        std::cout << "PASS mechanic constants: all " << std::size(tested)
                  << " SpellMechanic ids the classifier's tail reads match SharedDefines.h:1313-1349 "
                     "row for row (charm 1, disarm 3, fear 5, root 7, silence 9, sleep 10, stun 12, "
                     "knockout 14, bleed 15, polymorph 17, banish 18, shackle 20, horror 24, sapped "
                     "30), all fourteen are distinct, and none of them is 4 (MECHANIC_DISTRACT), 16 "
                     "(MECHANIC_BANDAGE) or 29 (MECHANIC_IMMUNE_SHIELD) - the three neighbouring rows "
                     "an earlier transcription of this header named instead\n";
}

// ---------------------------------------------------------------------------
// 1b. The classification census, over the player's own Spell.dbc.
// ---------------------------------------------------------------------------
void classificationCensus() {
    std::set<uint32_t> accepted;
    for (const auto& r : gImported.audit) if (r.status == "Supported decoder; imported") accepted.insert(r.id);
    // 982 at the implementation/the implementation; 990 since the implementation admitted the eight Shield Slam ranks
    // (effect 38 beside weapon damage); 1,004 since the implementation admitted the five form
    // boost spells and the nine entry-resource talent ranks (P06). None of the
    // eight, and none of the fourteen, classifies to a diminishing group: the
    // fourteen are passives with no mechanic at all.
    assert(accepted.size() == 1004);

    std::map<std::string, unsigned> byGroup, byType;
    std::map<uint32_t, LocalDiminishingGroup> classified;
    unsigned mismatched = 0;
    for (const auto& d : gImported.spells) {
        if (!accepted.count(d.id)) continue;
        const auto group = localDiminishingGroupForSpell(d, false);
        classified[d.id] = group;
        ++byGroup[groupName(group)];
        ++byType[typeName(localDiminishingGroupType(group))];

        const auto expectedGroup =
            listed(std::begin(kControlledStun), std::end(kControlledStun), d.id) ? LocalDiminishingGroup::ControlledStun :
            listed(std::begin(kSleep), std::end(kSleep), d.id) ? LocalDiminishingGroup::Sleep :
            listed(std::begin(kDisorient), std::end(kDisorient), d.id) ? LocalDiminishingGroup::Disorient :
            listed(std::begin(kSilence), std::end(kSilence), d.id) ? LocalDiminishingGroup::Silence :
            LocalDiminishingGroup::None;
        if (group != expectedGroup) {
            ++mismatched;
            std::cerr << "     " << d.id << " " << d.name << " (mechanic " << unsigned(d.mechanic)
                      << ", icon " << d.iconId << ", family " << d.spellFamily << ") classified "
                      << groupName(group) << "/" << typeName(localDiminishingGroupType(group))
                      << ", document section 4 says " << groupName(expectedGroup) << "/"
                      << typeName(localDiminishingGroupType(expectedGroup)) << "\n";
        }
    }
    // A stun applied by a proc lands in the shared STUN group rather than in
    // CONTROLLED_STUN; both are DRTYPE_ALL, so the ternary at SpellMgr.cpp:283
    // changes the record key and not the eligibility. Nothing in this build
    // passes `triggered` true - no proc applies a control - but the classifier
    // must still answer correctly, because the record key is what keeps a
    // proc-applied stun from sharing a ladder with a pressed one.
    for (auto id : kControlledStun) {
        assert(localDiminishingGroupForSpell(real(id), true) == LocalDiminishingGroup::Stun);
        assert(localDiminishingGroupType(LocalDiminishingGroup::Stun) == LocalDiminishingType::All);
    }
    // Neither DRTYPE_ALL group is gated by anything the target carries, and
    // both run to DIMINISHING_LEVEL_IMMUNE rather than the taunt ladder's rung 4.
    assert(localDiminishingMaxLevel(LocalDiminishingGroup::ControlledStun) == kLocalDiminishingLevelImmune);
    assert(localDiminishingMaxLevel(LocalDiminishingGroup::Taunt) == kLocalDiminishingLevelTauntImmune);
    // SpellMgr.cpp:294-311 in full, so the table is asserted and not only the
    // four groups this realm can currently reach.
    for (auto g : {LocalDiminishingGroup::Taunt, LocalDiminishingGroup::ControlledStun,
                   LocalDiminishingGroup::Stun, LocalDiminishingGroup::OpeningStun,
                   LocalDiminishingGroup::Cyclone, LocalDiminishingGroup::Charge})
        assert(localDiminishingGroupType(g) == LocalDiminishingType::All);
    for (auto g : {LocalDiminishingGroup::LimitOnly, LocalDiminishingGroup::None})
        assert(localDiminishingGroupType(g) == LocalDiminishingType::None);
    for (auto g : {LocalDiminishingGroup::Banish, LocalDiminishingGroup::ControlledRoot,
                   LocalDiminishingGroup::Disarm, LocalDiminishingGroup::Disorient,
                   LocalDiminishingGroup::Entrapment, LocalDiminishingGroup::Fear,
                   LocalDiminishingGroup::Horror, LocalDiminishingGroup::MindControl,
                   LocalDiminishingGroup::Root, LocalDiminishingGroup::ScatterShot,
                   LocalDiminishingGroup::Silence, LocalDiminishingGroup::Sleep,
                   LocalDiminishingGroup::DragonsBreath})
        assert(localDiminishingGroupType(g) == LocalDiminishingType::Player);

    const bool censusExact = expect(mismatched == 0,
        "classification census: " + std::to_string(mismatched) + " of the 1,004 accepted ids classify "
        "differently from source-backed fixtures section 4, which reproduces "
        "SpellMgr.cpp:107-292 against the same pinned Spell.dbc. The measured split is " +
        std::to_string(byType["DRTYPE_ALL"]) + " DRTYPE_ALL / " + std::to_string(byType["DRTYPE_PLAYER"]) +
        " DRTYPE_PLAYER / " + std::to_string(byType["DRTYPE_NONE"]) + " DRTYPE_NONE against the "
        "document's 8 / 10 / 964 (972 NONE since the reference added eight Shield Slam ranks). The document is right and the implementation is wrong: see the "
        "mechanic-constant failure above for the single cause and its fix.");
    const bool countsExact = expect(
        // 964 in the audit, 972 once the implementation admitted the eight Shield Slam ranks,
        // 986 once the implementation admitted the five form boost spells and the nine
        // entry-resource talent ranks: all fourteen are passives with no
        // mechanic on any effect, so every one classifies DRTYPE_NONE.
        byType["DRTYPE_ALL"] == 8 && byType["DRTYPE_PLAYER"] == 10 && byType["DRTYPE_NONE"] == 986,
        "classification totals: measured " + std::to_string(byType["DRTYPE_ALL"]) + " / " +
        std::to_string(byType["DRTYPE_PLAYER"]) + " / " + std::to_string(byType["DRTYPE_NONE"]) +
        " against the document's 8 DRTYPE_ALL / 10 DRTYPE_PLAYER / 964 DRTYPE_NONE, plus the eight Shield Slam ranks the reference admitted and the fourteen form passives the reference admitted (986 NONE).");

    // The eight are exactly the ids the document names, whatever else moved.
    std::set<uint32_t> measuredAll;
    for (const auto& [id, group] : classified)
        if (localDiminishingGroupType(group) == LocalDiminishingType::All) measuredAll.insert(id);
    const std::set<uint32_t> documentAll(std::begin(kControlledStun), std::end(kControlledStun));
    assert(measuredAll == documentAll);

    if (censusExact && countsExact)
        std::cout << "PASS classification census: all " << accepted.size()
                  << " audited-accepted ids classified by the shipped localDiminishingGroupForSpell "
                     "reproduce source-backed fixtures section 4 exactly - "
                  << byGroup["CONTROLLED_STUN"] << " CONTROLLED_STUN (853/5588/5589/10308 Hammer of "
                     "Justice, 5211/6798/8983 Bash, 12355 Impact), " << byGroup["SLEEP"]
                  << " SLEEP (2637/18657/18658 Hibernate), " << byGroup["DISORIENT"]
                  << " DISORIENT (9484/9485/10955 Shackle Undead, 11297/51724 Sap, 20066 Repentance), "
                  << byGroup["SILENCE"] << " SILENCE (15487) and " << byGroup["NONE"]
                  << " NONE - giving " << byType["DRTYPE_ALL"] << " DRTYPE_ALL, "
                  << byType["DRTYPE_PLAYER"] << " DRTYPE_PLAYER and " << byType["DRTYPE_NONE"]
                  << " DRTYPE_NONE; every one of the eight reclassifies to the shared STUN group when "
                     "applied by a proc, and GetDiminishingReturnsGroupType is asserted over all 21 "
                     "groups rather than the four this realm reaches\n";
}

// ---------------------------------------------------------------------------
// Runtime scaffolding. Every world is the shipped authority driving real
// imported definitions against the fixture's creature table.
// ---------------------------------------------------------------------------
std::shared_ptr<LocalWorldContent> drWorld(std::vector<uint32_t> ids, uint32_t damage = 0,
                                           uint32_t npcHealth = 100000000) {
    auto c = rewardContent();
    for (auto id : ids) c->spells.push_back(real(id));
    // A fixture instrument, used only to kill the creature in the clear-on-death
    // group. Nothing measured about diminishing returns comes from it.
    LocalSpellDefinition hammer; hammer.id = 990001; hammer.name = "Test executioner";
    hammer.damage = 100000000; hammer.range = 100; hammer.schoolMask = 1;
    c->spells.push_back(hammer);
    std::sort(c->spells.begin(), c->spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    auto& npc = c->npcs[0];
    npc.id = 50; npc.name = "Diminishing target"; npc.hostile = true;
    npc.health = npcHealth; npc.damage = damage; npc.level = 1; npc.armor = 0;
    npc.aggroRadius = 0.01f; npc.xp = 0; npc.money = 0; npc.loot.clear();
    return c;
}
LocalRealmPlayer drCaster(uint64_t guid, uint8_t classId, LocalResourceType resource = LocalResourceType::Mana) {
    auto p = rewardPlayer(guid);
    p.classId = classId; p.level = 80; p.health = p.maxHealth = 100000000;
    p.mana = p.maxMana = 1000000; p.resourceType = resource;
    p.x = p.y = p.z = 0; p.quests.clear();
    return p;
}
LocalRealmNpc drNpc(float x = 8, uint32_t health = 100000000) {
    auto n = rewardNpc();
    n.entry = 50; n.name = "Diminishing target"; n.level = 1;
    n.health = n.maxHealth = health; n.hostile = true; n.lootOwner = 0;
    n.x = n.homeX = x; n.y = n.homeY = 0; n.z = n.homeZ = 0;
    return n;
}
const LocalRealmNpc* findNpc(const LocalGameplay& game, uint64_t guid) {
    for (const auto& n : game.npcs()) if (n.guid == guid) return &n;
    return nullptr;
}
const LocalNpcDiminishing* record(const LocalRealmNpc& n, LocalDiminishingGroup g) {
    for (const auto& r : n.diminishing) if (r.group == uint8_t(g)) return &r;
    return nullptr;
}
// The last combat observation this cast produced against the creature.
// LocalGameplay::combatEvents() returns by value, so this copies rather than
// pointing into a temporary that dies at the end of the call.
std::optional<LocalCombatEvent> lastSpellEvent(const LocalGameplay& game, uint64_t target, uint32_t spellId) {
    std::optional<LocalCombatEvent> found;
    for (const auto& e : game.combatEvents())
        if (e.target == target && e.spell == spellId && e.kind == LocalCombatEventKind::SpellDamage) found = e;
    return found;
}

// One decisive cast. Hammer of Justice is SPELL_DAMAGE_CLASS_MAGIC and really
// can miss WorldObject::MagicSpellHitResult; a miss never reaches
// Spell::DoSpellHitOnUnit, so the diminishing state is untouched and retrying
// is state-preserving rather than a way of hiding a failure. Returns the
// outcome the authority actually resolved.
LocalMeleeOutcome castUntilResolved(LocalGameplay& game, LocalRealmPlayer& p, uint64_t target,
                                    uint32_t spellId, const std::vector<LocalRealmPlayer*>& players,
                                    unsigned attempts = 60) {
    for (unsigned i = 0; i < attempts; ++i) {
        p.globalCooldownMs = 0; p.cooldowns.clear(); p.categoryCooldowns.clear();
        p.mana = p.maxMana;
        std::string result;
        if (!game.execute(p, {LocalAction::CastSpell, target, spellId}, players, result)) {
            std::cerr << "FAIL: cast " << spellId << " rejected: " << result << "\n";
            std::abort();
        }
        const auto e = lastSpellEvent(game, target, spellId);
        // A control that landed emits no SpellDamage observation at all: the
        // avoided/nullified branch is the only caller of damageNpc here.
        if (!e || e->outcome == LocalMeleeOutcome::Hit) {
            const auto* n = findNpc(game, target);
            if (n) for (const auto& a : n->controls)
                if (a.spellId == spellId && a.casterGuid == p.guid) return LocalMeleeOutcome::Hit;
            continue;
        }
        if (e->outcome == LocalMeleeOutcome::Immune) return LocalMeleeOutcome::Immune;
        // Miss: nothing changed, roll again.
    }
    std::cerr << "FAIL: cast " << spellId << " never resolved in " << attempts << " attempts\n";
    std::abort();
}
// The same, for a control with a real cast bar: Hibernate is 1,500 ms of
// client-supplied cast time, so the authority resolves it inside tick rather
// than inside execute. Nothing about the definition is altered to avoid this.
struct DrWorld;
struct DrWorld {
    std::shared_ptr<LocalWorldContent> content;
    LocalGameplay game;
    std::vector<LocalRealmPlayer> casters;
    std::vector<LocalRealmPlayer*> players;
    uint64_t npcGuid = 0;
};
// A creature engaged by one or more player casters, with the authority clock
// already off zero: Impl::authorityClockMs starts at 0 and Unit::GetDiminishing
// treats a zero hitTime as "no record", exactly as Unit.cpp:11302 does.
void buildWorld(DrWorld& w, std::vector<uint32_t> spells, unsigned casters, uint8_t classId,
                LocalResourceType resource = LocalResourceType::Mana, uint32_t npcDamage = 0,
                uint32_t npcHealth = 100000000, float npcX = 8, bool advanceClock = true) {
    w.content = drWorld(std::move(spells), npcDamage, npcHealth);
    w.game.useContent(w.content);
    w.casters.reserve(casters);
    for (unsigned i = 0; i < casters; ++i) {
        auto p = drCaster(1 + i, classId, resource);
        p.x = float(i) * 0.05f;
        w.casters.push_back(p);
    }
    w.players.clear();
    for (auto& c : w.casters) w.players.push_back(&c);
    w.game.tick(0, w.players);
    auto n = drNpc(npcX, npcHealth);
    n.targetGuid = w.casters[0].guid; n.threat[0] = {w.casters[0].guid, 1000};
    w.game.setRemoteNpcs({n});
    w.npcGuid = n.guid;
    if (advanceClock) w.game.tick(.25f, w.players);
}
void advance(DrWorld& w, unsigned milliseconds) {
    for (unsigned done = 0; done < milliseconds; done += 250)
        w.game.tick(std::min(250u, milliseconds - done) / 1000.f, w.players);
}
// Every spell in this suite must be castable by the casters that drive it.
void teach(DrWorld& w, std::vector<uint32_t> ids) {
    for (auto& c : w.casters) { c.knownSpells = {1, 990001}; for (auto id : ids) c.knownSpells.push_back(id); }
}
LocalMeleeOutcome castThroughCastBar(DrWorld& w, LocalRealmPlayer& p, uint32_t spellId, unsigned attempts) {
    for (unsigned i = 0; i < attempts; ++i) {
        p.globalCooldownMs = 0; p.cooldowns.clear(); p.categoryCooldowns.clear();
        p.mana = p.maxMana;
        std::string result;
        if (!w.game.execute(p, {LocalAction::CastSpell, w.npcGuid, spellId}, w.players, result)) {
            std::cerr << "FAIL: cast " << spellId << " rejected: " << result << "\n";
            std::abort();
        }
        for (unsigned t = 0; t < 80 && p.castStatus == LocalCastStatus::Casting; ++t)
            w.game.tick(.05f, w.players);
        assert(p.castStatus != LocalCastStatus::Casting);
        const auto* n = findNpc(w.game, w.npcGuid);
        if (n) for (const auto& a : n->controls)
            if (a.spellId == spellId && a.casterGuid == p.guid) return LocalMeleeOutcome::Hit;
        const auto e = lastSpellEvent(w.game, w.npcGuid, spellId);
        if (e && e->outcome == LocalMeleeOutcome::Immune) return LocalMeleeOutcome::Immune;
    }
    std::cerr << "FAIL: cast " << spellId << " never landed in " << attempts << " attempts\n";
    std::abort();
}

LocalMeleeOutcome castThroughCastBar(DrWorld& w, LocalRealmPlayer& p, uint32_t spellId,
                                     unsigned attempts = 40);

// ---------------------------------------------------------------------------
// 2. The ladder, driven for real: two Paladins chaining Hammer of Justice rank 4.
// ---------------------------------------------------------------------------
void ladderDrivenForReal() {
    const auto& hammer = real(10308);
    assert(hammer.unsupportedReason.empty() && hammer.controlProfile == 1 && hammer.durationMs == 6000);
    assert(hammer.mechanic == kLocalMechanicStunned);
    assert(localDiminishingGroupForSpell(hammer, false) == LocalDiminishingGroup::ControlledStun);
    // One Paladin cannot pass rung 1: the 60,000 ms category cooldown outlives
    // the 6,000 ms stun plus the 15,000 ms window. Two can, because the record
    // is keyed on the target.
    assert(hammer.categoryCooldownMs == 60000 && !hammer.cooldownMs);
    assert(hammer.categoryCooldownMs > hammer.durationMs + kLocalDiminishingWindowMs);

    DrWorld w; buildWorld(w, {10308}, 2, 2);
    teach(w, {10308});

    std::array<uint32_t, 4> durations{}, mana{}, held{}, holderRemaining{};
    std::array<uint64_t, 4> holder{};
    std::array<uint8_t, 4> levelBefore{}, hitCountAfter{};
    std::array<LocalMeleeOutcome, 4> outcomes{};
    for (unsigned cast = 0; cast < 4; ++cast) {
        auto& caster = w.casters[cast % 2];
        const auto* before = findNpc(w.game, w.npcGuid);
        const auto* r = record(*before, LocalDiminishingGroup::ControlledStun);
        // What Spell.cpp:3201 reads immediately before Spell.cpp:3213
        // increments: an absent record is DIMINISHING_LEVEL_1.
        levelBefore[cast] = r ? r->hitCount : kLocalDiminishingLevel1;
        outcomes[cast] = castUntilResolved(w.game, caster, w.npcGuid, 10308, w.players);
        // castUntilResolved refills the pool immediately before each attempt,
        // so what is missing now is exactly what the resolved cast paid.
        mana[cast] = caster.maxMana - caster.mana;
        const auto* after = findNpc(w.game, w.npcGuid);
        const auto* stored = record(*after, LocalDiminishingGroup::ControlledStun);
        assert(stored);
        hitCountAfter[cast] = stored->hitCount;
        durations[cast] = 0;
        for (const auto& a : after->controls)
            if (a.spellId == 10308 && a.casterGuid == caster.guid) durations[cast] = a.remainingMs;
        held[cast] = uint32_t(after->controls.size());
        holder[cast] = after->controls.empty() ? 0 : after->controls[0].casterGuid;
        holderRemaining[cast] = after->controls.empty() ? 0 : after->controls[0].remainingMs;
    }
    // 1.0 / 0.5 / 0.25 / 0.0 of the client's own 6,000 ms.
    assert(durations[0] == 6000 && durations[1] == 3000 && durations[2] == 1500);
    assert(outcomes[0] == LocalMeleeOutcome::Hit && outcomes[1] == LocalMeleeOutcome::Hit &&
           outcomes[2] == LocalMeleeOutcome::Hit);
    // Each Paladin's landed Hammer REPLACES the other's (the implementation, P04 competing
    // auras): a stun from a second caster is not periodic, channelled or
    // DOT_STACKING_RULE, so Aura::CanStackWith reaches IsRankOf and removes
    // the first caster's (SpellAuras.cpp:2089-2120, :2160-2177). The creature
    // therefore holds ONE control after every landed cast - the the implementation fixture
    // asserted two coexisting controls, which the reference never has, and was
    // rewritten. The immune cast applied nothing: the first caster's rung-3
    // control still stands, unreplaced and unshortened.
    assert(held[0] == 1 && held[1] == 1 && held[2] == 1 && held[3] == 1);
    assert(holder[0] == w.casters[0].guid && holder[1] == w.casters[1].guid && holder[2] == w.casters[0].guid && holder[3] == w.casters[0].guid);
    assert(outcomes[3] == LocalMeleeOutcome::Immune);
    assert(durations[3] == 0 && holderRemaining[3] == 1500);
    // Unit.cpp:11301 reads DIMINISHING_LEVEL_1/2/3/IMMUNE, whose stored values
    // are 0/1/2/3; Unit.cpp:11326 clamps the fourth increment at
    // GetDiminishingReturnsMaxLevel, so the record reads 1/2/3/3 after them.
    assert(levelBefore[0] == kLocalDiminishingLevel1 && levelBefore[1] == kLocalDiminishingLevel2 &&
           levelBefore[2] == kLocalDiminishingLevel3 && levelBefore[3] == kLocalDiminishingLevelImmune);
    assert(hitCountAfter[0] == 1 && hitCountAfter[1] == 2 && hitCountAfter[2] == 3 && hitCountAfter[3] == 3);
    // The immune cast is a cast: it pays in full. Hammer of Justice is magic,
    // so localMeleeAvoided never applies its quarter-cost melee rule, and the
    // measured cost is the same on all four.
    assert(mana[0] && mana[0] == mana[1] && mana[1] == mana[2] && mana[2] == mana[3]);
    // One control held, one on the stack: a replacement within one group is a
    // refresh (no dip to zero, no restamp), never a second application.
    assert(record(*findNpc(w.game, w.npcGuid), LocalDiminishingGroup::ControlledStun)->stack == 1);

    std::cout << "PASS diminishing ladder: two Paladins chaining real 10308 Hammer of Justice rank 4 ("
              << hammer.durationMs << " ms, mechanic " << unsigned(hammer.mechanic)
              << ", category cooldown " << hammer.categoryCooldownMs << " ms) on one creature applied "
              << durations[0] << " / " << durations[1] << " / " << durations[2]
              << " ms and then LocalMeleeOutcome::Immune, reading DIMINISHING_LEVEL "
              << unsigned(levelBefore[0]) + 1 << "/" << unsigned(levelBefore[1]) + 1 << "/"
              << unsigned(levelBefore[2]) + 1 << "/IMMUNE before each cast and storing hitCount "
              << unsigned(hitCountAfter[0]) << "/" << unsigned(hitCountAfter[1]) << "/"
              << unsigned(hitCountAfter[2]) << "/" << unsigned(hitCountAfter[3])
              << " after them; each landed Hammer replaced the other Paladin's (one control held after every cast, "
                 "SpellAuras.cpp:2160-2177, stack 1), and the immune cast paid the same " << mana[3]
              << " mana as the three that landed and left the first Paladin's rung-3 control at " << holderRemaining[3]
              << " ms rather than silently doing nothing\n";
}

// ---------------------------------------------------------------------------
// 3. Immune is produced, observed, proc-masked and replicated. Before this
//    checkpoint LocalMeleeOutcome::Immune was declared, LAN-encoded, presented
//    and proc-masked but written by nothing.
// ---------------------------------------------------------------------------
void immuneReachesEverything() {
    DrWorld w; buildWorld(w, {10308}, 2, 2);
    teach(w, {10308});
    for (unsigned cast = 0; cast < 3; ++cast)
        assert(castUntilResolved(w.game, w.casters[cast % 2], w.npcGuid, 10308, w.players) ==
               LocalMeleeOutcome::Hit);
    auto& caster = w.casters[1];
    assert(castUntilResolved(w.game, caster, w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Immune);

    // (a) the combat observation
    const auto event = lastSpellEvent(w.game, w.npcGuid, 10308);
    assert(event && event->outcome == LocalMeleeOutcome::Immune);
    assert(event->kind == LocalCombatEventKind::SpellDamage);
    assert(event->source == caster.guid && event->target == w.npcGuid);
    assert(!event->attempted && !event->effective && !event->blocked);
    assert(localOutcomeNullifiesDamage(event->outcome) && !localMeleeAvoided(event->outcome));

    // (b) the proc hit mask
    const auto mask = localProcEventHitMask(*event);
    assert(mask == LocalProcHitImmune);
    assert(mask & LocalProcSupportedHits);
    assert(!(mask & (LocalProcHitNormal | LocalProcHitCritical | LocalProcHitMiss)));

    // (c) the melee-view codec, presented as CombatTextEntry::Type::IMMUNE
    const auto& view = caster.meleeViews.back();
    assert(view.outcome == LocalMeleeOutcome::Immune);
    assert(view.spell == 10308 && view.source == caster.guid && view.target == w.npcGuid);
    assert(!view.amount && !view.blocked && !view.healing);
    Writer wire; writeCast(wire, caster);
    LocalRealmPlayer copy; copy.guid = caster.guid; copy.classId = caster.classId;
    copy.positionRevision = caster.positionRevision;
    Reader r(wire.bytes.data(), wire.bytes.size());
    assert(readCast(r, copy) && r.valid && r.done());
    assert(copy.meleeViews.back().outcome == LocalMeleeOutcome::Immune);
    assert(copy.meleeViews.back().spell == 10308);
    assert(copy.meleeViews.back().source == caster.guid && copy.meleeViews.back().target == w.npcGuid);
    assert(copy.meleeSerial == caster.meleeSerial);
    // readCast enforces the invariant the immune outcome has to satisfy: a
    // damage-nullifying outcome carries no amount and no block.
    {
        auto tampered = caster; tampered.meleeViews.back().amount = 1;
        Writer bad; writeCast(bad, tampered);
        Reader br(bad.bytes.data(), bad.bytes.size());
        LocalRealmPlayer rejected; rejected.guid = caster.guid; rejected.classId = caster.classId;
        rejected.positionRevision = caster.positionRevision;
        assert(!readCast(br, rejected));
    }
    // The outcome's wire value is the one the enum has always declared; the
    // codec was already carrying it, which is why this costs no LAN bump.
    static_assert(uint8_t(LocalMeleeOutcome::Immune) == 10);
    static_assert(uint8_t(LocalMeleeOutcome::Deflect) == 11);

    std::cout << "PASS immune outcome: the fourth Hammer of Justice on one creature resolved "
                 "LocalMeleeOutcome::Immune (wire value "
              << unsigned(uint8_t(LocalMeleeOutcome::Immune))
              << ") - the first producer in this build - and it reached the combat observation as a "
                 "SpellDamage event with 0 attempted and 0 effective damage, the proc hit mask as "
                 "exactly LocalProcHitImmune (0x" << std::hex << mask << std::dec
              << ") with no NORMAL/CRITICAL/MISS bit beside it, and the melee-view codec, where it "
                 "round-tripped through writeCast/readCast and where a tampered non-zero amount on "
                 "the same view is refused\n";
}

// ---------------------------------------------------------------------------
// 4. A new record seeds at DIMINISHING_LEVEL_2. The off-by-one a port gets
//    wrong here is silent: it makes the SECOND control full duration.
// ---------------------------------------------------------------------------
void seedsAtLevelTwo() {
    // Bash is SPELL_DAMAGE_CLASS_MELEE, so it rolls no magic hit and every cast
    // is decisive - the cleanest instrument for an exact two-cast measurement.
    const auto& bash = real(8983);
    assert(bash.durationMs == 4000 && bash.controlProfile == 1);
    assert(localDiminishingGroupForSpell(bash, false) == LocalDiminishingGroup::ControlledStun);

    // Bash is a melee-range ability: the creature starts inside its reach.
    DrWorld w; buildWorld(w, {5211, 6798, 8983}, 2, 11, LocalResourceType::Rage, 0, 100000000, 2);
    teach(w, {5211, 6798, 8983});
    for (auto& c : w.casters) c.formSpellId = 5487; // Bear Form, which Bash requires
    const auto* npc = findNpc(w.game, w.npcGuid);
    assert(npc->diminishing.empty());

    assert(castUntilResolved(w.game, w.casters[0], w.npcGuid, 8983, w.players) == LocalMeleeOutcome::Hit);
    npc = findNpc(w.game, w.npcGuid);
    const auto* seeded = record(*npc, LocalDiminishingGroup::ControlledStun);
    assert(seeded && seeded->hitCount == kLocalDiminishingLevel2 && seeded->stack == 1);
    const unsigned seededHitCount = seeded->hitCount;
    uint32_t first = 0;
    for (const auto& a : npc->controls) if (a.casterGuid == w.casters[0].guid) first = a.remainingMs;
    assert(first == bash.durationMs); // the FIRST is undiminished

    assert(castUntilResolved(w.game, w.casters[1], w.npcGuid, 8983, w.players) == LocalMeleeOutcome::Hit);
    npc = findNpc(w.game, w.npcGuid);
    uint32_t second = 0;
    for (const auto& a : npc->controls) if (a.casterGuid == w.casters[1].guid) second = a.remainingMs;
    assert(second == bash.durationMs / 2); // and the SECOND is halved
    assert(record(*npc, LocalDiminishingGroup::ControlledStun)->hitCount == kLocalDiminishingLevel3);
    assert(validLocalNpcDiminishing(*npc));
    // Stated as the multiplier table so the seed is asserted and not only its
    // consequence: a record seeded at LEVEL_1 would have returned 1.0 twice.
    assert(localDiminishingMultiplier(LocalDiminishingGroup::ControlledStun, kLocalDiminishingLevel1, false, false) == 1.0f);
    assert(localDiminishingMultiplier(LocalDiminishingGroup::ControlledStun, kLocalDiminishingLevel2, false, false) == 0.5f);
    assert(localDiminishingMultiplier(LocalDiminishingGroup::ControlledStun, kLocalDiminishingLevel3, false, false) == 0.25f);
    assert(localDiminishingMultiplier(LocalDiminishingGroup::ControlledStun, kLocalDiminishingLevelImmune, false, false) == 0.0f);

    std::cout << "PASS level-2 seed: the first real 8983 Bash rank 3 on a fresh creature applied its "
                 "full " << first << " ms and left a record whose hitCount is "
              << unsigned(seeded->hitCount) << " (DIMINISHING_LEVEL_2), so the second - from a "
                 "different caster, inside the window - applied " << second
              << " ms; a record seeded at DIMINISHING_LEVEL_1 would have applied " << first
              << " ms twice and slipped the whole ladder one rung\n";
}

// ---------------------------------------------------------------------------
// 5. The fifteen-second window: measured from removal, gated on stack == 0,
//    strictly greater than 15,000 ms.
// ---------------------------------------------------------------------------
void windowFromRemoval() {
    // --- held: the window does not run while an aura of the group is applied -
    // Measured limit first. The window is 15,000 ms and the longest DRTYPE_ALL
    // control this build admits is the 6,000 ms Hammer of Justice rank 4, whose
    // own ladder shortens the next two to 3,000 and 1,500 before refusing a
    // fourth outright. A perfectly chained hold therefore runs 6,000 + 3,000 +
    // 1,500 = 10,500 ms at most, so NO runtime timeline can carry a stack past
    // the fifteen-second boundary. The runtime half asserts what it can - that
    // hitTime is stamped at the LAST removal and at nothing else, across a
    // 9,750 ms continuous hold - and the boundary itself is driven through the
    // shipped read at the end of this group.
    uint32_t heldMs = 0, heldStamp = 0, removalStamp = 0;
    {
        DrWorld w; buildWorld(w, {10308}, 4, 2);
        teach(w, {10308});
        const uint32_t ladder[3] = {6000, 3000, 1500};
        assert(castUntilResolved(w.game, w.casters[0], w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Hit);
        heldStamp = record(*findNpc(w.game, w.npcGuid), LocalDiminishingGroup::ControlledStun)->hitTimeMs;
        // Each caster owns its own slot, so no application is ever replaced in
        // place; every one of them raises the stack exactly once.
        for (unsigned rung = 0; rung < 3; ++rung) {
            // Leave the running control 250 ms of life, then chain the next one
            // on top of it from a fresh caster: the group is never empty.
            advance(w, ladder[rung] - 250);
            heldMs += ladder[rung] - 250;
            const auto* live = record(*findNpc(w.game, w.npcGuid), LocalDiminishingGroup::ControlledStun);
            assert(live && live->stack >= 1);
            assert(live->hitTimeMs == heldStamp); // never restamped while held
            if (rung == 2) break;
            assert(castUntilResolved(w.game, w.casters[rung + 1], w.npcGuid, 10308, w.players) ==
                   LocalMeleeOutcome::Hit);
            uint32_t applied = 0;
            for (const auto& a : findNpc(w.game, w.npcGuid)->controls)
                if (a.casterGuid == w.casters[rung + 1].guid) applied = a.remainingMs;
            assert(applied == ladder[rung + 1]);
        }
        const auto* held = record(*findNpc(w.game, w.npcGuid), LocalDiminishingGroup::ControlledStun);
        assert(held && held->hitCount == kLocalDiminishingLevelImmune && held->stack == 1);
        assert(held->hitTimeMs == heldStamp);
        // The ladder is exhausted while the group is still applied: the record
        // is at IMMUNE and has not decayed one rung over the whole hold.
        assert(castUntilResolved(w.game, w.casters[3], w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Immune);
        assert(record(*findNpc(w.game, w.npcGuid), LocalDiminishingGroup::ControlledStun)->stack == 1);
        // And the stamp lands on the LAST removal, 250 ms later, not on any of
        // the three earlier ones and not on any cast.
        advance(w, 250); heldMs += 250;
        const auto* released = record(*findNpc(w.game, w.npcGuid), LocalDiminishingGroup::ControlledStun);
        assert(released && !released->stack);
        removalStamp = released->hitTimeMs;
        assert(removalStamp == heldStamp + heldMs);
        assert(findNpc(w.game, w.npcGuid)->controls.empty());
    }

    // --- the boundary, per removal path -------------------------------------
    // expiry (LocalGameplay::tick's control sweep) and caster-revision
    // invalidation are the two paths that can remove a control carrying a
    // record. The third, breakNpcControlsOnDamage, is covered below: no
    // DRTYPE_ALL spell this build admits carries AURA_INTERRUPT_FLAG_TAKE_DAMAGE,
    // so it is measured rather than asserted into existence.
    struct PathResult { uint32_t atLimit = 0, pastLimit = 0; };
    auto drive = [&](unsigned mutation, unsigned waitMs) {
        DrWorld w; buildWorld(w, {10308}, 2, 2);
        teach(w, {10308});
        assert(castUntilResolved(w.game, w.casters[0], w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Hit);
        const auto stamp = record(*findNpc(w.game, w.npcGuid), LocalDiminishingGroup::ControlledStun)->hitTimeMs;
        if (mutation == 0) {
            advance(w, 6000); // the control's own duration, to the millisecond
        } else {
            ++w.casters[0].positionRevision; // Spell::CancelAura's local equivalent
            advance(w, 250);
        }
        const auto* n = findNpc(w.game, w.npcGuid);
        assert(n->controls.empty());
        const auto* r = record(*n, LocalDiminishingGroup::ControlledStun);
        assert(r && r->stack == 0 && r->hitCount == kLocalDiminishingLevel2);
        assert(r->hitTimeMs > stamp); // restamped at REMOVAL, not at the cast
        const auto removedAt = r->hitTimeMs;
        advance(w, waitMs - (waitMs % 250));
        for (unsigned extra = 0; extra < waitMs % 250; ++extra) w.game.tick(.001f, w.players);
        // The window is read by the next cast, which is what Unit::GetDiminishing
        // is for; nothing decays on a timer of its own.
        const auto* before = record(*findNpc(w.game, w.npcGuid), LocalDiminishingGroup::ControlledStun);
        assert(before && before->hitTimeMs == removedAt);
        assert(castUntilResolved(w.game, w.casters[1], w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Hit);
        uint32_t applied = 0;
        for (const auto& a : findNpc(w.game, w.npcGuid)->controls)
            if (a.casterGuid == w.casters[1].guid) applied = a.remainingMs;
        return applied;
    };
    PathResult expiry{drive(0, 15000), drive(0, 15001)};
    PathResult invalidation{drive(1, 15000), drive(1, 15001)};
    // getMSTimeDiff(...) > 15000: at exactly 15,000 the record survives.
    assert(expiry.atLimit == 3000 && expiry.pastLimit == 6000);
    assert(invalidation.atLimit == 3000 && invalidation.pastLimit == 6000);

    // --- break-on-damage -----------------------------------------------------
    // Measured, not assumed: of the eighteen spells that reach a group, the
    // nine carrying AURA_INTERRUPT_FLAG_TAKE_DAMAGE are all DRTYPE_PLAYER and
    // none of the eight DRTYPE_ALL stuns carries it. The hook therefore cannot
    // decay a real record in this build, and the honest test is that it runs on
    // the right group and leaves the stun's record alone.
    unsigned allWithBreak = 0, playerWithBreak = 0;
    for (auto id : kControlledStun)
        if (real(id).auraInterruptFlags & kLocalAuraInterruptTakeDamage) ++allWithBreak;
    for (const auto* set : {kSleep, kDisorient, kSilence}) {
        const size_t count = set == kSleep ? std::size(kSleep) : set == kDisorient ? std::size(kDisorient) : std::size(kSilence);
        for (size_t i = 0; i < count; ++i)
            if (real(set[i]).auraInterruptFlags & kLocalAuraInterruptTakeDamage) ++playerWithBreak;
    }
    assert(allWithBreak == 0 && playerWithBreak == 9);
    {
        // A Paladin holding a real Hammer of Justice stun (record, no
        // TAKE_DAMAGE) and a real Repentance (TAKE_DAMAGE, DRTYPE_PLAYER, no
        // record) on the same creature, then a damaging cast. The break hook
        // runs, removes exactly the Repentance and lowers exactly the group
        // that control belongs to.
        DrWorld w; buildWorld(w, {10308, 20066}, 1, 2);
        teach(w, {10308, 20066});
        assert(real(20066).auraInterruptFlags & kLocalAuraInterruptTakeDamage);
        assert(!(real(10308).auraInterruptFlags & kLocalAuraInterruptTakeDamage));
        assert(castUntilResolved(w.game, w.casters[0], w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Hit);
        assert(castUntilResolved(w.game, w.casters[0], w.npcGuid, 20066, w.players) == LocalMeleeOutcome::Hit);
        const auto* n = findNpc(w.game, w.npcGuid);
        assert(n->controls.size() == 2);
        const auto* stun = record(*n, LocalDiminishingGroup::ControlledStun);
        assert(stun && stun->stack == 1 && stun->hitCount == kLocalDiminishingLevel2);
        const auto stamp = stun->hitTimeMs;
        assert(!record(*n, LocalDiminishingGroup::Disorient)); // DRTYPE_PLAYER seeds nothing
        assert(n->diminishing.size() == 1);
        // A real damaging cast, so the break really goes through Unit::DealDamage's
        // local equivalent rather than a direct call.
        w.casters[0].globalCooldownMs = 0; w.casters[0].cooldowns.clear(); w.casters[0].mana = w.casters[0].maxMana;
        std::string result;
        assert(w.game.execute(w.casters[0], {LocalAction::CastSpell, w.npcGuid, 1}, w.players, result));
        n = findNpc(w.game, w.npcGuid);
        assert(n->controls.size() == 1 && n->controls[0].spellId == 10308);
        const auto* after = record(*n, LocalDiminishingGroup::ControlledStun);
        assert(after && after->stack == 1 && after->hitCount == kLocalDiminishingLevel2);
        assert(after->hitTimeMs == stamp); // the stun's record is untouched
        assert(n->diminishing.size() == 1);
    }
    // And the shipped helper the break path calls does stamp at zero, on the
    // group it is given and on no other. This is the production function, not
    // a restatement of it.
    {
        LocalRealmNpc probe;
        probe.diminishing.push_back({uint8_t(LocalDiminishingGroup::ControlledStun), kLocalDiminishingLevel2, 0, 111});
        probe.diminishing.push_back({uint8_t(LocalDiminishingGroup::Disorient), kLocalDiminishingLevel3, 0, 222});
        localDiminishingApply(probe, LocalDiminishingGroup::ControlledStun, true, 500);
        localDiminishingApply(probe, LocalDiminishingGroup::ControlledStun, true, 600);
        assert(probe.diminishing[0].stack == 2 && probe.diminishing[0].hitTimeMs == 111);
        localDiminishingApply(probe, LocalDiminishingGroup::ControlledStun, false, 700);
        assert(probe.diminishing[0].stack == 1 && probe.diminishing[0].hitTimeMs == 111); // not yet zero
        localDiminishingApply(probe, LocalDiminishingGroup::ControlledStun, false, 800);
        assert(probe.diminishing[0].stack == 0 && probe.diminishing[0].hitTimeMs == 800); // stamped
        assert(probe.diminishing[1].stack == 0 && probe.diminishing[1].hitTimeMs == 222); // untouched
        localDiminishingApply(probe, LocalDiminishingGroup::ControlledStun, false, 900);
        assert(probe.diminishing[0].stack == 0 && probe.diminishing[0].hitTimeMs == 800); // never underflows
        // The read the window is measured by, on the shipped function.
        assert(localDiminishingRead(probe, LocalDiminishingGroup::ControlledStun, 800 + 15000) == kLocalDiminishingLevel2);
        assert(probe.diminishing[0].hitCount == kLocalDiminishingLevel2);
        assert(localDiminishingRead(probe, LocalDiminishingGroup::ControlledStun, 800 + 15001) == kLocalDiminishingLevel1);
        assert(probe.diminishing[0].hitCount == kLocalDiminishingLevel1); // the read mutates, as the reference's does
        // The stack gate itself, at the boundary no runtime timeline can reach:
        // with one application still on the target the window never starts, so
        // a 20,000 ms - or a 200,000 ms - gap decays nothing.
        LocalRealmNpc holder;
        holder.diminishing.push_back({uint8_t(LocalDiminishingGroup::ControlledStun),
                                      kLocalDiminishingLevelImmune, 1, 1000});
        assert(localDiminishingRead(holder, LocalDiminishingGroup::ControlledStun, 1000 + 20000) == kLocalDiminishingLevelImmune);
        assert(localDiminishingRead(holder, LocalDiminishingGroup::ControlledStun, 1000 + 200000) == kLocalDiminishingLevelImmune);
        assert(holder.diminishing[0].hitCount == kLocalDiminishingLevelImmune);
        assert(localDiminishingMultiplier(LocalDiminishingGroup::ControlledStun,
                                          holder.diminishing[0].hitCount, false, false) == 0.0f);
        localDiminishingApply(holder, LocalDiminishingGroup::ControlledStun, false, 1000 + 200000);
        assert(!holder.diminishing[0].stack && holder.diminishing[0].hitTimeMs == 1000 + 200000);
        assert(localDiminishingRead(holder, LocalDiminishingGroup::ControlledStun, 1000 + 215000) == kLocalDiminishingLevelImmune);
        assert(localDiminishingRead(holder, LocalDiminishingGroup::ControlledStun, 1000 + 215001) == kLocalDiminishingLevel1);
    }

    std::cout << "PASS diminishing window: a real Hammer of Justice group held continuously for "
              << heldMs - 250 << " ms across three chained casters never restamped its removal time "
                 "and never decayed a rung, reaching DIMINISHING_LEVEL_IMMUNE while still applied - "
                 "and " << heldMs << " ms is the whole runtime budget, because the ladder shortens "
                 "the only DRTYPE_ALL control this build admits from 6000 to 3000 to 1500 ms and "
                 "then refuses a fourth, so no live timeline can carry a stack across the 15,000 ms "
                 "boundary; the stamp landed on the LAST removal at exactly "
              << removalStamp - heldStamp << " ms after the first cast, and the shipped "
                 "localDiminishingRead leaves a stack-1 record untouched at 20,000 and 200,000 ms "
                 "and then resets it 15,001 ms after that stack reaches zero. After removal by "
                 "expiry the next cast applied " << expiry.atLimit << " ms at 15,000 ms and "
              << expiry.pastLimit << " ms at 15,001 ms, and after removal by caster-revision "
                 "invalidation " << invalidation.atLimit << " ms and " << invalidation.pastLimit
              << " ms at the same two instants, so the comparison is strictly > 15000 and is "
                 "measured from removal. breakNpcControlsOnDamage removed a real 20066 Repentance "
                 "and lowered only its own DISORIENT group, leaving the stun record's stack and "
                 "stamp untouched - it cannot decay a record in this build at all, because "
              << allWithBreak << " of the 8 DRTYPE_ALL stuns carry AURA_INTERRUPT_FLAG_TAKE_DAMAGE "
                 "while all " << playerWithBreak << " spells that do are DRTYPE_PLAYER and seed no "
                 "record\n";
}

// ---------------------------------------------------------------------------
// 6. Groups are independent on one creature; the record is cleared on death and
//    on leash/reset.
// ---------------------------------------------------------------------------
void groupsAndClearing() {
    // --- independence -------------------------------------------------------
    // Every DRTYPE_ALL group this build can reach is CONTROLLED_STUN, so the
    // only independence a live cast can show is that a second group on the same
    // creature neither seeds nor disturbs the first. That is measured here, and
    // the record keying itself is asserted against the shipped helpers.
    {
        // 11297 Sap would be the natural second control here, but it cannot be
        // cast in this build at all: its requiredForms is FORM_STEALTH and
        // kLocalForms models no stealth, so localSpellFormReady refuses it (see
        // the DRTYPE_PLAYER group below). 20066 Repentance is the reachable
        // second control - a different mechanic, a different group, the same
        // creature.
        DrWorld w; buildWorld(w, {10308, 20066}, 1, 2);
        teach(w, {10308, 20066});
        // Two different groups on one creature: CONTROLLED_STUN (DRTYPE_ALL,
        // which seeds a record here) and DISORIENT (DRTYPE_PLAYER, which does
        // not). Repentance reaches DISORIENT only through MECHANIC_KNOCKOUT, so
        // this arm is also the runtime witness for the corrected constant.
        assert(localDiminishingGroupForSpell(real(20066), false) == LocalDiminishingGroup::Disorient);
        assert(real(20066).mechanic == kLocalMechanicKnockout && real(20066).iconId != 292);
        assert(castUntilResolved(w.game, w.casters[0], w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Hit);
        assert(castUntilResolved(w.game, w.casters[0], w.npcGuid, 20066, w.players) == LocalMeleeOutcome::Hit);
        const auto* n = findNpc(w.game, w.npcGuid);
        assert(n->controls.size() == 2 && n->diminishing.size() == 1);
        assert(record(*n, LocalDiminishingGroup::ControlledStun)->hitCount == kLocalDiminishingLevel2);
        assert(!record(*n, LocalDiminishingGroup::Disorient));
    }
    // Two groups, keyed and stepped independently, on the shipped read/increment
    // pair. LocalDiminishingGroup::Stun is the group a proc-applied stun lands
    // in; nothing produces one today, which is exactly why the keying has to be
    // asserted rather than inferred from a cast.
    {
        LocalRealmNpc n;
        assert(localDiminishingRead(n, LocalDiminishingGroup::ControlledStun, 1000) == kLocalDiminishingLevel1);
        localDiminishingIncrement(n, LocalDiminishingGroup::ControlledStun, 1000);
        localDiminishingIncrement(n, LocalDiminishingGroup::Stun, 1000);
        localDiminishingIncrement(n, LocalDiminishingGroup::ControlledStun, 1000);
        assert(n.diminishing.size() == 2);
        assert(localDiminishingRead(n, LocalDiminishingGroup::ControlledStun, 1000) == kLocalDiminishingLevel3);
        assert(localDiminishingRead(n, LocalDiminishingGroup::Stun, 1000) == kLocalDiminishingLevel2);
        assert(localDiminishingRead(n, LocalDiminishingGroup::Disorient, 1000) == kLocalDiminishingLevel1);
        assert(validLocalNpcDiminishing(n));
        // The clamp is per group and at that group's own maximum.
        for (unsigned i = 0; i < 8; ++i) localDiminishingIncrement(n, LocalDiminishingGroup::ControlledStun, 1000);
        assert(localDiminishingRead(n, LocalDiminishingGroup::ControlledStun, 1000) == kLocalDiminishingLevelImmune);
        assert(localDiminishingRead(n, LocalDiminishingGroup::Stun, 1000) == kLocalDiminishingLevel2);
        // The bound is honoured and the overflowing group is simply not recorded.
        for (unsigned g = 1; g <= 20; ++g) localDiminishingIncrement(n, LocalDiminishingGroup(g), 1000);
        assert(n.diminishing.size() == kLocalMaxNpcDiminishing);
        assert(validLocalNpcDiminishing(n));
    }
    // --- cleared on death ---------------------------------------------------
    unsigned clearedOnDeath = 0;
    {
        DrWorld w; buildWorld(w, {10308}, 1, 2, LocalResourceType::Mana, 0, 500);
        teach(w, {10308});
        assert(castUntilResolved(w.game, w.casters[0], w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Hit);
        assert(findNpc(w.game, w.npcGuid)->diminishing.size() == 1);
        w.casters[0].globalCooldownMs = 0; w.casters[0].cooldowns.clear(); w.casters[0].mana = w.casters[0].maxMana;
        std::string result;
        assert(w.game.execute(w.casters[0], {LocalAction::CastSpell, w.npcGuid, 990001}, w.players, result));
        const auto* n = findNpc(w.game, w.npcGuid);
        assert(n->dead && n->diminishing.empty() && n->controls.empty());
        clearedOnDeath = 1;
    }
    // --- leash/reset: the CONTROLS go, the RECORD stays ---------------------
    // Unit::setDeathState is ClearDiminishings' one and only caller
    // (Unit.cpp:11093). The reference's evade path removes the auras - each
    // lowering its own stack and, at zero, stamping the removal time - and
    // keeps the record, so a creature that leashed and re-engaged inside the
    // window still owes its ladder. That is what this measures end to end.
    unsigned clearedOnLeash = 0;
    uint32_t reengagedInside = 0, reengagedOutside = 0;
    auto leashAndRecast = [&](unsigned waitMs) {
        DrWorld w; buildWorld(w, {10308}, 2, 2);
        teach(w, {10308});
        assert(castUntilResolved(w.game, w.casters[0], w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Hit);
        const auto* engaged = findNpc(w.game, w.npcGuid);
        assert(engaged->diminishing.size() == 1);
        const auto stampAtCast = record(*engaged, LocalDiminishingGroup::ControlledStun)->hitTimeMs;
        assert(record(*engaged, LocalDiminishingGroup::ControlledStun)->stack == 1);
        const auto epochBefore = engaged->combatEpoch;
        // Out of the creature's 70-yard tether. LocalGameplay::tick's reset arm
        // reallocates the combat epoch and drops every transient list through
        // Impl::releaseNpcControls.
        // Exactly one tick out of range, so the removal stamp is the authority
        // clock at the end of that tick and the wait below starts from it with
        // no slack. Two ticks here would silently add 250 ms to every measured
        // interval and put the 15,000 ms boundary in the wrong place.
        w.casters[0].x = 500; w.casters[0].y = 500;
        advance(w, 250);
        const auto* leashed = findNpc(w.game, w.npcGuid);
        assert(!leashed->dead && !leashed->targetGuid && leashed->controls.empty());
        assert(leashed->health == leashed->maxHealth && leashed->x == leashed->homeX);
        assert(leashed->combatEpoch != epochBefore);
        // The record survived the reset, its stack was lowered by the release
        // rather than bypassed by a bare controls.clear(), and the removal time
        // was stamped at the leash rather than left at the cast.
        const auto* kept = record(*leashed, LocalDiminishingGroup::ControlledStun);
        assert(kept && kept->hitCount == kLocalDiminishingLevel2 && kept->stack == 0);
        assert(kept->hitTimeMs > stampAtCast);
        clearedOnLeash = unsigned(leashed->diminishing.size());
        // Re-engage: walk back and cast again, `waitMs` after the leash.
        w.casters[0].x = 0; w.casters[0].y = 0;
        // Re-engage immediately, with the fixture's own 10-damage instant. This
        // is a harness necessity, not a diminishing fact: LocalGameplay::regions
        // (local_gameplay.cpp:625-652) only re-seeds the deck from real spawns
        // and from creatures that still carry a targetGuid, so a creature
        // injected through setRemoteNpcs is dropped by the next region sweep the
        // moment it disengages. It costs nothing here - the damage breaks no
        // control (there are none left) and touches no record - and it is what a
        // player re-engaging actually does. The clock does not move inside
        // execute, so the wait below is measured from the leash exactly.
        w.casters[1].globalCooldownMs = 0; w.casters[1].cooldowns.clear();
        w.casters[1].mana = w.casters[1].maxMana;
        std::string pull;
        assert(w.game.execute(w.casters[1], {LocalAction::CastSpell, w.npcGuid, 1}, w.players, pull));
        {
            const auto* pulled = findNpc(w.game, w.npcGuid);
            assert(pulled && pulled->targetGuid && pulled->diminishing.size() == 1);
            const auto* untouched = record(*pulled, LocalDiminishingGroup::ControlledStun);
            assert(untouched && untouched->hitCount == kLocalDiminishingLevel2 && !untouched->stack);
        }
        advance(w, waitMs - (waitMs % 250));
        for (unsigned extra = 0; extra < waitMs % 250; ++extra) w.game.tick(.001f, w.players);
        assert(castUntilResolved(w.game, w.casters[1], w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Hit);
        uint32_t applied = 0;
        for (const auto& a : findNpc(w.game, w.npcGuid)->controls)
            if (a.casterGuid == w.casters[1].guid) applied = a.remainingMs;
        return applied;
    };
    reengagedInside = leashAndRecast(15000);
    reengagedOutside = leashAndRecast(15001);
    // Still diminished at the boundary, reset one millisecond past it: the
    // leash is a removal like any other, not an amnesty.
    assert(reengagedInside == 3000 && reengagedOutside == 6000);
    assert(clearedOnLeash == 1);

    std::cout << "PASS group independence and clearing: a stun and a real 20066 Repentance held "
                 "together on one creature left exactly one record, the CONTROLLED_STUN one, at "
                 "DIMINISHING_LEVEL_2 while the other control seeded none; the shipped "
                 "read/increment pair keys CONTROLLED_STUN and STUN separately, clamps each at its "
                 "own GetDiminishingReturnsMaxLevel and stops at kLocalMaxNpcDiminishing ("
              << kLocalMaxNpcDiminishing << ") without ever failing validLocalNpcDiminishing; the "
                 "record was cleared on death (" << clearedOnDeath
              << " records left) and SURVIVED leash/reset (" << clearedOnLeash
              << " record left, stack lowered to 0 and the removal time stamped at the leash), so a "
                 "creature re-engaged 15,000 ms later was still stunned for only "
              << reengagedInside << " ms and one millisecond later for the full " << reengagedOutside
              << " ms - matching Unit.cpp:11093, where ClearDiminishings has exactly one caller, "
                 "setDeathState, and evade removes the auras without clearing the record\n";
}

// ---------------------------------------------------------------------------
// 7. Sap and Hibernate do not diminish, at any rung. This is the group that
//    catches a transposed caster/target gate: Spell.cpp:3207 tests the TARGET
//    and :3211 the CASTER, and swapping them inverts this build twice over.
// ---------------------------------------------------------------------------
void playerGroupsUnaffected() {
    // --- Sap: classified, tabled, and measured unreachable ------------------
    // Both Sap ranks are DISORIENT/DRTYPE_PLAYER, and both are refused by
    // localSpellFormReady before any of this matters: SpellShapeshiftForm 30
    // (stealth) is not one of the nine forms kLocalForms models, and
    // 11297/51724 carry requiredForms 0x20000000 without the
    // allow-while-not-shapeshifted attribute. So the ladder table is asserted
    // on the shipped functions and the runtime proof is carried by Hibernate,
    // which is castable.
    unsigned sapRefused = 0;
    for (uint32_t id : {11297u, 51724u}) {
        const auto& sap = real(id);
        assert(localDiminishingGroupForSpell(sap, false) == LocalDiminishingGroup::Disorient);
        assert(localDiminishingGroupType(LocalDiminishingGroup::Disorient) == LocalDiminishingType::Player);
        assert(sap.mechanic == kLocalMechanicSapped);
        for (uint8_t level = kLocalDiminishingLevel1; level <= kLocalDiminishingLevelImmune; ++level)
            assert(localDiminishingMultiplier(LocalDiminishingGroup::Disorient, level, false, false) == 1.0f);
        // Its PvP clamp is equally dead: the limit is real and shorter than the
        // spell, and the clamp reads the same target opt-in.
        assert(localDiminishingLimitDuration(LocalDiminishingGroup::Disorient, sap) == 10000);
        assert(sap.durationMs > 10000);
        auto rogue = drCaster(1, 8, LocalResourceType::Energy);
        if (!localSpellFormReady(rogue, sap)) ++sapRefused;
    }
    assert(sapRefused == 2);

    // --- Hibernate: driven through its real 1,500 ms cast bar ---------------
    const auto& hibernate = real(18658);
    assert(localDiminishingGroupForSpell(hibernate, false) == LocalDiminishingGroup::Sleep);
    assert(localDiminishingGroupType(LocalDiminishingGroup::Sleep) == LocalDiminishingType::Player);
    assert(hibernate.mechanic == kLocalMechanicSleep && hibernate.castTimeMs == 1500);
    unsigned hibernateCasts = 0; uint32_t hibernateApplied = 0;
    {
        DrWorld w; buildWorld(w, {18658}, 2, 11);
        teach(w, {18658});
        // Four casts is one more than the ladder has rungs: a DRTYPE_ALL group
        // would already be immune by now.
        for (unsigned cast = 0; cast < 4; ++cast) {
            auto& caster = w.casters[cast % 2];
            assert(castThroughCastBar(w, caster, 18658) == LocalMeleeOutcome::Hit);
            ++hibernateCasts;
            const auto* n = findNpc(w.game, w.npcGuid);
            uint32_t applied = 0;
            for (const auto& a : n->controls) if (a.casterGuid == caster.guid) applied = a.remainingMs;
            hibernateApplied = applied;
            // Full duration at every rung, less the single 50 ms tick in which
            // the 1,500 ms cast completed: the authority applies the control and
            // runs its expiry sweep inside the same tick, which predates this
            // checkpoint and applies to every cast-time control. A rung-2
            // Hibernate would be 20,000 ms, not 39,950.
            assert(applied == hibernate.durationMs - 50);
            assert(n->diminishing.empty());          // and no record was ever seeded
        }
    }
    // --- Repentance: the row the corrected knockout constant moved ----------
    // 20066 is instant, Paladin, needs no form, and is the only accepted spell
    // whose group is decided by MECHANIC_KNOCKOUT. It is also the clearest PvP
    // clamp candidate in the build - a 60,000 ms control against a 6,000 ms
    // Paladin-branch GetDiminishingReturnsLimitDuration - and neither the clamp
    // nor the ladder may touch it, because both read the same target opt-in.
    const auto& repentance = real(20066);
    assert(localDiminishingGroupForSpell(repentance, false) == LocalDiminishingGroup::Disorient);
    assert(repentance.mechanic == kLocalMechanicKnockout && repentance.durationMs == 60000);
    assert(localDiminishingLimitDuration(LocalDiminishingGroup::Disorient, repentance) == 6000);
    unsigned repentanceCasts = 0;
    {
        DrWorld w; buildWorld(w, {20066}, 2, 2);
        teach(w, {20066});
        for (unsigned cast = 0; cast < 4; ++cast) {
            auto& caster = w.casters[cast % 2];
            assert(castUntilResolved(w.game, caster, w.npcGuid, 20066, w.players) == LocalMeleeOutcome::Hit);
            ++repentanceCasts;
            const auto* n = findNpc(w.game, w.npcGuid);
            uint32_t applied = 0;
            for (const auto& a : n->controls) if (a.casterGuid == caster.guid) applied = a.remainingMs;
            assert(applied == repentance.durationMs); // neither diminished nor clamped
            assert(n->diminishing.empty());
        }
    }

    // The gate under test, stated both ways on the shipped function: with no
    // opt-in the multiplier stays 1.0 at every rung, and the identical call
    // with the target opt-in true - which this realm's 14,496 creature
    // templates never satisfy - would have halved and then zeroed it. This is
    // what a transposed Spell.cpp:3207/:3211 pair would have inverted.
    for (uint8_t level = kLocalDiminishingLevel1; level <= kLocalDiminishingLevelImmune; ++level)
        assert(localDiminishingMultiplier(LocalDiminishingGroup::Sleep, level, false, false) == 1.0f);
    assert(localDiminishingMultiplier(LocalDiminishingGroup::Sleep, kLocalDiminishingLevel2, true, false) == 0.5f);
    assert(localDiminishingMultiplier(LocalDiminishingGroup::Sleep, kLocalDiminishingLevelImmune, true, false) == 0.0f);
    assert(localDiminishingMultiplier(LocalDiminishingGroup::Disorient, kLocalDiminishingLevel2, true, false) == 0.5f);
    // And the stun on the same realm does diminish without any opt-in at all,
    // so the difference is the group type and not a dead ladder.
    assert(localDiminishingMultiplier(LocalDiminishingGroup::ControlledStun, kLocalDiminishingLevel2, false, false) == 0.5f);
    assert(localDiminishingMultiplier(LocalDiminishingGroup::Silence, kLocalDiminishingLevel2, false, false) == 1.0f);
    // The taunt ladder, transcribed and unclaimed: with no OBEYS_TAUNT opt-in
    // every rung is 1.0, and with one it is the 0.65 series that runs to a
    // fifth rung DIMINISHING_LEVEL_IMMUNE never reaches.
    for (uint8_t level = kLocalDiminishingLevel1; level <= kLocalDiminishingLevelTauntImmune; ++level)
        assert(localDiminishingMultiplier(LocalDiminishingGroup::Taunt, level, false, false) == 1.0f);
    assert(localDiminishingMultiplier(LocalDiminishingGroup::Taunt, kLocalDiminishingLevel2, false, true) == 0.65f);
    assert(localDiminishingMultiplier(LocalDiminishingGroup::Taunt, kLocalDiminishingLevel3, false, true) == 0.4225f);
    assert(localDiminishingMultiplier(LocalDiminishingGroup::Taunt, kLocalDiminishingLevelImmune, false, true) == 0.274625f);
    assert(localDiminishingMultiplier(LocalDiminishingGroup::Taunt, kLocalDiminishingLevelTauntImmune, false, true) == 0.0f);

    std::cout << "PASS DRTYPE_PLAYER groups refused: " << repentanceCasts
              << " real 20066 Repentance casts - the one accepted spell whose group is decided by "
                 "MECHANIC_KNOCKOUT, and the clearest PvP-clamp candidate at " << repentance.durationMs
              << " ms against its 6,000 ms Paladin-branch limit - applied the full "
              << repentance.durationMs << " ms every time, neither diminished nor clamped; "
              << hibernateCasts
              << " real 18658 Hibernate casts from two druids, each driven through the client's own "
                 "1,500 ms cast bar, applied " << hibernateApplied << " ms of its full "
              << hibernate.durationMs
              << " ms every time - one 50 ms authority tick, not one diminishing rung, which would "
                 "have been 20,000 - and seeded no record at any rung, while the identical shipped "
                 "localDiminishingMultiplier call with the target opt-in set returns 0.5 and then "
                 "0.0 - so the gate is the target test at Spell.cpp:3207 and not a dead ladder, and "
                 "a transposed caster/target gate would have shown here; both Sap ranks classify "
                 "DISORIENT/DRTYPE_PLAYER with a 10,000 ms limit shorter than their 45,000 and "
                 "60,000 ms, hold 1.0 at every rung, and are refused before any of it by "
                 "localSpellFormReady, because SpellShapeshiftForm 30 is not one of the nine forms "
                 "kLocalForms models; the taunt ladder returns 1.0 at every rung without its "
                 "OBEYS_TAUNT opt-in and the 0.65/0.4225/0.274625/0.0 series with it\n";
}

// ---------------------------------------------------------------------------
// 7a. The authority clock never reads zero. Unit::GetDiminishing's
//     `if (!i->hitTime) return DIMINISHING_LEVEL_1;` (Unit.cpp:11302) is dead
//     code in the reference, where GameTime::GetGameTimeMS is never zero, and
//     load-bearing here, where Impl::authorityClockMs is a counter that has to
//     start somewhere. Seeded at 0 it made the whole ladder invisible for any
//     group whose record was created in the authority's first frame.
// ---------------------------------------------------------------------------
void firstFrameClock() {
    // No tick with a non-zero elapsedMs before the casts: the clock is still at
    // whatever Impl seeded it with, and three back-to-back Hammers of Justice
    // resolve inside execute without advancing it at all.
    DrWorld w; buildWorld(w, {10308}, 2, 2, LocalResourceType::Mana, 0, 100000000, 8, /*advanceClock=*/false);
    teach(w, {10308});
    std::array<uint32_t, 3> applied{};
    for (unsigned cast = 0; cast < 3; ++cast) {
        auto& caster = w.casters[cast % 2];
        assert(castUntilResolved(w.game, caster, w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Hit);
        for (const auto& a : findNpc(w.game, w.npcGuid)->controls)
            if (a.casterGuid == caster.guid) applied[cast] = a.remainingMs;
    }
    // Seeded at 0 this timeline measured 6000 / 6000 / 6000: the record's
    // hitTimeMs was 0, GetDiminishing's zero-hitTime arm returned
    // DIMINISHING_LEVEL_1 on every read, and the hitCount climbed unused.
    assert(applied[0] == 6000 && applied[1] == 3000 && applied[2] == 1500);
    const auto* r = record(*findNpc(w.game, w.npcGuid), LocalDiminishingGroup::ControlledStun);
    // Stack 1, not 2: each Paladin's Hammer replaced the other's (the implementation,
    // SpellAuras.cpp:2160-2177), a replacement within one group being a refresh.
    assert(r && r->hitCount == kLocalDiminishingLevelImmune && r->stack == 1);
    // The stamp is the clock's seed itself, which is what must never be zero.
    assert(r->hitTimeMs != 0);
    const auto seed = r->hitTimeMs;
    // And the field is as wide as the clock it is stamped from, so a realm up
    // for longer than 2^32 ms cannot truncate the stamp out from under the
    // comparison. Both are asserted as types, not as behaviour.
    static_assert(std::is_same_v<decltype(LocalNpcDiminishing::hitTimeMs), uint64_t>);
    {
        LocalRealmNpc wide;
        wide.diminishing.push_back({uint8_t(LocalDiminishingGroup::ControlledStun),
                                    kLocalDiminishingLevel2, 1, 0});
        const uint64_t past = uint64_t(1) << 33; // beyond a 32-bit millisecond clock
        localDiminishingApply(wide, LocalDiminishingGroup::ControlledStun, false, past);
        assert(wide.diminishing[0].hitTimeMs == past); // stamped whole, not truncated
        assert(localDiminishingRead(wide, LocalDiminishingGroup::ControlledStun, past + 15000) == kLocalDiminishingLevel2);
        assert(localDiminishingRead(wide, LocalDiminishingGroup::ControlledStun, past + 15001) == kLocalDiminishingLevel1);
    }
    std::cout << "PASS first-frame clock: three real Hammer of Justice casts resolved before the "
                 "authority clock had advanced once applied " << applied[0] << " / " << applied[1]
              << " / " << applied[2] << " ms and left the record at DIMINISHING_LEVEL_IMMUNE with a "
                 "non-zero stamp (" << seed << "); with the clock seeded at 0 the same timeline "
                 "measured 6000 / 6000 / 6000, because Unit.cpp:11302's zero-hitTime arm answered "
                 "DIMINISHING_LEVEL_1 forever. hitTimeMs is uint64_t like the clock it is compared "
                 "against, so a stamp past 2^32 ms survives whole and still resets at 15,001 ms and "
                 "not before\n";
}

// ---------------------------------------------------------------------------
// 7b. Impact 12355 is retired as a proc child. The reference builds its
//     spellbook from trainer tables, never from SkillLineAbility.dbc, and 12355
//     is only ever applied by 64343's aura-42 EffectTriggerSpell. This group
//     measures what the retirement rule actually changed rather than arguing it.
// ---------------------------------------------------------------------------
void impactRetired() {
    // The rule's own input, recomputed here from the player's own Spell.dbc so
    // the assertion does not depend on the importer agreeing with itself.
    std::set<uint32_t> procChildren;
    const auto* spells = gTables->get("Spell");
    for (uint32_t row = 0; row < spells->getRecordCount(); ++row)
        for (unsigned effect = 0; effect < 3; ++effect) {
            if (spells->getUInt32(row, 71 + effect) != 6 || spells->getUInt32(row, 95 + effect) != 42) continue;
            if (const auto child = spells->getUInt32(row, 116 + effect)) procChildren.insert(child);
        }
    assert(procChildren.count(12355)); // 64343 Impact, aura 42, ProcChance 100

    // The census did not move for THIS rule: the definition is retained, only
    // the direct cast is gone. 982 audited-accepted ids at the implementation and the implementation; 990
    // since the implementation admitted Shield Slam x8 and 1,004 since the implementation admitted the
    // five form boosts and nine entry-resource ranks - none of which is a proc
    // child either.
    std::set<uint32_t> accepted, defined;
    for (const auto& r : gImported.audit) if (r.status == "Supported decoder; imported") accepted.insert(r.id);
    for (const auto& d : gImported.spells) if (d.unsupportedReason.empty()) defined.insert(d.id);
    assert(accepted.size() == 1004);
    assert(accepted.count(12355) && defined.count(12355));

    // What the rule actually flipped. `triggeredOnly` is also carried by the
    // 21 proc children the importer synthesises itself, and those are proc
    // children by construction, so intersecting with them proves nothing. The
    // rule can only ever flip a row that was DECODED from the client and
    // accepted - a synthesised child is not an audited row at all - so that
    // intersection is the set it targets.
    std::set<uint32_t> flipped, allTriggered, synthesised;
    for (const auto& d : gImported.spells) {
        if (!d.triggeredOnly) continue;
        allTriggered.insert(d.id);
        if (!procChildren.count(d.id)) continue;
        (accepted.count(d.id) ? flipped : synthesised).insert(d.id);
    }
    const std::set<uint32_t> expectedFlipped{12355, 23880};
    assert(flipped == expectedFlipped);
    assert(gImported.procChildrenRetired == 2);
    assert(gImported.procChildrenRetired == flipped.size());
    // 28 triggeredOnly definitions at the implementation-the implementation; 35 since the implementation's
    // untrained-chain rule retired seven more decoded rows (Lightning
    // Overload's 49239/49240/49268/49269 and Lightning Shield's damage leaves
    // 26372/49278/49279); 40 since the implementation marked the five form boost spells
    // triggeredOnly, because HandleShapeshiftBoosts casts them and no player
    // learns one. None of the twelve is an aura-42 child, so this rule's own
    // count and set are unchanged and `untrainedChainRanksRetired` stays 7.
    assert(allTriggered.size() == 40 && gImported.untrainedChainRanksRetired == 7);
    for (auto id : {3025u, 1178u, 21178u, 9635u, 7381u})
        assert(allTriggered.count(id) && !procChildren.count(id) && !flipped.count(id));
    for (auto id : {49239u, 49240u, 49268u, 49269u, 26372u, 49278u, 49279u}) assert(allTriggered.count(id) && !procChildren.count(id) && !flipped.count(id));
    // 23885, the aura the Bloodthirst decoder synthesises, was already withheld
    // for its own reason: it is triggeredOnly, it is not an accepted decoded
    // row, and it is therefore not one of the two this rule moved.
    assert(allTriggered.count(23885) && !flipped.count(23885) && !accepted.count(23885));
    // Talent rows are appended after the rule runs, so a proc child that
    // arrives through importClientTalents would escape it. Measured: none does.
    unsigned talentProcChildren = 0;
    for (const auto& d : gImported.spells)
        if (d.talentId && !d.triggeredOnly && procChildren.count(d.id)) ++talentProcChildren;
    assert(talentProcChildren == 0);

    // What actually became uncastable, measured class by class rather than
    // argued: two fresh level-80 characters of every class, one realm with the
    // shipped definitions and one with the retirement undone on those two ids.
    auto spellbooks = [&](bool undoRetirement) {
        auto content = std::make_shared<LocalWorldContent>();
        content->spells = gImported.spells;
        if (undoRetirement)
            for (auto& d : content->spells) if (flipped.count(d.id)) d.triggeredOnly = false;
        std::sort(content->spells.begin(), content->spells.end(),
                  [](const auto& a, const auto& b) { return a.id < b.id; });
        content->clientStarterSpells = true; content->classResources = true;
        LocalItemDefinition item; item.id = 117; item.name = "x"; item.stack = 20; content->items = {item};
        LocalNpcDefinition enemy; enemy.id = 50; enemy.name = "e"; enemy.health = 100; enemy.hostile = true;
        content->npcs = {enemy};
        LocalGameplay game; game.useContent(content);
        std::map<uint8_t, std::set<uint32_t>> books;
        for (uint8_t classId = 1; classId <= 11; ++classId) {
            if (classId == 10) continue; // no class 10 in 3.3.5a
            LocalRealmPlayer p; p.guid = 100 + classId; p.classId = classId; p.race = 1; p.level = 80;
            p.health = p.maxHealth = 10000; p.mana = p.maxMana = 100000;
            game.initializePlayer(p, true);
            books[classId] = {p.knownSpells.begin(), p.knownSpells.end()};
        }
        return books;
    };
    const auto shipped = spellbooks(false), without = spellbooks(true);
    std::set<uint32_t> lost, gained;
    for (const auto& [classId, book] : without) {
        for (auto id : book) if (!shipped.at(classId).count(id)) lost.insert(id);
        for (auto id : shipped.at(classId)) if (!book.count(id)) gained.insert(id);
    }
    // Exactly the two, and nothing else moved in either direction for any class.
    assert(lost == expectedFlipped);
    assert(gained.empty());
    assert(!shipped.at(8).count(12355));  // Mage
    assert(!shipped.at(1).count(23880));  // Warrior
    assert(without.at(8).count(12355) && without.at(1).count(23880));
    unsigned totalShipped = 0, totalWithout = 0;
    for (const auto& [classId, book] : shipped) totalShipped += unsigned(book.size());
    for (const auto& [classId, book] : without) totalWithout += unsigned(book.size());
    assert(totalWithout - totalShipped == 2);

    // And the cast is refused even for a character whose saved spellbook still
    // names it: local_gameplay.cpp:3187 rejects before the known-spell test, and
    // initializePlayer erases the id from an existing list.
    std::string refusal, stale;
    {
        DrWorld w; buildWorld(w, {12355}, 1, 8);
        teach(w, {12355});
        assert(std::find(w.casters[0].knownSpells.begin(), w.casters[0].knownSpells.end(), 12355u) !=
               w.casters[0].knownSpells.end());
        w.casters[0].globalCooldownMs = 0; w.casters[0].mana = w.casters[0].maxMana;
        assert(!w.game.execute(w.casters[0], {LocalAction::CastSpell, w.npcGuid, 12355}, w.players, refusal));
        assert(refusal == "Triggered spell cannot be cast directly");
        assert(findNpc(w.game, w.npcGuid)->controls.empty());
        assert(findNpc(w.game, w.npcGuid)->diminishing.empty());
        auto stalePlayer = w.casters[0]; stalePlayer.gameplayInitialized = true;
        w.game.initializePlayer(stalePlayer, false);
        assert(std::find(stalePlayer.knownSpells.begin(), stalePlayer.knownSpells.end(), 12355u) ==
               stalePlayer.knownSpells.end());
        stale = "erased from a stale spellbook";
    }
    // Classification is unaffected - it does not read triggeredOnly - so the
    // census is still 8 DRTYPE_ALL while the RUNTIME-reachable set is now the
    // seven Hammer of Justice and Bash rows.
    assert(localDiminishingGroupForSpell(real(12355), false) == LocalDiminishingGroup::ControlledStun);
    assert(localDiminishingGroupType(LocalDiminishingGroup::ControlledStun) == LocalDiminishingType::All);
    unsigned castableStuns = 0;
    for (auto id : kControlledStun) if (!real(id).triggeredOnly) ++castableStuns;
    assert(castableStuns == 7);

    std::cout << "PASS Impact retired: 12355 is named by another row's aura-42 EffectTriggerSpell and "
                 "is now triggeredOnly, leaving the audited-accepted census unmoved at "
              << accepted.size() << " with the definition retained; the rule flipped exactly "
              << gImported.procChildrenRetired << " rows - {12355, 23880} - taking triggeredOnly to "
              << allTriggered.size() << ", left the already-withheld 23885 alone, and caught every "
                 "proc child in the set (" << talentProcChildren
              << " escaped through the talent import). Re-running initializePlayer for a fresh "
                 "level-80 character of all ten classes against a realm with the retirement undone, "
                 "the spellbooks lose exactly {12355 (Mage), 23880 (Warrior)} and gain nothing - "
              << totalWithout - totalShipped << " spells in total, so no previously castable ability "
                 "became uncastable. A Mage that still names 12355 is refused with \""
              << refusal << "\" and has it " << stale
              << "; classification is untouched (12355 is still CONTROLLED_STUN/DRTYPE_ALL), so the "
                 "census stays 8 DRTYPE_ALL while the runtime-reachable stun set is now the "
              << castableStuns << " Hammer of Justice and Bash rows\n";
}

// ---------------------------------------------------------------------------
// 7c. Replacing a control in place - the same caster recasting the same rank,
//     or landing a higher rank over a lower one - is a REFRESH of one aura, not
//     a removal and a re-application. Unit::_TryStackingOrRefreshingExistingAura
//     never calls ApplyDiminishingAura, so the group's stack must not dip to
//     zero, the removal stamp must not move, and the stack must not climb
//     either. Both errors are silent until the window: a dip restamps hitTime at
//     the recast and a climb leaves a stack that no expiry can bring to zero, so
//     the record never decays at all.
// ---------------------------------------------------------------------------
void replaceInPlaceIsRefresh() {
    // --- same rank, same caster: Hammer of Justice rank 4 recast on itself ---
    uint32_t sameRankFirst = 0, sameRankRefreshed = 0, sameRankThird = 0, sameRankAfterWindow = 0;
    uint64_t sameRankStamp = 0, sameRankRemoval = 0;
    {
        DrWorld w; buildWorld(w, {10308}, 2, 2);
        teach(w, {10308});
        auto& owner = w.casters[0];
        assert(castUntilResolved(w.game, owner, w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Hit);
        const auto* n = findNpc(w.game, w.npcGuid);
        for (const auto& a : n->controls) if (a.casterGuid == owner.guid) sameRankFirst = a.remainingMs;
        const auto* r = record(*n, LocalDiminishingGroup::ControlledStun);
        assert(r && r->stack == 1 && r->hitCount == kLocalDiminishingLevel2);
        sameRankStamp = r->hitTimeMs;
        // Move the clock so a restamp - which can only be written when the
        // stack passes through zero - would be observable as a changed value.
        advance(w, 1000);
        // castUntilResolved clears the 60,000 ms category cooldown before each
        // attempt, which is what makes a recast of the same rank possible inside
        // the running control; nothing about the definition is altered.
        assert(castUntilResolved(w.game, owner, w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Hit);
        n = findNpc(w.game, w.npcGuid);
        // One control, not two: the slot was replaced, and the refresh is a HIT
        // that Spell::DoSpellHitOnUnit reads and increments like any other, so
        // the refreshed duration is rung 2 of the ladder.
        unsigned owned = 0;
        for (const auto& a : n->controls) if (a.casterGuid == owner.guid) { ++owned; sameRankRefreshed = a.remainingMs; }
        assert(owned == 1 && n->controls.size() == 1);
        assert(sameRankRefreshed == 3000);
        r = record(*n, LocalDiminishingGroup::ControlledStun);
        assert(r && r->hitCount == kLocalDiminishingLevel3);
        assert(r->stack == 1);                 // neither 0 (dipped) nor 2 (climbed)
        assert(r->hitTimeMs == sameRankStamp); // never restamped: the stack never reached zero
        // The second caster reads rung 3 from the same record.
        assert(castUntilResolved(w.game, w.casters[1], w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Hit);
        n = findNpc(w.game, w.npcGuid);
        for (const auto& a : n->controls) if (a.casterGuid == w.casters[1].guid) sameRankThird = a.remainingMs;
        assert(sameRankThird == 1500);
        // The second caster's Hammer replaced the first's (the implementation, P04 competing
        // auras: SpellAuras.cpp:2160-2177) - one control, stack 1; the implementation's
        // fixture asserted two coexisting controls and a stack of 2.
        assert(n->controls.size() == 1 && n->controls[0].casterGuid == w.casters[1].guid);
        assert(record(*n, LocalDiminishingGroup::ControlledStun)->stack == 1);
        // Let the control run out. A stack that climbed on the refresh would
        // be left at 1 here, and the record would never stamp and never decay.
        advance(w, 3000);
        n = findNpc(w.game, w.npcGuid);
        assert(n->controls.empty());
        r = record(*n, LocalDiminishingGroup::ControlledStun);
        assert(r && r->stack == 0 && r->hitCount == kLocalDiminishingLevelImmune);
        assert(r->hitTimeMs > sameRankStamp);
        sameRankRemoval = r->hitTimeMs;
        // and 15,001 ms after that removal the ladder is back at rung 1.
        advance(w, 15000);
        w.game.tick(.001f, w.players);
        assert(castUntilResolved(w.game, w.casters[1], w.npcGuid, 10308, w.players) == LocalMeleeOutcome::Hit);
        n = findNpc(w.game, w.npcGuid);
        for (const auto& a : n->controls) if (a.casterGuid == w.casters[1].guid) sameRankAfterWindow = a.remainingMs;
        assert(sameRankAfterWindow == 6000);
    }
    assert(sameRankFirst == 6000);

    // --- rank upgrade, same caster: Bash rank 1 replaced by Bash rank 3 -----
    // Bash is SPELL_DAMAGE_CLASS_MELEE and rolls no magic hit, so every cast is
    // decisive; 5211 -> 6798 -> 8983 is the client's own SkillLineAbility
    // supersession chain, which is what makes the rank-3 cast land in the
    // rank-1 control's slot rather than beside it.
    const auto& bashOne = real(5211);
    const auto& bashThree = real(8983);
    assert(bashOne.durationMs == 2000 && bashThree.durationMs == 4000);
    assert(bashOne.supercededBySpell == 6798 && real(6798).supercededBySpell == 8983);
    uint32_t rankOneApplied = 0, rankThreeApplied = 0, rankDowngradeApplied = 0;
    uint64_t rankStamp = 0;
    {
        DrWorld w; buildWorld(w, {5211, 6798, 8983}, 2, 11, LocalResourceType::Rage, 0, 100000000, 2);
        teach(w, {5211, 6798, 8983});
        for (auto& c : w.casters) c.formSpellId = 5487; // Bear Form
        auto& owner = w.casters[0];
        assert(castUntilResolved(w.game, owner, w.npcGuid, 5211, w.players) == LocalMeleeOutcome::Hit);
        const auto* n = findNpc(w.game, w.npcGuid);
        for (const auto& a : n->controls) if (a.casterGuid == owner.guid) rankOneApplied = a.remainingMs;
        assert(rankOneApplied == 2000);
        const auto* r = record(*n, LocalDiminishingGroup::ControlledStun);
        assert(r && r->stack == 1 && r->hitCount == kLocalDiminishingLevel2);
        rankStamp = r->hitTimeMs;
        advance(w, 500);
        assert(castUntilResolved(w.game, owner, w.npcGuid, 8983, w.players) == LocalMeleeOutcome::Hit);
        n = findNpc(w.game, w.npcGuid);
        unsigned owned = 0; uint32_t heldSpell = 0;
        for (const auto& a : n->controls) if (a.casterGuid == owner.guid) { ++owned; heldSpell = a.spellId; rankThreeApplied = a.remainingMs; }
        assert(owned == 1 && n->controls.size() == 1 && heldSpell == 8983);
        assert(rankThreeApplied == 2000); // 4,000 ms at rung 2
        r = record(*n, LocalDiminishingGroup::ControlledStun);
        assert(r && r->stack == 1 && r->hitCount == kLocalDiminishingLevel3);
        assert(r->hitTimeMs == rankStamp);
        // The other direction replaces too (the implementation, P04 competing auras): a
        // lower rank over a higher one is Aura::CanStackWith -> IsRankOf ->
        // false -> RemoveOwnedAuras (SpellAuras.cpp:2160-2177); there is no
        // rank comparison and no refusal anywhere in the reference. the implementation's
        // fixture asserted the build's own "A higher control rank is already
        // active" refusal, which had no source, and was rewritten here. The
        // rank-1 cast is a hit, so Spell::DoSpellHitOnUnit steps the ladder to
        // rung 3 (25 % of 2,000 ms = 500 ms) and IncrDiminishing to immune; the
        // replacement within one group is a refresh, so the stack stays 1 and
        // the removal stamp is untouched.
        assert(castUntilResolved(w.game, owner, w.npcGuid, 5211, w.players) == LocalMeleeOutcome::Hit);
        n = findNpc(w.game, w.npcGuid);
        owned = 0; heldSpell = 0;
        for (const auto& a : n->controls) if (a.casterGuid == owner.guid) { ++owned; heldSpell = a.spellId; rankDowngradeApplied = a.remainingMs; }
        assert(owned == 1 && n->controls.size() == 1 && heldSpell == 5211 && rankDowngradeApplied == 500);
        r = record(*n, LocalDiminishingGroup::ControlledStun);
        assert(r && r->stack == 1 && r->hitCount == kLocalDiminishingLevelImmune && r->hitTimeMs == rankStamp);
        // Expiry of the single replaced control brings the stack to exactly zero.
        advance(w, 2000);
        n = findNpc(w.game, w.npcGuid);
        assert(n->controls.empty());
        r = record(*n, LocalDiminishingGroup::ControlledStun);
        assert(r && r->stack == 0 && r->hitTimeMs > rankStamp);
        assert(validLocalNpcDiminishing(*n));
    }

    // --- the shipped helper, stated directly ---------------------------------
    // A refresh calls neither arm of localDiminishingApply; the apply site
    // (local_gameplay.cpp, "A replacement within one group is a refresh") only
    // lowers-and-raises when the replaced control's group differs from the new
    // one. That predicate is asserted on the classifier: every rank pair that
    // can replace another in this build shares a group.
    for (auto pair : {std::pair<uint32_t, uint32_t>{853, 10308}, {5588, 10308}, {5589, 10308},
                      {5211, 8983}, {6798, 8983}, {5211, 6798}})
        assert(localDiminishingGroupForSpell(real(pair.first), false) ==
               localDiminishingGroupForSpell(real(pair.second), false));

    std::cout << "PASS replace-in-place refresh: a Paladin recasting real 10308 Hammer of Justice "
                 "rank 4 on its own running control 1,000 ms in replaced the slot rather than adding "
                 "one, applied " << sameRankRefreshed << " ms (rung 2 of " << sameRankFirst
              << ") and left the CONTROLLED_STUN record at stack 1 with its removal stamp unchanged ("
              << sameRankStamp << "), so the stack neither dipped to zero nor climbed to two; a "
                 "second Paladin then read rung 3 (" << sameRankThird << " ms) and replaced the first's control "
                 "(one held, stack 1 - the reference), the control "
                 "expired to a stack of exactly 0 stamped at " << sameRankRemoval
              << ", and 15,001 ms later the next cast applied the full " << sameRankAfterWindow
              << " ms - the decay a climbed stack would have blocked forever. A Druid landing real "
                 "8983 Bash rank 3 over its own 5211 rank 1 (" << rankOneApplied << " ms) likewise "
                 "replaced the slot at " << rankThreeApplied << " ms with stack 1 and the same stamp, "
                 "the rank-1 downgrade then replaced rank 3 in its slot at " << rankDowngradeApplied
              << " ms (rung 3, the ladder stepped to immune, stack 1, the same stamp - the reference "
                 "replaces a lower rank over a higher one, SpellAuras.cpp:2160-2177, and the reference dropped the "
                 "sourceless refusal), and the single replaced control expired to stack 0\n";
}

// ---------------------------------------------------------------------------
// 8. The save carries nothing of this group's, the NPC wire is untouched, and
//    the record never
//    reaches the wire. (LAN 83 -> 84 at the implementation for the melee view's resisted
//    amount, which is a player-page field; nothing here moved.)
// ---------------------------------------------------------------------------
void formatsUntouched() {
    // the implementation moved both, for the pet roster: writePet gained the pet's
    // command state, react state and stay point, and that one layout is
    // shared by the character save and the LAN pet deck. Nothing this
    // group measures moved with it.
    static_assert(SaveVersion == 30);
    // LAN 85 since the implementation; `resisted` was appended to each melee view at the reference.
    // The NPC record this group measures did not move at either.
    static_assert(Version == 85);
    static_assert(Version == lan::GameplayVersion);

    LocalWorldContent c;
    LocalNpcDefinition definition; definition.id = 50; definition.name = "Codec NPC"; definition.displayId = 100;
    c.npcs.push_back(definition);
    for (uint32_t id : {853u, 10308u, 15487u}) c.spells.push_back(real(id));
    std::sort(c.spells.begin(), c.spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

    LocalRealmNpc n; n.guid = 0xf130000000000001ULL; n.entry = 50; n.level = 20;
    n.health = 80; n.maxHealth = 100; n.playerThreat.viewerGuid = 41;
    n.controls = {{853, 3000, 41, 11, uint8_t(LocalNpcControlKind::Stun)},
                  {15487, 5000, 42, 12, uint8_t(LocalNpcControlKind::Silence)}};
    assert(validLocalNpcControls(n, c));
    Writer plain; writeNpc(plain, n);

    auto withRecord = n;
    for (unsigned i = 0; i < kLocalMaxNpcDiminishing; ++i)
        withRecord.diminishing.push_back({uint8_t(uint8_t(LocalDiminishingGroup::ControlledStun) + i),
                                          kLocalDiminishingLevelImmune, 1, 0xffffffffu});
    assert(withRecord.diminishing.size() == kLocalMaxNpcDiminishing);
    assert(validLocalNpcDiminishing(withRecord));
    Writer loaded; writeNpc(loaded, withRecord);
    // Byte-identical: the record costs nothing on the wire because it is not
    // on the wire, which is the whole reason LAN 83 did not move.
    assert(loaded.bytes == plain.bytes);
    assert(validLocalNpcControls(withRecord, c));
    {
        Reader r(loaded.bytes.data(), loaded.bytes.size());
        auto copy = readNpc(r, c);
        assert(r.valid && r.done());
        assert(copy.controls.size() == withRecord.controls.size());
        assert(copy.diminishing.empty()); // a guest never receives one
    }
    // The validator is a real bound, not decoration.
    {
        auto bad = withRecord; bad.diminishing.push_back({uint8_t(LocalDiminishingGroup::Fear), 0, 0, 0});
        assert(!validLocalNpcDiminishing(bad));
        bad = withRecord; bad.diminishing[0].group = 0;
        assert(!validLocalNpcDiminishing(bad));
        bad = withRecord; bad.diminishing[1].group = bad.diminishing[0].group;
        assert(!validLocalNpcDiminishing(bad));
        bad = withRecord; bad.diminishing[0].hitCount = kLocalDiminishingLevelTauntImmune;
        assert(!validLocalNpcDiminishing(bad)); // rung 4 belongs to taunt alone
        bad = withRecord; bad.diminishing[0].group = uint8_t(LocalDiminishingGroup::Taunt);
        bad.diminishing[0].hitCount = kLocalDiminishingLevelTauntImmune;
        assert(validLocalNpcDiminishing(bad));  // and taunt may hold it
        bad = withRecord; bad.diminishing[0].stack = kLocalMaxNpcControls + 1;
        assert(!validLocalNpcDiminishing(bad));
    }

    // NPC state is not persisted, so the player block cannot have moved.
    LocalRealmPlayer p; p.guid = 1; p.classId = 2; p.level = 80; p.money = 321;
    p.knownSpells = {10308, 20066};
    Writer current; writeProgress(current, p);
    LocalRealmPlayer restored; restored.guid = 1;
    Reader read(current.bytes.data(), current.bytes.size());
    assert(readProgress(read, restored) && read.done());
    assert(restored.money == p.money && restored.knownSpells == p.knownSpells);
    // The per-player block last grew at save 29 (the area emitters); save 30
    // grew the realm-level PET roster instead, so the migration is pinned at
    // its own boundary, 28 -> 29, and 30 is asserted to have left the player
    // block byte-identical.
    Writer previous; writeProgress(previous, p, 28);
    Writer atTwentyNine; writeProgress(atTwentyNine, p, 29);
    assert(previous.bytes.size() < atTwentyNine.bytes.size());
    assert(atTwentyNine.bytes == current.bytes);
    LocalRealmPlayer legacy; legacy.guid = 1;
    Reader old(previous.bytes.data(), previous.bytes.size());
    assert(readProgress(old, legacy, 28) && old.done());

    std::cout << "PASS save" << int(SaveVersion) << " / LAN" << int(Version)
              << " untouched: a creature carrying " << kLocalMaxNpcDiminishing
              << " diminishing records at DIMINISHING_LEVEL_IMMUNE with a live stack and a saturated "
                 "hitTime encodes to the identical " << plain.bytes.size()
              << " bytes as the same creature with none, readNpc hands the guest an empty list, "
                 "validLocalNpcDiminishing refuses an over-capacity list, group 0, a duplicate group, "
                 "a non-taunt rung 4 and a stack above kLocalMaxNpcControls while accepting taunt's "
                 "own rung 4, and the save" << int(SaveVersion) << " player block still round-trips "
                 "with save28 a strictly shorter readable prefix of save29, which is byte-identical to the current one\n";
}
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    assert(argc == 2);
    ClientTables tables;
    tables.load(argv[1]);
    gTables = &tables;
    gImported = tables.import();
    mechanicConstants();
    classificationCensus();
    ladderDrivenForReal();
    immuneReachesEverything();
    seedsAtLevelTwo();
    windowFromRemoval();
    groupsAndClearing();
    playerGroupsUnaffected();
    firstFrameClock();
    impactRetired();
    replaceInPlaceIsRefresh();
    formatsUntouched();
    if (gFailures)
        std::cerr << gFailures << " group(s) failed against "
                     "source-backed fixtures\n";
    return gFailures ? 1 : 0;
}
