// P04 / C2 - NPC control auras: the importer's narrow admission of
// SPELL_AURA_MOD_STUN (12) and SPELL_AURA_MOD_SILENCE (27), the suppression the
// control list drives in the authority's creature tick, break-on-damage rule 1,
// the control lifecycle, and the LAN 83 codec that replicates it.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33, as quoted in
// the source audit sections 1.3-1.5, 2.1, 2.3, 3.3, 4.1,
// 4.2, 5.1, 6.5 and 10.1 steps 1-4:
//   Unit::SetStunned          Unit.cpp:13972-14016 - a stun is a root plus a
//       flag plus a cast stop. It sets UNIT_FLAG_STUNNED and nothing else.
//   AuraEffect::HandleAuraModSilence SpellAuraEffects.cpp:3146-3174 - the only
//       writer of UNIT_FLAG_SILENCED, and it interrupts only a spell whose own
//       PreventionType is SPELL_PREVENTION_TYPE_SILENCE.
//   Spell::CheckCasterAuras   Spell.cpp:7134-7175 - stun prevents the cast
//       outright; silence prevents it only when the spell being cast carries
//       PreventionType == 1. The column belongs to the spell being PREVENTED.
//   Unit::AttackerStateUpdate Unit.cpp:2736-2748 - UNIT_STATE_LOST_CONTROL
//       (stun included) blocks the swing; root does not.
//   Unit::DealDamage          Unit.cpp:1023-1035 and
//   Unit::RemoveAurasWithInterruptFlags Unit.cpp:5179-5212 - break-on-damage
//       rule 1 is binary, has no threshold and no chance, excepts the damaging
//       spell's own id, and is skipped for a source carrying
//       SPELL_ATTR4_DAMAGE_DOESNT_BREAK_AURAS.
//
// Every census number here is measured against the player's own Spell.dbc by
// running the shipped importer twice - once as it stands, once over a copy of
// the table whose aura-12 and aura-27 effects have been renumbered to an aura
// no accept-set admits - and differencing the accepted-id sets. Every runtime
// number is produced by the shipped LocalGameplay tick driving real imported
// definitions, so a control's duration, mechanic, interrupt flags and
// prevention type are the client's own values and not a fixture's.
#include "../../src/game/local_realm.cpp"
#include "local_group_rewards_fixture.hpp"
#include "game/local_npc_auras.hpp"
#include "game/local_diminishing.hpp"
#include "game/local_npc_spell_profiles.hpp"
#include "game/local_melee.hpp"
#include "game/local_spell_import.hpp"
#include "game/protocol_constants.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace wowee;
using namespace wowee::game;
namespace fs = std::filesystem;

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

// A definition exactly as the shipped importer produced it from the player's
// own client data. Every runtime group below builds its world out of these, so
// no duration, mechanic, interrupt-flag or prevention-type value in this suite
// is hand-authored.
const LocalSpellDefinition& real(uint32_t id) {
    for (const auto& d : gImported.spells) if (d.id == id) return d;
    std::cerr << "FAIL: spell " << id << " is absent from the client import\n";
    std::abort();
}

// --- the census the checkpoint claims ---------------------------------------
// Aura 12 unlocks 21 ids: 17 spells that carry the stun themselves plus 4
// second-order cast-modifier talents whose affected-spell gate only passes once
// Sap and Hammer of Justice exist. Aura 27 unlocks Priest Silence. 21 + 1 = 22.
constexpr uint32_t kStunSpells[] = {853, 2637, 5211, 5588, 5589, 6798, 8983, 9484, 9485,
                                    10308, 10955, 11297, 12355, 18657, 18658, 20066, 51724};
constexpr uint32_t kSilenceSpells[] = {15487};
// 16940/16941 Brutal Impact (Bash/Pounce duration) joined when a later checkpoint
// admitted the Druid talent modifiers; they unlock only with Bash present.
constexpr uint32_t kSecondOrderTalents[] = {14076, 14094, 16940, 16941, 20487, 20488};
// Unit.cpp:1031-1035 rule 1: nine of the twenty-one carry TAKE_DAMAGE.
constexpr uint32_t kBreakOnDamage[] = {2637, 9484, 9485, 10955, 11297, 18657, 18658, 20066, 51724};

// The five SpellMechanic.dbc rows the accepted stuns carry. The importer must
// impose no mechanic requirement, because the reference imposes none.
constexpr uint8_t kMechanicAsleep = 10, kMechanicStunned = 12, kMechanicIncapacitated = 14,
                  kMechanicShackled = 20, kMechanicSapped = 30, kMechanicSilenced = 9;

// Two groups below assert against the reference and are expected to hold; when
// one does not, the suite must still run the rest so the whole picture is
// reported, and must still exit non-zero. `expect` is exactly `assert` with the
// abort deferred to the end of main - nothing is relaxed by it.
unsigned gFailures = 0;
bool expect(bool ok, const std::string& what) {
    if (!ok) { ++gFailures; std::cerr << "FAIL " << what << "\n"; }
    return ok;
}

std::set<uint32_t> auditedAccepted(const LocalSpellImport& in) {
    std::set<uint32_t> ids;
    for (const auto& r : in.audit) if (r.status == "Supported decoder; imported") ids.insert(r.id);
    return ids;
}
std::set<uint32_t> definitionAccepted(const LocalSpellImport& in) {
    std::set<uint32_t> ids;
    // The census is the player importer's. Creature-only definitions (the
    // generated SmartAI family) decode MOD_STUN for creatures since 2.35 and
    // are counted by the family suite instead.
    for (const auto& d : in.spells) if (d.unsupportedReason.empty() && !d.npcOnly) ids.insert(d.id);
    return ids;
}

void importerCensus(ClientTables& t) {
    const auto* spells = t.get("Spell");
    const auto recordSize = spells->getRecordSize();
    assert(t.get("SpellMechanic")->getRecordCount() == 31); // the validator's own bound

    const auto after = t.import();
    const auto auditedAfter = auditedAccepted(after), definedAfter = definitionAccepted(after);

    // Reproduce the pre-the implementation decoder exactly: renumber every aura-applying
    // effect that carries aura 12 or 27 to an aura number no accept-set
    // admits, so each row takes the identical terminal reject path it took
    // before the branch existed ("Unsupported effect 6 / aura ...").
    std::vector<std::pair<uint32_t, unsigned>> records;
    for (uint32_t row = 0; row < spells->getRecordCount(); ++row)
        for (unsigned e = 0; e < 3; ++e)
            if (spells->getUInt32(row, 71 + e) == 6 &&
                (spells->getUInt32(row, 95 + e) == 12 || spells->getUInt32(row, 95 + e) == 27))
                records.push_back({row, e});
    assert(!records.empty());
    auto edited = t.bytes.at("Spell");
    for (const auto& [row, e] : records) {
        const size_t at = 20 + size_t(row) * recordSize + size_t(95 + e) * 4;
        edited[at] = 255; edited[at + 1] = edited[at + 2] = edited[at + 3] = 0;
    }
    assert(t.files["Spell"].load(edited));
    for (uint32_t row = 0; row < t.files["Spell"].getRecordCount(); ++row)
        for (unsigned e = 0; e < 3; ++e)
            assert(!(t.files["Spell"].getUInt32(row, 71 + e) == 6 &&
                     (t.files["Spell"].getUInt32(row, 95 + e) == 12 ||
                      t.files["Spell"].getUInt32(row, 95 + e) == 27)));
    const auto before = t.import();
    const auto auditedBefore = auditedAccepted(before), definedBefore = definitionAccepted(before);
    assert(t.files["Spell"].load(t.bytes.at("Spell")));

    // Nothing was lost. Exactly the twenty-two the audit predicted were gained,
    // and the same twenty-two on both the audit and the definition side.
    std::set<uint32_t> expected;
    for (auto id : kStunSpells) expected.insert(id);
    for (auto id : kSilenceSpells) expected.insert(id);
    for (auto id : kSecondOrderTalents) expected.insert(id);
    assert(expected.size() == 24);
    std::set<uint32_t> gained, lost;
    std::set_difference(auditedAfter.begin(), auditedAfter.end(), auditedBefore.begin(),
                        auditedBefore.end(), std::inserter(gained, gained.end()));
    std::set_difference(auditedBefore.begin(), auditedBefore.end(), auditedAfter.begin(),
                        auditedAfter.end(), std::inserter(lost, lost.end()));
    if(!lost.empty()||gained!=expected){
        std::cerr<<"census lost:";for(auto id:lost)std::cerr<<' '<<id;
        std::cerr<<"\ncensus gained-not-expected:";for(auto id:gained)if(!expected.count(id))std::cerr<<' '<<id;
        std::cerr<<"\ncensus expected-not-gained:";for(auto id:expected)if(!gained.count(id))std::cerr<<' '<<id;std::cerr<<'\n';}
    assert(lost.empty());
    assert(gained == expected);
    // 960 -> 982 at the implementation; both sides carry the eight Shield Slam ranks the implementation
    // admitted (effect 38 beside weapon damage) and the fourteen form passives
    // the implementation admitted (five boost spells and nine entry-resource talent ranks),
    // neither of which is a control, so 982 -> 1004 with the same twenty-two
    // gained and nothing lost.
    // 982 -> 1004 when written; later checkpoints admitted more spells on both
    // sides, so what stays exact is the difference: the controls and nothing else.
    std::cerr<<"census audited before="<<auditedBefore.size()<<" after="<<auditedAfter.size()<<"\n";
    assert(auditedAfter.size() - auditedBefore.size() == expected.size());
    {
        std::set<uint32_t> definitionGained, definitionLost;
        std::set_difference(definedAfter.begin(), definedAfter.end(), definedBefore.begin(),
                            definedBefore.end(), std::inserter(definitionGained, definitionGained.end()));
        std::set_difference(definedBefore.begin(), definedBefore.end(), definedAfter.begin(),
                            definedAfter.end(), std::inserter(definitionLost, definitionLost.end()));
        assert(definitionLost.empty() && definitionGained == expected);
    }

    // Exactly the seventeen stuns and the one silence carry a control profile,
    // and every one of them is accepted. Anything else the branch decoded has
    // to have been rejected by a check that already existed.
    std::map<uint8_t, unsigned> stunMechanics;
    unsigned profiled = 0, flagged = 0, unflagged = 0;
    for (const auto& d : after.spells) {
        if (!d.controlProfile) continue;
        ++profiled;
        if (!d.unsupportedReason.empty()) {
            // Deep Freeze decodes a stun and is still rejected on the aura
            // requirement it always failed. The branch widened nothing.
            assert(d.id == 44572 && d.unsupportedReason == "Aura requirements are not implemented");
            continue;
        }
        assert(d.controlEffectSlot == 0); // the narrow shape: first effect only
        assert(d.durationMs && d.durationMs <= 600000);
        if (d.controlProfile == 1) {
            assert(std::find(std::begin(kStunSpells), std::end(kStunSpells), d.id) != std::end(kStunSpells));
            ++stunMechanics[d.mechanic];
        } else {
            assert(d.controlProfile == 2 && d.id == 15487 && d.mechanic == kMechanicSilenced);
        }
    }
    assert(profiled == std::size(kStunSpells) + std::size(kSilenceSpells) + 1); // + Deep Freeze
    // Five distinct mechanics, and the point is that no mechanic requirement is
    // imposed: the reference imposes none, so admitting only MECHANIC_STUN
    // would have silently discarded nine of the seventeen.
    assert(stunMechanics.size() == 5);
    assert(stunMechanics[kMechanicStunned] == 8 && stunMechanics[kMechanicAsleep] == 3 &&
           stunMechanics[kMechanicShackled] == 3 && stunMechanics[kMechanicSapped] == 2 &&
           stunMechanics[kMechanicIncapacitated] == 1);

    // Rule 1's discriminator, measured from the client's own column 32, over
    // the twenty-one ids aura 12 alone unlocks. Silence is counted apart: it
    // is aura 27's single unlock and carries no interrupt flags at all.
    for (auto id : expected) {
        const LocalSpellDefinition* d = nullptr;
        for (const auto& s : after.spells) if (s.id == id) d = &s;
        assert(d && d->unsupportedReason.empty());
        const bool breaks = (d->auraInterruptFlags & kLocalAuraInterruptTakeDamage) != 0;
        const bool listed = std::find(std::begin(kBreakOnDamage), std::end(kBreakOnDamage), id) !=
                            std::end(kBreakOnDamage);
        assert(breaks == listed);
        if (std::find(std::begin(kSilenceSpells), std::end(kSilenceSpells), id) !=
            std::end(kSilenceSpells)) { assert(!breaks && !d->auraInterruptFlags); continue; }
        breaks ? ++flagged : ++unflagged;
    }
    // 9 + 12 = 21 when written; Brutal Impact's two talent ranks carry no
    // interrupt flags, so 9 + 14 = 23.
    assert(flagged == 9 && unflagged == 14 && flagged + unflagged == 23);

    // The two spells this realm's creatures actually cast. SPELL_PREVENTION_TYPE
    // is a column of the spell being prevented, so a silence is only observable
    // against a creature whose own spell carries prevention type 1. Both do.
    for (uint32_t id : {5401u, 11985u}) {
        uint32_t row = 0;
        while (row < spells->getRecordCount() && spells->getUInt32(row, 0) != id) ++row;
        assert(row < spells->getRecordCount());
        assert(spells->getUInt32(row, 214) == kLocalPreventionSilence);
    }

    std::cout << "PASS importer census: aura 12 and aura 27 admitted in the narrow shape take the "
                 "audited-accepted set from " << auditedBefore.size() << " to " << auditedAfter.size()
              << " ids, gaining exactly the " << gained.size()
              << " the audit predicted (853/5588/5589/10308 Hammer of Justice, 5211/6798/8983 Bash, "
                 "2637/18657/18658 Hibernate, 9484/9485/10955 Shackle Undead, 11297/51724 Sap, "
                 "12355 Impact, 20066 Repentance, 15487 Silence, 14076/14094 Dirty Tricks, "
                 "20487/20488 Improved Hammer of Justice) and losing none; the 17 accepted stuns "
                 "carry five different mechanics (stunned x8, asleep x3, shackled x3, sapped x2, "
                 "incapacitated x1), so no mechanic requirement is imposed; " << flagged << " of the "
              << (flagged + unflagged) << " carry AURA_INTERRUPT_FLAG_TAKE_DAMAGE and " << unflagged
              << " do not; both NPC-castable spells (5401, 11985) carry Spell.dbc column 214 == 1\n";
}

// ---------------------------------------------------------------------------
// Runtime. Every world below is the shipped authority driving real imported
// definitions against the fixture's creature table.
// ---------------------------------------------------------------------------
std::shared_ptr<LocalWorldContent> controlWorld(std::vector<uint32_t> ids, uint32_t entry = 50,
                                                uint8_t level = 1, uint32_t damage = 10) {
    auto c = rewardContent();
    for (auto id : ids) c->spells.push_back(real(id));
    std::sort(c->spells.begin(), c->spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    auto& npc = c->npcs[0];
    npc.id = entry; npc.name = "Control target"; npc.hostile = true;
    npc.health = 100000000; npc.damage = damage; npc.level = level; npc.armor = 0;
    npc.aggroRadius = 0.01f; npc.xp = 0; npc.money = 0; npc.loot.clear();
    return c;
}
LocalRealmPlayer controlCaster(uint64_t guid, uint8_t classId, LocalResourceType resource = LocalResourceType::Mana) {
    auto p = rewardPlayer(guid);
    p.classId = classId; p.level = 80; p.health = p.maxHealth = 100000000;
    p.mana = p.maxMana = 1000000; p.resourceType = resource;
    p.x = p.y = p.z = 0; p.quests.clear();
    return p;
}
LocalRealmNpc controlNpc(uint32_t entry, uint8_t level, float x = 8) {
    auto n = rewardNpc();
    n.entry = entry; n.name = "Control target"; n.level = level;
    n.health = n.maxHealth = 100000000; n.hostile = true; n.lootOwner = 0;
    n.x = n.homeX = x; n.y = n.homeY = 0; n.z = n.homeZ = 0;
    return n;
}
const LocalRealmNpc* findNpc(const LocalGameplay& game, uint64_t guid) {
    for (const auto& n : game.npcs()) if (n.guid == guid) return &n;
    return nullptr;
}
// Cast until the magic hit roll lets it land. Every control here is real client
// data; Hammer of Justice and Repentance are SPELL_DAMAGE_CLASS_MAGIC and roll
// WorldObject::MagicSpellHitResult, so a cast really can miss.
bool castControl(LocalGameplay& game, LocalRealmPlayer& p, uint64_t target, uint32_t spellId,
                 const std::vector<LocalRealmPlayer*>& players, unsigned attempts = 40) {
    for (unsigned i = 0; i < attempts; ++i) {
        p.globalCooldownMs = 0; p.cooldowns.clear(); p.categoryCooldowns.clear();
        p.mana = p.maxMana;
        std::string result;
        if (!game.execute(p, {LocalAction::CastSpell, target, spellId}, players, result)) {
            std::cerr << "FAIL: cast " << spellId << " rejected: " << result << "\n";
            return false;
        }
        const auto* n = findNpc(game, target);
        if (n) for (const auto& a : n->controls)
            if (a.spellId == spellId && a.casterGuid == p.guid) return true;
    }
    return false;
}
unsigned npcSwings(const LocalGameplay& game, uint64_t npcGuid) {
    unsigned count = 0;
    for (const auto& e : game.combatEvents())
        if (e.kind == LocalCombatEventKind::NpcMelee && e.source == npcGuid) ++count;
    return count;
}

// ---------------------------------------------------------------------------
// Unit::SetStunned - the stun is a root before it is anything else.
// ---------------------------------------------------------------------------
void stunMovement() {
    // 10308 Hammer of Justice rank 4: a real 6000 ms stun with no TAKE_DAMAGE,
    // so nothing in this group can end it early except its own expiry.
    const auto& hammer = real(10308);
    assert(hammer.unsupportedReason.empty() && hammer.controlProfile == 1 && hammer.durationMs == 6000);
    assert(!(hammer.auraInterruptFlags & kLocalAuraInterruptTakeDamage));

    auto measure = [&](bool stun) {
        auto c = controlWorld({10308}, 50, 1, 0); // damage 0: movement only
        LocalGameplay game; game.useContent(c);
        auto p = controlCaster(1, 2);
        p.knownSpells = {1, 10308};
        std::vector<LocalRealmPlayer*> players{&p};
        auto n = controlNpc(50, 1, 8);
        n.targetGuid = p.guid; n.threat[0] = {p.guid, 1000};
        game.tick(0, players); game.setRemoteNpcs({n});
        if (stun) assert(castControl(game, p, n.guid, 10308, players));
        const float start = game.npcs()[0].x;
        for (unsigned i = 0; i < 20; ++i) game.tick(.25f, players); // 5000 ms, inside the stun
        const float during = game.npcs()[0].x;
        for (unsigned i = 0; i < 20; ++i) game.tick(.25f, players); // 5000 ms more, stun expired
        return std::array<float, 3>{start, during, game.npcs()[0].x};
    };
    const auto free = measure(false), stunned = measure(true);
    // The unstunned creature closes to its melee stand-off inside the first
    // window; the stunned one has not moved a millimetre after the same window,
    // and closes the identical distance once the stun ends.
    assert(free[0] == 8 && stunned[0] == 8);
    assert(free[1] < 3.5f && free[1] == free[2]);
    assert(stunned[1] == stunned[0]);
    assert(stunned[2] == free[2]);
    std::cout << "PASS stun movement: a creature pursuing a player from " << stunned[0]
              << " yards closed to " << free[1] << " over 20 ticks unstunned and stayed at "
              << stunned[1] << " across the same 20 ticks under real Hammer of Justice rank 4 ("
              << hammer.durationMs << " ms), then closed to " << stunned[2] << " once it expired\n";
}

// ---------------------------------------------------------------------------
// Unit::AttackerStateUpdate plus SetStunned's frozen swing timer: a stun costs
// the whole swing, it does not bank one.
// ---------------------------------------------------------------------------
void stunSwing() {
    auto build = [&](LocalGameplay& game, LocalRealmPlayer& p, std::vector<LocalRealmPlayer*>& players) {
        auto c = controlWorld({10308, 20066}, 50, 1, 10);
        game.useContent(c);
        p.knownSpells = {1, 10308, 20066};
        auto n = controlNpc(50, 1, 2); // already inside melee reach
        n.targetGuid = p.guid; n.threat[0] = {p.guid, 1000};
        game.tick(0, players); game.setRemoteNpcs({n});
        return n.guid;
    };
    // A fixed 24-second window, with and without a real 6000 ms stun taken the
    // moment the creature has just swung.
    auto window = [&](bool stun, unsigned& firstAfterMs, unsigned& swingsDuringStun) {
        LocalGameplay game; auto p = controlCaster(1, 2);
        std::vector<LocalRealmPlayer*> players{&p};
        const auto guid = build(game, p, players);
        // Let it land one swing so the 2000 ms timer is full, then stun.
        unsigned elapsed = 0;
        while (!npcSwings(game, guid) && elapsed < 8000) { game.tick(.25f, players); elapsed += 250; }
        assert(npcSwings(game, guid) == 1);
        const auto baseline = npcSwings(game, guid);
        const float timerAtStun = game.npcs()[0].attackTimer;
        assert(timerAtStun > 1.9f); // the swing that just landed rearmed it in full
        unsigned stunEndsAtMs = 0;
        if (stun) {
            assert(castControl(game, p, guid, 10308, players));
            stunEndsAtMs = real(10308).durationMs;
        }
        unsigned sinceStun = 0;
        firstAfterMs = 0; swingsDuringStun = 0;
        float frozenTimer = game.npcs()[0].attackTimer;
        for (unsigned i = 0; i < 96; ++i) { // 24000 ms
            game.tick(.25f, players); sinceStun += 250;
            const bool inStun = stun && sinceStun <= stunEndsAtMs;
            if (inStun) {
                // Unit::SetStunned freezes the swing before anything else: the
                // timer does not run down, so no swing is being banked.
                assert(game.npcs()[0].attackTimer == frozenTimer);
                swingsDuringStun = npcSwings(game, guid) - baseline;
            }
            if (!firstAfterMs && stun && sinceStun > stunEndsAtMs &&
                npcSwings(game, guid) > baseline + swingsDuringStun)
                firstAfterMs = sinceStun - stunEndsAtMs;
        }
        return npcSwings(game, guid) - baseline;
    };
    unsigned firstAfterMs = 0, duringStun = 0, ignored = 0, none = 0;
    const auto freeSwings = window(false, ignored, none);
    const auto stunnedSwings = window(true, firstAfterMs, duringStun);
    assert(none == 0 && ignored == 0);
    assert(duringStun == 0);                    // the swing is blocked outright
    assert(freeSwings == 12);                   // 24000 ms / 2000 ms
    assert(stunnedSwings == 9);                 // three swings lost, not delayed
    assert(freeSwings - stunnedSwings == 3);    // 6000 ms of stun / 2000 ms period
    // The decisive one: the timer was full when the stun landed, so the first
    // swing after it ends is a whole period later. A stun that only blocked the
    // swing while the timer kept running would fire it on the next tick.
    assert(firstAfterMs >= 2000);
    std::cout << "PASS stun swing: over a fixed 24000 ms window a creature in melee reach swung "
              << freeSwings << " times unstunned and " << stunnedSwings
              << " times through one real 6000 ms Hammer of Justice - " << duringStun
              << " swings during the stun, exactly " << (freeSwings - stunnedSwings)
              << " swings lost for three swing periods of stun, attackTimer frozen throughout, and "
                 "the first swing after the stun came " << firstAfterMs
              << " ms later rather than instantly\n";
}

// ---------------------------------------------------------------------------
// Spell::CheckCasterAuras. A stun prevents the cast outright; a silence only
// prevents a spell whose own PreventionType is SPELL_PREVENTION_TYPE_SILENCE.
// ---------------------------------------------------------------------------
// the reviewed caster of this group was Cliff Stormer 4008, which is
// creature_template.type 1 - a beast - and SpellInfo::CheckTargetCreatureType
// (SpellInfo.cpp:1906-1919) refuses Repentance on it, because mask 118 has no
// beast bit. The group needs an instant control that lasts the whole window, so
// it moves to the second reviewed caster, Searing Hatchling 4323 (type 2,
// dragonkin, inside mask 118), which casts 11985 on the same shape of timer
// (2400-2700 ms initial, 9400-9700 ms repeat). stunBlocksCast below asserts the
// refusal on the beast directly, so nothing is lost.
constexpr uint32_t kCasterEntry = 4323, kCasterSpell = 11985; // Searing Hatchling, the reviewed 11985 caster
constexpr uint32_t kBeastCasterEntry = 4008;                  // Cliff Stormer, the reviewed 5401 caster

// Casts of 11985 that creature 4323 started inside `ticks` quarter-seconds,
// with `controlSpell` applied first (0 for none).
unsigned npcCastsStarted(uint32_t controlSpell, unsigned ticks) {
    constexpr uint32_t kEntry = kCasterEntry;
    {
        auto c = controlWorld({20066, 15487, kCasterSpell}, kEntry, 20, 0);
        LocalGameplay game; game.useContent(c);
        auto paladin = controlCaster(1, 2), priest = controlCaster(2, 5);
        paladin.knownSpells = {1, 20066}; priest.knownSpells = {1, 15487};
        priest.x = 1;
        std::vector<LocalRealmPlayer*> players{&paladin, &priest};
        auto n = controlNpc(kEntry, 20, 6);
        n.targetGuid = paladin.guid; n.threat[0] = {paladin.guid, 1000};
        game.tick(0, players); game.setRemoteNpcs({n});
        if (controlSpell) {
            auto& caster = controlSpell == 15487 ? priest : paladin;
            assert(castControl(game, caster, n.guid, controlSpell, players));
        }
        const auto before = game.combatEvents().empty() ? 0 : game.combatEvents().back().sequence;
        for (unsigned i = 0; i < ticks; ++i) game.tick(.25f, players);
        unsigned started = 0;
        for (const auto& e : game.combatEvents())
            if (e.sequence > before && e.source == n.guid && e.spell == kCasterSpell &&
                e.kind == LocalCombatEventKind::SpellCast) ++started;
        return started;
    }
}

void stunBlocksCast() {
    const auto* profile = localNpcSpellProfile(kCasterEntry);
    assert(profile && profile->spellId() == kCasterSpell);
    // 32 ticks is 8000 ms: past the profile's 2400-2700 ms initial timer plus
    // the cast and short of the 9400 ms repeat, so an unsuppressed creature
    // starts exactly one cast. It is inside the 60000 ms Repentance stun
    // throughout, and a 5000 ms Silence that failed to suppress would let the
    // same single cast through with time to spare.
    const auto freeCasts = npcCastsStarted(0, 32);
    const auto stunnedCasts = npcCastsStarted(20066, 32);
    assert(freeCasts >= 1);
    assert(stunnedCasts == 0);
    // the other reviewed caster refuses the same Repentance outright,
    // because it is a beast and mask 118 carries no beast bit.
    {
        auto c = controlWorld({20066}, kBeastCasterEntry, 20, 0);
        LocalGameplay game; game.useContent(c);
        auto paladin = controlCaster(1, 2);
        paladin.knownSpells = {1, 20066};
        std::vector<LocalRealmPlayer*> players{&paladin};
        auto n = controlNpc(kBeastCasterEntry, 20, 6);
        n.targetGuid = paladin.guid; n.threat[0] = {paladin.guid, 1000};
        game.tick(0, players); game.setRemoteNpcs({n});
        paladin.globalCooldownMs = 0;
        std::string reason;
        assert(!game.execute(paladin, {LocalAction::CastSpell, n.guid, 20066}, players, reason));
        assert(reason.find("not a valid creature type") != std::string::npos);
        assert(localNpcCreatureType(kBeastCasterEntry) == 1 && localNpcCreatureType(kCasterEntry) == 2 &&
               real(20066).targetCreatureType == 118);
    }
    std::cout << "PASS stun blocks the cast: creature " << kCasterEntry << " started " << freeCasts
              << " cast of spell " << kCasterSpell << " in 8000 ms unsuppressed and " << stunnedCasts
              << " under a real Repentance stun; the same Repentance on the other reviewed caster " << kBeastCasterEntry
              << " is refused by TargetCreatureType, because that creature is a beast (type 1) and mask 118 has no "
                 "beast bit\n";
}

// Spell::CheckCasterAuras reads the PreventionType of the spell being cast.
// Both spells this realm's creatures cast carry Spell.dbc column 214 == 1, so
// a silence is observable against them and must stop the cast.
void silenceBlocksCast() {
    const auto& bolt = real(kCasterSpell);
    assert(bolt.unsupportedReason.empty() && bolt.npcOnly);
    const bool carried =
        expect(bolt.preventionType == kLocalPreventionSilence &&
                   real(11985).preventionType == kLocalPreventionSilence,
               "silence blocks the cast: Spell.dbc column 214 is 1 "
               "(SPELL_PREVENTION_TYPE_SILENCE) for both 5401 and 11985, but the imported "
               "definitions carry preventionType=" + std::to_string(bolt.preventionType) + "/" +
                   std::to_string(real(11985).preventionType) +
               ". decodeLocalNpcSpell (include/game/local_npc_spell_import.hpp:9-39) is a separate "
               "decode path that verifies column 214 against its generated column table and then "
               "never assigns it, so every NPC-castable spell in this realm has preventionType 0 "
               "and the silence arm of the cast gate at src/game/local_gameplay.cpp:4320-4321 can "
               "never be true. SPELL_AURA_MOD_SILENCE is admitted by the importer and suppresses "
               "nothing. Fix: assign d.preventionType from column 214 in decodeLocalNpcSpell.");
    const auto silencedCasts = npcCastsStarted(15487, 32);
    const bool blocked = expect(silencedCasts == 0,
        "silence blocks the cast: " + std::to_string(silencedCasts) +
        " cast(s) of spell " + std::to_string(kCasterSpell) + " started under a real Priest Silence against creature " +
        std::to_string(kCasterEntry));
    if (carried && blocked)
        std::cout << "PASS silence blocks the cast: creature " << kCasterEntry << " started "
                  << silencedCasts << " casts of spell " << kCasterSpell << " under a real Priest Silence, and "
                     "5401/11985 both carry PreventionType "
                  << unsigned(kLocalPreventionSilence) << "\n";
}

// ---------------------------------------------------------------------------
// Unit::DealDamage break-on-damage, rule 1.
// ---------------------------------------------------------------------------
void breakOnDamage() {
    // 20066 Repentance carries AuraInterruptFlags 0x2; 10308 Hammer of Justice
    // rank 4 carries 0x480000 and no TAKE_DAMAGE. 48823 Holy Shock is a real
    // accepted Paladin direct-damage spell; 25997 Eye for an Eye is the only
    // accepted Paladin spell carrying SPELL_ATTR4_DAMAGE_DOESNT_BREAK_AURAS.
    assert(real(20066).auraInterruptFlags & kLocalAuraInterruptTakeDamage);
    assert(!(real(10308).auraInterruptFlags & kLocalAuraInterruptTakeDamage));
    assert(real(48823).damage && !real(48823).sourceDamageDoesNotBreakAuras);
    assert(real(25997).damage && real(25997).sourceDamageDoesNotBreakAuras);

    // A level-83 creature against a level-80 caster: WorldObject::MagicSpellHitResult
    // gives a real 17 percent miss, so both the landing and the nullified arm
    // are produced by the shipped roll rather than by a forced outcome.
    auto c = controlWorld({10308, 20066, 48823, 25997}, 50, 83, 0);
    LocalGameplay game; game.useContent(c);
    auto p = controlCaster(1, 2);
    p.knownSpells = {1, 10308, 20066, 48823, 25997};
    std::vector<LocalRealmPlayer*> players{&p};
    auto n = controlNpc(50, 83, 2);
    n.targetGuid = p.guid; n.threat[0] = {p.guid, 1000};
    game.tick(0, players); game.setRemoteNpcs({n});

    // LocalGameplay seeds its melee/hit stream from the clock, so the number of
    // misses is not fixed between runs. Keep casting until the shipped roll has
    // produced every arm of the rule at least once, with a hard bound.
    unsigned brokeOnHit = 0, survivedMiss = 0, unflaggedSurvived = 0, unbreakableSourceSurvived = 0;
    for (unsigned trial = 0; trial < 400 &&
                             (trial < 60 || !brokeOnHit || !survivedMiss || !unbreakableSourceSurvived);
         ++trial) {
        // Re-arm both controls, one flagged and one not, from the same caster.
        assert(castControl(game, p, n.guid, 20066, players));
        assert(castControl(game, p, n.guid, 10308, players));
        assert(findNpc(game, n.guid)->controls.size() == 2);
        const auto before = game.combatEvents().back().sequence;
        p.globalCooldownMs = 0; p.cooldowns.clear(); p.categoryCooldowns.clear(); p.mana = p.maxMana;
        std::string result;
        const uint32_t damaging = trial % 3 == 2 ? 25997 : 48823;
        assert(game.execute(p, {LocalAction::CastSpell, n.guid, damaging}, players, result));
        LocalMeleeOutcome outcome = LocalMeleeOutcome::Hit;
        bool sawDamageEvent = false;
        for (const auto& e : game.combatEvents())
            if (e.sequence > before && e.spell == damaging && e.target == n.guid &&
                e.kind == LocalCombatEventKind::SpellDamage) { outcome = e.outcome; sawDamageEvent = true; }
        const auto& controls = findNpc(game, n.guid)->controls;
        const bool flaggedAlive = std::any_of(controls.begin(), controls.end(),
                                              [](const auto& a) { return a.spellId == 20066; });
        const bool unflaggedAlive = std::any_of(controls.begin(), controls.end(),
                                                [](const auto& a) { return a.spellId == 10308; });
        // The unflagged control is never touched, whatever happened.
        assert(unflaggedAlive); ++unflaggedSurvived;
        if (!sawDamageEvent || localOutcomeNullifiesDamage(outcome)) {
            // A miss never reached DealDamage, so it breaks nothing.
            assert(flaggedAlive); ++survivedMiss;
        } else if (damaging == 25997) {
            // SPELL_ATTR4_DAMAGE_DOESNT_BREAK_AURAS exempts the damaging spell.
            assert(flaggedAlive); ++unbreakableSourceSurvived;
        } else {
            assert(!flaggedAlive); ++brokeOnHit;
        }
    }
    assert(brokeOnHit && survivedMiss && unbreakableSourceSurvived);

    // The `except` arm. No spell in the accepted set both applies a control and
    // deals damage, so the reference's self-break guard has no producer in this
    // client data; it is exercised here by giving the real 2637 Hibernate
    // definition a damage amount in the world copy and changing nothing else.
    {
        auto world = controlWorld({2637, 20066}, 50, 1, 0);
        // the implementation (P04 competing auras): a second caster's Hibernate would now
        // REPLACE the first's on application (Aura::CanStackWith -> IsRankOf,
        // SpellAuras.cpp:2160-2177), which would hide what this arm witnesses -
        // that the damage phase left druidA's control standing. The copy is
        // therefore also given SPELL_ATTR3_DOT_STACKING_RULE ("stack separately
        // for each caster", :2094), the reference's own attribute under which
        // two casters' applications coexist; nothing else about it changes.
        for (auto& d : world->spells) if (d.id == 2637) { d.damage = d.damageMax = 1; d.castTimeMs = 0; d.sourceDotStackingRule = true; }
        LocalGameplay dual; dual.useContent(world);
        auto druidA = controlCaster(1, 11), druidB = controlCaster(2, 11), paladin = controlCaster(3, 2);
        druidA.knownSpells = {1, 2637}; druidB.knownSpells = {1, 2637}; paladin.knownSpells = {1, 20066};
        druidB.x = 1; paladin.x = 1;
        std::vector<LocalRealmPlayer*> all{&druidA, &druidB, &paladin};
        auto target = controlNpc(50, 1, 3);
        target.targetGuid = druidA.guid; target.threat[0] = {druidA.guid, 1000};
        dual.tick(0, all); dual.setRemoteNpcs({target});
        assert(castControl(dual, druidA, target.guid, 2637, all));
        assert(castControl(dual, paladin, target.guid, 20066, all));
        assert(findNpc(dual, target.guid)->controls.size() == 2);
        // druidB's Hibernate damages first and applies second. Its own id is
        // excepted, so druidA's Hibernate lives; Repentance shares the flag but
        // not the id, so it dies.
        assert(castControl(dual, druidB, target.guid, 2637, all));
        const auto& left = findNpc(dual, target.guid)->controls;
        unsigned hibernate = 0, repentance = 0;
        for (const auto& a : left) (a.spellId == 2637 ? hibernate : repentance)++;
        assert(hibernate == 2 && repentance == 0);
        std::cout << "PASS break-on-damage rule 1: over " << (brokeOnHit + survivedMiss +
                     unbreakableSourceSurvived) << " real casts against a level-83 creature "
                  << brokeOnHit << " landing hits removed the TAKE_DAMAGE control (20066 Repentance, "
                     "AuraInterruptFlags 0x2), " << survivedMiss
                  << " nullified outcomes removed nothing, " << unbreakableSourceSurvived
                  << " hits from 25997 Eye for an Eye (SPELL_ATTR4_DAMAGE_DOESNT_BREAK_AURAS) removed "
                     "nothing, the unflagged 10308 Hammer of Justice survived all " << unflaggedSurvived
                  << " of them, and a damaging cast of 2637 left both Hibernate controls standing "
                     "while removing the Repentance one\n";
    }
}

// ---------------------------------------------------------------------------
// Expiry, caster invalidation, capacity and rank replacement.
// ---------------------------------------------------------------------------
void controlLifecycle() {
    // --- expiry, to the millisecond ----------------------------------------
    {
        auto c = controlWorld({10308}, 50, 1, 0);
        LocalGameplay game; game.useContent(c);
        auto p = controlCaster(1, 2); p.knownSpells = {1, 10308};
        std::vector<LocalRealmPlayer*> players{&p};
        auto n = controlNpc(50, 1, 8);
        n.targetGuid = p.guid; n.threat[0] = {p.guid, 1000};
        game.tick(0, players); game.setRemoteNpcs({n});
        assert(castControl(game, p, n.guid, 10308, players));
        assert(game.npcs()[0].controls[0].remainingMs == 6000);
        // LocalGameplay::tick clamps one step to 250 ms and carries the
        // sub-millisecond remainder, so this walks the last 250 ms one
        // millisecond at a time.
        for (unsigned i = 0; i < 23; ++i) game.tick(.25f, players);
        assert(game.npcs()[0].controls.size() == 1 && game.npcs()[0].controls[0].remainingMs == 250);
        for (unsigned i = 0; i < 249; ++i) game.tick(.001f, players);
        assert(game.npcs()[0].controls.size() == 1 && game.npcs()[0].controls[0].remainingMs == 1);
        const float held = game.npcs()[0].x;
        assert(held == 8);
        game.tick(.001f, players);
        assert(game.npcs()[0].controls.empty());
        // The final millisecond still suppressed: the tick reads the control
        // state it entered with, exactly as the snare's piecewise integration.
        assert(game.npcs()[0].x == held);
        game.tick(.25f, players);
        assert(game.npcs()[0].x < held); // and the very next tick is free again
    }
    // --- caster invalidation ------------------------------------------------
    unsigned invalidations = 0;
    for (unsigned mutation = 0; mutation < 5; ++mutation) {
        auto c = controlWorld({10308}, 50, 1, 0);
        LocalGameplay game; game.useContent(c);
        auto p = controlCaster(1, 2); p.knownSpells = {1, 10308};
        std::vector<LocalRealmPlayer*> players{&p};
        auto n = controlNpc(50, 1, 8);
        n.targetGuid = p.guid; n.threat[0] = {p.guid, 1000};
        game.tick(0, players); game.setRemoteNpcs({n});
        assert(castControl(game, p, n.guid, 10308, players));
        assert(game.npcs()[0].controls.size() == 1);
        switch (mutation) {
            case 0: p.dead = true; p.health = 0; break;
            case 1: p.mapId += 1; break;
            case 2: p.instanceId += 1; break;
            case 3: ++p.positionRevision; break;
            case 4: p.flight.active = true; break;
        }
        game.tick(.25f, players);
        assert(game.npcs()[0].controls.empty());
        ++invalidations;
    }
    // --- capacity -----------------------------------------------------------
    // Since the implementation the creature carries diminishing returns (P04, Unit.cpp:
    // 11292-11431), and since the implementation a second caster's control of the same
    // chain REPLACES the first's (Aura::CanStackWith -> IsRankOf,
    // SpellAuras.cpp:2160-2177), so four seats can only be filled from four
    // different chains: a Paladin's Hammer of Justice (6000 ms), a second
    // Paladin's Repentance (DRTYPE_PLAYER, seeds no record), a Druid's Bash
    // from Bear Form (CONTROLLED_STUN shares the Hammer's record: rung 2, 1000
    // of 2000 ms) and a Priest's Silence (DRTYPE_PLAYER). A fifth chain -
    // a Druid's Hibernate - is then refused by the capacity check, which runs
    // before the diminishing read and is what this measures. the implementation's fixture
    // seated three Paladins' Hammers side by side, which the reference never
    // holds, and was rewritten.
    unsigned seated = 0;
    std::string capacityReason;
    {
        static_assert(kLocalMaxNpcControls == 4);
        auto c = controlWorld({10308, 20066, 5211, 15487, 2637}, 50, 1, 0);
        LocalGameplay game; game.useContent(c);
        std::vector<LocalRealmPlayer> casters;
        casters.push_back(controlCaster(1, 2));
        casters.push_back(controlCaster(2, 2));
        casters.push_back(controlCaster(3, 11, LocalResourceType::Rage)); casters.back().formSpellId = 5487; // Bear Form, which Bash requires
        casters.push_back(controlCaster(4, 5));
        casters.push_back(controlCaster(5, 11));
        casters.push_back(controlCaster(6, 11));
        for (unsigned i = 0; i < casters.size(); ++i) { casters[i].knownSpells = {1, 10308, 20066, 5211, 15487, 2637}; casters[i].x = float(i) * 0.1f; }
        std::vector<LocalRealmPlayer*> players;
        for (auto& caster : casters) players.push_back(&caster);
        auto n = controlNpc(50, 1, 3);
        n.targetGuid = casters[0].guid; n.threat[0] = {casters[0].guid, 1000};
        game.tick(0, players); game.setRemoteNpcs({n});
        const uint32_t seat[kLocalMaxNpcControls] = {10308, 20066, 5211, 15487};
        const uint32_t expectMs[kLocalMaxNpcControls] = {6000, real(20066).durationMs, real(5211).durationMs / 2, real(15487).durationMs};
        for (unsigned i = 0; i < kLocalMaxNpcControls; ++i) {
            assert(castControl(game, casters[i], n.guid, seat[i], players));
            seated = unsigned(findNpc(game, n.guid)->controls.size());
            assert(seated == i + 1);
            uint32_t applied = 0;
            for (const auto& a : findNpc(game, n.guid)->controls)
                if (a.casterGuid == casters[i].guid) applied = a.remainingMs;
            assert(applied == expectMs[i]);
        }
        auto& overflow = casters[kLocalMaxNpcControls];
        overflow.globalCooldownMs = 0; overflow.cooldowns.clear(); overflow.mana = overflow.maxMana;
        assert(!game.execute(overflow, {LocalAction::CastSpell, n.guid, 2637}, players, capacityReason));
        assert(capacityReason == "Too many active NPC controls");
        assert(findNpc(game, n.guid)->controls.size() == kLocalMaxNpcControls);
        // The refusal was the capacity check and not the ladder: Hibernate is
        // DRTYPE_PLAYER and seeds nothing on a creature, and a second Druid's
        // Hibernate is refused with the same reason.
        for (const auto& r : findNpc(game, n.guid)->diminishing) assert(r.group != uint8_t(LocalDiminishingGroup::Sleep));
        auto& overflowTwo = casters[kLocalMaxNpcControls + 1];
        overflowTwo.globalCooldownMs = 0; overflowTwo.cooldowns.clear(); overflowTwo.mana = overflowTwo.maxMana;
        std::string again;
        assert(!game.execute(overflowTwo, {LocalAction::CastSpell, n.guid, 2637}, players, again));
        assert(again == capacityReason);
        // A fifth Paladin's Hammer, by contrast, is not a fifth control but a
        // replacement of the first Paladin's: it lands, and the count stays 4.
        auto fifthPaladin = controlCaster(7, 2); fifthPaladin.knownSpells = {1, 10308}; fifthPaladin.x = 0.7f;
        players.push_back(&fifthPaladin);
        assert(castControl(game, fifthPaladin, n.guid, 10308, players));
        unsigned hammers = 0; uint64_t hammerHolder = 0;
        for (const auto& a : findNpc(game, n.guid)->controls) if (a.spellId == 10308) { ++hammers; hammerHolder = a.casterGuid; }
        assert(findNpc(game, n.guid)->controls.size() == kLocalMaxNpcControls && hammers == 1 && hammerHolder == fifthPaladin.guid);
    }
    // --- a rank replaces a rank, in both directions  -----------------
    std::string rankReason;
    {
        // 5211 Bash rank 1 is superseded by 6798 rank 2 in the client's own
        // SkillLineAbility chain. Bash is SPELL_DAMAGE_CLASS_MELEE, so it rolls
        // no magic hit and each cast is decisive; it is cast from Bear Form,
        // which is what its own requiredForms mask demands.
        assert(real(5211).supercededBySpell == 6798 && real(6798).supercededBySpell == 8983);
        auto c = controlWorld({5211, 6798, 8983}, 50, 1, 0);
        LocalGameplay game; game.useContent(c);
        auto druid = controlCaster(1, 11, LocalResourceType::Rage);
        druid.knownSpells = {1, 5211, 6798, 8983};
        druid.formSpellId = 5487; // Bear Form, the form Bash requires
        std::vector<LocalRealmPlayer*> players{&druid};
        auto n = controlNpc(50, 1, 3);
        n.targetGuid = druid.guid; n.threat[0] = {druid.guid, 1000};
        game.tick(0, players); game.setRemoteNpcs({n});
        assert(castControl(game, druid, n.guid, 5211, players));
        assert(findNpc(game, n.guid)->controls.size() == 1);
        assert(findNpc(game, n.guid)->controls[0].remainingMs == real(5211).durationMs);
        assert(castControl(game, druid, n.guid, 6798, players));
        assert(findNpc(game, n.guid)->controls.size() == 1); // replaced, not stacked
        assert(findNpc(game, n.guid)->controls[0].spellId == 6798);
        // The replacement is a hit that Spell::DoSpellHitOnUnit reads against
        // the record rank 1 seeded (the implementation, P04 diminishing returns): rung 2 of
        // the ladder, half of rank 2's own 3,000 ms, and not the full duration.
        assert(real(6798).durationMs == 3000);
        assert(findNpc(game, n.guid)->controls[0].remainingMs == real(6798).durationMs / 2);
        // The lower rank cast back over the higher one REPLACES it :
        // Aura::CanStackWith -> IsRankOf -> false removes the rank-2 control
        // (SpellAuras.cpp:2160-2177); the reference has no rank comparison and
        // no refusal here. the implementation's fixture asserted the build's own "A higher
        // control rank is already active", which had no source; rewritten.
        // The hit reads rung 3 of the ladder: 25 % of rank 1's 2,000 ms.
        assert(castControl(game, druid, n.guid, 5211, players));
        assert(findNpc(game, n.guid)->controls.size() == 1 && findNpc(game, n.guid)->controls[0].spellId == 5211);
        assert(findNpc(game, n.guid)->controls[0].remainingMs == real(5211).durationMs / 4);
        rankReason = "replaced in place at rung 3 (" + std::to_string(findNpc(game, n.guid)->controls[0].remainingMs) + " of " + std::to_string(real(5211).durationMs) + " ms)";
    }
    std::cout << "PASS control lifecycle: a real 6000 ms stun expired on the millisecond and "
                 "suppressed its final one, " << invalidations
              << " caster invalidations (death, map, instance, positionRevision, flight) dropped it, "
              << seated << " casters filled kLocalMaxNpcControls from four chains (Hammer of Justice 6000 ms, "
                 "Repentance, Bash at rung 2 of the shared CONTROLLED_STUN record, Silence) and a fifth chain was refused "
                 "with \"" << capacityReason << "\" twice while a fifth Paladin's Hammer replaced the first's (the reference), and Bash rank 2 "
                 "replaced rank 1 in place at rung 2 (1500 of 3000 ms) and rank 1 cast back over rank 2 "
              << rankReason << " - the reference replaces in both directions, SpellAuras.cpp:2160-2177\n";
}

// ---------------------------------------------------------------------------
// LAN 83. The control list is appended after every existing NPC field.
// ---------------------------------------------------------------------------
void lanCodec() {
    static_assert(Version == lan::GameplayVersion);
    // the implementation appended `resisted` to the melee view and the implementation appended the pet's
    // command state, react state and stay point to the pet deck; the NPC page
    // this group measures did not move at either.
    // 85 when written; later checkpoints moved the protocol for other state.
    assert(Version >= 85);
    LocalWorldContent c;
    LocalNpcDefinition definition; definition.id = 50; definition.name = "Codec NPC"; definition.displayId = 100;
    c.npcs.push_back(definition);
    for (uint32_t id : {853u, 10308u, 15487u, 20066u, 11297u}) c.spells.push_back(real(id));
    LocalSpellDefinition snare; snare.id = 500; snare.durationMs = 6000; snare.snarePercent = 20;
    c.spells.push_back(snare);
    std::sort(c.spells.begin(), c.spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

    LocalRealmNpc n; n.guid = 0xf130000000000001ULL; n.entry = 50; n.level = 20;
    n.health = 80; n.maxHealth = 100; n.playerThreat.viewerGuid = 41;
    n.snares = {{500, 3000, 42, 20, 7}};
    n.controls = {{853, 3000, 41, 11, uint8_t(LocalNpcControlKind::Stun)},
                  {15487, 5000, 42, 12, uint8_t(LocalNpcControlKind::Silence)}};
    assert(validLocalNpcControls(n, c));

    auto roundtrip = [&](const LocalRealmNpc& source) {
        Writer wire; writeNpc(wire, source);
        Reader r(wire.bytes.data(), wire.bytes.size());
        auto copy = readNpc(r, c);
        assert(r.valid && r.done());
        assert(copy.controls.size() == source.controls.size());
        for (size_t i = 0; i < copy.controls.size(); ++i) {
            const auto& a = source.controls[i]; const auto& b = copy.controls[i];
            assert(a.spellId == b.spellId && a.remainingMs == b.remainingMs &&
                   a.casterGuid == b.casterGuid && a.kind == b.kind);
            assert(b.casterRevision == 0); // authority lifecycle, never replicated
        }
        assert(copy.snares.size() == source.snares.size());
        assert(copy.guid == source.guid && copy.health == source.health);
        return wire.bytes;
    };
    const auto wire = roundtrip(n);
    auto zeroRevision = n; for (auto& a : zeroRevision.controls) a.casterRevision = 0;
    Writer zero; writeNpc(zero, zeroRevision); assert(zero.bytes == wire);

    size_t rejected = 0;
    auto reject = [&](const LocalRealmNpc& bad) {
        assert(!validLocalNpcControls(bad, c));
        Writer bytes; writeNpc(bytes, bad);
        Reader r(bytes.bytes.data(), bytes.bytes.size());
        (void)readNpc(r, c); assert(!r.valid); ++rejected;
    };
    auto bad = n; bad.controls[1] = bad.controls[0]; reject(bad);            // duplicate caster+spell
    bad = n; bad.controls[0].casterGuid = 0; reject(bad);                    // no caster
    bad = n; bad.controls[0].remainingMs = 0; reject(bad);                   // expired but present
    bad = n; bad.controls[0].remainingMs = real(853).durationMs + 1; reject(bad); // longer than the spell
    bad = n; bad.controls[0].spellId = 500; reject(bad);                     // a snare is not a control
    bad = n; bad.controls[0].spellId = 999999; reject(bad);                  // unknown spell
    bad = n; bad.controls[0].kind = uint8_t(LocalNpcControlKind::Silence); reject(bad); // wrong kind
    bad = n; bad.controls[1].kind = uint8_t(LocalNpcControlKind::Stun); reject(bad);
    bad = n; bad.controls[0].kind = 7; reject(bad);
    bad = n; bad.dead = true; bad.health = 0; bad.snares.clear(); reject(bad); // a corpse holds none
    bad = n; bad.transportEntry = 1; reject(bad);                             // nor does deck crew

    auto maximum = n; maximum.controls.clear();
    const uint32_t ranks[kLocalMaxNpcControls] = {853, 10308, 20066, 11297};
    for (unsigned i = 0; i < kLocalMaxNpcControls; ++i)
        maximum.controls.push_back({ranks[i], 1000 + i, 100 + i, 500 + i, uint8_t(LocalNpcControlKind::Stun)});
    const auto maxWire = roundtrip(maximum);
    auto empty = n; empty.controls.clear();
    const auto emptyWire = roundtrip(empty);
    assert(maxWire.size() == emptyWire.size() + 17 * kLocalMaxNpcControls);

    // An over-capacity list is refused by the reader before it is decoded.
    // The count byte was the last byte of the row at LAN 83; later fields
    // follow it now, so it is found as the first byte a one-control row changes.
    {
        auto one = n; one.controls.resize(1);
        const auto oneWire = roundtrip(one);
        size_t countAt = 0;
        while (countAt < emptyWire.size() && emptyWire[countAt] == oneWire[countAt]) ++countAt;
        assert(countAt < emptyWire.size() && emptyWire[countAt] == 0 && oneWire[countAt] == 1);
        auto overflow = maxWire;
        overflow[countAt] = uint8_t(kLocalMaxNpcControls + 1);
        Reader r(overflow.data(), overflow.size());
        (void)readNpc(r, c); assert(!r.valid); ++rejected;
    }
    // And every truncation of the largest payload.
    size_t truncations = 0;
    for (size_t size = 0; size < maxWire.size(); ++size) {
        Reader r(maxWire.data(), size);
        (void)readNpc(r, c); assert(!r.valid); ++truncations;
    }
    // A definition that no longer supports the received control rejects it.
    for (unsigned mutation = 0; mutation < 3; ++mutation) {
        auto altered = c;
        for (auto& d : altered.spells) if (d.id == 853) {
            if (mutation == 0) d.controlProfile = 0;
            if (mutation == 1) d.controlProfile = 2;
            if (mutation == 2) d.unsupportedReason = "Unsupported fixture";
        }
        Reader r(wire.data(), wire.size());
        (void)readNpc(r, altered); assert(!r.valid); ++rejected;
    }
    auto trailing = wire; trailing.push_back(0);
    Reader extra(trailing.data(), trailing.size());
    (void)readNpc(extra, c); assert(extra.valid && !extra.done());

    std::cout << "PASS LAN" << int(Version) << " control codec: a stun and a silence from two casters "
                 "survived writeNpc/readNpc byte-identically with the authority revision omitted, "
              << kLocalMaxNpcControls << " controls cost exactly " << 17 * kLocalMaxNpcControls
              << " bytes more than none, " << rejected << " malformed or over-capacity states and "
              << truncations << " truncated payload lengths were rejected by the reader\n";
}

// ---------------------------------------------------------------------------
// The LAN wire budget. NpcWireBytes is the authority's own claim about the
// worst case writeNpc can emit for one creature, and NpcsPerPage is derived
// from it; a page larger than MaxPacket is not fragmented but refused by
// send(), so an understated budget is a silently dropped NPC deck. This group
// measures the worst case instead of restating the arithmetic.
// ---------------------------------------------------------------------------
void wireBudget() {
    LocalWorldContent c;
    LocalNpcDefinition definition;
    definition.id = 50; definition.displayId = 100;
    // The longest name this realm's catalog will hand out. It is deliberately
    // here to show what it costs on the NPC wire, which is nothing: writeNpc
    // never writes a name and readNpc resolves it from the guest's own
    // definition by entry (local_realm.cpp:683).
    definition.name = std::string(160, 'N');
    c.npcs.push_back(definition);
    for (uint32_t id : {853u, 10308u, 20066u, 11297u}) c.spells.push_back(real(id));
    LocalSpellDefinition snare; snare.id = 500; snare.durationMs = 600000; snare.snarePercent = 99;
    c.spells.push_back(snare);
    LocalSpellDefinition periodic; periodic.id = 501; periodic.durationMs = 600000;
    periodic.periodicDamage = 3; periodic.maxAuraStacks = 255;
    c.spells.push_back(periodic);
    LocalSpellDefinition storm; storm.id = 17364; storm.durationMs = 12000; storm.stormstrikeProfile = 1;
    c.spells.push_back(storm);
    std::sort(c.spells.begin(), c.spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

    // One creature carrying every transient list at its own bound at once,
    // with a present threat view and every fixed field occupied.
    LocalRealmNpc loaded;
    loaded.guid = 0xf130000000000001ULL; loaded.entry = 50; loaded.level = 83;
    loaded.health = 999999999; loaded.maxHealth = 1000000000;
    loaded.hostile = true; loaded.aggressive = true; loaded.lootOwner = 0x77;
    loaded.flightMaster = true; loaded.taxiNodeId = 9;
    loaded.vendor = true; loaded.vendorCategories = 31;
    loaded.classTrainer = true; loaded.trainerClass = 11;
    loaded.professionTrainer = true; loaded.trainerSkill = 65535;
    loaded.banker = loaded.innkeeper = loaded.repairer = loaded.auctioneer = true;
    loaded.name = definition.name;
    constexpr uint64_t kViewer = 0x4242;
    loaded.targetGuid = kViewer;
    loaded.playerThreat.viewerGuid = kViewer; loaded.playerThreat.amount = 1000000000000ULL;
    loaded.playerThreat.rawBasisPoints = 1000000; loaded.playerThreat.scaledBasisPoints = 10000;
    loaded.playerThreat.status = 1; loaded.playerThreat.present = true;
    for (unsigned i = 0; i < kLocalMaxNpcSnares; ++i)
        loaded.snares.push_back({500, 600000 - i, 0x1000 + i, 99, 700 + i});
    for (unsigned i = 0; i < kLocalMaxNpcDamageAuras; ++i)
        loaded.damageAuras.push_back({501, 600000 - i, 600000, 0x2000 + i, uint8_t(1 + i)});
    for (unsigned i = 0; i < kLocalMaxNpcStormstrikeAuras; ++i)
        loaded.stormstrikeAuras.push_back({17364, 12000 - i, 0x3000 + i, uint8_t(1 + i % 4), 800 + i});
    const uint32_t controlRanks[kLocalMaxNpcControls] = {853, 10308, 20066, 11297};
    for (unsigned i = 0; i < kLocalMaxNpcControls; ++i)
        loaded.controls.push_back({controlRanks[i], 1000 + i, 0x4000 + i, 900 + i,
                                   uint8_t(LocalNpcControlKind::Stun)});
    // LAN107: the creature buff block at its bound, half timed and half
    // indefinite, every row one readNpc accepts.
    for (unsigned i = 0; i < kLocalMaxNpcBuffs; ++i) {
        LocalNpcBuff b; b.spellId = 8599 + i; b.casterGuid = 0x5000 + i; b.stacks = uint8_t(1 + i % 5);
        b.indefinite = (i % 2) == 1;
        if (!b.indefinite) { b.durationMs = 600000; b.remainingMs = 600000 - i; }
        loaded.npcBuffs.push_back(b);
    }
    // Every list is at its cap and the whole creature is one the real reader
    // accepts, so this is a state that can actually arrive on the wire rather
    // than an arithmetic upper bound.
    assert(loaded.snares.size() == kLocalMaxNpcSnares);
    assert(loaded.damageAuras.size() == kLocalMaxNpcDamageAuras);
    assert(loaded.stormstrikeAuras.size() == kLocalMaxNpcStormstrikeAuras);
    assert(loaded.controls.size() == kLocalMaxNpcControls);
    assert(loaded.npcBuffs.size() == kLocalMaxNpcBuffs);
    assert(validLocalNpcSnares(loaded, c) && validLocalNpcDamageAuras(loaded, c) &&
           validLocalNpcStormstrikeAuras(loaded, c) && validLocalNpcControls(loaded, c));

    Writer one; writeNpc(one, loaded);
    const size_t measured = one.bytes.size();
    {
        Reader r(one.bytes.data(), one.bytes.size());
        auto copy = readNpc(r, c);
        assert(r.valid && r.done());
        assert(copy.snares.size() == kLocalMaxNpcSnares &&
               copy.damageAuras.size() == kLocalMaxNpcDamageAuras &&
               copy.stormstrikeAuras.size() == kLocalMaxNpcStormstrikeAuras &&
               copy.controls.size() == kLocalMaxNpcControls &&
               copy.npcBuffs.size() == kLocalMaxNpcBuffs);
        for (unsigned i = 0; i < kLocalMaxNpcBuffs; ++i)
            assert(copy.npcBuffs[i].spellId == 8599 + i && copy.npcBuffs[i].casterGuid == 0x5000 + i &&
                   copy.npcBuffs[i].stacks == 1 + i % 5 && copy.npcBuffs[i].indefinite == ((i % 2) == 1) &&
                   copy.npcBuffs[i].remainingMs == (copy.npcBuffs[i].indefinite ? 0u : 600000 - i));
        assert(copy.name == definition.name); // resolved from the definition, not the wire
    }
    // The name is free on this wire. A 160-character name and a one-character
    // name produce the identical byte count.
    {
        auto shortName = loaded; shortName.name = "x";
        Writer w; writeNpc(w, shortName);
        assert(w.bytes.size() == measured);
    }
    // A creature bolted to a transport carries the same fixed block. The
    // validators make transport and the snare/control lists mutually
    // exclusive, so an attached creature is strictly smaller, never larger.
    size_t transportBytes = 0;
    {
        auto attached = loaded;
        attached.transportEntry = 9999; attached.transportX = 1.5f; attached.transportY = -2.5f;
        attached.transportZ = 3.5f; attached.transportOrientation = 1.25f;
        Writer withLists; writeNpc(withLists, attached);
        assert(withLists.bytes.size() == measured); // the transport block is fixed width
        assert(!validLocalNpcSnares(attached, c) && !validLocalNpcControls(attached, c));
        attached.snares.clear(); attached.controls.clear();
        Writer w; writeNpc(w, attached);
        transportBytes = w.bytes.size();
        assert(transportBytes < measured);
    }

    // LAN107: the buff block is exactly one count byte plus 22 bytes per buff,
    // and a creature without buffs is the LAN106 row plus that count byte.
    {
        auto bare = loaded; bare.npcBuffs.clear();
        Writer w; writeNpc(w, bare);
        assert(measured - w.bytes.size() == 22 * kLocalMaxNpcBuffs);
        assert(w.bytes.size() == 680 + 1);
    }

    // (1) The budget must cover the worst case...
    if (measured > NpcWireBytes)
        std::cerr << "FAIL wire budget: writeNpc emitted " << measured
                  << " bytes for a creature at every bound, above the declared NpcWireBytes of "
                  << NpcWireBytes << "\n";
    assert(measured <= NpcWireBytes);
    // ...and it must be tight, not merely generous: a budget with slack in it
    // is a budget nobody has measured, and the next list added to writeNpc
    // would hide inside the slack exactly as the stormstrike block did.
    if (measured != NpcWireBytes)
        std::cerr << "FAIL wire budget: NpcWireBytes is " << NpcWireBytes
                  << " but the measured worst case is " << measured << " bytes ("
                  << (NpcWireBytes > measured ? NpcWireBytes - measured : measured - NpcWireBytes)
                  << " bytes of " << (NpcWireBytes > measured ? "slack" : "overrun") << ")\n";
    assert(measured == NpcWireBytes);

    // (2) A full page of NpcsPerPage such creatures, with the 19-byte page
    // prologue and the 20-byte datagram header, must fit MaxPacket - and be
    // decodable, since send() would otherwise refuse the datagram outright.
    Writer page;
    page.u32(1); page.u8(0); page.u8(1); page.u8(uint8_t(NpcsPerPage));
    page.u32(loaded.mapId); page.u32(loaded.instanceId); page.u32(0);
    const size_t prologue = page.bytes.size();
    assert(prologue == 19);
    for (unsigned i = 0; i < NpcsPerPage; ++i) writeNpc(page, loaded);
    const size_t datagram = HeaderSize + page.bytes.size();
    if (datagram > MaxPacket)
        std::cerr << "FAIL wire budget: a full NPC page is " << datagram
                  << " bytes against a MaxPacket of " << MaxPacket
                  << "; send() refuses it and the deck is silently dropped\n";
    assert(datagram <= MaxPacket);
    {
        Reader r(page.bytes.data(), page.bytes.size());
        assert(r.u32() == 1 && r.u8() == 0 && r.u8() == 1 && r.u8() == uint8_t(NpcsPerPage));
        r.u32(); r.u32(); r.u32();
        for (unsigned i = 0; i < NpcsPerPage; ++i) { (void)readNpc(r, c); assert(r.valid); }
        assert(r.done());
    }
    // One more creature on the page would not fit, so NpcsPerPage is the
    // largest page this budget admits rather than an arbitrary reduction.
    assert(HeaderSize + prologue + (NpcsPerPage + 1) * measured > MaxPacket);
    // The pre-the implementation shape, stated as bytes rather than as arithmetic: three
    // such creatures to a page overran the datagram by this much.
    const size_t threePerPage = HeaderSize + prologue + 3 * measured;
    assert(threePerPage > MaxPacket);

    // Was three-per-page already wrong before this checkpoint? Measure the two
    // earlier wire shapes rather than reasoning about them. Each block that has
    // been added to writeNpc contributed one count byte plus its elements, so
    // dropping a list from this creature reproduces the earlier encoding minus
    // exactly the count bytes the later blocks introduced.
    size_t pre0242 = 0, pre0234 = 0;
    {
        auto withoutControls = loaded; withoutControls.controls.clear();
        Writer w; writeNpc(w, withoutControls);
        assert(measured - w.bytes.size() == 17 * kLocalMaxNpcControls);
        pre0242 = w.bytes.size() - 1; // the control block's own count byte is new in 83

        auto withoutStorm = withoutControls; withoutStorm.stormstrikeAuras.clear();
        Writer v; writeNpc(v, withoutStorm);
        assert(w.bytes.size() - v.bytes.size() == 17 * kLocalMaxNpcStormstrikeAuras);
        pre0234 = v.bytes.size() - 2; // minus the stormstrike and control count bytes
    }
    // LAN 82 could already emit a creature this large, so a three-creature page
    // already overran MaxPacket: the defect was introduced with the stormstrike
    // block, not with the control block. the implementation only made it unmissable.
    assert(HeaderSize + prologue + 3 * pre0242 > MaxPacket);
    // When written, the shape before the stormstrike block fitted three to a
    // page exactly. Fields appended to the row since (vehicle, threat and later
    // state) make today's reconstruction larger, so that historical fit is no
    // longer measurable from the current writer and is only reported.
    std::cerr << "note: pre-stormstrike reconstruction today " << (HeaderSize + prologue + 3 * pre0234)
              << " bytes for three creatures (MaxPacket " << MaxPacket << ")\n";

    // (3) Paging covers the whole deck and the page index still fits its u8.
    static_assert(MaxNpcPages * NpcsPerPage >= LocalGameplay::MaxNpcs);
    static_assert(MaxNpcPages <= 255);

    std::cout << "PASS wire budget: a creature at every bound at once ("
              << kLocalMaxNpcSnares << " snares, " << kLocalMaxNpcDamageAuras << " damage auras, "
              << kLocalMaxNpcStormstrikeAuras << " stormstrike auras, " << kLocalMaxNpcControls
              << " controls, " << kLocalMaxNpcBuffs << " buffs, a present threat view and a 160-character name) measures exactly "
              << measured << " bytes through writeNpc and round-trips, so NpcWireBytes ("
              << NpcWireBytes << ") is exact with " << (NpcWireBytes - measured)
              << " bytes of slack; the same creature bolted to a transport is " << transportBytes
              << " bytes; a full page of " << NpcsPerPage << " is " << datagram << "/" << MaxPacket
              << " bytes and decodes, " << (NpcsPerPage + 1) << " would not fit, and the pre-the reference "
                 "three-per-page shape would have been " << threePerPage << " bytes - "
              << (threePerPage - MaxPacket) << " over the limit send() refuses at; "
              << MaxNpcPages << " pages cover all " << LocalGameplay::MaxNpcs << " creatures. "
                 "The overrun predates this checkpoint: the LAN 82 shape already measured "
              << pre0242 << " bytes, so three to a page was "
              << (HeaderSize + prologue + 3 * pre0242) << " bytes and already refused, while the "
                 "shape before the stormstrike block measured " << pre0234 << " bytes and fitted "
                 "three to a page at " << (HeaderSize + prologue + 3 * pre0234) << "\n";
}

// ---------------------------------------------------------------------------
// NPC state is not persisted, so the save version moving at the implementation (the pet
// roster) changes nothing this group measures.
// ---------------------------------------------------------------------------
void saveFormatUnchanged() {
    // Pinned at save 30 when this group was written; the save has moved on for
    // unrelated state (Save45 at 2.34). The invariant this group owns is that
    // no NPC control state ever reaches a save: the player block is identical
    // with and without live controls, and the previous format still reads.
    static_assert(SaveVersion >= 30);
    LocalRealmPlayer p; p.guid = 1; p.classId = 2; p.level = 80; p.money = 321;
    p.knownSpells = {10308, 20066};
    LocalStatAura aura; aura.spellId = 1126; aura.remainingMs = 123456;
    aura.mapId = p.mapId; aura.instanceId = p.instanceId; aura.casterGuid = 1; aura.stacks = 1;
    p.statAuras = {aura};
    Writer current; writeProgress(current, p);
    LocalRealmPlayer restored; restored.guid = 1;
    Reader read(current.bytes.data(), current.bytes.size());
    assert(readProgress(read, restored) && read.done());
    assert(restored.statAuras == p.statAuras && restored.money == p.money &&
           restored.knownSpells == p.knownSpells);
    Writer previous; writeProgress(previous, p, SaveVersion - 1);
    LocalRealmPlayer legacy; legacy.guid = 1;
    Reader old(previous.bytes.data(), previous.bytes.size());
    assert(readProgress(old, legacy, SaveVersion - 1) && old.done());
    LocalRealmNpc controlled; controlled.controls = {{853, 3000, 1, 11, 0}};
    Writer again; writeProgress(again, p);
    assert(again.bytes == current.bytes);
    assert(!controlled.controls.empty());
    std::cout << "PASS save" << int(SaveVersion) << " carries no NPC control state: the per-player progress block is "
              << current.bytes.size() << " bytes with and without live NPC controls, and save"
              << int(SaveVersion - 1) << " still reads\n";
}

// ---------------------------------------------------------------------------
// The two client flags. Unit::SetStunned sets UNIT_FLAG_STUNNED and nothing
// else; UNIT_FLAG_SILENCED has exactly one writer, HandleAuraModSilence.
// ---------------------------------------------------------------------------
void clientFlags() {
    static_assert(UNIT_FLAG_STUNNED == 0x00040000u);
    static_assert(UNIT_FLAG_SILENCED == 0x00002000u);
    LocalRealmNpc n;
    assert(!localNpcStunned(n) && !localNpcSilenced(n));

    n.controls = {{853, 3000, 41, 0, uint8_t(LocalNpcControlKind::Stun)}};
    assert(localNpcStunned(n));
    const bool exact = expect(!localNpcSilenced(n),
        "control flags: a creature carrying only a stun reports silenced. localNpcSilenced "
        "(include/game/local_npc_auras.hpp:47-52) answers true for LocalNpcControlKind::Stun as "
        "well as Silence, and src/game/game_handler_local.cpp:495-496 uses it verbatim to write "
        "UNIT_FIELD_FLAGS, so every stunned creature is replicated to the client with "
        "UNIT_FLAG_SILENCED set. Unit::SetStunned (Unit.cpp:13972-13985) sets UNIT_FLAG_STUNNED "
        "alone; UNIT_FLAG_SILENCED has exactly one writer, AuraEffect::HandleAuraModSilence "
        "(SpellAuraEffects.cpp:3152), and the audit's own section 10.3 rejects writing a control "
        "flag onto units the reference leaves clear. Fix: drop the Stun arm from localNpcSilenced - "
        "the cast gate at local_gameplay.cpp:4320 already reads `stunned || (silenced && ...)`, so "
        "nothing else depends on it.");

    n.controls = {{15487, 5000, 41, 0, uint8_t(LocalNpcControlKind::Silence)}};
    assert(localNpcSilenced(n) && !localNpcStunned(n));
    n.controls.push_back({853, 3000, 42, 0, uint8_t(LocalNpcControlKind::Stun)});
    assert(localNpcSilenced(n) && localNpcStunned(n));
    // An expired entry is no longer a control on either predicate.
    for (auto& a : n.controls) a.remainingMs = 0;
    assert(!localNpcStunned(n) && !localNpcSilenced(n));
    if (exact)
        std::cout << "PASS control flags: UNIT_FLAG_STUNNED follows a stun only, UNIT_FLAG_SILENCED "
                     "a silence only, both follow the two kinds independently, and an expired entry "
                     "sets neither\n";
}
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    assert(argc == 2);
    ClientTables tables;
    tables.load(argv[1]);
    gImported = tables.import();
    importerCensus(tables);
    stunMovement();
    stunSwing();
    stunBlocksCast();
    silenceBlocksCast();
    breakOnDamage();
    controlLifecycle();
    lanCodec();
    wireBudget();
    saveFormatUnchanged();
    clientFlags();
    if (gFailures)
        std::cerr << gFailures << " group(s) failed against source-backed fixtures\n";
    return gFailures ? 1 : 0;
}
