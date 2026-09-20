// P03/D3 outcome vocabulary, the player-cast magic hit roll and the
// conditional periodic critical, at checkpoint the reference.
//
// Scope, stated plainly. This fixture covers the four coupled sites that the
// the implementation plan (the source audit section 5) actually
// touches - the hit-mask constructor, the proc filter vocabulary, the melee
// view save/LAN codec and the two new producers - and it does so through the
// real code in every case: the codec is reached by including the realm
// translation unit exactly as local_combat_view_test.cpp does, and the
// two producers are driven through LocalGameplay::execute/tick rather than
// through a re-implementation of their rules.
//
// What it deliberately does NOT claim: no resistance, immunity or deflection
// producer exists at this baseline, and none is fabricated here. Resist,
// Immune and Deflect are exercised as *vocabulary* - an outcome that the mask
// builder, the filter set and the wire format must all agree about - which is
// the whole of what section 5.6 admits. Likewise no granting aura (source aura
// 286) is importable, so the periodic-critical grant is supplied by an explicit
// fixture descriptor; the mechanism is tested, its reachability is not claimed.
//
// Rolls live on an unseeded authority PRNG, so every statistical assertion here
// is a bounded loop with a margin wide enough that failure means the rule
// changed, not that the dice were unkind; each such bound is justified in place.
#include "../../src/game/local_realm.cpp"
#include "local_group_rewards_fixture.hpp"
#include "game/local_periodic_critical.hpp"
#include "game/local_proc_rules.hpp"
#include "game/local_spell_critical.hpp"
#include <iostream>

namespace {
using namespace wowee::game;

// --- Source constants this checkpoint is pinned against ---------------------
// SpellMgr.h:271 PROC_HIT_MASK_ALL. Bit 12 (PROC_HIT_INTERRUPT 0x1000) is not
// a member of it; bit 7 (PROC_HIT_EVADE 0x80) is, and stays rejected locally
// because no evade/reachability state exists to produce it.
constexpr uint32_t kSourceProcHitMaskAll = 0x2FFFu;
constexpr uint32_t kSourceProcHitEvade = 0x80u;
constexpr uint32_t kSourceProcHitInterrupt = 0x1000u;

// Fixture spell identifiers.
constexpr uint32_t kBolt = 900, kAlwaysHitBolt = 901, kHeal = 902;
constexpr uint32_t kDot = 910, kHot = 911;
constexpr uint32_t kGrant = 920, kWrongFamilyGrant = 921, kDisjointGrant = 922;
constexpr uint32_t kCritTalentSpell = 930, kCritTalentId = 7000;
constexpr uint32_t kFixtureNpcEntry = 51;
// The periodic family the grant descriptor names, and the two class-mask bits
// the damage-over-time and healing-over-time fixtures occupy inside it.
constexpr uint32_t kPeriodicFamily = 3;
constexpr uint32_t kDotFlag = 0x10, kHotFlag = 0x20, kUnrelatedFlag = 0x40;
constexpr uint32_t kBoltCost = 30, kBoltDamage = 50;
constexpr uint32_t kDotAmount = 100, kHotAmount = 40;
constexpr uint8_t kCasterLevel = 20;
constexpr uint8_t kCritTalentPct = 40;

// --- (a)-(c): pure vocabulary fixtures --------------------------------------
LocalCombatEvent bareEvent(LocalCombatEventKind kind, LocalMeleeOutcome outcome) {
    LocalCombatEvent e;
    e.kind = kind; e.source = 1; e.target = 2; e.spell = kBolt;
    e.attackType = LocalCombatAttackType::Magic; e.outcome = outcome;
    return e;
}

LocalRealmPlayer codecOwner() {
    LocalRealmPlayer p; p.guid = 101; p.classId = 8;
    p.positionRevision = p.meleeViewPositionRevision = 7;
    return p;
}
std::vector<uint8_t> encodeCast(const LocalRealmPlayer& p) { Writer w; writeCast(w, p); return w.bytes; }
bool decodeCast(const std::vector<uint8_t>& bytes, LocalRealmPlayer& p) {
    Reader r(bytes.data(), bytes.size()); return readCast(r, p) && r.done();
}
LocalMeleeView damageView(uint8_t outcome, uint32_t serial) {
    LocalMeleeView v; v.serial = serial; v.spell = kBolt; v.source = 101; v.target = 202;
    v.outcome = LocalMeleeOutcome(outcome); return v;
}
// A single observation placed in the last window slot - the window is written
// in order and rejects an empty slot after a populated one, so a lone probe
// belongs at the end. Accepted or not.
bool codecAccepts(const LocalMeleeView& v) {
    auto p = codecOwner(); p.meleeViews.back() = v;
    auto loaded = codecOwner(); return decodeCast(encodeCast(p), loaded);
}
void sameView(const LocalMeleeView& a, const LocalMeleeView& b) {
    assert(a.serial == b.serial && a.spell == b.spell && a.amount == b.amount && a.blocked == b.blocked);
    assert(a.source == b.source && a.target == b.target && a.outcome == b.outcome);
    assert(a.offHand == b.offHand && a.healing == b.healing);
}

// --- (d)-(e): runtime fixtures ----------------------------------------------
LocalSpellDefinition boltSpell(uint32_t id, bool alwaysHit) {
    LocalSpellDefinition d;
    d.id = id; d.name = "Fixture bolt " + std::to_string(id); d.clientSpell = true;
    d.sourceDamageClass = 1; d.schoolMask = 4; d.range = 100; d.resourceType = 255;
    d.damage = kBoltDamage; d.damageMax = kBoltDamage; d.mana = kBoltCost;
    d.spellFamily = kPeriodicFamily; d.spellFamilyFlags = {1, 0, 0};
    d.sourceAlwaysHit = alwaysHit;
    // SPELL_ATTR4_NO_CAST_LOG, "Cannot be resisted" (SharedDefines.h:518),
    // which Unit::CalcAbsorbResist honours (Unit.cpp:2341). This fixture
    // measures the hit roll alone; since the implementation a landed hit against a creature
    // six levels above the caster would otherwise lose the reference's level
    // term to partial resistance, which is that checkpoint's own suite's job.
    d.sourceNoCastLog = true;
    return d;
}
LocalSpellDefinition healSpell() {
    LocalSpellDefinition d;
    d.id = kHeal; d.name = "Fixture heal"; d.clientSpell = true;
    d.sourceDamageClass = 1; d.schoolMask = 2; d.range = 100; d.resourceType = 255;
    d.heal = 60; d.healMax = 60; d.mana = 10;
    d.spellFamily = kPeriodicFamily; d.spellFamilyFlags = {2, 0, 0};
    return d;
}
LocalSpellDefinition dotSpell() {
    LocalSpellDefinition d;
    d.id = kDot; d.name = "Fixture damage over time"; d.clientSpell = true;
    d.sourceDamageClass = 1; d.schoolMask = 4; d.range = 100; d.resourceType = 255;
    d.periodicDamage = kDotAmount; d.periodicDamageMax = kDotAmount;
    d.periodicIntervalMs = 1000; d.durationMs = 600000; d.mana = 10;
    d.spellFamily = kPeriodicFamily; d.spellFamilyFlags = {kDotFlag, 0, 0};
    return d;
}
LocalSpellDefinition hotSpell() {
    LocalSpellDefinition d;
    d.id = kHot; d.name = "Fixture heal over time"; d.clientSpell = true;
    d.sourceDamageClass = 1; d.schoolMask = 2; d.range = 100; d.resourceType = 255;
    d.periodicHeal = kHotAmount; d.periodicHealMax = kHotAmount;
    d.periodicIntervalMs = 1000; d.durationMs = 600000; d.mana = 10;
    d.spellFamily = kPeriodicFamily; d.spellFamilyFlags = {kHotFlag, 0, 0};
    return d;
}
// A timed buff carrying the aura-286 grant descriptor. No such row is
// importable at this baseline (audit section 3.5); this is the fixture that
// stands in for one, and nothing here claims it is reachable in play.
LocalSpellDefinition grantSpell(uint32_t id, uint32_t family, std::array<uint32_t, 3> mask) {
    LocalSpellDefinition d;
    d.id = id; d.name = "Fixture periodic-crit grant " + std::to_string(id);
    d.clientSpell = true; d.schoolMask = 2; d.resourceType = 255; d.range = 0;
    d.buffArmor = 1; d.durationMs = 600000; d.buffSelfOnly = true;
    d.periodicCritFamily = family; d.periodicCritMask = mask;
    return d;
}
// A passive spell-critical talent. Without it a fixture caster's spell critical
// chance is a few hundred basis points, which would force the periodic tests to
// run tens of thousands of ticks to separate "cannot crit" from "rarely crits".
LocalSpellDefinition critTalentSpell() {
    LocalSpellDefinition d;
    d.id = kCritTalentSpell; d.name = "Fixture spell critical talent";
    d.clientSpell = true; d.passive = true; d.resourceType = 255;
    d.talentId = kCritTalentId; d.talentRank = 1; d.talentTab = 1;
    d.allowableClasses = 0xffffffffu; d.passiveSpellCritPct = kCritTalentPct;
    return d;
}
std::shared_ptr<LocalWorldContent> outcomeContent() {
    auto c = rewardContent();
    // A creature entry that never swings back: the fixtures below tick for
    // hundreds of frames and an incidental melee stream would only add noise.
    LocalNpcDefinition target = c->npcs[0];
    target.id = kFixtureNpcEntry; target.name = "Outcome fixture target";
    target.health = 1000000; target.damage = 0; target.armor = 0;
    target.aggroRadius = 0; target.hostile = true; target.loot.clear(); target.money = 0;
    c->npcs.push_back(target);
    c->spells.push_back(boltSpell(kBolt, false));
    c->spells.push_back(boltSpell(kAlwaysHitBolt, true));
    c->spells.push_back(healSpell());
    c->spells.push_back(dotSpell());
    c->spells.push_back(hotSpell());
    c->spells.push_back(grantSpell(kGrant, kPeriodicFamily, {kDotFlag | kHotFlag, 0, 0}));
    c->spells.push_back(grantSpell(kWrongFamilyGrant, kPeriodicFamily + 5, {kDotFlag | kHotFlag, 0, 0}));
    c->spells.push_back(grantSpell(kDisjointGrant, kPeriodicFamily, {kUnrelatedFlag, 0, 0}));
    c->spells.push_back(critTalentSpell());
    std::sort(c->spells.begin(), c->spells.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(c->npcs.begin(), c->npcs.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return c;
}
LocalRealmPlayer castingPlayer(uint64_t guid, bool critTalent) {
    auto p = rewardPlayer(guid);
    p.classId = 8; p.race = 1; p.level = kCasterLevel;
    p.health = p.maxHealth = 100000;
    p.knownSpells = {1, kBolt, kAlwaysHitBolt, kHeal, kDot, kHot, kGrant, kWrongFamilyGrant, kDisjointGrant};
    if (critTalent) p.talents = {{kCritTalentId, 1}};
    return p;
}
// The region pass keeps an installed creature across ticks only while it is
// engaged - an idle actor with no spawn row behind it is culled - so a fixture
// that ticks for hundreds of frames has to hand it a target and the threat that
// justifies one, exactly as local_npc_spell_fixture.hpp does.
LocalRealmNpc fixtureNpc(uint8_t level, uint64_t engagedWith) {
    LocalRealmNpc n;
    n.guid = 10; n.entry = kFixtureNpcEntry; n.name = "Outcome fixture target";
    n.health = n.maxHealth = 1000000; n.hostile = true; n.level = level;
    n.x = n.y = n.z = n.homeX = n.homeY = n.homeZ = 0;
    n.targetGuid = engagedWith; n.threat[0] = {engagedWith, 100000};
    return n;
}

// The authority history is a 128-slot ring, so a test that ticks for hundreds
// of frames has to read it as it goes. Sequence numbers are strictly
// increasing, which is what lets this assert that nothing was dropped between
// two reads rather than merely hoping the window was large enough.
struct EventTap {
    const LocalGameplay& game;
    uint64_t seen = 0;
    explicit EventTap(const LocalGameplay& g) : game(g) {}
    std::vector<LocalCombatEvent> drain() {
        std::vector<LocalCombatEvent> fresh;
        for (const auto& e : game.combatEvents()) if (e.sequence > seen) fresh.push_back(e);
        assert(fresh.empty() || fresh.front().sequence == seen + 1);
        if (!fresh.empty()) seen = fresh.back().sequence;
        return fresh;
    }
};

// Clear the gates that would otherwise refuse a rapid second cast, and refill
// the resource pool so each cast in a bounded loop starts from the same place.
void readyToCast(LocalRealmPlayer& p) {
    p.globalCooldownMs = 0; p.cooldowns.clear(); p.categoryCooldowns.clear();
    p.mana = p.maxMana;
}
bool castAt(LocalGameplay& game, LocalRealmPlayer& p, const std::vector<LocalRealmPlayer*>& roster,
            uint32_t spellId, uint64_t target, std::string& result) {
    readyToCast(p);
    return game.execute(p, {LocalAction::CastSpell, target, spellId}, roster, result);
}

struct CastOutcome {
    bool missed = false, landed = false, finished = false, hitEvent = false;
    uint32_t attempted = 0, effective = 0, manaPaid = 0;
    LocalMeleeOutcome outcome = LocalMeleeOutcome::Hit;
};
// One complete cast of a direct damage spell, read back from the real event
// stream rather than from the return string.
CastOutcome castBolt(LocalGameplay& game, LocalRealmPlayer& p, const std::vector<LocalRealmPlayer*>& roster,
                     EventTap& tap, uint32_t spellId, uint64_t target) {
    std::string result;
    readyToCast(p);
    const auto manaBefore = p.mana;
    const bool ok = game.execute(p, {LocalAction::CastSpell, target, spellId}, roster, result);
    assert(ok);
    CastOutcome out;
    out.manaPaid = manaBefore - p.mana;
    for (const auto& e : tap.drain()) {
        if (e.kind == LocalCombatEventKind::SpellFinish && e.spell == spellId) out.finished = true;
        if (e.kind != LocalCombatEventKind::SpellDamage || e.spell != spellId) continue;
        assert(!out.hitEvent); // exactly one damage observation per single-target cast
        out.hitEvent = true; out.outcome = e.outcome;
        out.attempted = e.attempted; out.effective = e.effective;
        out.missed = e.outcome == LocalMeleeOutcome::Miss;
        out.landed = !out.missed;
    }
    assert(out.hitEvent && out.finished);
    return out;
}
} // namespace

int main() {
    // =====================================================================
    // (a) localProcEventHitMask: a bare avoidance bit, on damage and on heal
    // =====================================================================
    // The three new outcomes are the SpellMissInfo shape, not the DamageInfo
    // shape: Unit.cpp builds the proc-extra mask from a non-NONE miss result
    // alone and never adds NORMAL or CRITICAL on top of it.
    struct { LocalMeleeOutcome outcome; uint32_t bit; } const avoidance[] = {
        {LocalMeleeOutcome::Resist, LocalProcHitFullResist},
        {LocalMeleeOutcome::Immune, LocalProcHitImmune},
        {LocalMeleeOutcome::Deflect, LocalProcHitDeflect},
    };
    for (const auto& row : avoidance) {
        for (auto kind : {LocalCombatEventKind::SpellDamage, LocalCombatEventKind::PeriodicDamage,
                          LocalCombatEventKind::DirectHeal, LocalCombatEventKind::PeriodicHeal}) {
            auto e = bareEvent(kind, row.outcome);
            assert(localProcEventHitMask(e) == row.bit);
            // An absorbed or blocked amount contributes its own bit and must
            // not smuggle NORMAL back in through the trailing promotion.
            e.absorbed = 40; e.blocked = 25; e.attempted = 65; e.effective = 0;
            const auto mixed = localProcEventHitMask(e);
            assert(mixed == (row.bit | LocalProcHitAbsorb | LocalProcHitBlock));
            assert(!(mixed & (LocalProcHitNormal | LocalProcHitCritical)));
            // Even a producer that wrongly reported surviving damage alongside
            // a nullifying outcome must not reach the NORMAL/CRITICAL branch.
            e.effective = 10;
            assert(!(localProcEventHitMask(e) & (LocalProcHitNormal | LocalProcHitCritical)));
            // Nothing promotes an avoidance result to a critical either.
            e.absorbed = e.blocked = e.attempted = e.effective = 0;
            assert(localProcEventHitMask(e) == row.bit);
        }
        // The new outcomes join the existing bare set, not the damaging one.
        assert(!(row.bit & (LocalProcHitNormal | LocalProcHitCritical)));
    }
    // Controls: the pre-existing bare outcomes behave identically, and an
    // ordinary landed hit still reports NORMAL or CRITICAL.
    for (auto [outcome, bit] : {std::pair{LocalMeleeOutcome::Miss, uint32_t(LocalProcHitMiss)},
                                std::pair{LocalMeleeOutcome::Dodge, uint32_t(LocalProcHitDodge)},
                                std::pair{LocalMeleeOutcome::Parry, uint32_t(LocalProcHitParry)}}) {
        auto e = bareEvent(LocalCombatEventKind::SpellDamage, outcome);
        assert(localProcEventHitMask(e) == bit);
    }
    {
        auto e = bareEvent(LocalCombatEventKind::SpellDamage, LocalMeleeOutcome::Hit);
        e.attempted = e.effective = 40;
        assert(localProcEventHitMask(e) == LocalProcHitNormal);
        e.outcome = LocalMeleeOutcome::Critical;
        assert(localProcEventHitMask(e) == LocalProcHitCritical);
        auto heal = bareEvent(LocalCombatEventKind::PeriodicHeal, LocalMeleeOutcome::Critical);
        heal.attempted = 40; heal.effective = 0; // full overheal still has HealInfo
        assert(localProcEventHitMask(heal) == LocalProcHitCritical);
    }
    std::cout << "PASS bare avoidance mask: FULL_RESIST/IMMUNE/DEFLECT on direct and periodic "
                 "damage and healing carry no NORMAL and no CRITICAL, with or without an "
                 "absorbed or blocked amount\n";

    // =====================================================================
    // (b) validLocalProcFilters: the admitted hit vocabulary
    // =====================================================================
    static_assert(LocalProcSupportedHits == 0x2F7Fu,
                  "the reference admits FULL_RESIST, IMMUNE and DEFLECT and nothing else");
    static_assert(LocalProcHitFullResist == 0x8u && LocalProcHitImmune == 0x100u &&
                  LocalProcHitDeflect == 0x200u, "new bits must equal their source values");
    // Against the reference's own complete mask: EVADE is the only member the
    // local set still refuses, and INTERRUPT is not a member of it at all.
    static_assert((kSourceProcHitMaskAll & ~LocalProcSupportedHits) == kSourceProcHitEvade);
    static_assert(!(kSourceProcHitMaskAll & kSourceProcHitInterrupt));
    static_assert(!(LocalProcSupportedHits & kSourceProcHitEvade));
    static_assert(!(LocalProcSupportedHits & kSourceProcHitInterrupt));
    {
        LocalProcDefinition p; p.flags = 0x10; p.chance = 100;
        assert(validLocalProcFilters(p)); // the definition is otherwise admissible
        for (uint32_t bit : {uint32_t(LocalProcHitFullResist), uint32_t(LocalProcHitImmune),
                             uint32_t(LocalProcHitDeflect)}) {
            p.hitMask = bit; assert(validLocalProcFilters(p));
        }
        p.hitMask = LocalProcHitFullResist | LocalProcHitImmune | LocalProcHitDeflect;
        assert(validLocalProcFilters(p));
        p.hitMask = LocalProcHitFullResist | LocalProcHitCritical | LocalProcHitDeflect | LocalProcHitAbsorb;
        assert(validLocalProcFilters(p));
        p.hitMask = LocalProcSupportedHits; assert(validLocalProcFilters(p));
        // Rejection is whole-definition, not per-bit: one unsupported bit
        // discards every supported bit beside it.
        for (uint32_t bad : {kSourceProcHitEvade, kSourceProcHitInterrupt,
                             uint32_t(kSourceProcHitEvade | LocalProcHitFullResist),
                             uint32_t(kSourceProcHitInterrupt | LocalProcSupportedHits),
                             0x4000u, 0x80000000u}) {
            p.hitMask = bad; assert(!validLocalProcFilters(p));
        }
        // Exhaustive: every single bit of the field is admitted exactly when it
        // is a member of the supported set, so a future bit cannot be smuggled
        // in without this line noticing.
        for (unsigned bit = 0; bit < 32; ++bit) {
            p.hitMask = 1u << bit;
            assert(validLocalProcFilters(p) == bool(LocalProcSupportedHits & (1u << bit)));
        }
        p.hitMask = 0; assert(validLocalProcFilters(p)); // implicit default is still admitted
        // The trap-activation flag stays outside the supported proc flags.
        p.flags = 0x00200000u; assert(!validLocalProcFilters(p));
    }
    std::cout << "PASS filter vocabulary: hitMask 0x8/0x100/0x200 and every combination admitted, "
                 "EVADE 0x80 and INTERRUPT 0x1000 rejected whole-definition, all 32 bits checked "
                 "against LocalProcSupportedHits 0x2F7F\n";

    // =====================================================================
    // (c) The melee view save/LAN codec
    // =====================================================================
    // These values are persisted and LAN-encoded, so the enum is append-only;
    // pin the numbers the wire format actually carries.
    static_assert(uint8_t(LocalMeleeOutcome::Hit) == 0 && uint8_t(LocalMeleeOutcome::Miss) == 1 &&
                  uint8_t(LocalMeleeOutcome::Dodge) == 2 && uint8_t(LocalMeleeOutcome::Parry) == 3 &&
                  uint8_t(LocalMeleeOutcome::Block) == 4 && uint8_t(LocalMeleeOutcome::Critical) == 5 &&
                  uint8_t(LocalMeleeOutcome::Glancing) == 6 && uint8_t(LocalMeleeOutcome::Crushing) == 7 &&
                  uint8_t(LocalMeleeOutcome::Reflect) == 8 && uint8_t(LocalMeleeOutcome::Resist) == 9 &&
                  uint8_t(LocalMeleeOutcome::Immune) == 10 && uint8_t(LocalMeleeOutcome::Deflect) == 11,
                  "outcomes are appended, never reordered: old saves and old peers keep their meaning");
    {
        // Round-trip the three new outcomes together, in one window, beside an
        // ordinary landed hit.
        auto p = codecOwner();
        p.meleeViews[0] = damageView(0, 1); p.meleeViews[0].amount = 40;
        p.meleeViews[1] = damageView(9, 2);
        p.meleeViews[2] = damageView(10, 3);
        p.meleeViews[3] = damageView(11, 4);
        const auto bytes = encodeCast(p);
        assert(bytes.size() == CastWireBytes);
        auto loaded = codecOwner();
        assert(decodeCast(bytes, loaded));
        assert(loaded.meleeSerial == 4);
        for (size_t i = 0; i < p.meleeViews.size(); ++i) sameView(p.meleeViews[i], loaded.meleeViews[i]);
        // One past the last admitted outcome, and well past it.
        for (uint8_t bad : {uint8_t(12), uint8_t(13), uint8_t(200), uint8_t(255)})
            assert(!codecAccepts(damageView(bad, 1)));
        // Every damage-nullifying outcome carries no amount and no block - the
        // three new ones exactly as much as the three that already did.
        for (uint8_t outcome : {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(9), uint8_t(10), uint8_t(11)}) {
            assert(localOutcomeNullifiesDamage(LocalMeleeOutcome(outcome)));
            assert(codecAccepts(damageView(outcome, 1)));
            auto withAmount = damageView(outcome, 1); withAmount.amount = 1;
            assert(!codecAccepts(withAmount));
            auto withBlock = damageView(outcome, 1); withBlock.blocked = 1;
            assert(!codecAccepts(withBlock));
            auto withBoth = damageView(outcome, 1); withBoth.amount = 40; withBoth.blocked = 10;
            assert(!codecAccepts(withBoth));
        }
        // Controls: an outcome that does not nullify damage still carries one.
        for (uint8_t outcome : {uint8_t(0), uint8_t(4), uint8_t(5), uint8_t(6), uint8_t(7), uint8_t(8)}) {
            assert(!localOutcomeNullifiesDamage(LocalMeleeOutcome(outcome)));
            auto carrying = damageView(outcome, 1); carrying.amount = 40;
            assert(codecAccepts(carrying));
        }
        // Healing views remain restricted to Hit and Critical: a healing
        // observation can never be resisted, immune or deflected on this wire.
        for (uint8_t outcome : {uint8_t(9), uint8_t(10), uint8_t(11)}) {
            auto healing = damageView(outcome, 1);
            healing.healing = true; healing.target = 101; healing.amount = 0;
            assert(!codecAccepts(healing));
        }
    }
    std::cout << "PASS melee view codec: outcomes 9/10/11 round-trip through the real "
                 "writeCast/readCast at " << CastWireBytes << " bytes, 12 and above rejected, "
                 "every damage-nullifying outcome rejects an amount or a block, healing stays "
                 "Hit/Critical\n";

    // =====================================================================
    // (d) The real cast path: a player magic spell can now miss
    // =====================================================================
    {
        auto content = outcomeContent();
        LocalGameplay game; game.useContent(content);
        auto p = castingPlayer(1, false);
        std::vector<LocalRealmPlayer*> roster{&p};
        game.tick(0, roster);
        EventTap tap(game);
        tap.drain();

        // A creature six levels above the caster: MagicSpellHitResult with the
        // creature multiplier 11 gives 94 - 4*11 = 50 percent hit, so both
        // outcomes are all but certain inside this bound. Failing to see either
        // in 200 draws is a probability below 2^-199.
        game.setRemoteNpcs({fixtureNpc(kCasterLevel + 6, p.guid)});
        const auto npcGuid = game.npcs()[0].guid;
        unsigned misses = 0, lands = 0;
        for (unsigned i = 0; i < 200; ++i) {
            const auto healthBefore = game.npcs()[0].health;
            const auto out = castBolt(game, p, roster, tap, kBolt, npcGuid);
            const auto healthAfter = game.npcs()[0].health;
            // Full resource cost either way: the reduced cost of an avoided
            // special is a melee rule and must not reach a missed magic spell.
            assert(out.manaPaid == kBoltCost);
            if (out.missed) {
                ++misses;
                assert(out.attempted == 0 && out.effective == 0);
                assert(healthAfter == healthBefore);
                // The miss reaches the proc system as a bare MISS bit.
                LocalCombatEvent shape = bareEvent(LocalCombatEventKind::SpellDamage, LocalMeleeOutcome::Miss);
                assert(localProcEventHitMask(shape) == LocalProcHitMiss);
            } else {
                ++lands;
                assert(out.outcome == LocalMeleeOutcome::Hit || out.outcome == LocalMeleeOutcome::Critical);
                assert(out.attempted > 0 && out.effective == out.attempted);
                assert(healthBefore - healthAfter == out.effective);
            }
        }
        assert(misses && lands);

        // A creature far below the caster: the level term clamps the hit chance
        // at its ceiling, so the spell cannot miss at all.
        game.setRemoteNpcs({fixtureNpc(uint8_t(kCasterLevel - 10), p.guid)});
        for (unsigned i = 0; i < 200; ++i) {
            const auto out = castBolt(game, p, roster, tap, kBolt, game.npcs()[0].guid);
            assert(out.landed && out.attempted > 0);
        }

        // Eleven levels above: 94 - 9*11 is negative and clamps to the floor of
        // one percent hit, so ~99 percent of draws miss. The creature
        // multiplier is what this bound pins - the player-side multiplier 7
        // would give 31 percent hit and about 414 misses here, twelve standard
        // deviations away from the threshold in the other direction.
        game.setRemoteNpcs({fixtureNpc(kCasterLevel + 11, p.guid)});
        unsigned farMisses = 0;
        for (unsigned i = 0; i < 600; ++i)
            if (castBolt(game, p, roster, tap, kBolt, game.npcs()[0].guid).missed) ++farMisses;
        assert(farMisses >= 560);

        // ALWAYS_HIT is honoured at the same hopeless level difference.
        for (unsigned i = 0; i < 300; ++i)
            assert(castBolt(game, p, roster, tap, kAlwaysHitBolt, game.npcs()[0].guid).landed);

        // A positive spell never rolls: the reference returns SPELL_MISS_NONE
        // for one, and the local heal path is unconditional.
        p.health = p.maxHealth / 2;
        for (unsigned i = 0; i < 300; ++i) {
            const auto before = p.health;
            std::string result;
            assert(castAt(game, p, roster, kHeal, 0, result));
            bool healed = false;
            for (const auto& e : tap.drain()) {
                assert(e.outcome != LocalMeleeOutcome::Miss);
                if (e.kind != LocalCombatEventKind::DirectHeal) continue;
                healed = true;
                assert(e.spell == kHeal && e.attempted > 0);
                assert(e.outcome == LocalMeleeOutcome::Hit || e.outcome == LocalMeleeOutcome::Critical);
            }
            assert(healed && p.health > before);
            p.health = p.maxHealth / 2;
        }
        std::cout << "PASS player magic hit roll: " << misses << " misses and " << lands
                  << " landings in 200 casts six levels up, zero misses ten levels down, "
                  << farMisses << "/600 misses eleven levels up (creature multiplier 11), "
                  << "ALWAYS_HIT and positive spells never roll; a miss carries zero amount, "
                     "leaves the creature untouched, still fires FINISH and still pays "
                  << kBoltCost << " resource in full\n";
    }

    // =====================================================================
    // (e) The conditional periodic critical: an application-time snapshot
    // =====================================================================
    {
        auto content = outcomeContent();
        const auto* dot = content->spell(kDot);
        const auto* hot = content->spell(kHot);
        const auto* grant = content->spell(kGrant);
        assert(dot && hot && grant);

        // The grant is what decides whether the chance exists at all.
        auto probe = castingPlayer(1, true);
        LocalGameplay bare; bare.useContent(content);
        std::vector<LocalRealmPlayer*> probeRoster{&probe};
        bare.tick(0, probeRoster);
        assert(localSpellCritChance(probe, bare.content(), dot->schoolMask) > 0);
        assert(localPeriodicCritChanceBasisPoints(&probe, bare.content(), *dot) == 0);
        assert(localPeriodicCritChanceBasisPoints(nullptr, bare.content(), *dot) == 0);
        probe.statAuras.push_back({kGrant, 600000, probe.mapId, probe.instanceId, probe.guid});
        const auto granted = localPeriodicCritChanceBasisPoints(&probe, bare.content(), *dot);
        assert(granted > 0);
        assert(localPeriodicCritChanceBasisPoints(&probe, bare.content(), *hot) == granted);
        // A grant whose family or class mask does not reach this spell grants
        // nothing, even while the caster's own critical chance is unchanged.
        probe.statAuras = {{kWrongFamilyGrant, 600000, probe.mapId, probe.instanceId, probe.guid},
                           {kDisjointGrant, 600000, probe.mapId, probe.instanceId, probe.guid}};
        assert(localPeriodicCritChanceBasisPoints(&probe, bare.content(), *dot) == 0);

        const auto critAmount = localMagicCriticalAmount(kDotAmount);
        assert(critAmount != kDotAmount);

        // --- Damage over time, applied WITHOUT a grant ---------------------
        {
            LocalGameplay game; game.useContent(content);
            auto p = castingPlayer(1, true);
            std::vector<LocalRealmPlayer*> roster{&p};
            game.tick(0, roster);
            game.setRemoteNpcs({fixtureNpc(uint8_t(kCasterLevel - 10), p.guid)});
            EventTap tap(game); tap.drain();
            std::string result;
            assert(castAt(game, p, roster, kDot, game.npcs()[0].guid, result));
            tap.drain();
            unsigned ticks = 0, crits = 0;
            for (unsigned frame = 0; frame < 1600 && ticks < 400; ++frame) {
                // Halfway through, grant the aura. A snapshot taken at
                // application cannot be changed by this.
                if (ticks == 200 && p.statAuras.empty())
                    p.statAuras.push_back({kGrant, 600000, p.mapId, p.instanceId, p.guid});
                game.tick(0.25f, roster);
                for (const auto& e : tap.drain()) {
                    if (e.kind != LocalCombatEventKind::PeriodicDamage || e.spell != kDot) continue;
                    ++ticks;
                    if (e.outcome == LocalMeleeOutcome::Critical) ++crits;
                    assert(e.outcome == LocalMeleeOutcome::Hit);
                    assert(e.attempted == kDotAmount);
                    assert(localProcEventHitMask(e) == LocalProcHitNormal);
                }
            }
            assert(ticks >= 400 && crits == 0);
            assert(!p.statAuras.empty()); // the grant really was present for half of them
            std::cout << "PASS periodic damage without a grant: " << ticks
                      << " ticks, zero criticals, and granting the aura mid-effect changes "
                         "nothing - the chance is snapshotted at application\n";
        }

        // --- Damage over time, applied WITH a grant ------------------------
        {
            LocalGameplay game; game.useContent(content);
            auto p = castingPlayer(1, true);
            std::vector<LocalRealmPlayer*> roster{&p};
            game.tick(0, roster);
            game.setRemoteNpcs({fixtureNpc(uint8_t(kCasterLevel - 10), p.guid)});
            std::string result;
            assert(castAt(game, p, roster, kGrant, 0, result));
            assert(p.statAuras.size() == 1 && p.statAuras[0].spellId == kGrant);
            EventTap tap(game); tap.drain();
            assert(castAt(game, p, roster, kDot, game.npcs()[0].guid, result));
            tap.drain();
            unsigned ticks = 0, crits = 0, normals = 0, afterRemoval = 0, critsAfterRemoval = 0;
            bool removed = false;
            for (unsigned frame = 0; frame < 1600 && ticks < 400; ++frame) {
                // Once the effect is running, take the grant away. The snapshot
                // is the caster's chance at application, not at tick.
                if (!removed && ticks >= 200) {
                    assert(game.execute(p, {LocalAction::CancelStatAura, 0, kGrant}, roster, result));
                    assert(p.statAuras.empty());
                    assert(localPeriodicCritChanceBasisPoints(&p, game.content(), *dot) == 0);
                    removed = true;
                }
                game.tick(0.25f, roster);
                for (const auto& e : tap.drain()) {
                    if (e.kind != LocalCombatEventKind::PeriodicDamage || e.spell != kDot) continue;
                    ++ticks;
                    if (removed) ++afterRemoval;
                    if (e.outcome == LocalMeleeOutcome::Critical) {
                        ++crits; if (removed) ++critsAfterRemoval;
                        assert(e.attempted == critAmount);
                        // This is the whole point of the mechanism: the tick
                        // reaches the proc system as a critical.
                        assert(localProcEventHitMask(e) & LocalProcHitCritical);
                        assert(!(localProcEventHitMask(e) & LocalProcHitNormal));
                    } else {
                        ++normals;
                        assert(e.outcome == LocalMeleeOutcome::Hit);
                        assert(e.attempted == kDotAmount);
                        assert(localProcEventHitMask(e) == LocalProcHitNormal);
                    }
                }
            }
            assert(ticks >= 400 && removed);
            // At the fixture's granted chance both outcomes are overwhelmingly
            // likely in 400 draws; seeing none of either would mean the
            // snapshot was not taken, or was taken as certainty.
            assert(crits > 0 && normals > 0);
            assert(afterRemoval > 0 && critsAfterRemoval > 0);
            std::cout << "PASS periodic damage with a grant: " << crits << " critical and "
                      << normals << " ordinary ticks of " << ticks << ", each critical "
                         "carrying LocalProcHitCritical and the post-armour critical amount; "
                      << critsAfterRemoval << " criticals still landed across " << afterRemoval
                      << " ticks after the granting aura was cancelled\n";
        }

        // --- Healing over time, both directions ---------------------------
        const auto critHeal = localMagicCriticalAmount(kHotAmount);
        assert(critHeal != kHotAmount);
        for (bool withGrant : {false, true}) {
            LocalGameplay game; game.useContent(content);
            auto p = castingPlayer(1, true);
            std::vector<LocalRealmPlayer*> roster{&p};
            game.tick(0, roster);
            p.health = 1000;
            std::string result;
            if (withGrant) {
                assert(castAt(game, p, roster, kGrant, 0, result));
                assert(p.statAuras.size() == 1);
            }
            EventTap tap(game); tap.drain();
            assert(castAt(game, p, roster, kHot, 0, result));
            tap.drain();
            unsigned ticks = 0, crits = 0, normals = 0;
            bool toggled = false;
            for (unsigned frame = 0; frame < 1600 && ticks < 400; ++frame) {
                if (!toggled && ticks >= 200) {
                    // Change the caster's periodic-critical state after
                    // application, in whichever direction this pass did not use.
                    if (withGrant) {
                        assert(game.execute(p, {LocalAction::CancelStatAura, 0, kGrant}, roster, result));
                        assert(p.statAuras.empty());
                    } else {
                        p.statAuras.push_back({kGrant, 600000, p.mapId, p.instanceId, p.guid});
                    }
                    toggled = true;
                }
                game.tick(0.25f, roster);
                for (const auto& e : tap.drain()) {
                    if (e.kind != LocalCombatEventKind::PeriodicHeal || e.spell != kHot) continue;
                    ++ticks;
                    if (e.outcome == LocalMeleeOutcome::Critical) {
                        ++crits;
                        assert(withGrant);
                        assert(e.attempted == critHeal);
                        assert(localProcEventHitMask(e) & LocalProcHitCritical);
                    } else {
                        ++normals;
                        assert(e.outcome == LocalMeleeOutcome::Hit);
                        assert(e.attempted == kHotAmount);
                        assert(localProcEventHitMask(e) == LocalProcHitNormal);
                    }
                }
            }
            assert(ticks >= 400 && toggled);
            assert(normals > 0);
            assert(withGrant ? crits > 0 : crits == 0);
            std::cout << "PASS periodic heal " << (withGrant ? "with" : "without")
                      << " a grant at application: " << crits << " critical and " << normals
                      << " ordinary ticks of " << ticks
                      << "; the post-application change of the caster's grant is ignored\n";
        }
    }

    // =====================================================================
    // (f) localPeriodicCritGrantAffects: IsAffectedOnSpell, negative cases
    // =====================================================================
    {
        auto periodic = dotSpell(); // family 3, class mask word 0 bit 0x10
        const auto affecting = grantSpell(kGrant, kPeriodicFamily, {kDotFlag, 0, 0});
        assert(localPeriodicCritGrantAffects(affecting, periodic));
        // Wrong family: a matching class mask in another family never affects.
        assert(!localPeriodicCritGrantAffects(
            grantSpell(kGrant, kPeriodicFamily + 5, {kDotFlag, 0, 0}), periodic));
        assert(!localPeriodicCritGrantAffects(
            grantSpell(kGrant, kPeriodicFamily - 1, {kDotFlag, 0, 0}), periodic));
        // Right family, non-intersecting class mask.
        assert(!localPeriodicCritGrantAffects(
            grantSpell(kGrant, kPeriodicFamily, {kUnrelatedFlag, 0, 0}), periodic));
        // A bit of the same number in a different mask word does not intersect.
        assert(!localPeriodicCritGrantAffects(
            grantSpell(kGrant, kPeriodicFamily, {0, kDotFlag, 0}), periodic));
        assert(!localPeriodicCritGrantAffects(
            grantSpell(kGrant, kPeriodicFamily, {0, 0, kDotFlag}), periodic));
        // Empty grant: no descriptor at all, and a descriptor with no mask.
        assert(!localPeriodicCritGrantAffects(LocalSpellDefinition{}, periodic));
        assert(!localPeriodicCritGrantAffects(grantSpell(kGrant, 0, {kDotFlag, 0, 0}), periodic));
        assert(!localPeriodicCritGrantAffects(grantSpell(kGrant, kPeriodicFamily, {0, 0, 0}), periodic));
        // A periodic spell with no family of its own is never affected.
        auto familyless = periodic; familyless.spellFamily = 0; familyless.spellFamilyFlags = {0, 0, 0};
        assert(!localPeriodicCritGrantAffects(affecting, familyless));
        // Each of the three mask words intersects independently.
        for (unsigned word = 0; word < 3; ++word) {
            auto target = periodic;
            target.spellFamilyFlags = {0, 0, 0}; target.spellFamilyFlags[word] = kDotFlag;
            std::array<uint32_t, 3> mask{0, 0, 0}; mask[word] = kDotFlag | kUnrelatedFlag;
            assert(localPeriodicCritGrantAffects(grantSpell(kGrant, kPeriodicFamily, mask), target));
        }
        std::cout << "PASS grant predicate: wrong family, non-intersecting class mask, "
                     "mask in the wrong word, an empty descriptor and a family-less periodic "
                     "spell all grant nothing; each of the three mask words intersects on its own\n";
    }

    std::cout << "PASS P03/D3 outcome vocabulary at the reference: representable Resist/Immune/Deflect "
                 "across mask construction, proc filters and the wire format; one new real "
                 "producer (the player-to-creature magic hit roll); the conditional periodic "
                 "critical mechanism with an application-time snapshot. No resistance, immunity "
                 "or deflection producer is claimed, and no granting aura is claimed reachable.\n";
    return 0;
}
