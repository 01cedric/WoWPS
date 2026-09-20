// P04 / the implementation - competing and stacking auras: rank identity from the reference's
// spell_ranks table, the replace-not-refuse rule of Aura::CanStackWith in all
// five containers, the different-caster rule for controls and snares, the
// spell-specific exclusivity of LoadSpellSpecific (curses, corruption, mage
// armour, elemental shields), the group tables as data, the trainer side
// effect measured before and after, the load-time normaliser, and the
// an untouched per-player save block (save 30 / LAN 85 since the implementation, which moved
// the realm-level pet roster and nothing here).
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33, as measured
// in the source audit sections 5-8 and 10.1:
//   SpellMgr::LoadSpellRanks                SpellMgr.cpp:1279-1388 - spell_ranks is
//       the rank chain; Talent.dbc ranks through LoadSpellTalentRanks.
//   SpellInfo::IsRankOf / IsHighRankOf      SpellInfo.cpp:3006-3025.
//   Unit::_TryStackingOrRefreshingExistingAura Unit.cpp:4350-4407 - same id AND
//       same caster refreshes; nothing else does.
//   Unit::_RemoveNoStackAurasDueToAura      Unit.cpp:4662-4683 - a weaker
//       EXCLUSIVE_HIGHEST aura removes itself; else every owned aura the new one
//       cannot stack with is removed.
//   Aura::CanStackWith                      SpellAuras.cpp:2027-2180 - specifics,
//       group rules, family, the different-caster periodic exemption, IsRankOf.
//   SpellInfo::LoadSpellSpecific            SpellInfo.cpp:2081-2253.
//   SpellInfo::IsAuraExclusiveBySpecific{,PerCaster}With SpellInfo.cpp:1406-1465.
//   SpellMgr::CheckSpellGroupStackRules     SpellMgr.cpp:811-856.
//   Spell::CheckCast                        Spell.cpp:6996-7001 - the only core
//       SPELL_FAILED_AURA_BOUNCED: a stronger EXCLUSIVE_HIGHEST group-mate.
//   Unit::IsHighestExclusiveAuraEffect      Unit.cpp:4311-4348.
//   Player::_addSpell                       Player.cpp - a learned higher rank
//       marks the lower rank inactive (SMSG_SUPERCEDED_SPELL).
//
// Every census number is measured by running the shipped importer over the
// player's own DBC set against the shipped generated tables; every runtime
// number comes from the shipped LocalGameplay tick driving real imported
// definitions.
#include "../../src/game/local_realm.cpp"
#include "local_group_rewards_fixture.hpp"
#include "game/local_diminishing.hpp"
#include "game/local_npc_auras.hpp"
#include "game/local_spell_ranks.hpp"
#include "game/local_proc_talents.hpp"
#include "game/local_proc_rules.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_world_catalog.hpp"
#include "game/local_services.hpp"
#include <algorithm>
#include <array>
#include <cassert>
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
};
LocalSpellImport gImported;
ClientTables* gTables = nullptr;
std::map<uint32_t, uint32_t> gClientSuperceded; // SkillLineAbility column 8, first non-zero row per spell
std::map<uint32_t, std::string> gNames;

const LocalSpellDefinition& real(uint32_t id) {
    for (const auto& d : gImported.spells) if (d.id == id) return d;
    std::cerr << "FAIL: spell " << id << " is absent from the client import\n";
    std::abort();
}
bool accepted(const LocalSpellDefinition& d) { return d.clientSpell && d.unsupportedReason.empty(); }
bool castable(const LocalSpellDefinition& d) { return accepted(d) && !d.passive && !d.triggeredOnly && !d.npcOnly; }
// The audit's "castable aura spell" (Appendix B step 4): lands in one of the
// five containers.
bool auraSpell(const LocalSpellDefinition& d) {
    return castable(d) && (d.controlProfile || d.snarePercent || d.periodicDamage || d.periodicHeal ||
                           d.periodicHealMaxHealthPct || (localHasTimedAura(d) && !d.formId));
}
// The the implementation link: SkillLineAbility column 8 when non-zero, else a reviewed
// profile's own successor. The profiles are the ones the audit lists at
// local_spell_import.hpp:153, :190, :237, :377, :405 and local_ward_import.hpp:65:
// the reactive shields, Molten Armor, Earth Shield, Thorns / Mana Shield, the
// Frostbolt / Frost Shock snares and the two wards. They agree with spell_ranks
// (asserted below), so their value is the shipped one.
bool profileLinked(const LocalSpellDefinition& d) {
    return detail::reactiveShieldProfile(d.id) || (d.mageArmorGroup && d.spiritCritRatingPct == 35) ||
           localEarthShield(d) || d.wardProfile || d.snarePercent ||
           (d.proc.effect == LocalProcEffect::MeleeDamageShield && d.spellFamily == 7) || d.manaPerAbsorbMilli;
}
// The twenty hand-picked starters never read their SkillLineAbility row at
// the implementation (local_spell_import.hpp, the `starters` loop): only a profile could
// link one (Frostbolt r1, Rend r1, Thorns r1 were; Fireball r1 was not).
constexpr uint32_t kStarters[] = {78, 635, 21084, 75, 2973, 1752, 2098, 585, 2050, 45462, 45477, 45902, 403, 331, 133, 168, 686, 687, 5176, 5185};
uint32_t linkBefore(const LocalSpellDefinition& d) {
    const bool starter = std::find(std::begin(kStarters), std::end(kStarters), d.id) != std::end(kStarters);
    const auto it = gClientSuperceded.find(d.id);
    if (!starter && it != gClientSuperceded.end() && it->second) return it->second;
    return profileLinked(d) ? d.supercededBySpell : 0;
}
std::string nameOf(uint32_t id) { const auto it = gNames.find(id); return it == gNames.end() ? "#" + std::to_string(id) : it->second; }

// ---------------------------------------------------------------------------
// Runtime scaffolding: the shipped authority driving real imported definitions
// against a plain hostile creature (no immunity set, no resistance row) whose
// health is raised so nothing dies.
// ---------------------------------------------------------------------------
struct Caster { uint8_t classId; LocalResourceType resource; uint8_t level; };
struct World {
    std::shared_ptr<LocalWorldContent> content;
    LocalGameplay game;
    std::vector<LocalRealmPlayer> casters;
    std::vector<LocalRealmPlayer*> players;
    uint64_t npcGuid = 0;
};
const LocalRealmNpc* findNpc(const LocalGameplay& game, uint64_t guid) {
    for (const auto& n : game.npcs()) if (n.guid == guid) return &n;
    return nullptr;
}
void buildWorld(World& w, std::vector<uint32_t> spells, std::vector<Caster> casters, uint8_t npcLevel = 20) {
    w.content = rewardContent();
    for (auto id : spells) w.content->spells.push_back(real(id));
    std::sort(w.content->spells.begin(), w.content->spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    LocalNpcDefinition target; target.id = 60; target.name = "Stacking dummy"; target.level = npcLevel; target.hostile = true;
    target.health = 100000000; target.damage = 0; target.aggroRadius = 0.01f;
    w.content->npcs.push_back(target);
    std::sort(w.content->npcs.begin(), w.content->npcs.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
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
    auto n = rewardNpc();
    n.entry = target.id; n.name = target.name; n.level = target.level;
    n.health = n.maxHealth = target.health; n.hostile = true; n.lootOwner = 0;
    n.x = n.homeX = 8; n.y = n.homeY = 0; n.z = n.homeZ = 0;
    n.targetGuid = w.casters[0].guid; n.threat[0] = {w.casters[0].guid, 1000};
    w.game.setRemoteNpcs({n});
    w.npcGuid = n.guid;
    w.game.tick(.25f, w.players);
}
void reset(LocalRealmPlayer& p) {
    p.globalCooldownMs = 0; p.cooldowns.clear(); p.categoryCooldowns.clear();
    p.mana = p.maxMana; p.comboPoints = 0; p.comboTarget = 0;
}
bool execute(World& w, LocalRealmPlayer& p, uint32_t spellId, uint64_t target, std::string& result) {
    reset(p);
    const bool ok = w.game.execute(p, {LocalAction::CastSpell, target, spellId}, w.players, result);
    if (!ok) return false;
    for (unsigned t = 0; t < 120 && p.castStatus == LocalCastStatus::Casting; ++t) w.game.tick(.05f, w.players);
    assert(p.castStatus != LocalCastStatus::Casting);
    return true;
}
// Cast at the creature until the spell's own aura is on it (a miss applies
// nothing and changes nothing, so retrying preserves state).
void castUntilLanded(World& w, LocalRealmPlayer& p, uint32_t spellId, unsigned attempts = 80) {
    for (unsigned i = 0; i < attempts; ++i) {
        std::string result;
        if (!execute(w, p, spellId, w.npcGuid, result)) { std::cerr << "FAIL: cast " << spellId << " rejected: " << result << "\n"; std::abort(); }
        const auto* n = findNpc(w.game, w.npcGuid);
        assert(n);
        for (const auto& a : n->controls) if (a.spellId == spellId && a.casterGuid == p.guid) return;
        for (const auto& a : n->snares) if (a.spellId == spellId && a.casterGuid == p.guid) return;
        for (const auto& a : n->damageAuras) if (a.spellId == spellId && a.casterGuid == p.guid) return;
    }
    std::cerr << "FAIL: cast " << spellId << " never landed in " << attempts << " attempts\n";
    std::abort();
}
void castOnPlayer(World& w, LocalRealmPlayer& p, uint32_t spellId, const LocalRealmPlayer& target) {
    std::string result;
    if (!execute(w, p, spellId, target.guid, result)) { std::cerr << "FAIL: cast " << spellId << " on player rejected: " << result << "\n"; std::abort(); }
}
std::vector<LocalHealingAuraView> dots(const World& w, uint64_t target) {
    const auto* n = findNpc(w.game, target); assert(n); return n->damageAuras;
}
uint8_t snarePercent(const LocalRealmNpc& n) { uint8_t best = 0; for (const auto& a : n.snares) if (a.remainingMs) best = std::max(best, a.percent); return best; }
std::string describeDots(const std::vector<LocalHealingAuraView>& v) {
    std::ostringstream s;
    for (const auto& a : v) s << " " << nameOf(a.spellId) << "(" << a.spellId << ",caster " << (a.casterGuid & 0xff) << "," << a.remainingMs << "ms)";
    return s.str();
}

// ---------------------------------------------------------------------------
// 1. The rank census: the shipped table, the chains it closes, and the client
//    column it supersedes.
// ---------------------------------------------------------------------------
std::vector<uint32_t> gGainedChains; // first ranks of the 17 chains the table linked
void rankCensus() {
    constexpr size_t rows = sizeof(kLocalSpellRanks) / sizeof(kLocalSpellRanks[0]);
    std::set<uint32_t> chains, ids;
    uint32_t previousFirst = 0; uint8_t previousRank = 0;
    for (const auto& r : kLocalSpellRanks) {
        chains.insert(r.first); assert(ids.insert(r.id).second);
        if (r.first == previousFirst) assert(r.rank == previousRank + 1); else { assert(r.rank == 1 && r.id == r.first); }
        previousFirst = r.first; previousRank = r.rank;
    }
    assert(localSpellRankNext(133) == 143 && localSpellRankNext(143) == 145 && localSpellRankFirst(145) == 133);
    assert(localSpellRankNext(42833) == 0 && localSpellRankFirst(42833) == 133); // Fireball r16 is the top
    assert(localSpellRankNext(139) == 6074 && localSpellRankFirst(6074) == 139); // Renew, absent from column 8
    assert(!localSpellRankRow(1) && !localSpellRankNext(1) && !localSpellRankFirst(1));
    const bool tableExact = expect(rows == 3502 && chains.size() == 598,
        "spell_ranks: " + std::to_string(rows) + " rows / " + std::to_string(chains.size()) + " chains against 3502 / 598");

    // The accepted castable aura spells, grouped by the reference's first rank.
    std::map<uint32_t, std::vector<uint32_t>> byChain;
    unsigned auraSpells = 0;
    for (const auto& d : gImported.spells) if (auraSpell(d)) { ++auraSpells; byChain[d.firstRankSpell].push_back(d.id); }
    unsigned multiChains = 0, multiSpells = 0, linkedBefore = 0, linkedBeforeSpells = 0, linkedAfter = 0, unlinkedAfterSpells = 0;
    std::vector<uint32_t> gained, gainedSpells;
    for (auto& [first, members] : byChain) {
        if (members.size() < 2) continue;
        ++multiChains; multiSpells += members.size();
        // A chain is linked end to end when every non-top member names its successor.
        // A talent-granted member (Pyroblast r1, 11366) is chained by
        // Talent.dbc in the reference and is never a trainer rank; the chain
        // is linked when every other non-top member names its successor.
        const auto linked = [&](auto link) {
            unsigned tops = 0;
            for (auto id : members) if (!real(id).talentId && !link(real(id))) ++tops;
            return tops <= 1;
        };
        const bool before = linked([](const LocalSpellDefinition& d) { return linkBefore(d); });
        const bool after = linked([](const LocalSpellDefinition& d) { return d.supercededBySpell; });
        if (before) { ++linkedBefore; linkedBeforeSpells += members.size(); }
        if (after) ++linkedAfter; else unlinkedAfterSpells += members.size();
        if (!before && after) { gained.push_back(first); gainedSpells.insert(gainedSpells.end(), members.begin(), members.end()); }
    }
    gGainedChains = gained;
    std::string gainedText;
    for (auto first : gained) gainedText += (gainedText.empty() ? "" : ", ") + nameOf(byChain[first].front()) + " " + std::to_string(byChain[first].size());
    // the source audit section 7.3: 279 castable aura
    // spells, 34 chains with >= 2 accepted members (274 spells), 17 linked end to
    // end before (121 spells), 17 with no rank identity (153 spells).
    const bool chainsExact = expect(auraSpells == 279 && multiChains == 34 && multiSpells == 274 && linkedBefore == 17 &&
                                    linkedBeforeSpells == 121 && gained.size() == 17 && gainedSpells.size() == 153 &&
                                    linkedAfter == 34 && unlinkedAfterSpells == 0,
        "chain census: " + std::to_string(auraSpells) + " castable aura spells, " + std::to_string(multiChains) + " chains / " +
        std::to_string(multiSpells) + " spells with >= 2 members, linked before " + std::to_string(linkedBefore) + " / " +
        std::to_string(linkedBeforeSpells) + ", gained " + std::to_string(gained.size()) + " / " + std::to_string(gainedSpells.size()) +
        ", linked after " + std::to_string(linkedAfter) + ", unlinked after " + std::to_string(unlinkedAfterSpells) +
        " against 279; 34 / 274; 17 / 121; 17 / 153; 34; 0");

    // Every accepted id that had a link: 164 (92 from the column, 72 from a
    // profile); every profile link agrees with the table; where the column and
    // the table both speak, how often they disagree (the table wins).
    unsigned hadLink = 0, fromColumn = 0, fromProfile = 0, profileAgrees = 0, bothSpeak = 0, disagree = 0, columnOnly = 0, tableOnly = 0;
    std::vector<uint32_t> disagreements;
    for (const auto& d : gImported.spells) {
        if (!accepted(d) || d.talentId || d.triggeredOnly) continue;
        const auto column = gClientSuperceded.count(d.id) ? gClientSuperceded.at(d.id) : 0u;
        const auto table = localSpellRankNext(d.id);
        const bool profile = profileLinked(d) && d.supercededBySpell;
        const auto before = linkBefore(d);
        if (before) { ++hadLink; if (before == column && !profile) ++fromColumn; else ++fromProfile; }
        if (profile && localSpellRankRow(d.id) && table == d.supercededBySpell) ++profileAgrees;
        if (column && localSpellRankRow(d.id)) { ++bothSpeak; if (column != table) { ++disagree; disagreements.push_back(d.id); } }
        if (column && !localSpellRankRow(d.id)) ++columnOnly;
        if (!column && table) ++tableOnly;
        // An internal child (Flurry's, Molten Armor's, Go for the Throat's
        // proc auras) is listed by the table but is never trained or cast
        // directly; its link stays zero and its first rank is the table's.
        if (!d.triggeredOnly) assert(d.supercededBySpell == (localSpellRankRow(d.id) ? table : column ? column : d.supercededBySpell));
        assert(d.firstRankSpell); // every imported definition has a first rank
        if (localSpellRankRow(d.id)) assert(d.firstRankSpell == localSpellRankFirst(d.id));
    }
    // Section 7.3 counted 164 over the 982 ids accepted at the implementation (92 column,
    // 72 profile); the implementation admitted the eight Shield Slam ranks, every one with
    // a column link, so the shipped importer carries 172. One profile-linked
    // id also carries a column link and is counted as column here; all 72
    // profile links agree with the table.
    unsigned profileTotal = 0;
    for (const auto& d : gImported.spells) if (accepted(d) && !d.talentId && !d.triggeredOnly && profileLinked(d) && d.supercededBySpell) ++profileTotal;
    // the implementation's own audit JSON carries 171 accepted retained ids with a link:
    // the 170 counted here plus Earth Shield r1 (974), a talent-granted rank
    // whose profile link is the seventy-second profile link of section 7.3.
    assert(real(974).talentId && real(974).supercededBySpell == 32593 && localSpellRankNext(974) == 32593);
    const bool linksExact = expect(hadLink == 170 && fromColumn + fromProfile == 170 && profileTotal == 71 && profileAgrees == 71,
        "the reference links: " + std::to_string(hadLink) + " accepted ids carried a link (" + std::to_string(fromColumn) + " column, " +
        std::to_string(fromProfile) + " profile), " + std::to_string(profileAgrees) + " of " + std::to_string(profileTotal) + " profile links agree with spell_ranks, against 170 / 71 / 71 (plus the talent Earth Shield r1)");
    // Talent ranks are chained by Talent.dbc, not the table.
    unsigned talentInTable = 0, talents = 0, talentFirstOk = 0;
    for (const auto& d : gImported.spells) if (d.talentId && accepted(d)) { ++talents; if (localSpellRankRow(d.id)) ++talentInTable;
        for (const auto& e : gImported.spells) if (e.talentId == d.talentId && e.talentRank == 1 && e.firstRankSpell == d.firstRankSpell) { ++talentFirstOk; break; } }
    assert(talentFirstOk == talents);
    if (tableExact && chainsExact && linksExact)
        std::cout << "PASS rank census: spell_ranks ships " << rows << " rows in " << chains.size() << " contiguous chains (SpellMgr::LoadSpellRanks "
                     "would drop 0); of the " << auraSpells << " accepted castable aura spells, " << multiChains << " chains hold >= 2 accepted members ("
                  << multiSpells << " spells); the client column plus the reviewed profiles linked " << linkedBefore << " of them (" << linkedBeforeSpells
                  << " spells) and the table now links all " << linkedAfter << " - the " << gained.size() << " chains gained (" << gainedSpells.size()
                  << " spells): " << gainedText << "; " << hadLink << " accepted ids carried a link before (" << fromColumn << " column, " << fromProfile
                  << " profile; with the talent-granted Earth Shield r1 that is 2.00's own 171 - the audit's 164 over the 982 ids of 2.00 plus the eight Shield Slam ranks 2.00 admitted, less one starter the audit counted through its column), all " << profileTotal << " + 1 profile links agree with the table, the column and the table both speak on " << bothSpeak
                  << " accepted ids and disagree on " << disagree << (disagree ? " (" + join(disagreements) + "; the table wins)" : "")
                  << ", the column alone links " << columnOnly << " accepted ids the table does not list, the table alone links " << tableOnly
                  << "; " << talentInTable << " of " << talents << " accepted talent ranks are in spell_ranks and every talent rank's first rank is its rank 1\n";
}

// ---------------------------------------------------------------------------
// 2. The trainer and the fresh spellbook, before and after, every class at 80.
// ---------------------------------------------------------------------------
struct TrainableDiff { unsigned classId; std::vector<uint32_t> before, after, removed; unsigned freshBefore = 0, freshAfter = 0; };
std::vector<TrainableDiff> gTrainable;
std::shared_ptr<LocalWorldContent> fullContent(bool before) {
    auto c = std::make_shared<LocalWorldContent>();
    c->spells = gImported.spells;
    if (before) for (auto& d : c->spells) if (!d.talentId) d.supercededBySpell = linkBefore(d);
    c->clientStarterSpells = true;
    std::sort(c->spells.begin(), c->spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    LocalNpcDefinition trainer; trainer.id = 5497; trainer.name = "Trainer"; trainer.health = 100; trainer.hostile = false;
    c->npcs.push_back(trainer);
    return c;
}
void trainableSetDiff() {
    unsigned totalBefore = 0, totalAfter = 0, totalRemoved = 0, freshBefore = 0, freshAfter = 0;
    std::set<uint32_t> removedChains;
    std::vector<uint32_t> removedIds;
    for (uint8_t classId : {1, 2, 3, 4, 5, 6, 7, 8, 9, 11}) {
        TrainableDiff row; row.classId = classId;
        for (bool before : {true, false}) {
            LocalGameplay game; game.useContent(fullContent(before));
            auto p = rewardPlayer(1); p.classId = classId; p.level = 80; p.money = 1000000000; p.knownSpells.clear(); p.quests.clear();
            p.resourceType = classId == 1 ? LocalResourceType::Rage : classId == 4 ? LocalResourceType::Energy : classId == 6 ? LocalResourceType::RunicPower : LocalResourceType::Mana;
            std::vector<LocalRealmPlayer*> players{&p};
            game.tick(0, players);
            auto trainer = rewardNpc(0xf13000000000000aULL); trainer.entry = 5497; trainer.hostile = false; trainer.x = 1; trainer.y = 0; trainer.z = 0;
            trainer.classTrainer = true; trainer.trainerClass = classId; trainer.targetGuid = 0; trainer.threat = {};
            game.setRemoteNpcs({trainer});
            auto offers = game.trainableSpells(p, trainer.guid);
            std::sort(offers.begin(), offers.end());
            // The fresh spellbook (initializePlayer(fresh)) at 80: every reached
            // ability, highest rank only, through the same supercededHere gate.
            auto fresh = p; fresh.gameplayInitialized = false;
            game.initializePlayer(fresh, true);
            (before ? row.before : row.after) = offers;
            (before ? row.freshBefore : row.freshAfter) = unsigned(fresh.knownSpells.size());
        }
        std::set_difference(row.before.begin(), row.before.end(), row.after.begin(), row.after.end(), std::back_inserter(row.removed));
        std::vector<uint32_t> added;
        std::set_difference(row.after.begin(), row.after.end(), row.before.begin(), row.before.end(), std::back_inserter(added));
        assert(added.empty()); // the table only ever removes an outranked offer
        // Every removed offer has a higher rank the character has reached.
        const auto c = fullContent(false);
        for (auto id : row.removed) {
            const auto* d = c->spell(id); assert(d && d->supercededBySpell);
            bool reached = false;
            for (auto next = d->supercededBySpell; next; next = c->spell(next) ? c->spell(next)->supercededBySpell : 0)
                if (const auto* n = c->spell(next); n && accepted(*n) && n->baseLevel <= 80) { reached = true; break; }
            assert(reached);
            removedChains.insert(d->firstRankSpell); removedIds.push_back(id);
        }
        totalBefore += row.before.size(); totalAfter += row.after.size(); totalRemoved += row.removed.size();
        freshBefore += row.freshBefore; freshAfter += row.freshAfter;
        gTrainable.push_back(row);
    }
    std::sort(removedIds.begin(), removedIds.end());
    std::string perClass;
    for (const auto& r : gTrainable) perClass += (perClass.empty() ? "" : ", ") + std::string("class ") + std::to_string(r.classId) + " " +
        std::to_string(r.before.size()) + "->" + std::to_string(r.after.size()) + " (fresh book " + std::to_string(r.freshBefore) + "->" + std::to_string(r.freshAfter) + ")";
    // Section 7.3's wider census: over every accepted castable spell (649),
    // 77 chains hold >= 2 accepted members and 54 of them (449 spells) had no
    // link. That is the population the trainer used to offer rank by rank; the
    // offers actually withdrawn at level 80 are fewer than 449 because each
    // chain keeps its top rank, a talent-granted rank (Pyroblast r1) is never
    // offered, and a rank above the character's reach is not offered either.
    std::map<uint32_t, std::vector<uint32_t>> castableChains;
    unsigned castableSpells = 0;
    for (const auto& d : gImported.spells) if (castable(d) && !d.mountDisplayId) { ++castableSpells; castableChains[d.firstRankSpell].push_back(d.id); }
    unsigned wideChains = 0, wideUnlinked = 0, wideUnlinkedSpells = 0, wideUnlinkedAfter = 0;
    for (const auto& [first, members] : castableChains) {
        if (members.size() < 2) continue;
        ++wideChains;
        const auto linked = [&](auto link) { unsigned tops = 0; for (auto id : members) if (!real(id).talentId && !link(real(id))) ++tops; return tops <= 1; };
        if (!linked([](const LocalSpellDefinition& d) { return linkBefore(d); })) { ++wideUnlinked; wideUnlinkedSpells += members.size(); }
        if (!linked([](const LocalSpellDefinition& d) { return d.supercededBySpell; })) ++wideUnlinkedAfter;
    }
    // (657 castable and 78 chains at the audit's 649 / 77 plus the eight
    // Shield Slam ranks the implementation admitted, a column-linked chain of its own. 650
    // and 75 since the implementation retired seven proc copies from the castable set -
    // Lightning Overload's 49239/49240 and 49268/49269 and Lightning Shield's
    // damage leaves 26372/49278/49279, three chains of their own that the
    // table had linked - so 51 chains / 442 spells were unlinked before.)
    const bool wideExact = expect(castableSpells == 650 && wideChains == 75 && wideUnlinked == 51 && wideUnlinkedSpells == 442 && wideUnlinkedAfter == 0,
        "castable census: " + std::to_string(castableSpells) + " castable, " + std::to_string(wideChains) + " chains >= 2, " + std::to_string(wideUnlinked) + " / " +
        std::to_string(wideUnlinkedSpells) + " unlinked before, " + std::to_string(wideUnlinkedAfter) + " after, against 650 / 75 / 51 / 442 / 0 (657 / 78 / 54 / 449 / 0 at the reference)");
    // The trainer diff itself, pinned at what this checkpoint measured: 477 ->
    // 109 (368 withdrawn in 54 chains) at the implementation; re-measured at the implementation with the
    // seven proc copies retired from both sides, 470 -> 106 (364 in 51 chains);
    // 471 -> 107 since the implementation admitted Stance Mastery 12678, which really is a
    // warrior trainer spell (SpellFamilyName WARRIOR, SpellIconID 139,
    // SpellLevel 20) and was refused only because it carries its amount on a
    // SPELL_AURA_DUMMY effect. The five form boost spells the implementation also admitted
    // are marked triggeredOnly - HandleShapeshiftBoosts casts them and no
    // player learns one - so they add nothing to either side. The withdrawal
    // itself is untouched: 364 offers in 51 chains, as at the reference.
    const bool diffExact = expect(totalBefore == 471 && totalAfter == 107 && totalRemoved == 364 && removedChains.size() == 51 && totalAfter + totalRemoved == totalBefore,
        "trainable set: " + std::to_string(totalBefore) + " offers before, " + std::to_string(totalAfter) + " after, " + std::to_string(totalRemoved) +
        " outranked offers withdrawn across " + std::to_string(removedChains.size()) + " chains, against 471 / 107 / 364 / 51 (470 / 106 / 364 / 51 at 2.00)") && wideExact;
    if (diffExact)
        std::cout << "PASS trainer side effect: over the " << castableSpells << " accepted castable spells " << wideChains << " chains hold >= 2 members and "
                  << wideUnlinked << " of them (" << wideUnlinkedSpells << " spells) had no rank identity - the audit's 54 / 449 less the three proc-copy chains the reference retired - and 0 remain unlinked; at level 80 "
                     "with an empty spellbook the class trainers offered " << totalBefore << " abilities across the ten classes before and offer " << totalAfter
                  << " after - " << totalRemoved << " outranked offers in " << removedChains.size() << " chains are withdrawn (each chain keeps its top rank; "
                     "Player::_addSpell marks a lower rank inactive and the trainer never offers it), 0 offers added; the fresh level-80 spellbook shrinks "
                  << freshBefore << " -> " << freshAfter << "; per class " << perClass << "; the withdrawn ids: " << join(removedIds, 1000) << "\n";
}
void writeTrainableJson(const fs::path& path) {
    std::ofstream f(path);
    f << "{\n  \"checkpoint\": \"2.00, re-measured at 2.00 with the seven proc copies 49239/49240/49268/49269 and 26372/49278/49279 retired from both sides\",\n  \"source\": \"tools/tests/local_aura_stacking_test.cpp trainableSetDiff, level 80, empty spellbook, class trainer\",\n  \"classes\": [\n";
    for (size_t i = 0; i < gTrainable.size(); ++i) {
        const auto& r = gTrainable[i];
        f << "    {\"classId\": " << r.classId << ", \"before\": " << r.before.size() << ", \"after\": " << r.after.size()
          << ", \"freshSpellbookBefore\": " << r.freshBefore << ", \"freshSpellbookAfter\": " << r.freshAfter
          << ", \"withdrawn\": [" << join(r.removed, 10000) << "]}" << (i + 1 < gTrainable.size() ? ",\n" : "\n");
    }
    f << "  ]\n}\n";
}

// ---------------------------------------------------------------------------
// 3. Same caster, one target: a rank replaces a rank in both directions, in
//    every container, and the one bounce the reference has.
// ---------------------------------------------------------------------------
void sameCasterRankReplacement() {
    // Fireball r1 (133) then r2 (143): one burn, r2's. The audit's section 8.3
    // measured three concurrent Fireball burns on one creature at the reference.
    assert(real(133).periodicDamage && real(143).periodicDamage && real(143).firstRankSpell == 133 && real(133).supercededBySpell == 143);
    World w; buildWorld(w, {133, 143, 145}, {{8, LocalResourceType::Mana, 20}});
    auto& mage = w.casters[0];
    castUntilLanded(w, mage, 133);
    castUntilLanded(w, mage, 143);
    auto held = dots(w, w.npcGuid);
    const bool upgrade = expect(held.size() == 1 && held[0].spellId == 143 && held[0].casterGuid == mage.guid,
        "Fireball r1 then r2:" + describeDots(held));
    // r2 then r1: the reference REPLACES (CanStackWith -> IsRankOf -> false ->
    // RemoveOwnedAuras, SpellAuras.cpp:2160-2177; no rank comparison anywhere).
    // The build used to refuse this with "A higher damage-over-time rank is
    // already active"; that refusal has no source.
    castUntilLanded(w, mage, 133);
    held = dots(w, w.npcGuid);
    const bool downgrade = expect(held.size() == 1 && held[0].spellId == 133,
        "Fireball r2 then r1:" + describeDots(held));
    // r3 over r1 skips a rank: still one.
    castUntilLanded(w, mage, 145);
    held = dots(w, w.npcGuid);
    assert(held.size() == 1 && held[0].spellId == 145);
    // The same id refreshes in place (step 1): still one entry, never two,
    // and the duration is the full one again. (The miss roll is time-seeded;
    // a missed recast changes nothing, and if the burn ran out meanwhile the
    // landed recast is a fresh single entry - the observation is the same.)
    for (unsigned i = 0; i < 4; ++i) w.game.tick(.5f, w.players);
    castUntilLanded(w, mage, 145);
    held = dots(w, w.npcGuid);
    assert(held.size() == 1 && held[0].spellId == 145 && held[0].remainingMs + 100 >= real(145).durationMs);

    // Renew (priest HoT on a friendly player): r1 (139) then r2 (6074) then r1.
    World h; buildWorld(h, {139, 6074}, {{5, LocalResourceType::Mana, 20}, {5, LocalResourceType::Mana, 20}});
    auto& priest = h.casters[0]; auto& ally = h.casters[1];
    castOnPlayer(h, priest, 139, ally); h.game.tick(.05f, h.players);
    castOnPlayer(h, priest, 6074, ally); h.game.tick(.05f, h.players);
    assert(ally.healingAuras.size() == 1 && ally.healingAuras[0].spellId == 6074);
    castOnPlayer(h, priest, 139, ally); h.game.tick(.05f, h.players);
    const bool renew = expect(ally.healingAuras.size() == 1 && ally.healingAuras[0].spellId == 139,
        "Renew r2 then r1 leaves " + std::to_string(ally.healingAuras.size()) + " HoT(s)");

    // Controls: Hammer of Justice r1 (853) then r4 (10308) then r1 - one control.
    World k; buildWorld(k, {853, 10308}, {{2, LocalResourceType::Mana, 80}}, 39);
    auto& paladin = k.casters[0];
    castUntilLanded(k, paladin, 853);
    castUntilLanded(k, paladin, 10308);
    const auto* kn = findNpc(k.game, k.npcGuid);
    assert(kn->controls.size() == 1 && kn->controls[0].spellId == 10308);
    castUntilLanded(k, paladin, 853);
    kn = findNpc(k.game, k.npcGuid);
    const bool control = expect(kn->controls.size() == 1 && kn->controls[0].spellId == 853, "HoJ r4 then r1 leaves " + std::to_string(kn->controls.size()) + " control(s)");

    // Snares: Frostbolt r1 (116) then r2 (205) then r1 - one snare, 40 % throughout.
    World s; buildWorld(s, {116, 205}, {{8, LocalResourceType::Mana, 20}});
    castUntilLanded(s, s.casters[0], 116);
    castUntilLanded(s, s.casters[0], 205);
    const auto* sn = findNpc(s.game, s.npcGuid);
    assert(sn->snares.size() == 1 && sn->snares[0].spellId == 205);
    castUntilLanded(s, s.casters[0], 116);
    sn = findNpc(s.game, s.npcGuid);
    const bool snare = expect(sn->snares.size() == 1 && sn->snares[0].spellId == 116 && snarePercent(*sn) == 40,
        "Frostbolt r2 then r1 leaves " + std::to_string(sn->snares.size()) + " snare(s)");

    // Stat auras: Lightning Shield r3 (905) then r1 (324): replaced, not refused
    // (182 of the audit's ordered pairs were this refusal). Then the one bounce:
    // Thorns is group 1113 EXCLUSIVE_HIGHEST, and Spell::CheckCast compares the
    // computed amounts - r1 (467) over r3 (1075) is SPELL_FAILED_AURA_BOUNCED,
    // r3 over r1 replaces.
    World ls; buildWorld(ls, {324, 905}, {{7, LocalResourceType::Mana, 20}});
    auto& shaman = ls.casters[0];
    castOnPlayer(ls, shaman, 905, shaman);
    castOnPlayer(ls, shaman, 324, shaman);
    unsigned shields = 0; uint32_t shieldId = 0;
    for (const auto& a : shaman.statAuras) if (real(a.spellId).proc.effect != LocalProcEffect::None) { ++shields; shieldId = a.spellId; }
    const bool lightning = expect(shields == 1 && shieldId == 324, "Lightning Shield r3 then r1 leaves " + std::to_string(shields) + " shield(s), id " + std::to_string(shieldId));
    assert(localSpellGroupStackRule(467, 467) == LocalSpellGroupStackRule::ExclusiveHighest);
    World th; buildWorld(th, {467, 782, 1075}, {{11, LocalResourceType::Mana, 20}});
    auto& druid = th.casters[0];
    castOnPlayer(th, druid, 1075, druid);
    std::string bounced;
    reset(druid);
    const bool refused = !th.game.execute(druid, {LocalAction::CastSpell, druid.guid, 467}, th.players, bounced);
    const bool thorns = expect(refused && bounced == "A more powerful spell is already active",
        "Thorns r1 over r3: refused=" + std::to_string(refused) + " \"" + bounced + "\"");
    reset(druid);
    assert(!th.game.execute(druid, {LocalAction::CastSpell, druid.guid, 782}, th.players, bounced)); // r2 over r3: weaker, bounced too
    unsigned thornsHeld = 0; uint32_t thornsId = 0;
    for (const auto& a : druid.statAuras) if (real(a.spellId).proc.effect == LocalProcEffect::MeleeDamageShield) { ++thornsHeld; thornsId = a.spellId; }
    assert(thornsHeld == 1 && thornsId == 1075);
    // Fresh druid: r1, then r3 replaces it; the amounts order as the ranks do.
    World th2; buildWorld(th2, {467, 782, 1075}, {{11, LocalResourceType::Mana, 20}});
    auto& druid2 = th2.casters[0];
    castOnPlayer(th2, druid2, 467, druid2);
    const auto r1Amount = druid2.statAuras.back().procAmountSnapshot;
    castOnPlayer(th2, druid2, 1075, druid2);
    thornsHeld = 0;
    for (const auto& a : druid2.statAuras) if (real(a.spellId).proc.effect == LocalProcEffect::MeleeDamageShield) { ++thornsHeld; thornsId = a.spellId; }
    assert(thornsHeld == 1 && thornsId == 1075 && druid2.statAuras.back().procAmountSnapshot > r1Amount);
    if (upgrade && downgrade && renew && control && snare && lightning && thorns)
        std::cout << "PASS same-caster rank replacement: Fireball r1 then r2 on one creature leaves one burn (r2's), r2 then r1 leaves one burn (r1's - "
                     "Aura::CanStackWith -> IsRankOf -> false -> RemoveOwnedAuras, SpellAuras.cpp:2160-2177, no rank comparison), r3 over r1 one, and a "
                     "recast of the same id refreshes in place (one entry, full duration); Renew r2 then r1 one HoT; Hammer of Justice r4 then r1 one control; Frostbolt r2 then r1 one "
                     "snare at 40 %; Lightning Shield r3 then r1 one shield (r1) where the reference refused; Thorns r1 over r3 is refused with \"" << bounced
                  << "\" (group 1113 EXCLUSIVE_HIGHEST, Spell.cpp:6996-7001, the reference's only core bounce) and r3 over r1 replaces (amount "
                  << druid2.statAuras.back().procAmountSnapshot << " over " << r1Amount << ")\n";
}

// ---------------------------------------------------------------------------
// 4. Different casters: controls and snares replace, periodic auras coexist.
// ---------------------------------------------------------------------------
void differentCasters() {
    // Two mages' Frostbolt: CanStackWith's different-caster arm exempts only a
    // channelled existing aura, DOT_STACKING_RULE and periodic auras
    // (SpellAuras.cpp:2089-2120); a snare is none of those, so the second
    // mage's snare replaces the first's (IsRankOf, :2160-2177). The strongest-
    // wins amount rule (GetMaxNegativeAuraModifier) then reads one entry.
    World s; buildWorld(s, {116, 42842}, {{8, LocalResourceType::Mana, 80}, {8, LocalResourceType::Mana, 80}}, 39);
    castUntilLanded(s, s.casters[0], 116);
    castUntilLanded(s, s.casters[1], 42842);
    const auto* sn = findNpc(s.game, s.npcGuid);
    const bool snares = expect(sn->snares.size() == 1 && sn->snares[0].casterGuid == s.casters[1].guid && sn->snares[0].spellId == 42842 && snarePercent(*sn) == 40,
        "two mages' Frostbolt: " + std::to_string(sn->snares.size()) + " snare(s)");
    castUntilLanded(s, s.casters[0], 116);
    sn = findNpc(s.game, s.npcGuid);
    assert(sn->snares.size() == 1 && sn->snares[0].casterGuid == s.casters[0].guid);
    // Two paladins' Hammer of Justice: the second replaces the first; the
    // diminishing ladder still steps per hit (Spell::DoSpellHitOnUnit).
    World k; buildWorld(k, {10308}, {{2, LocalResourceType::Mana, 80}, {2, LocalResourceType::Mana, 80}}, 39);
    castUntilLanded(k, k.casters[0], 10308);
    const auto* kn = findNpc(k.game, k.npcGuid);
    assert(kn->controls.size() == 1 && kn->controls[0].remainingMs == 6000);
    castUntilLanded(k, k.casters[1], 10308);
    kn = findNpc(k.game, k.npcGuid);
    const bool controls = expect(kn->controls.size() == 1 && kn->controls[0].casterGuid == k.casters[1].guid && kn->controls[0].remainingMs == 3000,
        "two paladins' HoJ: " + std::to_string(kn->controls.size()) + " control(s), " + std::to_string(kn->controls.empty() ? 0u : kn->controls[0].remainingMs) + " ms");
    // Two priests' Renew on one ally: PERIODIC_HEAL from different casters stacks.
    World h; buildWorld(h, {139}, {{5, LocalResourceType::Mana, 20}, {5, LocalResourceType::Mana, 20}, {5, LocalResourceType::Mana, 20}});
    auto& ally = h.casters[2];
    castOnPlayer(h, h.casters[0], 139, ally); castOnPlayer(h, h.casters[1], 139, ally); h.game.tick(.05f, h.players);
    const bool renews = expect(ally.healingAuras.size() == 2, "two priests' Renew: " + std::to_string(ally.healingAuras.size()) + " HoT(s)");
    // Two warlocks' Curse of Agony: PERIODIC_DAMAGE from different casters
    // stacks; the per-caster CURSE specific does not cross casters.
    World d; buildWorld(d, {980}, {{9, LocalResourceType::Mana, 20}, {9, LocalResourceType::Mana, 20}});
    castUntilLanded(d, d.casters[0], 980); castUntilLanded(d, d.casters[1], 980);
    const auto held = dots(d, d.npcGuid);
    const bool agonies = expect(held.size() == 2, "two warlocks' Curse of Agony:" + describeDots(held));
    // Two mages' Fireball burns coexist too (audit S4: unchanged).
    World f; buildWorld(f, {133}, {{8, LocalResourceType::Mana, 20}, {8, LocalResourceType::Mana, 20}});
    castUntilLanded(f, f.casters[0], 133); castUntilLanded(f, f.casters[1], 133);
    assert(dots(f, f.npcGuid).size() == 2);
    if (snares && controls && renews && agonies)
        std::cout << "PASS different casters: two mages' Frostbolt (r1 and r16) leave one snare on the creature - the second mage's, at the strongest-wins "
                     "40 % - and the first mage's recast takes it back (a snare is not periodic, channelled or DOT_STACKING_RULE, SpellAuras.cpp:2089-2120, "
                     "so IsRankOf replaces it, :2160-2177; the reference kept both, 282 of the audit's pairs); two paladins' Hammer of Justice leave one control, the "
                     "second paladin's at 3000 ms (rung 2 of the ladder, stepped per hit); two priests' Renew leave two HoTs and two warlocks' Curse of Agony "
                     "two burns (SPELL_AURA_PERIODIC_HEAL / _DAMAGE from different casters stack, :2098-2120); two mages' Fireball burns coexist\n";
}

// ---------------------------------------------------------------------------
// 5. Spell-specific exclusivity: one curse per warlock per target, corruption,
//    and the census of LoadSpellSpecific over the accepted set.
// ---------------------------------------------------------------------------
void specifics() {
    using S = LocalSpellSpecific;
    assert(localSpellSpecific(real(980)) == S::Curse && localSpellSpecific(real(603)) == S::Curse);
    assert(localSpellSpecific(real(7648)) == S::WarlockCorruption && localSpellSpecific(real(11672)) == S::WarlockCorruption); // r4 and r6: r1-r3 and r5 are rejected upstream (area targeting)
    assert(real(7648).firstRankSpell == 172 && real(11672).firstRankSpell == 172);
    assert(localSpellSpecific(real(30482)) == S::MageArmor && localSpellSpecific(real(324)) == S::ElementalShield &&
           localSpellSpecific(real(52127)) == S::ElementalShield && localSpellSpecific(real(974)) == S::ElementalShield);
    assert(localSpellSpecific(real(7294)) == S::Aura && localSpellSpecific(real(133)) == S::Normal && localSpellSpecific(real(139)) == S::Normal);
    assert(localAuraExclusiveBySpecificPerCaster(S::Curse, S::Curse) && !localAuraExclusiveBySpecific(S::Curse, S::Curse));
    assert(localAuraExclusiveBySpecific(S::MageArmor, S::MageArmor) && localAuraExclusiveBySpecific(S::ElementalShield, S::ElementalShield));
    assert(!localAuraExclusiveBySpecific(S::Curse, S::WarlockCorruption) && !localAuraExclusiveBySpecificPerCaster(S::Curse, S::WarlockCorruption));
    std::map<S, unsigned> census;
    unsigned mageArmorAgree = 0, mageArmorTotal = 0, shieldAgree = 0, shieldTotal = 0, withArea = 0;
    for (const auto& d : gImported.spells) {
        if (castable(d) && d.areaAuraProfile) ++withArea;
        if (!auraSpell(d) && !(castable(d) && d.areaAuraProfile)) continue;
        ++census[localSpellSpecific(d)];
        // The two hand rules of the implementation are exactly two values of the specific.
        if (d.mageArmorGroup || localSpellSpecific(d) == S::MageArmor) { ++mageArmorTotal; mageArmorAgree += bool(d.mageArmorGroup) == (localSpellSpecific(d) == S::MageArmor); }
        if (localElementalShield(d) || localSpellSpecific(d) == S::ElementalShield) { ++shieldTotal; shieldAgree += localElementalShield(d) == (localSpellSpecific(d) == S::ElementalShield); }
    }
    // Section 7.4 over the 279 (plus the seven Retribution Aura ranks, which
    // land in the area-aura path): CURSE 11, WARLOCK_CORRUPTION 6, AURA 7. The
    // audit wrote ELEMENTAL_SHIELD 22 and MAGE_ARMOR "Molten Armor x2"; the
    // accepted members are 25 (Lightning 11, Water 9, Earth 5 - the audit's
    // 10 / 8 / 4 are its section 7.3 LINK counts, one below each chain's
    // membership) and 3 (Molten Armor 30482, 43045, 43046). The pinned data wins.
    const bool censusExact = expect(census[S::ElementalShield] == 25 && census[S::Curse] == 11 && census[S::WarlockCorruption] == 6 && census[S::Aura] == 7 && withArea == 7 &&
                                    census[S::MageArmor] == 3 && mageArmorAgree == mageArmorTotal && mageArmorTotal == 3 && shieldAgree == shieldTotal && shieldTotal == 25 &&
                                    !census[S::Sting] && !census[S::Seal] && !census[S::Hand] && !census[S::Aspect] && !census[S::Food] && !census[S::Drink] &&
                                    !census[S::Scroll] && !census[S::Charm] && !census[S::Tracker] && !census[S::MagePolymorph] && !census[S::Presence],
        "specific census: shield " + std::to_string(census[S::ElementalShield]) + " curse " + std::to_string(census[S::Curse]) + " corruption " +
        std::to_string(census[S::WarlockCorruption]) + " aura " + std::to_string(census[S::Aura]) + " mage armour " + std::to_string(census[S::MageArmor]) +
        " (hand rule agrees " + std::to_string(mageArmorAgree) + "/" + std::to_string(mageArmorTotal) + ", shield hand rule agrees " + std::to_string(shieldAgree) +
        "/" + std::to_string(shieldTotal) + ") against 25 / 11 / 6 / 7 / 3");
    // The periodic arm of CanStackWith reads EffectApplyAuraName; a column-less
    // fixture definition is read through the importer's derived periodic
    // fields instead (local_spell_ranks.hpp). On every imported definition the
    // two reads must agree, or the fallback would be a second rule.
    unsigned periodicRaw = 0, periodicMismatch = 0;
    std::vector<uint32_t> mismatched;
    for (const auto& d : gImported.spells) {
        if (!auraSpell(d)) continue;
        bool raw = false;
        for (unsigned k = 0; k < 3; ++k) if (((d.effectMask >> k) & 1u) && localPeriodicAuraType(d.effectAura[k])) raw = true;
        const bool derived = d.periodicDamage || d.periodicHeal || d.periodicHealMaxHealthPct;
        periodicRaw += raw;
        if (raw != derived) { ++periodicMismatch; mismatched.push_back(d.id); }
        assert(d.effectMask); // every imported definition carries its columns
    }
    const bool periodicExact = expect(periodicMismatch == 0, "periodic read: " + std::to_string(periodicMismatch) + " of the accepted aura spells disagree between the raw columns and the derived fields (" + join(mismatched) + ")");
    // One warlock: Curse of Agony (980) then Curse of Doom (603): one curse - Doom.
    World w; buildWorld(w, {980, 603, 7648, 11672}, {{9, LocalResourceType::Mana, 80}});
    auto& lock = w.casters[0];
    castUntilLanded(w, lock, 980);
    castUntilLanded(w, lock, 603);
    auto held = dots(w, w.npcGuid);
    const bool curses = expect(held.size() == 1 && held[0].spellId == 603, "Agony then Doom:" + describeDots(held));
    // Corruption beside the curse: a different specific, and a different chain - coexist.
    castUntilLanded(w, lock, 7648);
    held = dots(w, w.npcGuid);
    const bool corruptionBeside = expect(held.size() == 2, "Doom + Corruption:" + describeDots(held));
    // Corruption r6 over r4: one (chain and WARLOCK_CORRUPTION both say so).
    castUntilLanded(w, lock, 11672);
    held = dots(w, w.npcGuid);
    bool oneCorruption = held.size() == 2;
    for (const auto& a : held) if (localSpellSpecific(real(a.spellId)) == S::WarlockCorruption) oneCorruption = oneCorruption && a.spellId == 11672;
    expect(oneCorruption, "Corruption r6 over r4:" + describeDots(held));
    // Agony back over Doom: replaced again (per caster, either direction).
    castUntilLanded(w, lock, 980);
    held = dots(w, w.npcGuid);
    unsigned curseCount = 0;
    for (const auto& a : held) if (localSpellSpecific(real(a.spellId)) == S::Curse) ++curseCount;
    assert(curseCount == 1 && held.size() == 2);
    if (censusExact && periodicExact && curses && corruptionBeside && oneCorruption)
        std::cout << "PASS spell specifics: LoadSpellSpecific over the 279 accepted castable aura spells and the " << withArea << " Retribution Aura ranks - ELEMENTAL_SHIELD " << census[S::ElementalShield]
                  << " (Lightning 11, Water 9, Earth 5: the audit's 22 counted links, not members)"
                  << ", CURSE " << census[S::Curse] << ", WARLOCK_CORRUPTION " << census[S::WarlockCorruption] << ", AURA " << census[S::Aura] << ", MAGE_ARMOR "
                  << census[S::MageArmor] << " (the three accepted Molten Armor ranks), everything else NORMAL (" << census[S::Normal] << "); the the reference mage-armour and elemental-shield hand rules agree "
                     "with the specific on every one of their " << mageArmorTotal << " + " << shieldTotal << " members; the raw EffectApplyAuraName read of CanStackWith's periodic arm "
                     "marks " << periodicRaw << " of them periodic and agrees with the importer's derived periodic fields on all (the fallback a column-less fixture reads); "
                     "one warlock's Curse of Agony then Curse of Doom "
                     "leaves one curse (Doom), Corruption r4 lands beside it, Corruption r6 replaces r4, Agony over Doom replaces again - "
                     "IsAuraExclusiveBySpecificPerCasterWith, SpellInfo.cpp:1449-1465, 110 + 30 of the audit's pairs\n";
}

// ---------------------------------------------------------------------------
// 6. The group tables as data: load, membership, and zero new pairs.
// ---------------------------------------------------------------------------
void spellGroups() {
    constexpr size_t groupRows = sizeof(kLocalSpellGroups) / sizeof(kLocalSpellGroups[0]);
    constexpr size_t ruleRows = sizeof(kLocalSpellGroupStackRules) / sizeof(kLocalSpellGroupStackRules[0]);
    std::set<uint32_t> groups; unsigned negatives = 0;
    for (const auto& r : kLocalSpellGroups) { groups.insert(r.group); negatives += r.spell < 0; }
    std::array<unsigned, 5> histogram{};
    for (const auto& r : kLocalSpellGroupStackRules) { assert(r.rule <= 4 && groups.count(r.group)); ++histogram[r.rule]; }
    // Every listed positive member is a first rank (the loader drops the rest;
    // at the pin nothing is dropped).
    unsigned nonFirst = 0;
    for (const auto& r : kLocalSpellGroups) if (r.spell > 0 && localSpellRankRow(uint32_t(r.spell)) && localSpellRankFirst(uint32_t(r.spell)) != uint32_t(r.spell)) ++nonFirst;
    // The audit's section 7.1 counted 68 rule rows (18 / 3 / 20 / 25 / 2); the
    // pinned file holds 69 (19 EXCLUSIVE). The pinned source wins.
    const bool tablesExact = expect(groupRows == 532 && groups.size() == 113 && negatives == 75 && ruleRows == 69 &&
                                    histogram == std::array<unsigned, 5>{2, 19, 3, 20, 25} && nonFirst == 0,
        "group tables: " + std::to_string(groupRows) + " rows / " + std::to_string(groups.size()) + " groups / " + std::to_string(negatives) +
        " sub-group rows; " + std::to_string(ruleRows) + " rules " + std::to_string(histogram[0]) + "/" + std::to_string(histogram[1]) + "/" +
        std::to_string(histogram[2]) + "/" + std::to_string(histogram[3]) + "/" + std::to_string(histogram[4]) + "; " + std::to_string(nonFirst) + " non-first members");
    // Section 7.2: membership among the accepted ids - 17 ids in 7 groups.
    std::set<uint32_t> memberIds, memberGroups;
    for (const auto& d : gImported.spells) if (accepted(d)) for (auto g : localSpellGroupsOf(d.firstRankSpell)) { memberIds.insert(d.id); memberGroups.insert(g); }
    assert(localSpellMemberOfGroup(7294, 1054) && localSpellMemberOfGroup(7294, 1052) && localSpellMemberOfGroup(467, 1113) && localSpellMemberOfGroup(1126, 1089));
    // Two Retribution Aura ranks share sub-group 1052 of both 1054 and 1056, so
    // CheckSpellGroupStackRules excludes those (SpellMgr.cpp:826-840) and only
    // rule-less 1052 remains: DEFAULT. Retribution Aura against Sanctified
    // Retribution 63531 (sub-group 1053 of 1054 and 1056) reads SAME_EFFECT.
    assert(localSpellGroupStackRule(7294, 7294) == LocalSpellGroupStackRule::Default);
    assert(localSpellGroupStackRule(7294, 63531) == LocalSpellGroupStackRule::ExclusiveSameEffect && localSpellMemberOfGroup(63531, 1053));
    assert(localSpellGroupStackRule(133, 143) == LocalSpellGroupStackRule::Default);
    // Group 1010 "Blessings" is -1002 -1005 -1007 -1008 -1009: two Blessings of
    // Might ranks share sub-group 1002, so 1010 is excluded and 1002's own rule
    // answers; Blessing of Might vs Blessing of Wisdom meet only in 1010.
    assert(localSpellMemberOfGroup(19740, 1010) && localSpellMemberOfGroup(19742, 1010));
    const bool membershipExact = expect(memberIds.size() == 17 && memberGroups.size() == 7 && memberGroups == std::set<uint32_t>{1052, 1054, 1056, 1078, 1089, 1107, 1113},
        "group membership: " + std::to_string(memberIds.size()) + " accepted ids in " + std::to_string(memberGroups.size()) + " groups (" + join(memberGroups) + ")");
    // Every ordered pair of accepted castable aura spells that share a
    // container: pairs a group rule forbids that the chain rule would not.
    std::vector<const LocalSpellDefinition*> auras;
    for (const auto& d : gImported.spells) if (auraSpell(d)) auras.push_back(&d);
    for (const auto& d : gImported.spells) if (castable(d) && d.areaAuraProfile) auras.push_back(&d);
    const auto container = [](const LocalSpellDefinition& d) {
        return d.areaAuraProfile ? 6 : d.controlProfile ? 4 : d.snarePercent ? 5 : d.periodicDamage ? 2 : (d.periodicHeal || d.periodicHealMaxHealthPct) ? 3 : 1; };
    unsigned groupForbidden = 0, groupForbiddenNewPairs = 0, thornsPairs = 0, thornsAgree = 0, retributionPairs = 0;
    LocalWorldContent c; c.spells = gImported.spells;
    std::sort(c.spells.begin(), c.spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (const auto* a : auras) for (const auto* b : auras) {
        if (a == b || container(*a) != container(*b)) continue;
        const auto rule = localSpellGroupStackRule(a->firstRankSpell, b->firstRankSpell);
        const bool forbids = rule == LocalSpellGroupStackRule::Exclusive || rule == LocalSpellGroupStackRule::ExclusiveHighest || rule == LocalSpellGroupStackRule::ExclusiveFromSameCaster;
        if (forbids) { ++groupForbidden; if (!localSameRankChain(c, *a, *b)) ++groupForbiddenNewPairs; }
        if (a->firstRankSpell == 467 && b->firstRankSpell == 467) {
            ++thornsPairs;
            assert(rule == LocalSpellGroupStackRule::ExclusiveHighest);
            // The amount rule and the rank rule agree: the higher rank has the larger damage-shield amount.
            const bool aHigher = laterSpellRank(c, b->id, a->id);
            if (aHigher == (a->proc.amount > b->proc.amount)) ++thornsAgree;
        }
        if (a->firstRankSpell == 7294 && b->firstRankSpell == 7294) { ++retributionPairs; assert(rule == LocalSpellGroupStackRule::Default); }
    }
    // Thorns: 8 ranks -> 56 ordered pairs, all EXCLUSIVE_HIGHEST and all in one chain (the audit's 28 are the unordered, same-caster "reject" pairs).
    const bool pairsExact = expect(groupForbiddenNewPairs == 0 && thornsPairs == 56 && thornsAgree == 56 && retributionPairs == 42,
        "group pairs: " + std::to_string(groupForbidden) + " forbidden by a rule, " + std::to_string(groupForbiddenNewPairs) + " of them outside the chain rule; Thorns " +
        std::to_string(thornsPairs) + " pairs, " + std::to_string(thornsAgree) + " agree; Retribution Aura " + std::to_string(retributionPairs));
    if (tablesExact && membershipExact && pairsExact)
        std::cout << "PASS spell groups as data: " << groupRows << " spell_group rows in " << groups.size() << " groups (" << negatives << " sub-group references) and "
                  << ruleRows << " stack rules (DEFAULT " << histogram[0] << ", EXCLUSIVE " << histogram[1] << ", FROM_SAME_CASTER " << histogram[2] << ", SAME_EFFECT "
                  << histogram[3] << ", HIGHEST " << histogram[4] << " - the pinned file has 69 rows and 19 EXCLUSIVE, where the audit's section 7.1 wrote 68 and 18) "
                     "load with 0 non-first members; " << memberIds.size() << " accepted ids sit in " << memberGroups.size() << " groups (Retribution Aura x7, Thorns x8, "
                     "Mark of the Wild r1, Death Wish); over every ordered same-container pair of the accepted castable aura spells a group rule forbids " << groupForbidden
                  << " and " << groupForbiddenNewPairs << " of them lie outside the chain rule (the audit's zero); all " << thornsPairs << " Thorns pairs are "
                     "EXCLUSIVE_HIGHEST and the amount comparison of Unit.cpp:4311-4348 orders them exactly as the ranks do; the " << retributionPairs
                  << " Retribution Aura pairs read DEFAULT - the ranks share sub-group 1052 of both SAME_EFFECT groups, which CheckSpellGroupStackRules excludes "
                     "(SpellMgr.cpp:826-840) - so the rank rule alone decides them, as it does in the reference\n";
}

// ---------------------------------------------------------------------------
// 7. The load-time normaliser on a synthetic old save.
// ---------------------------------------------------------------------------
void oldSaveNormalisation() {
    // A save 29 player written with three Thorns ranks (a chain the client
    // column already linked, so only a LAN peer's cast could have produced it),
    // Lightning Shield r1 beside Water Shield r1 (the specific), and a
    // spellbook holding Fireball r1, r2, r3 and every Renew rank below r5.
    auto c = fullContent(false);
    LocalGameplay game; game.useContent(c);
    LocalRealmPlayer p = rewardPlayer(1); p.classId = 11; p.level = 80; p.gameplayInitialized = true; p.quests.clear();
    p.knownSpells = {133, 143, 145, 139, 6074, 6075, 6076, 467, 782, 1075};
    const auto aura = [&](uint32_t id, uint32_t remaining) {
        LocalStatAura a; a.spellId = id; a.remainingMs = remaining; a.casterGuid = p.guid; a.procCharges = real(id).proc.charges;
        a.procAmountSnapshot = real(id).proc.amount; a.hasProcAmountSnapshot = a.procAmountSnapshot != 0; return a; };
    p.statAuras = {aura(467, 300000), aura(782, 200000), aura(1075, 100000)};
    Writer w; writeProgress(w, p);
    LocalRealmPlayer loaded; loaded.guid = 1;
    Reader r(w.bytes.data(), w.bytes.size());
    assert(readProgress(r, loaded) && r.done());
    assert(loaded.statAuras.size() == 3 && loaded.knownSpells.size() == 10);
    // The periodic containers are not in the save at all: a Fireball burn on a
    // creature is authority state (Impl::periodicDamage) that never reaches
    // writeProgress, so "three Fireball-DoT ranks in a save" cannot exist; the
    // three known Fireball ranks and the three Thorns auras are what a save can hold.
    game.initializePlayer(loaded, false);
    std::vector<uint32_t> known = loaded.knownSpells; std::sort(known.begin(), known.end());
    unsigned thorns = 0; uint32_t kept = 0;
    for (const auto& a : loaded.statAuras) if (real(a.spellId).firstRankSpell == 467) { ++thorns; kept = a.spellId; }
    const bool normalised = expect(known == std::vector<uint32_t>{145, 1075, 6076} && thorns == 1 && kept == 1075,
        "old save: known " + join(known) + ", Thorns auras " + std::to_string(thorns) + " kept " + std::to_string(kept));
    // The normaliser alone, on a two-shield shaman: the specific keeps the longer.
    LocalRealmPlayer sh = rewardPlayer(2); sh.classId = 7; sh.level = 80;
    sh.statAuras = {aura(324, 50000), aura(52127, 90000)};
    normalizeLocalCompetingAuras(sh, *c);
    const bool shields = expect(sh.statAuras.size() == 1 && sh.statAuras[0].spellId == 52127, "two shields normalised to " + std::to_string(sh.statAuras.size()));
    // Nothing is granted fresh time and a clean player is untouched.
    assert(loaded.statAuras[0].remainingMs <= 100000);
    LocalRealmPlayer clean = rewardPlayer(3); clean.classId = 8; clean.level = 80; clean.knownSpells = {145, 6076, 42842}; clean.gameplayInitialized = true;
    clean.statAuras = {aura(30482, 1000)};
    const auto beforeClean = clean.knownSpells; const auto beforeAuras = clean.statAuras.size();
    assert(normalizeLocalKnownRanks(clean, *c) == 0); normalizeLocalCompetingAuras(clean, *c);
    assert(clean.knownSpells == beforeClean && clean.statAuras.size() == beforeAuras);
    if (normalised && shields)
        std::cout << "PASS old-save normalisation: a save" << int(SaveVersion) << " druid written with Thorns r1 + r2 + r3 and a spellbook of Fireball r1-r3, Renew "
                     "r1-r4 and Thorns r1-r3 loads with one Thorns (r3, the highest rank, its own remaining time) and a spellbook of Fireball r3, Renew r4, Thorns r3 "
                     "(Player::_addSpell marks a lower rank inactive; this build erases it, as training always did); Lightning Shield beside Water Shield "
                     "normalises to one; a clean player is untouched; the periodic containers are authority state and never enter the save\n";
}

// ---------------------------------------------------------------------------
// 8. Save 30 / LAN 85 (the implementation moved both for the pet roster); the per-player
//    block is untouched and the content fingerprint carries the rank identity.
// ---------------------------------------------------------------------------
void formats() {
    // the implementation moved both: writePet gained the pet's command state, react state
    // and stay point, and that one layout is shared by the character save and
    // the LAN pet deck, so a the implementation peer would mis-parse the deck.
    static_assert(SaveVersion == 30);
    static_assert(Version == 85 && Version == lan::GameplayVersion);
    static_assert(CastWireBytes == 222);
    static_assert(NpcWireBytes == 618);
    LocalRealmPlayer saved; saved.guid = 1; saved.classId = 8; saved.level = 80; saved.money = 321; saved.knownSpells = {145, 42842};
    Writer current; writeProgress(current, saved);
    LocalRealmPlayer restored; restored.guid = 1;
    Reader read(current.bytes.data(), current.bytes.size());
    assert(readProgress(read, restored) && read.done());
    assert(restored.money == saved.money && restored.knownSpells == saved.knownSpells);
    // The per-player block last grew at save 29 (the area emitters). Save 30
    // grew the realm-level PET roster instead, so pin the per-player migration
    // at its own boundary rather than against a moving SaveVersion - 1, and
    // assert that 30 left the player block byte-identical to 29.
    Writer atTwentyEight; writeProgress(atTwentyEight, saved, 28);
    Writer atTwentyNine; writeProgress(atTwentyNine, saved, 29);
    assert(atTwentyEight.bytes.size() < atTwentyNine.bytes.size());
    assert(atTwentyNine.bytes == current.bytes);
    // The fingerprint moves with firstRankSpell and the raw stacking columns:
    // two contents that differ only in one first rank do not join.
    const auto fingerprintOf = [](std::vector<LocalSpellDefinition> spells) {
        LocalGameplay game; auto c = std::make_shared<LocalWorldContent>(); game.useContent(c);
        std::string error; const bool ok = game.setStarterSpells(spells, "", error);
        assert(ok);
        return c->fingerprint; };
    std::vector<LocalSpellDefinition> spells;
    for (const auto& d : gImported.spells) if (d.clientSpell && d.allowableClasses) spells.push_back(d); // the realm's own admission gate
    const auto base = fingerprintOf(spells), again = fingerprintOf(spells);
    for (auto& d : spells) if (d.id == 143) d.firstRankSpell = 143;
    const auto moved = fingerprintOf(spells);
    assert(base == again && base != moved);
    std::cout << "PASS formats: save" << int(SaveVersion) << " and LAN" << int(Version) << " carry nothing of this package's (CastWireBytes " << CastWireBytes << ", NpcWireBytes "
              << NpcWireBytes << ", the player block round-trips, save28 is a strictly shorter prefix of save29 and save29 is byte-identical to the current one); rank identity is definition "
                 "data folded into the content fingerprint, which two peers whose importers disagree on one first rank do not share\n";
}

// ---------------------------------------------------------------------------
// 9. The exclusivity rules that already held: mage armour, elemental shield
//    with its carried cooldown, paladin area-aura dominance.
// ---------------------------------------------------------------------------
void existingExclusivity() {
    // Molten Armor r1 (30482) then r2 (43045): one armour, r2 (the MAGE_ARMOR specific and the chain).
    World m; buildWorld(m, {30482, 43045, 34913, 43043, 43044}, {{8, LocalResourceType::Mana, 80}}); // the armour and its retaliation children
    auto& mage = m.casters[0];
    castOnPlayer(m, mage, 30482, mage); castOnPlayer(m, mage, 43045, mage);
    unsigned armours = 0; uint32_t armourId = 0;
    for (const auto& a : mage.statAuras) if (real(a.spellId).mageArmorGroup) { ++armours; armourId = a.spellId; }
    const bool molten = expect(armours == 1 && armourId == 43045, "Molten Armor r1 then r2: " + std::to_string(armours));
    castOnPlayer(m, mage, 30482, mage);
    armours = 0; for (const auto& a : mage.statAuras) if (real(a.spellId).mageArmorGroup) { ++armours; armourId = a.spellId; }
    assert(armours == 1 && armourId == 30482);
    // Lightning Shield (324) then Water Shield (52127): one shield, the internal
    // cooldown carried across (the 02.2x rule, now the ELEMENTAL_SHIELD specific).
    World s; buildWorld(s, {324, 52127}, {{7, LocalResourceType::Mana, 80}});
    auto& shaman = s.casters[0];
    castOnPlayer(s, shaman, 324, shaman);
    for (auto& a : shaman.statAuras) if (a.spellId == 324) a.procCooldownMs = 3000;
    castOnPlayer(s, shaman, 52127, shaman);
    unsigned shields = 0; uint32_t shieldId = 0, carried = 0;
    for (const auto& a : shaman.statAuras) if (localElementalShield(real(a.spellId))) { ++shields; shieldId = a.spellId; carried = a.procCooldownMs; }
    const bool elemental = expect(shields == 1 && shieldId == 52127 && carried == 3000, "Lightning then Water Shield: " + std::to_string(shields) + " shield(s), cooldown " + std::to_string(carried));
    // Two paladins' Retribution Aura: SPELL_SPECIFIC_AURA is per caster and the
    // area-aura path keeps one emitter per caster, both applied, the stronger
    // effective (IsPaladinAuraDominant) - CanStackWith's own exemption (:2164).
    World a; buildWorld(a, {7294, 10298}, {{2, LocalResourceType::Mana, 80}, {2, LocalResourceType::Mana, 80}});
    castOnPlayer(a, a.casters[0], 7294, a.casters[0]);
    castOnPlayer(a, a.casters[1], 10298, a.casters[1]);
    for (unsigned i = 0; i < 4; ++i) a.game.tick(.25f, a.players);
    assert(a.casters[0].areaEmitters.size() == 1 && a.casters[1].areaEmitters.size() == 1);
    castOnPlayer(a, a.casters[0], 10298, a.casters[0]);
    for (unsigned i = 0; i < 4; ++i) a.game.tick(.25f, a.players);
    const bool retribution = expect(a.casters[0].areaEmitters.size() == 1 && a.casters[0].areaEmitters[0].spellId == 10298 && a.casters[1].areaEmitters.size() == 1,
        "Retribution Aura emitters: " + std::to_string(a.casters[0].areaEmitters.size()) + " / " + std::to_string(a.casters[1].areaEmitters.size()));
    assert(localAuraCanStackWith(*a.content, real(10298), 1, real(7294), 2)); // the area-aura path's exemption, stated by CanStackWith itself
    assert(!localAuraCanStackWith(*a.content, real(10298), 1, real(7294), 1));
    // Shadow Word: Pain r1 (589) then r2 (594) then r1 from one priest: one
    // periodic on the creature each time (a chain the table linked: 12 ranks).
    World r; buildWorld(r, {589, 594}, {{5, LocalResourceType::Mana, 20}});
    castUntilLanded(r, r.casters[0], 594);
    castUntilLanded(r, r.casters[0], 589);
    const auto pains = dots(r, r.npcGuid);
    const bool rend = expect(pains.size() == 1 && pains[0].spellId == 589, "SW:P r2 then r1:" + describeDots(pains));
    if (molten && elemental && retribution && rend)
        std::cout << "PASS existing exclusivity: Molten Armor r1 then r2 leaves one armour and r1 over r2 replaces it (SPELL_SPECIFIC_MAGE_ARMOR); Lightning "
                     "Shield then Water Shield leaves one shield carrying the 3000 ms internal cooldown (SPELL_SPECIFIC_ELEMENTAL_SHIELD); two paladins' "
                     "Retribution Aura keep one emitter each through the area-aura path (CanStackWith's own SPELL_SPECIFIC_AURA exemption, SpellAuras.cpp:2164) "
                     "and a paladin's rank upgrade replaces his emitter; Shadow Word: Pain r2 then r1 leaves one periodic (r1)\n";
}
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    assert(argc >= 2);
    ClientTables tables;
    tables.load(argv[1]);
    gTables = &tables;
    gImported = tables.import();
    {
        // Column 8 exactly as the importer read it at rows of a class,
        // profession or secondary skill line, merged per spell in (spell, line)
        // order, the first non-zero value kept (local_spell_import.hpp, "One entry per spell").
        const auto* sla = tables.get("SkillLineAbility"); const auto* lines = tables.get("SkillLine");
        std::set<uint32_t> lineIds;
        for (uint32_t row = 0; row < lines->getRecordCount(); ++row) {
            const auto category = lines->getUInt32(row, 1);
            if (category == kLocalSkillCategoryClass || category == kLocalSkillCategoryProfession || category == kLocalSkillCategorySecondary) lineIds.insert(lines->getUInt32(row, 0));
        }
        std::vector<std::array<uint32_t, 3>> rows;
        for (uint32_t row = 0; row < sla->getRecordCount(); ++row) {
            const auto spell = sla->getUInt32(row, 2), line = sla->getUInt32(row, 1), next = sla->getUInt32(row, 8);
            if (spell && (lineIds.count(line) || spell == 5019)) rows.push_back({spell, line, next});
        }
        std::sort(rows.begin(), rows.end());
        for (const auto& r : rows) if (r[2] && !gClientSuperceded.count(r[0])) gClientSuperceded[r[0]] = r[2];
        for (const auto& d : gImported.spells) gNames[d.id] = d.name;
    }
    rankCensus();
    trainableSetDiff();
    if (argc >= 3) writeTrainableJson(argv[2]);
    sameCasterRankReplacement();
    differentCasters();
    specifics();
    spellGroups();
    oldSaveNormalisation();
    formats();
    existingExclusivity();
    if (gFailures)
        std::cerr << gFailures << " group(s) failed against source-backed fixtures\n";
    return gFailures ? 1 : 0;
}
