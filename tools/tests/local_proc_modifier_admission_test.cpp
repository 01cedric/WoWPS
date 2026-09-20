#include "local_group_rewards_fixture.hpp"
#include "game/local_proc_chance_modifiers.hpp"
#include "game/local_proc_timing.hpp"
#include "game/local_pet.hpp"
#include "game/local_spell_import.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>

// P03 / C3 - admission of chance and PPM spell modifiers (SpellModOp 18 and 26).
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33, as quoted in
// the source audit sections 2.2-2.5 and 4.1:
//   AuraEffect::ApplySpellMod    SpellAuraEffects.cpp:813-818 - every real apply
//       reaches Player::AddSpellMod. Nothing there requires a talent, a
//       prerequisite or a passive aura, so a timed buff is an equal source.
//   Player::IsAffectedBySpellmod Player.cpp:9997 - a charged modifier aura with
//       no charges left is skipped.
//   Player::ApplySpellMod        Player.cpp:10040, :10045-10046, :10062, :10083 -
//       flat terms add, percentages for these operations add, a zero accumulated
//       multiplier stops further percentages, and the result is base*mul+flat.
//   Player::RemoveSpellMods      Player.cpp:10213 - an aura whose spell has a
//       spell_proc entry is skipped by modifier consumption and debited by
//       Aura::ConsumeProcCharges instead, so one charge is never spent twice.
//   Unit::GetSpellModOwner       Unit.cpp:12511-12535, used by
//       Unit::GetPPMProcChance (Unit.cpp:10203) for the modifier owner, while
//       the weapon period at SpellAuras.cpp:2378 is the aura caster's own
//       GetAttackTime. For a summon-cast aura those are two different units.
//
// The dispatcher halves mirror the authority's own proc stream: every eligible
// swing is replayed through localRollProcBasisPoints with the chance the source
// rules say must have been used, so each group asserts an exact probability
// rather than a frequency. A chance of zero or of a full ten thousand basis
// points never advances that stream, on either side, so those cases stay exact
// as well.
namespace {
using namespace wowee;
using namespace wowee::game;

constexpr std::array<uint32_t, 3> kMask{0, 0, 0x40000000};
constexpr uint32_t kProcAura = 99, kModifierAura = 200, kChargedAura = 300;
constexpr uint32_t kTalentFlat = 100, kTalentPct = 101;
constexpr uint64_t kProcSeed = 0x9e3779b97f4a7c15ULL; // LocalGameplay::procRandomState

LocalSpellDefinition procAura(uint32_t id = kProcAura, uint8_t chance = 20, float ppm = 0) {
    LocalSpellDefinition d;
    d.id = id; d.name = "Modifier admission proc"; d.durationMs = 600000;
    d.spellFamily = 3; d.spellFamilyFlags = kMask;
    d.proc.effect = LocalProcEffect::HealOwner; d.proc.spellId = 199; d.proc.flags = 8;
    d.proc.amount = 3; d.proc.chance = chance; d.proc.ppm = ppm;
    d.range = 100; d.buffSelfOnly = false;
    return d;
}

// A timed buff the caster carries. It is not passive and not a talent, which is
// exactly the source AuraEffect::ApplySpellMod admits and the the implementation evaluator
// could not see. buffHealth makes it a real timed aura the authority keeps.
LocalSpellDefinition modifierAura(uint32_t id, uint8_t operation, bool percentage, int32_t amount,
                                  std::array<uint32_t, 3> mask = kMask) {
    LocalSpellDefinition d;
    d.id = id; d.name = "Timed modifier"; d.durationMs = 600000;
    d.spellFamily = 3; d.buffHealth = 10;
    d.passiveCastModifiers[0] = {operation, percentage, true, amount, mask};
    return d;
}

LocalSpellDefinition modifierTalent(uint32_t id, uint8_t operation, bool percentage, int32_t amount,
                                    std::array<uint32_t, 3> mask = kMask) {
    LocalSpellDefinition d;
    d.id = id; d.talentId = id; d.talentRank = 1; d.passive = true;
    d.allowableClasses = 128; d.spellFamily = 3;
    d.passiveCastModifiers[0] = {operation, percentage, true, amount, mask};
    return d;
}

std::shared_ptr<LocalWorldContent> sortedContent(std::shared_ptr<LocalWorldContent> c) {
    std::sort(c->npcs.begin(), c->npcs.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(c->spells.begin(), c->spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return c;
}

LocalRealmPlayer modifierCaster(uint64_t guid = 1) {
    auto p = rewardPlayer(guid);
    p.classId = 8; p.level = 80; p.health = p.maxHealth = 100000;
    return p;
}

LocalStatAura application(uint32_t spellId, const LocalRealmPlayer& owner, uint32_t remainingMs = 600000) {
    LocalStatAura a;
    a.spellId = spellId; a.remainingMs = remainingMs;
    a.mapId = owner.mapId; a.instanceId = owner.instanceId; a.casterGuid = owner.guid;
    return a;
}

// ---------------------------------------------------------------------------
// (a), (b), (d) and the rule half of (c): the admission record itself.
// ---------------------------------------------------------------------------
void admissionRecord() {
    auto c = rewardContent();
    auto p = modifierCaster();
    const auto aura = procAura();

    c->spells.push_back(modifierAura(kModifierAura, 18, false, 40));
    c->spells.push_back(modifierAura(kModifierAura + 1, 26, true, 50));
    c->spells.push_back(modifierTalent(kTalentFlat, 18, false, 5));
    c->spells.push_back(modifierTalent(kTalentPct, 18, true, 50));
    c = sortedContent(c);

    // ---- (a) a timed aura is a modifier source ----------------------------
    p.statAuras = {application(kModifierAura, p)};
    {
        const auto m = localProcChanceModifiersForCaster(&p, *c, aura);
        assert(m.chanceFlat == 40 && m.chanceMultiplier == 1 && m.ppmFlat == 0 && m.ppmMultiplier == 1);
    }
    // ... and it stops contributing on the millisecond it expires. One
    // millisecond of duration still counts; zero never does.
    p.statAuras[0].remainingMs = 1;
    assert(localProcChanceModifiersForCaster(&p, *c, aura).chanceFlat == 40);
    p.statAuras[0].remainingMs = 0;
    assert(localProcChanceModifiersForCaster(&p, *c, aura).chanceFlat == 0);
    p.statAuras[0].remainingMs = 600000;

    // A foreign caster's application on this player is that caster's modifier,
    // never this one's. A zero GUID is the legacy self-caster and does count.
    p.statAuras[0].casterGuid = p.guid + 1;
    assert(localProcChanceModifiersForCaster(&p, *c, aura).chanceFlat == 0);
    p.statAuras[0].casterGuid = 0;
    assert(localProcChanceModifiersForCaster(&p, *c, aura).chanceFlat == 40);
    p.statAuras[0].casterGuid = p.guid;

    // The application has to be in the caster's own map and instance.
    p.statAuras[0].mapId = p.mapId + 1;
    assert(localProcChanceModifiersForCaster(&p, *c, aura).chanceFlat == 0);
    p.statAuras[0].mapId = p.mapId;
    p.statAuras[0].instanceId = p.instanceId + 1;
    assert(localProcChanceModifiersForCaster(&p, *c, aura).chanceFlat == 0);
    p.statAuras[0].instanceId = p.instanceId;
    assert(localProcChanceModifiersForCaster(&p, *c, aura).chanceFlat == 40);

    // SpellInfo::IsAffected: another family, or a mask that selects nothing,
    // contributes nothing, and an ignore-caster-modifiers proc takes none at all.
    {
        auto other = aura; other.spellFamily = 7;
        assert(localProcChanceModifiersForCaster(&p, *c, other).chanceFlat == 0);
        auto foreign = aura; foreign.spellFamilyFlags = {1, 0, 0};
        assert(localProcChanceModifiersForCaster(&p, *c, foreign).chanceFlat == 0);
        auto ignored = aura; ignored.sourceIgnoreCasterModifiers = true;
        assert(localProcChanceModifiersForCaster(&p, *c, ignored).chanceFlat == 0);
    }
    // An unavailable source sibling contributes nothing in either pass.
    for (auto& d : c->spells) if (d.id == kModifierAura) d.unsupportedReason = "Unavailable source sibling";
    assert(localProcChanceModifiersForCaster(&p, *c, aura).chanceFlat == 0);
    for (auto& d : c->spells) if (d.id == kModifierAura) d.unsupportedReason.clear();

    // AuraEffect::ApplySpellMod imposes neither of the talent-only gates, so a
    // non-passive aura that also names a talent prerequisite still contributes.
    for (auto& d : c->spells) if (d.id == kModifierAura) {
        assert(!d.passive);
        d.talentPrerequisites[0] = 4242;
    }
    assert(localProcChanceModifiersForCaster(&p, *c, aura).chanceFlat == 40);
    for (auto& d : c->spells) if (d.id == kModifierAura) d.talentPrerequisites[0] = 0;

    // Operation 26 lands on the PPM accumulator, never on the chance one.
    p.statAuras = {application(kModifierAura + 1, p)};
    {
        const auto m = localProcChanceModifiersForCaster(&p, *c, aura);
        assert(m.ppmMultiplier == 1.5f && m.ppmFlat == 0 && m.chanceFlat == 0 && m.chanceMultiplier == 1);
    }

    // ---- (b) a charged modifier aura with nothing left to spend ------------
    // Player::IsAffectedBySpellmod, Player.cpp:9997.
    for (auto& d : c->spells) if (d.id == kModifierAura) d.proc.charges = 3;
    p.statAuras = {application(kModifierAura, p)};
    p.statAuras[0].procCharges = 0;
    assert(localProcChanceModifiersForCaster(&p, *c, aura).chanceFlat == 0);
    p.statAuras[0].procCharges = 1;
    assert(localProcChanceModifiersForCaster(&p, *c, aura).chanceFlat == 40);
    // The skip is conditioned on the definition declaring charges at all: an
    // uncharged aura is never gated on a charge counter it does not use.
    for (auto& d : c->spells) if (d.id == kModifierAura) d.proc.charges = 0;
    p.statAuras[0].procCharges = 0;
    assert(localProcChanceModifiersForCaster(&p, *c, aura).chanceFlat == 40);

    // ---- (c), rule half: which charge system owns the debit ----------------
    // Player.cpp:10213 splits on whether the modifier aura's own spell has a
    // proc entry, not on whether it is charged.
    assert(localModifierChargeOwnedByProc(aura));
    assert(!localModifierChargeOwnedByProc(modifierAura(kModifierAura, 18, false, 40)));
    {
        auto charged = modifierAura(kModifierAura, 18, false, 40);
        charged.proc.charges = 3;
        assert(!localModifierChargeOwnedByProc(charged)); // charged, but no proc effect
        charged.proc = aura.proc;
        assert(localModifierChargeOwnedByProc(charged));
        charged.proc.effect = LocalProcEffect::None;
        assert(!localModifierChargeOwnedByProc(charged));
    }

    // ---- (d) a talent and a timed aura accumulate together -----------------
    assert(validLocalTalents(p));
    p.talents = {{kTalentFlat, 1}, {kTalentPct, 1}};
    p.statAuras = {application(kModifierAura, p), application(kModifierAura + 2, p)};
    for (auto& d : c->spells) if (d.id == kModifierAura + 1) d = modifierAura(kModifierAura + 2, 18, true, 25);
    c = sortedContent(c);
    {
        // Percentages add across both sources (Player.cpp:10062) and flats stay
        // independent of them (Player.cpp:10040, :10083).
        const auto m = localProcChanceModifiersForCaster(&p, *c, aura);
        assert(m.chanceMultiplier == 1.75f && m.chanceFlat == 45);
        LocalProcDefinition proc = aura.proc;
        LocalCombatEvent e; e.kind = LocalCombatEventKind::NpcMelee; e.attackType = LocalCombatAttackType::Melee;
        // talent 5 + aura 40 flat, talent 50 + aura 25 percent: 20 * 1.75 + 45 = 80.
        assert(localProcChanceBasisPoints(proc, e, {true, 2000}, m) == 8000);
    }
    // The -100 percent floor stops further percentages while flats keep adding.
    // The talent pass runs first, so the floor is reached before the aura's own
    // percentage is offered.
    for (auto& d : c->spells) if (d.id == kTalentPct) d.passiveCastModifiers[0].amount = -100;
    {
        const auto m = localProcChanceModifiersForCaster(&p, *c, aura);
        assert(m.chanceMultiplier == 0.f && m.chanceFlat == 45);
        LocalProcDefinition proc = aura.proc;
        LocalCombatEvent e; e.kind = LocalCombatEventKind::NpcMelee; e.attackType = LocalCombatAttackType::Melee;
        assert(localProcChanceBasisPoints(proc, e, {true, 2000}, m) == 4500); // 20*0 + 45
    }
    std::cout << "PASS modifier admission record: timed aura source, expiry on the millisecond, "
                 "foreign caster/map/instance/family rejection, charged-aura gate, proc-owned charge "
                 "split, talent plus aura accumulation and the -100 percent floor\n";
}

// ---------------------------------------------------------------------------
// Shared runtime scaffolding for the dispatcher halves.
// ---------------------------------------------------------------------------
struct Swings {
    unsigned eligible = 0, procs = 0;
};

// Forces one melee swing per authority tick. When `replay` is supplied, every
// matching swing is also rolled against `expected` on a mirror of the
// authority's own proc stream, so the observed activations pin the exact chance
// rather than a frequency.
Swings beat(LocalGameplay& game, LocalRealmPlayer& owner, const LocalProcDefinition& proc,
            const std::vector<LocalRealmPlayer*>& players, unsigned ticks, uint64_t& seen,
            uint32_t expected = 0, uint64_t* replay = nullptr) {
    Swings s;
    auto n = rewardNpc();
    n.health = n.maxHealth = 100000; n.level = 1; n.x = n.homeX = 1;
    n.targetGuid = owner.guid; n.threat[0] = {owner.guid, 1000};
    for (unsigned i = 0; i < ticks; ++i) {
        owner.health = owner.maxHealth / 2; n.attackTimer = 0;
        game.setRemoteNpcs({n}); game.tick(.001f, players);
        bool wanted = false, saw = false;
        for (const auto& e : game.combatEvents()) if (e.sequence > seen) {
            if (e.kind == LocalCombatEventKind::NpcMelee && localProcMatches(proc, e, owner.guid)) {
                ++s.eligible;
                if (replay) wanted = localRollProcBasisPoints(expected, *replay);
            }
            if (e.kind == LocalCombatEventKind::ProcHeal) { saw = true; ++s.procs; }
            seen = std::max(seen, e.sequence);
        }
        if (replay) assert(wanted == saw);
    }
    return s;
}

bool carries(const LocalRealmPlayer& p, uint32_t spellId) {
    return std::any_of(p.statAuras.begin(), p.statAuras.end(),
                       [&](const auto& a) { return a.spellId == spellId && a.remainingMs; });
}

// ---------------------------------------------------------------------------
// (a), through the real dispatcher: the timed aura's op-18 record changes the
// chance the authority actually rolls, and the authority's own aura tick ends
// that contribution when the application lapses.
// ---------------------------------------------------------------------------
void timedAuraRuntime() {
    auto c = rewardContent();
    const auto aura = procAura(); // 20 percent on its own
    assert(validLocalProc(aura));
    c->spells.push_back(aura);
    c->spells.push_back(modifierAura(kModifierAura, 18, false, 30)); // 20 + 30 = 50 percent
    c = sortedContent(c);

    LocalGameplay game; game.useContent(c);
    auto owner = modifierCaster();
    owner.statAuras = {application(kProcAura, owner), application(kModifierAura, owner, 30)};
    uint64_t seen = 0, replay = kProcSeed;
    const std::vector<LocalRealmPlayer*> players{&owner};

    const auto boosted = beat(game, owner, aura.proc, players, 25, seen, 5000, &replay);
    assert(carries(owner, kModifierAura));
    assert(boosted.eligible > 5);

    // Run out the remaining duration; the authority's own tick retires it.
    while (carries(owner, kModifierAura)) game.tick(.001f, players);
    assert(carries(owner, kProcAura));

    const auto bare = beat(game, owner, aura.proc, players, 160, seen, 2000, &replay);
    assert(bare.eligible > 60);
    assert(boosted.procs && bare.procs); // both phases really rolled
    std::cout << "PASS timed modifier runtime: " << boosted.procs << "/" << boosted.eligible
              << " swings at the modified 50 percent while the aura was carried and " << bare.procs
              << "/" << bare.eligible << " at the bare 20 percent after the authority expired it\n";
}

// ---------------------------------------------------------------------------
// (c), through the real dispatcher: a charged aura that is BOTH a modifier
// source and a proc source is debited once per activation. The proc path owns
// that debit (Player.cpp:10213); if modifier consumption took one as well, two
// charges would buy only one activation.
// ---------------------------------------------------------------------------
void chargedDebitRuntime() {
    auto c = rewardContent();
    auto charged = procAura(kChargedAura, 100);
    charged.proc.charges = 2;
    charged.passiveCastModifiers[0] = {18, false, true, 10, kMask};
    assert(validLocalProc(charged));
    assert(localModifierChargeOwnedByProc(charged));
    c->spells.push_back(charged);
    c = sortedContent(c);

    LocalGameplay game; game.useContent(c);
    auto owner = modifierCaster();
    owner.statAuras = {application(kChargedAura, owner)};
    owner.statAuras[0].procCharges = 2;
    uint64_t seen = 0;
    const std::vector<LocalRealmPlayer*> players{&owner};

    // A swing that misses or is dodged is not a matching event, so advance one
    // tick at a time until the first one the proc actually selects.
    Swings first;
    for (unsigned i = 0; i < 200 && !first.eligible; ++i) {
        const auto step = beat(game, owner, charged.proc, players, 1, seen);
        first.eligible += step.eligible; first.procs += step.procs;
    }
    assert(first.eligible == 1 && first.procs == 1);
    assert(carries(owner, kChargedAura) && owner.statAuras[0].procCharges == 1);

    const auto rest = beat(game, owner, charged.proc, players, 60, seen);
    assert(rest.eligible > 10 && rest.procs == 1);
    assert(!carries(owner, kChargedAura)); // the second charge retired it
    std::cout << "PASS charged modifier debit: two charges bought exactly two activations over "
              << (first.eligible + rest.eligible) << " matching swings, one debit each\n";
}

// ---------------------------------------------------------------------------
// (f) A summon-cast aura: Unit::GetSpellModOwner resolves the modifier owner to
// the player, while Aura::GetCaster keeps the weapon period on the summon.
// ---------------------------------------------------------------------------
std::shared_ptr<LocalWorldContent> petContent(const LocalSpellDefinition& aura) {
    auto c = rewardContent();
    LocalNpcDefinition beast;
    beast.id = 30; beast.name = "Owned beast"; beast.health = 400; beast.damage = 20;
    beast.armor = 0; beast.xp = 0; beast.respawnSeconds = 30;
    c->npcs.push_back(beast);
    LocalSpellDefinition summon;
    summon.id = 2; summon.name = "Summon beast"; summon.clientSpell = true; summon.range = 0;
    summon.summonPetEntry = 30; summon.summonPetKind = uint8_t(LocalPetKind::Controlled);
    summon.summonPetEffectSlot = 0;
    c->spells.push_back(summon);
    c->spells.push_back(aura);
    return sortedContent(c);
}

// Summons a beast, gives it `period` as its own attack period and hands the
// owner an application of `auraId` that the summon - not the owner - cast.
uint64_t summonWithAura(LocalGameplay& game, LocalRealmPlayer& owner, uint32_t auraId, uint32_t period) {
    std::string message;
    owner.knownSpells = {1, 2};
    if (!game.execute(owner, {LocalAction::CastSpell, 0, 2}, {&owner}, message)) {
        std::cerr << "summon rejected: " << message << '\n';
        std::abort();
    }
    assert(game.pets().size() == 1);
    auto pets = game.pets(); pets[0].attackPeriodMs = period; game.setRemotePets(pets);
    assert(game.pets()[0].attackPeriodMs == period);
    const auto petGuid = game.pets()[0].guid;
    assert(localPetGuid(petGuid) && petGuid != owner.guid);
    auto a = application(auraId, owner);
    a.casterGuid = petGuid;
    owner.statAuras.push_back(a);
    return petGuid;
}

void petModifierOwner() {
    const auto aura = procAura(); // 20 percent on its own
    auto c = petContent(aura);
    c->spells.push_back(modifierTalent(kTalentFlat, 18, false, 80)); // 20 + 80 = certainty
    c = sortedContent(c);
    LocalGameplay game; game.useContent(c);
    auto owner = modifierCaster();
    owner.talents = {{kTalentFlat, 1}};
    const std::vector<LocalRealmPlayer*> players{&owner};
    summonWithAura(game, owner, kProcAura, 8000);
    uint64_t seen = game.combatEvents().empty() ? 0 : game.combatEvents().back().sequence;
    uint64_t replay = kProcSeed;

    const auto boosted = beat(game, owner, aura.proc, players, 25, seen, 10000, &replay);
    assert(boosted.eligible > 5 && boosted.procs == boosted.eligible);

    owner.talents.clear();
    const auto bare = beat(game, owner, aura.proc, players, 160, seen, 2000, &replay);
    assert(bare.eligible > 60 && bare.procs && bare.procs < bare.eligible);
    std::cout << "PASS pet modifier owner: the owner's own op-18 talent decided a summon-cast aura ("
              << boosted.procs << "/" << boosted.eligible << " with it, " << bare.procs << "/"
              << bare.eligible << " without)\n";
}

void petPpmPeriod() {
    // Aura::CalcProcChance asks the aura's caster - the summon - for the attack
    // time (SpellAuras.cpp:2378); only the PPM modifier goes through
    // GetSpellModOwner (Unit.cpp:10203). The two units must not be collapsed.
    const auto aura = procAura(kProcAura, 0, 7.5f);
    assert(validLocalProc(aura));
    auto c = petContent(aura);
    LocalGameplay game; game.useContent(c);
    auto owner = modifierCaster();
    const std::vector<LocalRealmPlayer*> players{&owner};
    // The owner is unarmed, so the player-side period is the source 2000 ms
    // unarmed timer. The summon swings at 8000 ms.
    assert(localProcTimingForCaster(&owner, *c, LocalCombatEvent{}).weaponPeriodMs == 2000);
    summonWithAura(game, owner, kProcAura, 8000);
    uint64_t seen = game.combatEvents().empty() ? 0 : game.combatEvents().back().sequence;

    const auto swings = beat(game, owner, aura.proc, players, 400, seen);
    assert(swings.eligible > 100);
    // 7.5 PPM over the summon's own 8000 ms period is 10000 basis points, so
    // every matching swing must activate. The owner's 2000 ms would give 2500.
    if (swings.procs != swings.eligible)
        std::cerr << "FAIL pet PPM period: the weapon period came from the modifier owner, not the "
                     "aura's caster - " << swings.procs << " of " << swings.eligible
                  << " matching swings activated. 7.5 PPM at the summon's 8000 ms is certainty; the "
                     "owner's unarmed 2000 ms is 25 percent (src/game/local_gameplay.cpp:1243 hands "
                     "localProcTimingForCaster the same pointer the pet-owner walk produced at :1216-1218)\n";
    assert(swings.procs == swings.eligible);
    std::cout << "PASS pet PPM period: " << swings.procs << "/" << swings.eligible
              << " matching swings used the summon's own 8000 ms attack period\n";
}

// ---------------------------------------------------------------------------
// (e) The real importer over the player's own client data.
// ---------------------------------------------------------------------------
struct ClientTables {
    std::map<std::string, pipeline::DBCFile> files;
    std::map<std::string, std::vector<uint8_t>> bytes;
    const pipeline::DBCFile* get(const char* name) { return &files.at(name); }
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

// Every spell id the importer admits. (e) requires that decoding operations 18
// and 26 leaves this set bit-identical: the gate in item 2 is what keeps the 44
// new records from making one single spell reachable that was not reachable
// before, so a widened set here is the failure this checkpoint must not have.
std::set<uint32_t> acceptedSpellIds(const LocalSpellImport& in) {
    std::set<uint32_t> ids;
    for (const auto& d : in.spells) if (d.unsupportedReason.empty()) ids.insert(d.id);
    return ids;
}

bool carriesChanceModifier(const LocalSpellDefinition& d) {
    return std::any_of(d.passiveCastModifiers.begin(), d.passiveCastModifiers.end(),
                       [](const auto& m) { return m.active && (m.operation == 18 || m.operation == 26); });
}

void put32(std::vector<uint8_t>& bytes, uint32_t row, uint32_t column, uint32_t value, uint32_t recordSize) {
    const size_t at = 20 + size_t(row) * recordSize + size_t(column) * 4;
    bytes[at] = uint8_t(value); bytes[at + 1] = uint8_t(value >> 8);
    bytes[at + 2] = uint8_t(value >> 16); bytes[at + 3] = uint8_t(value >> 24);
}

void importerGate(const std::filesystem::path& dbc) {
    ClientTables t;
    for (const auto* name : {"Spell", "SpellRange", "SpellCastTimes", "SpellDuration", "SpellIcon",
                             "SpellRadius", "SpellRuneCost", "SkillLine", "SkillLineAbility",
                             "Talent", "TalentTab"}) {
        std::ifstream f(dbc / (std::string(name) + ".dbc"), std::ios::binary);
        t.bytes[name] = {std::istreambuf_iterator<char>(f), {}};
        assert(t.files[name].load(t.bytes[name]));
    }
    const auto* spells = t.get("Spell");
    const auto recordSize = spells->getRecordSize();

    // The whole op-18 / op-26 population in the player's own Spell.dbc.
    std::set<uint32_t> population;
    std::vector<std::pair<uint32_t, unsigned>> records;
    for (uint32_t row = 0; row < spells->getRecordCount(); ++row)
        for (unsigned e = 0; e < 3; ++e) {
            const auto aura = spells->getUInt32(row, 95 + e), op = spells->getUInt32(row, 110 + e);
            if ((aura == 107 || aura == 108) && (op == 18 || op == 26)) {
                population.insert(spells->getUInt32(row, 0));
                records.push_back({row, e});
            }
        }
    assert(population.size() == 44 && records.size() == 53);

    const auto after = t.import();
    const auto acceptedAfter = acceptedSpellIds(after);
    std::map<uint32_t, std::string> afterReason;
    unsigned decoded = 0, accepted = 0;
    for (const auto& d : after.spells) if (population.count(d.id)) {
        afterReason[d.id] = d.unsupportedReason;
        if (!carriesChanceModifier(d)) continue;
        ++decoded;
        if (d.unsupportedReason.empty()) ++accepted;
        // The honest rejection: the modifier decoded, its affected spell did not.
        assert(d.unsupportedReason == "Affected spells are not implemented for this modifier");
        assert(d.talentId && d.talentRank);
        const auto row = std::find_if(after.audit.begin(), after.audit.end(),
                                      [&](const auto& r) { return r.talent && r.id == d.id; });
        assert(row != after.audit.end() && row->status == d.unsupportedReason);
    }
    assert(accepted == 0);

    // Reproduce the pre-the implementation decoder exactly by flipping the low bit of every
    // one of those EffectMiscValues: 18 becomes 19 and 26 becomes 27, neither of
    // which any accept-set admits, so the decode takes the identical reject path.
    auto edited = t.bytes.at("Spell");
    for (const auto& [row, e] : records) edited[20 + size_t(row) * recordSize + (110 + e) * 4] ^= 1;
    assert(t.files["Spell"].load(edited));
    for (uint32_t row = 0; row < t.files["Spell"].getRecordCount(); ++row)
        for (unsigned e = 0; e < 3; ++e) {
            const auto aura = t.files["Spell"].getUInt32(row, 95 + e);
            const auto op = t.files["Spell"].getUInt32(row, 110 + e);
            assert(!((aura == 107 || aura == 108) && (op == 18 || op == 26)));
        }
    const auto before = t.import();
    std::map<uint32_t, std::string> beforeReason;
    for (const auto& d : before.spells) if (population.count(d.id)) beforeReason[d.id] = d.unsupportedReason;
    const auto acceptedBefore = acceptedSpellIds(before);
    assert(t.files["Spell"].load(t.bytes.at("Spell")));

    // Nothing new became reachable. Not one id joined or left the accepted set.
    assert(!acceptedAfter.empty() && acceptedAfter == acceptedBefore);

    unsigned reachable = 0, changed = 0;
    for (const auto id : population) {
        if (!afterReason.count(id) || !beforeReason.count(id)) {
            assert(!afterReason.count(id) && !beforeReason.count(id)); // unreachable either way
            continue;
        }
        ++reachable;
        if (afterReason.at(id) == beforeReason.at(id)) continue;
        ++changed;
        // Every changed rank traded an aura-unsupported reason for the honest one.
        assert(beforeReason.at(id) == "Unsupported effect 6 / aura 107" ||
               beforeReason.at(id) == "Unsupported effect 6 / aura 108");
        assert(afterReason.at(id) == "Affected spells are not implemented for this modifier");
    }
    // Every rank whose reason changed is exactly a rank that now carries a
    // decoded op-18/26 record, and no other.
    assert(changed == decoded && changed == 13 && reachable == 26);

    // No imported proc carries a PPM, so operation 26 has no reachable target in
    // the supplied data at all - it can only ever be rejected here.
    const auto ppmTargets = std::count_if(after.spells.begin(), after.spells.end(), [](const auto& c) {
        return !c.passive && c.unsupportedReason.empty() && c.proc.effect != LocalProcEffect::None && c.proc.ppm > 0;
    });
    assert(ppmTargets == 0);

    // The other direction of the gate, on the same real rows: talent 64 rank 1
    // (spell 11190, Mage, family 3) carries one aura-108 modifier. Re-pointing
    // only its operation at 18 leaves it rejected, because nothing its class
    // mask selects is a proc; re-pointing that mask at Molten Armor's own proc
    // mask - a spell this build really does import - makes the record acceptable.
    constexpr uint32_t kProbeTalent = 11190, kMoltenArmor = 30482;
    const auto molten = std::find_if(after.spells.begin(), after.spells.end(),
                                     [](const auto& d) { return d.id == kMoltenArmor; });
    assert(molten != after.spells.end() && molten->unsupportedReason.empty() && !molten->passive);
    assert(molten->spellFamily == 3 && molten->proc.spellFamily == 3 && molten->proc.chance);
    uint32_t probeRow = 0;
    while (probeRow < spells->getRecordCount() && spells->getUInt32(probeRow, 0) != kProbeTalent) ++probeRow;
    assert(probeRow < spells->getRecordCount());
    unsigned probeEffect = 3;
    for (unsigned e = 0; e < 3; ++e) {
        const auto a = spells->getUInt32(probeRow, 95 + e);
        if (a == 107 || a == 108) { probeEffect = e; break; }
    }
    assert(probeEffect < 3 && spells->getUInt32(probeRow, 110 + probeEffect) != 18);

    auto probe = t.bytes.at("Spell");
    put32(probe, probeRow, 110 + probeEffect, 18, recordSize);
    assert(t.files["Spell"].load(probe));
    {
        const auto out = t.import();
        const auto d = std::find_if(out.spells.begin(), out.spells.end(),
                                    [](const auto& s) { return s.id == kProbeTalent; });
        assert(d != out.spells.end() && carriesChanceModifier(*d));
        assert(d->unsupportedReason == "Affected spells are not implemented for this modifier");
    }
    for (unsigned k = 0; k < 3; ++k)
        put32(probe, probeRow, spell335::EffectClassMask + probeEffect * 3 + k,
              molten->proc.spellFamilyFlags[k], recordSize);
    assert(t.files["Spell"].load(probe));
    {
        const auto out = t.import();
        const auto d = std::find_if(out.spells.begin(), out.spells.end(),
                                    [](const auto& s) { return s.id == kProbeTalent; });
        assert(d != out.spells.end() && carriesChanceModifier(*d));
        assert(d->unsupportedReason.empty());
        assert(d->passiveCastModifiers[probeEffect].operation == 18);
        assert(d->passiveCastModifiers[probeEffect].mask == molten->proc.spellFamilyFlags);
        const auto row = std::find_if(out.audit.begin(), out.audit.end(),
                                      [&](const auto& r) { return r.talent && r.id == kProbeTalent; });
        assert(row != out.audit.end() && row->status.find("not implemented") == std::string::npos);
    }
    assert(t.files["Spell"].load(t.bytes.at("Spell")));

    std::cout << "PASS importer gate: " << population.size()
              << " client spells carry operation 18 or 26 across " << records.size() << " effects, "
              << reachable << " reach the definition set as talent ranks, " << accepted
              << " are accepted, " << changed
              << " talent ranks traded an aura-unsupported reason for \"Affected spells are not "
                 "implemented for this modifier\", and the same record aimed at a real imported proc "
                 "is accepted instead; the accepted spell set is unchanged at "
              << acceptedAfter.size() << " ids\n";
}
}

int main(int argc, char** argv) {
    assert(argc == 2);
    admissionRecord();
    timedAuraRuntime();
    chargedDebitRuntime();
    importerGate(argv[1]);
    petModifierOwner();
    petPpmPeriod();
    return 0;
}
