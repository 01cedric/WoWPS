// P05 / the implementation - the Spell.dbc level columns and line of sight.
//
// Two things are proved here.
//
// One: Spell.dbc column 38 is BaseLevel and column 39 is SpellLevel
// (DBCStructure.h:1679-1680). previously decodeClientSpell read column 39 for
// both, so the definition carried SpellLevel twice and the real BaseLevel
// nowhere. This suite measures the column identity against the client's own
// bytes, names the accepted definitions the two columns disagree on, and then
// drives every consumer of the corrected field through the shipped authority -
// the level scaling of real cast amounts at several character levels, the
// trainer and unlock sets for every class, the trainer price, the ward level
// penalty and the proc amount - before and after.
//
// Two: line of sight. The authority holds no collision data and none could be
// produced in the environment this was written in, which had no client MPQ and
// no extracted WMO/M2/ADT asset. What is proved is therefore the runtime half
// against geometry authored in this file - a wall between a caster and its
// target - and the inert behaviour with nothing installed, which is the state
// the build ships in.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33.
//   SpellEntry field order            DBCStructure.h:1675-1682.
//   SpellEffectInfo::CalcValue        SpellInfo.cpp:414-431 - the clamp between
//       BaseLevel and MaxLevel and the max(BaseLevel, SpellLevel) subtrahend.
//   Unit::CalculateLevelPenalty       Unit.cpp:3208-3223 - SpellLevel, not BaseLevel.
//   Spell::CheckCast                  Spell.cpp:6092-6118 - the line-of-sight gate
//       and its two attribute escapes.
//   WorldObject::IsWithinLOSInMap     Object.cpp:1412-1436.
//   WorldObject::GetHitSpherePointFor Object.cpp:1295-1302.
//   Map::isInLineOfSight              Map.cpp:1545-1582; WorldConfig.cpp:558.
//   StaticMapTree::isInLineOfSight    MapTree.cpp:129-149.
//   BIH::intersectRay                 BoundingIntervalHierarchy.h:117-276.
//   VMAP::IntersectTriangle           WorldModel.cpp:34-84.
//   ObjectMgr::LoadTrainers           ObjectMgr.cpp:9940-9965 - ReqLevel is a
//       world-database column, which is why this realm's unlock level is a
//       documented local stand-in.
#include "game/local_bots.hpp"
#define private public
#include "game/local_realm.hpp"
#undef private
#include "../../src/game/local_realm.cpp"
#include "local_group_rewards_fixture.hpp"
#include "game/local_collision.hpp"
#include "game/local_spell_critical.hpp"
#include "game/local_line_of_sight.hpp"
#include "game/local_proc_rules.hpp"
#include "game/local_services.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_spell_range.hpp"
#include "game/local_ward_reflection.hpp"
#include <algorithm>
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
    void indexColumns() {
        const auto* spells = get("Spell");
        for (uint32_t row = 0; row < spells->getRecordCount(); ++row)
            rows[spells->getUInt32(row, 0)] = row;
    }
    uint32_t column(uint32_t spellId, uint32_t col) {
        const auto it = rows.find(spellId);
        return it == rows.end() ? 0 : get("Spell")->getUInt32(it->second, col);
    }
    bool has(uint32_t spellId) const { return rows.count(spellId) != 0; }
    std::map<uint32_t, uint32_t> rows;
};
LocalSpellImport gImported;
ClientTables* gTables = nullptr;

const LocalSpellDefinition& real(uint32_t id) {
    for (const auto& d : gImported.spells) if (d.id == id) return d;
    std::cerr << "FAIL: spell " << id << " is absent from the client import\n";
    std::abort();
}
bool accepted(const LocalSpellDefinition& d) { return d.clientSpell && d.unsupportedReason.empty(); }
bool decodedGenerically(const LocalSpellDefinition& d) { return !d.npcOnly && !d.triggeredOnly; }

// ---------------------------------------------------------------------------
// 1. The column identity, measured against the client's own bytes.
// ---------------------------------------------------------------------------
std::set<uint32_t> gDisagree;          // accepted, non-passive, generically decoded
std::set<uint32_t> gDisagreeAll;       // every accepted definition
void columnIdentity() {
    const auto* spells = gTables->get("Spell");
    unsigned rows = 0, differ = 0, baseBelow = 0, baseAbove = 0;
    unsigned shapeBaseZero = 0, shapeSpellZero = 0, shapeBoth = 0;
    for (uint32_t row = 0; row < spells->getRecordCount(); ++row) {
        ++rows;
        const uint32_t base = spells->getUInt32(row, 38), spell = spells->getUInt32(row, 39);
        if (base == spell) continue;
        ++differ;
        if (base < spell) ++baseBelow; else ++baseAbove;
        if (!base) ++shapeBaseZero; else if (!spell) ++shapeSpellZero; else ++shapeBoth;
    }
    expect(rows == 49839, "the client's Spell.dbc still holds 49,839 rows");
    expect(differ == 3090 && baseBelow == 2442 && baseAbove == 648,
           "columns 38 and 39 differ on 3,090 rows: 2,442 with BaseLevel below SpellLevel and 648 above");
    expect(shapeBaseZero == 2397 && shapeSpellZero == 634 && shapeBoth == 59,
           "2,397 of those have BaseLevel 0, 634 SpellLevel 0 and 59 two different non-zero levels");

    // Every accepted definition the generic decoder produced now carries column
    // 38 in baseLevel and column 39 in spellLevel.
    unsigned genericMismatch = 0, mountMismatch = 0;
    std::set<uint32_t> separatePath;
    unsigned mounts = 0;
    for (const auto& d : gImported.spells) {
        if (!accepted(d) || !gTables->has(d.id)) continue;
        const uint32_t base = gTables->column(d.id, 38), spell = gTables->column(d.id, 39);
        if (base != spell) {
            gDisagreeAll.insert(d.id);
            if (decodedGenerically(d) && !d.passive) gDisagree.insert(d.id);
        }
        if (!decodedGenerically(d)) { if (d.baseLevel != base) separatePath.insert(d.id); continue; }
        if (d.baseLevel != base) ++genericMismatch;
        if (d.mountDisplayId) {
            ++mounts;
            // decodeClientGroundMount carries BaseLevel and deliberately not
            // SpellLevel: SpellLevel is the initial-threat term (Spell.cpp:5780)
            // and a mount never lands on a target.
            if (d.spellLevel != 0) ++mountMismatch;
        } else if (d.spellLevel != uint16_t(std::min<uint32_t>(spell, 65535))) ++genericMismatch;
    }
    expect(!genericMismatch, "every generically decoded accepted definition carries column 38 as baseLevel and column 39 as spellLevel");
    expect(!mountMismatch, "the ground-mount decoder carries BaseLevel alone, as it did before the reference");
    // The definitions that do not come out of decodeClientSpell - the two
    // NPC-decoded spells, the hand-built Stormstrike children and every
    // triggered child a profile decoder synthesises - carry a level their own
    // decoder sets rather than a column it reads. the implementation does not touch them:
    // none is a player's explicit cast, so none reaches a level rule through
    // any path but its parent's.
    for (auto id : separatePath) {
        const auto& d = real(id);
        expect(d.triggeredOnly || d.npcOnly,
               "spell " + std::to_string(id) + " carries a hand-set level and is triggered or NPC-only");
    }
    // 5401 and 11985 are not in that set: local_npc_spell_import.hpp:34 sets
    // their level to 5 and 20 by hand and the client's BaseLevel happens to
    // agree, which is why the implementation's own NOTE line did not list them either.
    expect(real(5401).baseLevel == gTables->column(5401, 38) && real(11985).baseLevel == gTables->column(11985, 38),
           "the two NPC-decoded spells' hand-set levels happen to equal their BaseLevel column");
    expect(gDisagree.size() == 182, "182 accepted, non-passive, generically decoded definitions had the wrong column before the reference");
    // 274 at the implementation over the 990; 278 since the implementation's census admitted four more
    // whose columns differ - the three Tactical Mastery ranks (BaseLevel 0
    // against SpellLevel 1, which the unlock floor absorbs) and Stance Mastery
    // 12678 (BaseLevel 0 against SpellLevel **20**, which it does not).
    expect(gDisagreeAll.size() == 278, "278 accepted definitions in all, 274 of them at the reference plus the reference's four");
    // The shape of the 182 is what decides the blast radius: every one of them
    // is BaseLevel 0 against SpellLevel 1.
    unsigned zeroAgainstOne = 0, disagreeingMounts = 0;
    for (auto id : gDisagree) {
        if (!gTables->column(id, 38) && gTables->column(id, 39) == 1) ++zeroAgainstOne;
        if (real(id).mountDisplayId) ++disagreeingMounts;
    }
    expect(zeroAgainstOne == 182, "every one of the 182 is BaseLevel 0 against SpellLevel 1");
    expect(gTables->column(16268, 38) == 1 && gTables->column(16268, 39) == 0 && real(16268).baseLevel == 1,
           "the one accepted row whose BaseLevel exceeds its SpellLevel is the passive Spirit Weapons 16268, 1 against 0");
    expect(real(116).baseLevel == 4 && real(116).spellLevel == 4 && real(116).maxLevel == 8,
           "Frostbolt rank 1 carries BaseLevel 4, SpellLevel 4 and MaxLevel 8");
    std::cout << "PASS column identity: Spell.dbc column 38 is BaseLevel and 39 SpellLevel (DBCStructure.h:1679-1680); they differ on "
              << differ << " of " << rows << " client rows (" << baseBelow << " BaseLevel below, " << baseAbove
              << " above) and on " << gDisagreeAll.size() << " accepted definitions, " << gDisagree.size()
              << " of them non-passive and generically decoded - every one of those BaseLevel 0 against SpellLevel 1, "
              << disagreeingMounts << " of them ground mounts out of " << mounts
              << " accepted mounts in all; " << separatePath.size()
              << " accepted definitions carry a level their own decoder sets rather than either column, every one of them "
                 "triggered or NPC-only; the 182 ids are " << join(gDisagree) << "\n";
}

// ---------------------------------------------------------------------------
// 2. The amount consumers, driven.
// ---------------------------------------------------------------------------
// The the implementation expression, verbatim from that release's local_gameplay.cpp:
//     effective = maxLevel ? min(level, maxLevel) : level
//     amount    = (low + max(low, high)) / 2 + max(0, effective - column39) * scale
double amount0247(uint32_t level, uint32_t column39, uint32_t maxLevel,
                  uint32_t low, uint32_t high, float scale) {
    const uint32_t effective = maxLevel ? std::min(level, maxLevel) : level;
    return (double(low) + std::max(low, high)) * 0.5 +
           double(effective > column39 ? effective - column39 : 0) * double(scale);
}
// SpellInfo.cpp:414-431, which is what the implementation runs.
double amountReference(uint32_t casterLevel, uint32_t baseLevel, uint16_t spellLevel, uint32_t maxLevel,
                       uint32_t low, uint32_t high, float scale) {
    double amount = (double(low) + std::max(low, high)) * 0.5;
    if (scale != 0.f) {
        int64_t level = int64_t(casterLevel);
        if (level > int64_t(maxLevel) && maxLevel > 0) level = int64_t(maxLevel);
        else if (level < int64_t(baseLevel)) level = int64_t(baseLevel);
        level -= int64_t(std::max<uint32_t>(baseLevel, spellLevel));
        amount += double(level) * double(scale);
    }
    return amount;
}

struct World {
    std::shared_ptr<LocalWorldContent> content;
    LocalGameplay game;
    std::vector<LocalRealmPlayer> casters;
    std::vector<LocalRealmPlayer*> players;
    uint64_t npcGuid = 0;
    float npcX = 0;
};
void buildWorld(World& w, const std::vector<uint32_t>& spells, uint8_t classId, uint8_t level, float npcX = 5.f) {
    w.content = rewardContent();
    for (auto id : spells) w.content->spells.push_back(real(id));
    std::sort(w.content->spells.begin(), w.content->spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    auto& enemy = w.content->npcs[0];
    enemy.health = 100000000; enemy.damage = 0; enemy.aggroRadius = 0.01f; enemy.xp = 0; enemy.money = 0; enemy.loot.clear();
    w.game.useContent(w.content);
    auto p = rewardPlayer(1);
    p.classId = classId; p.level = level; p.health = p.maxHealth = 100000000;
    p.mana = p.maxMana = 10000000; p.resourceType = LocalResourceType::Mana;
    p.x = p.y = p.z = 0; p.orientation = 0; p.quests.clear();
    p.knownSpells = {1};
    for (auto id : spells) p.knownSpells.push_back(id);
    w.casters.push_back(p);
    w.players.clear();
    for (auto& c : w.casters) w.players.push_back(&c);
    w.game.tick(0, w.players);
    auto n = rewardNpc(10);
    n.health = n.maxHealth = 100000000; n.hostile = true; n.lootOwner = 0;
    n.x = n.homeX = npcX; n.y = n.homeY = 0; n.z = n.homeZ = 0; n.orientation = float(M_PI);
    n.targetGuid = w.casters[0].guid; n.threat[0] = {w.casters[0].guid, 1000};
    w.game.setRemoteNpcs({n});
    w.npcGuid = n.guid; w.npcX = npcX;
    w.game.tick(.25f, w.players);
}
void pin(World& w) {
    auto npcs = w.game.npcs();
    bool changed = false;
    for (auto& n : npcs) if (n.guid == w.npcGuid && (n.x != w.npcX || n.y != 0 || n.z != 0)) {
        n.x = w.npcX; n.y = 0; n.z = 0; changed = true;
    }
    if (changed) w.game.setRemoteNpcs(npcs);
}
uint64_t lastSequence(const LocalGameplay& game) {
    uint64_t s = 0;
    for (const auto& e : game.combatEvents()) s = std::max(s, e.sequence);
    return s;
}
struct CastResult { bool executed = false; std::string result; std::optional<LocalCombatEvent> event; };
CastResult castOnce(World& w, uint32_t spellId, uint64_t target, LocalCombatEventKind kind) {
    auto& p = w.casters[0];
    p.globalCooldownMs = 0; p.cooldowns.clear(); p.categoryCooldowns.clear();
    p.mana = p.maxMana; p.comboPoints = 0; p.comboTarget = 0;
    p.attackTarget = 0; p.attackTimer = p.offHandTimer = 1000.f;
    const auto before = lastSequence(w.game);
    CastResult r;
    r.executed = w.game.execute(p, {LocalAction::CastSpell, target, spellId}, w.players, r.result);
    if (!r.executed) return r;
    for (unsigned t = 0; t < 200 && p.castStatus == LocalCastStatus::Casting; ++t) { pin(w); w.game.tick(.05f, w.players); pin(w); }
    for (const auto& e : w.game.combatEvents())
        if (e.sequence > before && e.spell == spellId && e.kind == kind) r.event = e;
    return r;
}

void amountConsumers() {
    // How many of the 182 carry a per-level scale at all - the measurement that
    // decides whether the corrected column can move an amount.
    unsigned scaled = 0, scaledAccepted = 0, withProcScale = 0;
    for (const auto& d : gImported.spells) {
        if (!accepted(d)) continue;
        const bool hasScale = d.damagePerLevel != 0.f || d.healPerLevel != 0.f ||
                              d.periodicDamagePerLevel != 0.f || d.periodicHealPerLevel != 0.f;
        if (hasScale) ++scaledAccepted;
        if (hasScale && gDisagreeAll.count(d.id)) ++scaled;
        if (d.proc.amountPerLevel != 0.f && gDisagreeAll.count(d.id)) ++withProcScale;
    }
    expect(!scaled && !withProcScale,
           "not one of the definitions the two columns disagree on carries a per-level amount scale");

    // Every accepted definition, at every level 1..80: does the corrected read
    // move the amount the reference would compute?
    unsigned movedDirect = 0, movedHeal = 0, comparisons = 0;
    for (const auto& d : gImported.spells) {
        if (!accepted(d) || !gTables->has(d.id)) continue;
        const uint32_t column39 = gTables->column(d.id, 39);
        for (uint32_t level = 1; level <= 80; ++level) {
            if (d.damagePerLevel != 0.f) {
                ++comparisons;
                if (std::abs(amountReference(level, d.baseLevel, d.spellLevel, d.maxLevel, d.damage, d.damageMax, d.damagePerLevel) -
                             amount0247(level, column39, d.maxLevel, d.damage, d.damageMax, d.damagePerLevel)) > 1e-6) ++movedDirect;
            }
            if (d.healPerLevel != 0.f) {
                ++comparisons;
                if (std::abs(amountReference(level, d.baseLevel, d.spellLevel, d.maxLevel, d.heal, d.healMax, d.healPerLevel) -
                             amount0247(level, column39, d.maxLevel, d.heal, d.healMax, d.healPerLevel)) > 1e-6) ++movedHeal;
            }
        }
    }
    expect(!movedDirect && !movedHeal,
           "no accepted definition's scaled amount moves at any level between 1 and 80");

    // And the shipped tick agrees with the reference's own arithmetic, driven.
    unsigned driven = 0;
    for (const uint32_t id : {116u, 133u, 143u, 145u}) {
        const auto& d = real(id);
        for (uint32_t level : {1u, 5u, 12u, 40u, 80u}) {
            World w; buildWorld(w, {id}, 8, uint8_t(level));
            auto r = castOnce(w, id, w.npcGuid, LocalCombatEventKind::SpellDamage);
            if (!expect(r.executed, "Frostbolt/Fireball casts at level " + std::to_string(level))) continue;
            // The magic hit roll and the resist roll can take a cast away
            // entirely; only a cast that produced an amount has one to check.
            unsigned tries = 0;
            auto landed = r;
            while ((!landed.event || landed.event->outcome == LocalMeleeOutcome::Miss ||
                    landed.event->outcome == LocalMeleeOutcome::Resist) && tries++ < 40)
                landed = castOnce(w, id, w.npcGuid, LocalCombatEventKind::SpellDamage);
            if (!expect(landed.event && landed.event->outcome != LocalMeleeOutcome::Miss &&
                        landed.event->outcome != LocalMeleeOutcome::Resist,
                        "spell " + std::to_string(id) + " lands at least once in 40 casts at level " + std::to_string(level)))
                continue;
            r = landed;
            const auto expectedAmount = uint32_t(std::clamp(
                amountReference(level, d.baseLevel, d.spellLevel, d.maxLevel, d.damage, d.damageMax, d.damagePerLevel), 0.0, 1000000.0));
            expect(r.event->attempted == expectedAmount ||
                   r.event->attempted == localMagicCriticalAmount(expectedAmount),
                   "spell " + std::to_string(id) + " at level " + std::to_string(level) + " lands SpellEffectInfo::CalcValue's amount"
                   " (attempted " + std::to_string(r.event->attempted) + ", expected " + std::to_string(expectedAmount) +
                   " from low " + std::to_string(d.damage) + " high " + std::to_string(d.damageMax) +
                   " base " + std::to_string(d.baseLevel) + " spell " + std::to_string(d.spellLevel) +
                   " max " + std::to_string(d.maxLevel) + ")");
            expect(std::abs(amount0247(level, gTables->column(id, 39), d.maxLevel, d.damage, d.damageMax, d.damagePerLevel) -
                            double(expectedAmount)) < 1.0,
                   "and the reference's expression gave the same number for spell " + std::to_string(id));
            ++driven;
        }
    }
    // The proc amount rule, over the 28 accepted procs that carry a level.
    unsigned procsWithLevel = 0, procColumnMismatch = 0;
    for (const auto& d : gImported.spells) {
        if (!accepted(d) || !d.proc.baseLevel || !gTables->has(d.id)) continue;
        ++procsWithLevel;
        if (gTables->column(d.id, 38) != gTables->column(d.id, 39) && decodedGenerically(d)) ++procColumnMismatch;
    }
    expect(procsWithLevel == 28 && !procColumnMismatch,
           "28 accepted definitions carry a proc level, and columns 38 and 39 agree on every one of them");
    // The ward level penalty is SpellLevel's rule, not BaseLevel's
    // (Unit.cpp:3208-3223). Reading the right field changes no ward.
    unsigned wardMoved = 0, wards = 0;
    for (const auto& d : gImported.spells) {
        if (!accepted(d) || !d.wardProfile) continue;
        ++wards;
        for (uint32_t level = 1; level <= 80; ++level) {
            LocalSpellDefinition old = d; old.spellLevel = uint16_t(gTables->column(d.id, 39));
            if (std::abs(localWardLevelPenalty(level, d) - localWardLevelPenalty(level, old)) > 1e-6f) ++wardMoved;
        }
    }
    expect(wards > 0 && !wardMoved, "the ward level penalty is unchanged at every level for every ward rank");
    std::cout << "PASS amount consumers: " << scaledAccepted << " accepted definitions carry a per-level amount scale and "
              << scaled << " of the " << gDisagreeAll.size() << " whose columns disagree do; " << comparisons
              << " (definition, level) pairs were compared between the reference's expression and SpellEffectInfo::CalcValue "
              << "(SpellInfo.cpp:414-431) and " << (movedDirect + movedHeal) << " moved; " << driven
              << " real casts were driven through the shipped tick at levels 1, 5, 12, 40 and 80 and every one landed "
              << "the reference's amount; " << procsWithLevel << " procs carry a level and " << wards
              << " ward ranks were re-measured at 80 levels each with no change\n";
}

// ---------------------------------------------------------------------------
// 3. The trainer and unlock sets, before and after.
// ---------------------------------------------------------------------------
void trainerAndUnlock() {
    // The unlock level and the price over every accepted definition.
    unsigned unlockMoved = 0, costMoved = 0, definitions = 0;
    for (const auto& d : gImported.spells) {
        if (!accepted(d) || !gTables->has(d.id)) continue;
        ++definitions;
        LocalSpellDefinition old = d; old.baseLevel = gTables->column(d.id, 39); // the the implementation read
        if (localSpellUnlockLevel(d) != localSpellUnlockLevel(old)) ++unlockMoved;
        if (localTrainerSpellCost(d) != localTrainerSpellCost(old)) ++costMoved;
    }
    // the implementation measured 0 and 0 over the 990, because every accepted definition
    // whose columns differed was BaseLevel 0 against SpellLevel 1 and the floor
    // at 1 absorbs that. the implementation admits Stance Mastery 12678 - BaseLevel 0 against
    // SpellLevel 20 - and localSpellUnlockLevel now reads
    // max(BaseLevel, SpellLevel) for exactly that reason, so this comparison
    // against the the implementation read moves on that one definition and on no other.
    // Still zero, and now for a stronger reason than the floor at 1: the rule
    // reads max(BaseLevel, SpellLevel), and substituting SpellLevel for
    // BaseLevel cannot change a maximum that already includes SpellLevel.
    expect(!unlockMoved && !costMoved,
           "no accepted definition's unlock level or trainer price moves, because the rule is "
           "max(BaseLevel, SpellLevel) and the the reference read only ever substituted SpellLevel for BaseLevel");
    expect(localSpellUnlockLevel(real(12678)) == 20,
           "and it reads 20, which is the level the game teaches Stance Mastery at");
    unsigned unlockDiffersFromBaseLevel = 0;
    for (const auto& d : gImported.spells)
        if (accepted(d) && gTables->has(d.id) &&
            localSpellUnlockLevel(d) != std::clamp<uint32_t>(d.baseLevel, 1, 80)) ++unlockDiffersFromBaseLevel;
    expect(unlockDiffersFromBaseLevel == 1,
           "over the whole accepted set exactly one definition's unlock level differs from BaseLevel alone");

    // And the real trainer, driven, for every class at three levels.
    auto content = std::make_shared<LocalWorldContent>();
    content->spells = gImported.spells; content->clientStarterSpells = true;
    std::sort(content->spells.begin(), content->spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    LocalNpcDefinition trainerDefinition; trainerDefinition.id = 5497; trainerDefinition.name = "Trainer";
    trainerDefinition.health = 100; trainerDefinition.hostile = false;
    content->npcs.push_back(trainerDefinition);

    // The same content with the implementation's column, so both sets come out of the same
    // authority rather than out of two different builds.
    auto before = std::make_shared<LocalWorldContent>(*content);
    for (auto& d : before->spells) if (gTables->has(d.id)) d.baseLevel = gTables->column(d.id, 39);

    size_t offeredAfter = 0, offeredBefore = 0, classes = 0;
    std::set<uint32_t> differing;
    for (const auto& which : {content, before}) {
        LocalGameplay game; game.useContent(which);
        for (uint8_t classId : {1, 2, 3, 4, 5, 6, 7, 8, 9, 11}) {
            for (uint8_t level : {1, 40, 80}) {
                auto p = rewardPlayer(1);
                p.classId = classId; p.level = level; p.money = 1000000000;
                p.knownSpells.clear(); p.quests.clear(); p.resourceType = LocalResourceType::Mana;
                std::vector<LocalRealmPlayer*> players{&p};
                game.tick(0, players);
                auto trainer = rewardNpc(0xf13000000000000aULL);
                trainer.entry = 5497; trainer.hostile = false; trainer.x = 1;
                trainer.classTrainer = true; trainer.trainerClass = classId;
                trainer.targetGuid = 0; trainer.threat = {};
                game.setRemoteNpcs({trainer});
                const auto offers = game.trainableSpells(p, trainer.guid);
                if (which == content) { offeredAfter += offers.size(); ++classes; for (auto id : offers) differing.insert(id); }
                else { offeredBefore += offers.size(); for (auto id : offers) differing.erase(id); }
            }
        }
    }
    expect(offeredAfter == offeredBefore && differing.empty(),
           "the trainer offers the same abilities before and after the column correction");
    expect(offeredAfter > 0 && classes == 30, "30 class/level trainer sets were driven and they are not empty");
    std::cout << "PASS trainer and unlock: " << definitions
              << " accepted definitions were re-measured and " << unlockMoved << " unlock levels and " << costMoved
              << " trainer prices moved; the shipped trainer was driven for 10 classes at levels 1, 40 and 80 - "
              << offeredAfter << " offers before and " << offeredBefore
              << " after, with an empty symmetric difference; the reference's own authority is trainer_spell.ReqLevel "
              << "(ObjectMgr.cpp:9950), a world-database column this realm does not import, so the unlock level stays a "
              << "documented local stand-in built from the ability's own BaseLevel\n";
}

// ---------------------------------------------------------------------------
// 4. Line of sight: the ray test against synthetic geometry.
// ---------------------------------------------------------------------------
// A wall: an axis-aligned quad at x = wallX spanning y and z, as two triangles.
LocalCollisionTile wallTile(uint32_t mapId, float wallX, float halfWidth, float loZ, float hiZ,
                            uint32_t flags = 0, uint32_t tileX = 32, uint32_t tileY = 32) {
    LocalCollisionTile tile;
    tile.mapId = mapId; tile.tileX = tileX; tile.tileY = tileY;
    const LocalCollisionVec3 a{wallX, -halfWidth, loZ}, b{wallX, halfWidth, loZ},
                             c{wallX, halfWidth, hiZ}, d{wallX, -halfWidth, hiZ};
    tile.vertices = {a, b, c, d};
    tile.triangles = {{0, 1, 2, flags}, {0, 2, 3, flags}};
    tile.buildTree();
    return tile;
}

void lineOfSightRay() {
    // 1. Nothing installed is "visible", which is what this build ships.
    LocalCollisionData none;
    expect(none.empty() && !none.covers(0), "an uninstalled pack covers no map");
    expect(none.isInLineOfSight(0, 0, 0, 0, 100, 0, 0, true), "and answers visible");
    expect(localLineOfSightReady(nullptr, 0, 0, 0, 0, 30, 0, 0, 0.389f), "and so does the gate with no pack at all");

    // 2. A wall between two points blocks; the same points either side of it do not.
    LocalCollisionData data;
    data.adopt(wallTile(0, 5.f, 20.f, -20.f, 20.f));
    expect(data.covers(0) && !data.covers(1) && data.tileCount() == 1 && data.triangleCount() == 2,
           "one tile with two triangles covers map 0 and no other map");
    expect(!data.isInLineOfSight(0, 0, 0, 0, 10, 0, 0, true), "a wall at x=5 blocks a ray from x=0 to x=10");
    expect(data.isInLineOfSight(0, 0, 0, 0, 4.9f, 0, 0, true), "a ray that stops short of it does not");
    expect(data.isInLineOfSight(0, 6, 0, 0, 10, 0, 0, true), "a ray that starts past it does not");
    expect(data.isInLineOfSight(0, 0, 25, 0, 10, 25, 0, true), "a ray beside the wall's extent passes");
    expect(data.isInLineOfSight(0, 0, 0, 25, 10, 0, 25, true), "a ray above the wall's extent passes");
    expect(data.isInLineOfSight(1, 0, 0, 0, 10, 0, 0, true), "a map the pack does not cover is always visible");
    // MapTree.cpp:139-143: a degenerate segment is visible, an infinite one is not.
    expect(data.isInLineOfSight(0, 0, 0, 0, 0, 0, 0, true), "a zero-length segment is visible");
    expect(!data.isInLineOfSight(0, 0, 0, 0, std::numeric_limits<float>::infinity(), 0, 0, true),
           "a non-finite segment is refused, as StaticMapTree::isInLineOfSight refuses it");

    // 3. ModelIgnoreFlags::M2 (WorldModel.cpp:545-552): an M2 wall is skipped
    //    when the caller asks for it and blocks when it does not.
    LocalCollisionData doodad;
    doodad.adopt(wallTile(0, 5.f, 20.f, -20.f, 20.f, kLocalCollisionFromM2));
    expect(doodad.isInLineOfSight(0, 0, 0, 0, 10, 0, 0, true), "an M2 wall does not block a cast, which asks to ignore M2");
    expect(!doodad.isInLineOfSight(0, 0, 0, 0, 10, 0, 0, false), "and does block a caller that does not ask");

    // 4. The tree really is a tree: a wall of many triangles answers the same as
    //    a wall of two, over a grid dense enough to force interior nodes.
    LocalCollisionTile dense;
    dense.mapId = 0; dense.tileX = 32; dense.tileY = 32;
    for (int i = 0; i < 20; ++i)
        for (int j = 0; j < 20; ++j) {
            const float y0 = -10.f + float(i), y1 = y0 + 1.f, z0 = -10.f + float(j), z1 = z0 + 1.f;
            const uint32_t base = uint32_t(dense.vertices.size());
            dense.vertices.push_back({5.f, y0, z0}); dense.vertices.push_back({5.f, y1, z0});
            dense.vertices.push_back({5.f, y1, z1}); dense.vertices.push_back({5.f, y0, z1});
            dense.triangles.push_back({base, base + 1, base + 2, 0});
            dense.triangles.push_back({base, base + 2, base + 3, 0});
        }
    dense.buildTree();
    expect(dense.triangles.size() == 800 && dense.tree.objects().size() == 800 && dense.tree.tree().size() > 3,
           "an 800-triangle wall builds a tree with interior nodes");
    LocalCollisionData grid; grid.adopt(dense);
    // The tree's answer is compared against a brute-force sweep of all 800
    // triangles, which is the only check that can catch a wrong traversal.
    unsigned blocked = 0, clear = 0, brute = 0, disagreements = 0;
    for (int i = 0; i < 40; ++i) {
        const float y = -20.f + float(i);
        const bool visible = grid.isInLineOfSight(0, 0, y, 0, 10, y, 0, true);
        (visible ? clear : blocked)++;
        const LocalCollisionVec3 origin{0, y, 0}, direction{1, 0, 0};
        bool hit = false;
        for (const auto& t : dense.triangles) {
            float distance = 10.f;
            if (localCollisionIntersectTriangle(t, dense.vertices, origin, direction, distance)) hit = true;
        }
        if (hit) ++brute;
        if (hit == visible) ++disagreements;
    }
    expect(blocked == 21 && clear == 19 && brute == 21 && !disagreements,
           "the tree blocks the same 21 of 40 parallel rays a brute-force sweep of all 800 triangles blocks");

    // 5. The pack round-trips through its own format, and a corrupted byte is refused.
    const auto bytes = localCollisionEncodeTile(dense);
    LocalCollisionTile decoded; std::string error;
    expect(localCollisionDecodeTile(bytes, decoded, error), "a tile decodes from its own bytes: " + error);
    expect(decoded.triangles.size() == dense.triangles.size() && decoded.vertices.size() == dense.vertices.size(),
           "and carries the same geometry");
    LocalCollisionData reloaded; reloaded.adopt(decoded);
    expect(!reloaded.isInLineOfSight(0, 0, 0, 0, 10, 0, 0, true), "and answers the same");
    auto corrupt = bytes; corrupt[64] ^= 0x40;
    LocalCollisionTile rejected;
    expect(!localCollisionDecodeTile(corrupt, rejected, error), "a corrupted tile is refused, not trusted");
    auto truncated = bytes; truncated.resize(truncated.size() - 8);
    expect(!localCollisionDecodeTile(truncated, rejected, error), "and so is a truncated one");

    // 6. The pack writes and reads a manifest, and the fingerprint moves with it.
    const auto directory = fs::temp_directory_path() / ("wcol" + std::to_string(::getpid()));
    fs::create_directories(directory);
    const auto file = LocalCollisionData::localCollisionTileFileName(0, 32, 32);
    { std::ofstream out(directory / file, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); }
    std::vector<LocalCollisionData::Entry> entries(1);
    entries[0].mapId = 0; entries[0].tileX = 32; entries[0].tileY = 32;
    entries[0].triangles = uint32_t(dense.triangles.size());
    entries[0].hash = localCollisionHash(2166136261U, bytes.data(), bytes.size());
    for (unsigned i = 0; i < 3; ++i) { entries[0].lo[i] = dense.tree.bounds().lo[i]; entries[0].hi[i] = dense.tree.bounds().hi[i]; }
    entries[0].file = file;
    {
        std::ofstream out(directory / "collision.manifest");
        out << "WCOLMANIFEST 1\n" << "fingerprint " << LocalCollisionData::computeFingerprint(entries) << "\n";
        out.precision(9);
        out << "tile 0 32 32 " << entries[0].triangles << " " << entries[0].hash << " "
            << entries[0].lo[0] << " " << entries[0].lo[1] << " " << entries[0].lo[2] << " "
            << entries[0].hi[0] << " " << entries[0].hi[1] << " " << entries[0].hi[2] << " " << file << "\n";
    }
    LocalCollisionData fromDisk;
    expect(fromDisk.load(directory.string(), error), "a pack loads from its manifest: " + error);
    expect(fromDisk.tileCount() == 1 && fromDisk.triangleCount() == 800, "with the tile it lists");
    expect(!fromDisk.isInLineOfSight(0, 0, 0, 0, 10, 0, 0, true), "and blocks the same ray, read lazily off disk");
    expect(fromDisk.fingerprint() == grid.fingerprint(), "the pack's fingerprint is the same either way it was built");
    // A manifest whose fingerprint does not match its tiles is refused.
    {
        std::ofstream out(directory / "collision.manifest", std::ios::app);
        out << "tile 0 33 32 1 1 0 0 0 1 1 1 " << file << "\n";
    }
    LocalCollisionData tampered;
    expect(!tampered.load(directory.string(), error) && error.find("fingerprint") != std::string::npos,
           "a manifest whose fingerprint does not match its tile list is refused");
    std::error_code ec; fs::remove_all(directory, ec);
    std::cout << "PASS line of sight ray: the BIH (BoundingIntervalHierarchy.h:117-276) and the Moeller-Trumbore triangle "
                 "test (WorldModel.cpp:34-84) block the same 21 of 40 parallel rays at an 800-triangle synthetic wall that a "
                 "brute-force sweep of all 800 triangles blocks; StaticMapTree::isInLineOfSight's degenerate and non-finite arms (MapTree.cpp:139-143) hold; "
                 "ModelIgnoreFlags::M2 skips a doodad wall for a cast and not for a caller that does not ask; a tile "
                 "round-trips through the pack format and a corrupted or truncated one is refused; a manifest loads, reads "
                 "its tile lazily and is refused when its fingerprint does not match. NO REAL MAP DATA WAS EXTRACTED OR "
                 "VERIFIED HERE - every triangle above was authored in this file\n";
}

// ---------------------------------------------------------------------------
// 5. Line of sight through the shipped cast gate.
// ---------------------------------------------------------------------------
void lineOfSightRuntime() {
    // How many accepted spells carry the two attributes that skip the test.
    std::set<uint32_t> ignoring;
    unsigned castable = 0;
    for (const auto& d : gImported.spells) {
        if (!accepted(d) || d.passive) continue;
        ++castable;
        if (d.sourceIgnoreLineOfSight) ignoring.insert(d.id);
    }
    expect(!ignoring.empty(), "some accepted spells carry SPELL_ATTR2_IGNORE_LINE_OF_SIGHT");
    for (auto id : ignoring)
        expect((gTables->column(id, 6) & 0x4u) || (gTables->column(id, 9) & 0x04000000u),
               "every ignoring spell really carries one of the two attributes in the client's bytes");

    // A real Frostbolt at a real creature, with and without a wall between them.
    World w; buildWorld(w, {116u}, 8, 80, 20.f);
    auto& p = w.casters[0];
    const auto clear = castOnce(w, 116u, w.npcGuid, LocalCombatEventKind::SpellDamage);
    expect(clear.executed, "Frostbolt lands at 20 yd with no collision pack installed: " + clear.result);

    // The same world, with a wall between the caster at x=0 and the creature at
    // x=20. Both ends are raised by DEFAULT_COLLISION_HEIGHT, so the wall spans
    // it (Object.cpp:1412-1435, ObjectDefines.h:49).
    w.game.adoptCollisionTile(wallTile(p.mapId, 10.f, 30.f, -30.f, 30.f));
    const auto blocked = castOnce(w, 116u, w.npcGuid, LocalCombatEventKind::SpellDamage);
    expect(!blocked.executed && blocked.result == "Target is not in line of sight",
           "and is refused once a wall stands between them: " + blocked.result);

    // A wall that does not cross the line does not refuse it.
    World open; buildWorld(open, {116u}, 8, 80, 20.f);
    open.game.adoptCollisionTile(wallTile(open.casters[0].mapId, 10.f, 30.f, 40.f, 60.f));
    const auto above = castOnce(open, 116u, open.npcGuid, LocalCombatEventKind::SpellDamage);
    expect(above.executed, "a wall 40 yd overhead does not refuse the same cast: " + above.result);

    // A pack that covers another map leaves this one alone.
    World other; buildWorld(other, {116u}, 8, 80, 20.f);
    other.game.adoptCollisionTile(wallTile(other.casters[0].mapId + 1, 10.f, 30.f, -30.f, 30.f));
    const auto elsewhere = castOnce(other, 116u, other.npcGuid, LocalCombatEventKind::SpellDamage);
    expect(elsewhere.executed, "a pack covering a different map does not refuse this one: " + elsewhere.result);

    std::cout << "PASS line of sight runtime: " << ignoring.size() << " of " << castable
              << " accepted non-passive spells carry SPELL_ATTR2_IGNORE_LINE_OF_SIGHT or "
                 "SPELL_ATTR5_ALWAYS_AOE_LINE_OF_SIGHT and skip the gate (Spell.cpp:6092-6093); a real Frostbolt at a real "
                 "creature 20 yd away lands with no pack installed, is refused with \"Target is not in line of sight\" once "
                 "a synthetic wall stands between them, lands again when the wall is 40 yd overhead, and lands when the "
                 "pack covers a different map. THE WALL IS SYNTHETIC: no client asset exists here to extract a real one "
                 "from, and the shipped build installs no pack at all\n";
}

// ---------------------------------------------------------------------------
// 6. Formats.
// ---------------------------------------------------------------------------
void formats() {
    // the implementation moved both for the pet roster; this package added no persisted or
    // replicated field of its own, and no replicated record changed shape.
    expect(SaveVersion == 30, "Save 30, and nothing of this package's is in it");
    expect(lan::GameplayVersion == 85, "LAN 85, and nothing of this package's is on it");
    expect(CastWireBytes == 222 && NpcWireBytes == 618,
           "no replicated record changed shape");
    // The content fingerprint moves with the corrected column and with the new
    // attribute, so a incompatible peer is refused at the join check.
    std::vector<LocalSpellDefinition> spells;
    for (const auto& d : gImported.spells)
        if (d.clientSpell && d.allowableClasses && d.name.size() <= 96 && d.unsupportedReason.size() <= 256)
            spells.push_back(d);
    std::sort(spells.begin(), spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    const auto fingerprintOf = [](std::vector<LocalSpellDefinition> set) {
        LocalGameplay game; auto c = std::make_shared<LocalWorldContent>();
        game.useContent(c); std::string error;
        assert(game.setStarterSpells(set, "", error));
        return game.content().fingerprint;
    };
    const auto after = fingerprintOf(spells);
    auto before = spells;
    for (auto& d : before) if (gTables->has(d.id)) d.baseLevel = gTables->column(d.id, 39);
    const auto with0247Column = fingerprintOf(before);
    auto withoutAttribute = spells;
    for (auto& d : withoutAttribute) d.sourceIgnoreLineOfSight = false;
    expect(after != with0247Column, "the content fingerprint moves with the corrected baseLevel column");
    expect(after != fingerprintOf(withoutAttribute),
           "and with the new sourceIgnoreLineOfSight definition field");
    std::cout << "PASS formats: Save " << unsigned(SaveVersion) << " and LAN " << unsigned(lan::GameplayVersion)
              << " unchanged, CastWireBytes " << CastWireBytes << " and NpcWireBytes " << NpcWireBytes
              << " unchanged; the content fingerprint moves with both the corrected baseLevel column and the new "
                 "sourceIgnoreLineOfSight field, and folds in a collision pack's own fingerprint when one is installed, "
                 "so a incompatible peer is refused at the join check\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: local_spell_level_los_test DBC_DIRECTORY\n"; return 2; }
    ClientTables tables; tables.load(argv[1]); tables.indexColumns();
    gTables = &tables;
    gImported = tables.import();
    columnIdentity();
    amountConsumers();
    trainerAndUnlock();
    lineOfSightRay();
    lineOfSightRuntime();
    formats();
    if (gFailures) { std::cerr << gFailures << " checks failed\n"; return 1; }
    return 0;
}
